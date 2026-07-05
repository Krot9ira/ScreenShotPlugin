// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

#include "AsyncScreenShotBPLibrary.h"
#include "AsyncScreenShot.h"
#include "Async/TaskGraphInterfaces.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "RenderUtils.h"
#include "GameFramework/GameUserSettings.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Widgets/SWindow.h"
#include "Runtime/Launch/Resources/Version.h"
#include "TimerManager.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "Math/Float16Color.h"
#include "Engine/World.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <vector>
#include <stdio.h>
#include <filesystem>
namespace fs = std::filesystem;

UAsyncScreenShotBPLibrary::UAsyncScreenShotBPLibrary(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{

}

static void PngWriteCallback(void* context, void* data, int size)
{
    FILE* file = static_cast<FILE*>(context);
    fwrite(data, 1, size, file);
}

// Platform-agnostic helpers shared by both the GDI window capture path (Windows-only) and the RHI render-target
// readback path (portable). Keeping these outside the PLATFORM_WINDOWS guard is what lets the RT-based screenshot
// pipeline compile and run on non-Windows platforms too.
static bool PathExists(const FString& Path)
{
#if PLATFORM_WINDOWS
    return fs::exists(std::wstring(TCHAR_TO_WCHAR(*Path)));
#else
    return fs::exists(std::string(TCHAR_TO_UTF8(*Path)));
#endif
}

static void CreateDirectoriesForFile(const FString& FilePath)
{
#if PLATFORM_WINDOWS
    fs::create_directories(fs::path(std::wstring(TCHAR_TO_WCHAR(*FilePath))).parent_path());
#else
    fs::create_directories(fs::path(std::string(TCHAR_TO_UTF8(*FilePath))).parent_path());
#endif
}

static FILE* OpenFileForWrite(const FString& Path)
{
    FILE* File = nullptr;
#if PLATFORM_WINDOWS
    std::wstring WidePath(TCHAR_TO_WCHAR(*Path));
    _wfopen_s(&File, WidePath.c_str(), L"wb");
#else
    File = fopen(TCHAR_TO_UTF8(*Path), "wb");
#endif
    return File;
}

// When bAutoUniqueName is true and FullPath already exists, appends a numeric suffix (_0001, _0002, ...)
// until a free path is found, instead of silently overwriting an existing screenshot.
static FString MakeUniquePath(const FString& FullPath, bool bAutoUniqueName)
{
    if (!bAutoUniqueName || !PathExists(FullPath))
    {
        return FullPath;
    }

    const FString Dir = FPaths::GetPath(FullPath);
    const FString BaseName = FPaths::GetBaseFilename(FullPath);
    const FString Ext = FPaths::GetExtension(FullPath, true);

    for (int32 Suffix = 1; Suffix < 100000; ++Suffix)
    {
        FString Candidate = FPaths::Combine(Dir, FString::Printf(TEXT("%s_%04d"), *BaseName, Suffix)) + Ext;
        if (!PathExists(Candidate))
        {
            return Candidate;
        }
    }
    return FullPath;
}

// Crops (optional) and downscales (optional, nearest-neighbor) an FColor pixel buffer in place.
// Used by the render-target readback path. Width/Height are updated to reflect the new dimensions.
static void CropAndDownscaleColors(TArray<FColor>& Pixels, int32& Width, int32& Height,
    int32 CropX, int32 CropY, int32 CropWidth, int32 CropHeight, float DownscaleFactor)
{
    if (CropWidth > 0 && CropHeight > 0 && Width > 0 && Height > 0)
    {
        const int32 SrcX = FMath::Clamp(CropX, 0, Width - 1);
        const int32 SrcY = FMath::Clamp(CropY, 0, Height - 1);
        const int32 SrcW = FMath::Clamp(CropWidth, 1, Width - SrcX);
        const int32 SrcH = FMath::Clamp(CropHeight, 1, Height - SrcY);

        TArray<FColor> Cropped;
        Cropped.SetNumUninitialized(SrcW * SrcH);
        for (int32 Y = 0; Y < SrcH; Y++)
        {
            FMemory::Memcpy(Cropped.GetData() + Y * SrcW, Pixels.GetData() + (SrcY + Y) * Width + SrcX, SrcW * sizeof(FColor));
        }
        Pixels = MoveTemp(Cropped);
        Width = SrcW;
        Height = SrcH;
    }

    if (DownscaleFactor > 0.f && DownscaleFactor < 1.0f && Width > 0 && Height > 0)
    {
        const int32 NewWidth = FMath::Max(1, FMath::RoundToInt(Width * DownscaleFactor));
        const int32 NewHeight = FMath::Max(1, FMath::RoundToInt(Height * DownscaleFactor));

        TArray<FColor> Resized;
        Resized.SetNumUninitialized(NewWidth * NewHeight);
        for (int32 Y = 0; Y < NewHeight; Y++)
        {
            const int32 SrcY = FMath::Min(Height - 1, FMath::FloorToInt((Y + 0.5f) * Height / NewHeight));
            for (int32 X = 0; X < NewWidth; X++)
            {
                const int32 SrcX = FMath::Min(Width - 1, FMath::FloorToInt((X + 0.5f) * Width / NewWidth));
                Resized[Y * NewWidth + X] = Pixels[SrcY * Width + SrcX];
            }
        }
        Pixels = MoveTemp(Resized);
        Width = NewWidth;
        Height = NewHeight;
    }
}

#if PLATFORM_WINDOWS
#include "AsyncScreenshotWinCapture.h"
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

// Converts a BGRA (bottom-up if h>0) DIB buffer into a top-down RGBA buffer. Alpha is forced to opaque (255):
// GDI's PrintWindow does not populate meaningful alpha for ordinary (non-layered) windows, so trusting the
// captured alpha would often produce a fully transparent image.
static void Bgra2Rgba(const unsigned char* src, int w, int h, int d, unsigned char* dst)
{
    unsigned char* pTempDst = dst;
    for (int i = abs(h) - 1; i >= 0; i--)
    {
        const unsigned char* pTempSrc = nullptr;
        if (h > 0)
        {
            pTempSrc = src + w * i * d;
        }
        else
        {
            pTempSrc = src + w * abs(i + h + 1) * d;
        }

        for (int j = 0; j < w; j++)
        {
            *(pTempDst) = *(pTempSrc + 2);
            *(pTempDst + 1) = *(pTempSrc + 1);
            *(pTempDst + 2) = *(pTempSrc);
            *(pTempDst + 3) = 255;
            pTempDst += 4;
            pTempSrc += d;
        }
    }
}

static void CropAndDownscaleBytes(std::vector<unsigned char>& Pixels, int32& Width, int32& Height, int32 Components,
    int32 CropX, int32 CropY, int32 CropWidth, int32 CropHeight, float DownscaleFactor)
{
    if (CropWidth > 0 && CropHeight > 0 && Width > 0 && Height > 0)
    {
        const int32 SrcX = FMath::Clamp(CropX, 0, Width - 1);
        const int32 SrcY = FMath::Clamp(CropY, 0, Height - 1);
        const int32 SrcW = FMath::Clamp(CropWidth, 1, Width - SrcX);
        const int32 SrcH = FMath::Clamp(CropHeight, 1, Height - SrcY);

        std::vector<unsigned char> Cropped((size_t)SrcW * SrcH * Components);
        for (int32 Y = 0; Y < SrcH; Y++)
        {
            memcpy(Cropped.data() + (size_t)Y * SrcW * Components,
                Pixels.data() + ((size_t)(SrcY + Y) * Width + SrcX) * Components,
                (size_t)SrcW * Components);
        }
        Pixels = std::move(Cropped);
        Width = SrcW;
        Height = SrcH;
    }

    if (DownscaleFactor > 0.f && DownscaleFactor < 1.0f && Width > 0 && Height > 0)
    {
        const int32 NewWidth = FMath::Max(1, FMath::RoundToInt(Width * DownscaleFactor));
        const int32 NewHeight = FMath::Max(1, FMath::RoundToInt(Height * DownscaleFactor));

        std::vector<unsigned char> Resized((size_t)NewWidth * NewHeight * Components);
        for (int32 Y = 0; Y < NewHeight; Y++)
        {
            const int32 SrcY = FMath::Min(Height - 1, FMath::FloorToInt((Y + 0.5f) * Height / NewHeight));
            for (int32 X = 0; X < NewWidth; X++)
            {
                const int32 SrcX = FMath::Min(Width - 1, FMath::FloorToInt((X + 0.5f) * Width / NewWidth));
                memcpy(&Resized[((size_t)Y * NewWidth + X) * Components], &Pixels[((size_t)SrcY * Width + SrcX) * Components], Components);
            }
        }
        Pixels = std::move(Resized);
        Width = NewWidth;
        Height = NewHeight;
    }
}

// Writes a 32bpp BGRA BMP from a top-down RGBA buffer (BMP's native row order is bottom-up).
static bool WriteBmpFromRgba(FILE* File, const std::vector<unsigned char>& Rgba, int32 Width, int32 Height)
{
    BITMAPFILEHEADER bmfHeader = {};
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = Width;
    bi.biHeight = Height;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    const uint32 dwBmpSize = (uint32)Width * 4 * (uint32)Height;
    bmfHeader.bfType = 0x4D42;
    bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmfHeader.bfSize = dwBmpSize + bmfHeader.bfOffBits;

    fwrite(&bmfHeader, sizeof(bmfHeader), 1, File);
    fwrite(&bi, sizeof(bi), 1, File);

    std::vector<unsigned char> Row((size_t)Width * 4);
    for (int32 Y = Height - 1; Y >= 0; Y--)
    {
        const unsigned char* Src = Rgba.data() + (size_t)Y * Width * 4;
        for (int32 X = 0; X < Width; X++)
        {
            Row[X * 4 + 0] = Src[X * 4 + 2];
            Row[X * 4 + 1] = Src[X * 4 + 1];
            Row[X * 4 + 2] = Src[X * 4 + 0];
            Row[X * 4 + 3] = Src[X * 4 + 3];
        }
        fwrite(Row.data(), 1, Row.size(), File);
    }
    return true;
}

bool CaptureWindowToFile(HWND hWnd, const FString& PathToSave, const FString& Name, EImageFormat ImageFormat, int32 Quality, bool bAutoUniqueName,
    int32 CropX, int32 CropY, int32 CropWidth, int32 CropHeight, float DownscaleFactor, FString& OutFullPath)
{
    if (!hWnd) return false;

    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    int width_px = rcClient.right - rcClient.left;
    int height_px = rcClient.bottom - rcClient.top;
    if (width_px <= 0 || height_px <= 0) return false;

    HDC hdcWindow = GetDC(hWnd);
    HDC hdcMemDC = CreateCompatibleDC(hdcWindow);
    if (!hdcMemDC) { ReleaseDC(hWnd, hdcWindow); return false; }

    HBITMAP hbmScreen = CreateCompatibleBitmap(hdcWindow, width_px, height_px);
    if (!hbmScreen) { DeleteDC(hdcMemDC); ReleaseDC(hWnd, hdcWindow); return false; }

    SelectObject(hdcMemDC, hbmScreen);

    if (!PrintWindow(hWnd, hdcMemDC, PW_RENDERFULLCONTENT))
    {
        DeleteObject(hbmScreen);
        DeleteDC(hdcMemDC);
        ReleaseDC(hWnd, hdcWindow);
        return false;
    }

    // Use the explicit -W entry point: HideWindowsPlatformTypes.h (included via AsyncScreenshotWinCapture.h) undefines
    // the plain "GetObject" convenience macro to avoid clashing with unrelated identifiers elsewhere in this file.
    BITMAP bmpScreen;
    GetObjectW(hbmScreen, sizeof(BITMAP), &bmpScreen);

    DWORD dwBmpSize = ((bmpScreen.bmWidth * 32 + 31) / 32) * 4 * bmpScreen.bmHeight;
    HANDLE hDIB = GlobalAlloc(GHND, dwBmpSize);
    char* lpbitmap = (char*)GlobalLock(hDIB);

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmpScreen.bmWidth;
    bi.biHeight = bmpScreen.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    GetDIBits(hdcMemDC, hbmScreen, 0, bmpScreen.bmHeight, lpbitmap, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    std::vector<unsigned char> Rgba((size_t)width_px * height_px * 4);
    Bgra2Rgba((unsigned char*)lpbitmap, width_px, height_px, 4, Rgba.data());

    GlobalUnlock(hDIB);
    GlobalFree(hDIB);
    DeleteObject(hbmScreen);
    DeleteDC(hdcMemDC);
    ReleaseDC(hWnd, hdcWindow);

    CropAndDownscaleBytes(Rgba, width_px, height_px, 4, CropX, CropY, CropWidth, CropHeight, DownscaleFactor);

    FString FormatEnd;
    switch (ImageFormat)
    {
    case EImageFormat::bpm: FormatEnd = TEXT(".bmp"); break;
    case EImageFormat::jpg: FormatEnd = TEXT(".jpg"); break;
    case EImageFormat::png: FormatEnd = TEXT(".png"); break;
    default: FormatEnd = TEXT(".png"); break;
    }

    FString FullPath = FPaths::Combine(PathToSave, Name) + FormatEnd;
    FullPath = MakeUniquePath(FullPath, bAutoUniqueName);
    CreateDirectoriesForFile(FullPath);

    FILE* File = OpenFileForWrite(FullPath);
    if (!File)
    {
        UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Failed to open file for writing: %s"), *FullPath);
        return false;
    }

    bool bWriteOk = false;
    switch (ImageFormat)
    {
    case EImageFormat::jpg:
        bWriteOk = stbi_write_jpg_to_func(PngWriteCallback, File, width_px, height_px, 4, Rgba.data(), Quality) != 0;
        break;
    case EImageFormat::png:
        bWriteOk = stbi_write_png_to_func(PngWriteCallback, File, width_px, height_px, 4, Rgba.data(), width_px * 4) != 0;
        break;
    case EImageFormat::bpm:
    default:
        bWriteOk = WriteBmpFromRgba(File, Rgba, width_px, height_px);
        break;
    }
    fclose(File);

    if (!bWriteOk)
    {
        UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Failed to write image (stb error): %s"), *FullPath);
        return false;
    }

    OutFullPath = FullPath;
    return true;
}

HWND GetActiveGameWindow()
{
    check(IsInGameThread());
    if (GEngine && GEngine->GameViewport)
    {
        TSharedPtr<SWindow> Win = GEngine->GameViewport->GetWindow();
        if (Win.IsValid() && Win->GetNativeWindow().IsValid())
        {
            return static_cast<HWND>(Win->GetNativeWindow()->GetOSWindowHandle());
        }
    }
    return nullptr;
}

void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name, EImageFormat ImageFormat, int Quality, bool bAutoUniqueName)
{
    if (Name.IsEmpty())
    {
        Name = TEXT("Blank");
    }

    // HWND must be fetched on the game thread: GEngine->GameViewport is not thread-safe.
    HWND hWnd = GetActiveGameWindow();
    if (!hWnd)
    {
        UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: No valid game window HWND, aborting SaveGameScreen"));
        return;
    }

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [PathToSave, Name, ImageFormat, Quality, bAutoUniqueName, hWnd] {
        FString OutFullPath;
        CaptureWindowToFile(hWnd, PathToSave, Name, ImageFormat, Quality, bAutoUniqueName, -1, -1, -1, -1, 1.0f, OutFullPath);
    });
}

#else // !PLATFORM_WINDOWS

void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name, EImageFormat ImageFormat, int Quality, bool bAutoUniqueName)
{
    UE_LOG(LogTemp, Warning, TEXT("AsyncScreenshot: SaveGameScreen (GDI window capture) is only implemented on Windows"));
}

#endif // PLATFORM_WINDOWS


void PollRTRead(FRHICommandListImmediate& RHICmdList, TSharedPtr<FAsyncReadEntireRTData, ESPMode::ThreadSafe> ReadData, TWeakObjectPtr<UAsyncScreenshotRTAction> ReadAction, bool bFlushRHI)
{
    SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::PollRTRead", FColor::Magenta);

    check(IsInRenderingThread());


    // If we didn't flush the RHI then make sure the previous rendering commands got done
    if (!bFlushRHI)
    {
        // Return if we haven't finished the texture commands
        if (!ReadData->TextureFence.IsValid() || !ReadData->TextureFence->Poll() || ReadData->StartReading)
        {
            return;
        }
    }
    ReadData->FinishedRead = false;
    SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::MapTexture", FColor::Magenta);
    void* OutputBuffer = NULL;
    int32 RowPitchInPixels, Height;

    if (bFlushRHI)
    {
        // This flushes the command list
        RHICmdList.MapStagingSurface(ReadData->Texture, ReadData->TextureFence, OutputBuffer, RowPitchInPixels, Height);
    }
    else
    {
#if (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2)
        GDynamicRHI->RHIMapStagingSurface_RenderThread(RHICmdList, ReadData->Texture, INDEX_NONE, ReadData->TextureFence, OutputBuffer, RowPitchInPixels, Height);
#else
        GDynamicRHI->RHIMapStagingSurface_RenderThread(RHICmdList, ReadData->Texture, ReadData->TextureFence, OutputBuffer, RowPitchInPixels, Height);
#endif
    }
    ReadData->StartReading = true;
    const int32 Width = ReadData->Texture->GetSizeX();
    check(RowPitchInPixels >= Width);
    check(Height == ReadData->Texture->GetSizeY());
    ReadData->PixelColors.Empty(Width * Height);
    ReadData->PixelColors.SetNum(Width * Height);
    const EPixelFormat Format = ReadData->Texture->GetFormat();

    if (Format == EPixelFormat::PF_B8G8R8A8)
    {
        const int32 BytesPerPixel = GPixelFormats[Format].BlockBytes; // = 4 для B8G8R8A8
        uint8* Src = static_cast<uint8*>(OutputBuffer);
        FColor* Dst = ReadData->PixelColors.GetData();

        for (int32 Y = 0; Y < Height; Y++)
        {
            // Смещение в байтах с учётом pitch
            uint8* RowSrc = Src + Y * RowPitchInPixels * BytesPerPixel;
            FMemory::Memcpy(Dst + Y * Width, RowSrc, Width * BytesPerPixel);
        }
    }
    else if (Format == EPixelFormat::PF_FloatRGBA)
    {
        FFloat16Color* Src = static_cast<FFloat16Color*>(OutputBuffer);
        FColor* Dst = ReadData->PixelColors.GetData();

        if (ReadData->bWantsLinearColor)
        {
            ReadData->LinearColors.SetNumUninitialized(Width * Height);
        }

        for (int32 Y = 0; Y < Height; Y++)
        {
            FFloat16Color* RowSrc = Src + Y * RowPitchInPixels;
            for (int32 X = 0; X < Width; X++)
            {
                const FFloat16Color& P = RowSrc[X];
                FLinearColor Lin(P.R.GetFloat(), P.G.GetFloat(), P.B.GetFloat(), P.A.GetFloat());
                Dst[Y * Width + X] = Lin.ToFColor(false);
                if (ReadData->bWantsLinearColor)
                {
                    ReadData->LinearColors[Y * Width + X] = Lin;
                }
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("AsyncScreenshot: Unsupported RT format! Format: %d"), static_cast<int32>(Format));
    }

    RHICmdList.UnmapStagingSurface(ReadData->Texture);
    ReadData->FinishedRead = true;
}

UAsyncScreenshotRTAction* UAsyncScreenshotRTAction::SaveRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* RenderTarget, FString PathToSave, FString Name, bool bFlushRHI,
    bool bAutoUniqueName, bool bExportHDRForFloatRT, bool bSaveToDisk,
    int32 CropX, int32 CropY, int32 CropWidth, int32 CropHeight, float DownscaleFactor)
{
    UAsyncScreenshotRTAction* BlueprintNode = NewObject<UAsyncScreenshotRTAction>();
    BlueprintNode->WorldContextObject = WorldContextObject;
    BlueprintNode->RT = RenderTarget;
    BlueprintNode->bFlushRHI = bFlushRHI;
    BlueprintNode->ReadRTData = MakeShared<FAsyncReadEntireRTData, ESPMode::ThreadSafe>();
    BlueprintNode->ReadRTData->FinishedRead = false;
    BlueprintNode->ReadRTData->bWantsLinearColor = bExportHDRForFloatRT;
    BlueprintNode->SavedPathToSave = PathToSave;
    BlueprintNode->SavedName = Name;
    BlueprintNode->bAutoUniqueName = bAutoUniqueName;
    BlueprintNode->bExportHDRForFloatRT = bExportHDRForFloatRT;
    BlueprintNode->bSaveToDisk = bSaveToDisk;
    BlueprintNode->bReturnAsTexture = false;
    BlueprintNode->CropX = CropX;
    BlueprintNode->CropY = CropY;
    BlueprintNode->CropWidth = CropWidth;
    BlueprintNode->CropHeight = CropHeight;
    BlueprintNode->DownscaleFactor = DownscaleFactor;

    // TODO(hdr-crop): the .hdr export path writes the full, uncropped/undownscaled linear buffer -
    // CropAndDownscaleColors only runs on the PNG/texture path. Warn instead of silently ignoring the request.
    const bool bWantsCrop = (CropWidth > 0 && CropHeight > 0);
    const bool bWantsDownscale = (DownscaleFactor > 0.f && DownscaleFactor < 1.0f);
    if (bExportHDRForFloatRT && (bWantsCrop || bWantsDownscale))
    {
        UE_LOG(LogTemp, Warning, TEXT("AsyncScreenshot: Crop/Downscale are not yet supported together with bExportHDRForFloatRT; the .hdr file will be written at full render target resolution."));
    }

    return BlueprintNode;
}

UAsyncScreenshotRTAction* UAsyncScreenshotRTAction::SaveRenderTargetsMultiplyAlpha(UObject* WorldContextObject, UTextureRenderTarget2D* ColorRT, UTextureRenderTarget2D* AlphaRT, FString PathToSave, FString Name, bool bFlushRHI, bool bAutoUniqueName)
{
    UAsyncScreenshotRTAction* Node = NewObject<UAsyncScreenshotRTAction>();
    Node->WorldContextObject = WorldContextObject;
    Node->RT = ColorRT;
    Node->AlphaRT = AlphaRT;
    Node->CombineMode = EAsyncRTCombineMode::MultiplyAlpha;
    Node->bFlushRHI = bFlushRHI;
    Node->SavedPathToSave = PathToSave;
    Node->SavedName = Name;
    Node->bAutoUniqueName = bAutoUniqueName;
    Node->CombinedData = MakeShared<FAsyncReadCombinedRTData, ESPMode::ThreadSafe>();
    Node->CombinedData->ColorRT = MakeShared<FAsyncReadEntireRTData, ESPMode::ThreadSafe>();
    Node->CombinedData->AlphaRT = MakeShared<FAsyncReadEntireRTData, ESPMode::ThreadSafe>();
    Node->CombinedData->Mode = Node->CombineMode;
    return Node;
}

void WritePixelsToFile(FString PathToSave, FString Name, TArray<FColor>& PixelToCopy, int32 InWidth, int32 InHeight, TWeakObjectPtr<UAsyncScreenshotRTAction> Action,
    bool bAutoUniqueName, bool bSaveToDisk, bool bReturnAsTexture)
{
    TArray<FColor> PixelArrayCopy = PixelToCopy;

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Action, PathToSave, Name, PixelArrayCopy, InWidth, InHeight, bAutoUniqueName, bSaveToDisk, bReturnAsTexture]() {

        SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::WriteImage", FColor::Magenta);

        if (bSaveToDisk)
        {
            const int32 size = PixelArrayCopy.Num();

            FString FullPath = FPaths::Combine(PathToSave, Name) + FString(".png");
            FullPath = MakeUniquePath(FullPath, bAutoUniqueName);
            CreateDirectoriesForFile(FullPath);

            std::vector<uint8> data;
            data.reserve((size_t)size * 4);
            for (int32 i = 0; i < size; i++) {
                data.push_back(PixelArrayCopy[i].R);
                data.push_back(PixelArrayCopy[i].G);
                data.push_back(PixelArrayCopy[i].B);
                data.push_back(PixelArrayCopy[i].A);
            }

            FILE* File = OpenFileForWrite(FullPath);

            if (File)
            {
                int Result = stbi_write_png_to_func(
                    PngWriteCallback,
                    File,
                    InWidth,
                    InHeight,
                    4,
                    data.data(),
                    4 * InWidth
                );

                fclose(File);

                if (Result == 0)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to write PNG (stb error): %s"), *FullPath);
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to open file for writing: %s"), *FullPath);
            }
        }

        AsyncTask(ENamedThreads::GameThread, [Action, PixelArrayCopy, InWidth, InHeight, bReturnAsTexture]() {
            if (Action.IsValid())
            {
                if (bReturnAsTexture)
                {
                    UTexture2D* Tex = UTexture2D::CreateTransient(InWidth, InHeight, PF_B8G8R8A8, NAME_None,
                        TConstArrayView64<uint8>(reinterpret_cast<const uint8*>(PixelArrayCopy.GetData()), (int64)PixelArrayCopy.Num() * sizeof(FColor)));
                    if (Tex)
                    {
                        Tex->UpdateResource();
                        Action->OnCapturedTexture.Broadcast(Tex);
                    }
                }
                Action->OnSaveRenderTarget.Broadcast();
                Action->SetReadyToDestroy();
            }
        });
    });
}

// Writes the raw (non-tonemapped) linear float pixels of a PF_FloatRGBA render target to a Radiance .hdr file,
// preserving full HDR range. Chosen over a custom EXR writer since stb_image_write already ships an HDR encoder,
// avoiding a new third-party dependency.
void WriteLinearPixelsToHDRFile(FString PathToSave, FString Name, TArray<FLinearColor> PixelToCopy, int32 InWidth, int32 InHeight, TWeakObjectPtr<UAsyncScreenshotRTAction> Action, bool bAutoUniqueName)
{
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Action, PathToSave, Name, PixelToCopy, InWidth, InHeight, bAutoUniqueName]() {

        SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::WriteHDR", FColor::Magenta);

        FString FullPath = FPaths::Combine(PathToSave, Name) + FString(".hdr");
        FullPath = MakeUniquePath(FullPath, bAutoUniqueName);
        CreateDirectoriesForFile(FullPath);

        std::vector<float> data;
        data.reserve((size_t)PixelToCopy.Num() * 4);
        for (const FLinearColor& C : PixelToCopy)
        {
            data.push_back(C.R);
            data.push_back(C.G);
            data.push_back(C.B);
            data.push_back(C.A);
        }

        FILE* File = OpenFileForWrite(FullPath);
        if (File)
        {
            int Result = stbi_write_hdr_to_func(PngWriteCallback, File, InWidth, InHeight, 4, data.data());
            fclose(File);

            if (Result == 0)
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to write HDR (stb error): %s"), *FullPath);
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to open file for writing: %s"), *FullPath);
        }

        AsyncTask(ENamedThreads::GameThread, [Action]() {
            if (Action.IsValid())
            {
                Action->OnSaveRenderTarget.Broadcast();
                Action->SetReadyToDestroy();
            }
        });
    });
}



FString UAsyncScreenShotBPLibrary::GetScreenshotSavePath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir());
}

void UAsyncScreenShotBPLibrary::SetPngCompressionLevel(int32 Level)
{
    stbi_write_png_compression_level = FMath::Clamp(Level, 0, 9);
}

void UAsyncScreenShotBPLibrary::SaveScreenshotMetadata(FString PathToSave, FString Name, const TMap<FString, FString>& Metadata)
{
    FString Json = TEXT("{\n");
    Json += FString::Printf(TEXT("  \"Timestamp\": \"%s\""), *FDateTime::Now().ToIso8601());
    for (const TPair<FString, FString>& Pair : Metadata)
    {
        const FString EscapedValue = Pair.Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
        Json += FString::Printf(TEXT(",\n  \"%s\": \"%s\""), *Pair.Key, *EscapedValue);
    }
    Json += TEXT("\n}\n");

    const FString FullPath = FPaths::Combine(PathToSave, Name) + TEXT(".json");
    CreateDirectoriesForFile(FullPath);
    FFileHelper::SaveStringToFile(Json, *FullPath);
}

// Safety net: if the GPU fence never signals (RT destroyed, GPU hang), bail out instead of polling forever.
static constexpr uint64 MaxWaitFrames = 300;

void UAsyncScreenshotRTAction::OnNextFrame()
{
    check(IsInGameThread());

    switch (CombineMode)
    {
    case EAsyncRTCombineMode::SingleRT:
    {

        check(ReadRTData.IsValid());

        if (ReadRTData->FinishedRead)
        {
            int32 Width = ReadRTData->Texture->GetSizeX();
            int32 Height = ReadRTData->Texture->GetSizeY();

            if (bExportHDRForFloatRT && ReadRTData->LinearColors.Num() == Width * Height)
            {
                WriteLinearPixelsToHDRFile(SavedPathToSave, SavedName, ReadRTData->LinearColors, Width, Height, this, bAutoUniqueName);
            }
            else
            {
                CropAndDownscaleColors(ReadRTData->PixelColors, Width, Height, CropX, CropY, CropWidth, CropHeight, DownscaleFactor);
                WritePixelsToFile(SavedPathToSave, SavedName, ReadRTData->PixelColors, Width, Height, this, bAutoUniqueName, bSaveToDisk, bReturnAsTexture);
            }
            return;
        }
        else
        {
            if (GFrameCounter - StartFrame > MaxWaitFrames)
            {
                UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Timed out waiting for render target readback"));
                OnSaveRenderTarget.Broadcast();
                SetReadyToDestroy();
                return;
            }

            ENQUEUE_RENDER_COMMAND(FReadRTAsync)([WeakThis = TWeakObjectPtr<UAsyncScreenshotRTAction>(this), ReadRTData = ReadRTData](FRHICommandListImmediate& RHICmdList)
                {
                    PollRTRead(RHICmdList, ReadRTData, WeakThis, false);
                });



            WorldContextObject->GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UAsyncScreenshotRTAction::OnNextFrame);
        }
        break;
    }
    case EAsyncRTCombineMode::MultiplyAlpha:
    {
        if (!CombinedData->ColorRT->FinishedRead || !CombinedData->AlphaRT->FinishedRead)
        {
            if (GFrameCounter - StartFrame > MaxWaitFrames)
            {
                UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Timed out waiting for render target readback"));
                OnSaveRenderTarget.Broadcast();
                SetReadyToDestroy();
                return;
            }

            ENQUEUE_RENDER_COMMAND(PollRTs)(
                [ColorData = CombinedData->ColorRT,
                AlphaData = CombinedData->AlphaRT,
                bFlushRHI = bFlushRHI,
                WeakThis = TWeakObjectPtr<UAsyncScreenshotRTAction>(this)]
                (FRHICommandListImmediate& RHICmdList)
                {
                    PollRTRead(RHICmdList, ColorData, WeakThis, bFlushRHI);
                    PollRTRead(RHICmdList, AlphaData, WeakThis, bFlushRHI);
                });
            WorldContextObject->GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UAsyncScreenshotRTAction::OnNextFrame);
            return;
        }

        TArray<FColor>& Color = CombinedData->ColorRT->PixelColors;
        TArray<FColor>& Alpha = CombinedData->AlphaRT->PixelColors;

        if (Color.Num() != Alpha.Num())
        {
            UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Color/Alpha RT size mismatch (%d vs %d)"), Color.Num(), Alpha.Num());
            OnSaveRenderTarget.Broadcast();
            SetReadyToDestroy();
            return;
        }

        for (int32 i = 0; i < Color.Num(); ++i)
        {
            const float ColorAlpha = Color[i].A / 255.f;
            const float MaskAlphaInv = (255 - Alpha[i].A) / 255.f;
            Color[i].A = (uint8)(FMath::Clamp(ColorAlpha * MaskAlphaInv, 0.f, 1.f) * 255);
        }

        WritePixelsToFile(SavedPathToSave, SavedName, Color, CombinedData->ColorRT->Texture->GetSizeX(), CombinedData->ColorRT->Texture->GetSizeY(), this, bAutoUniqueName, true, false);
        return;
        break;
    }
    }

}

void UAsyncScreenshotRTAction::Activate()
{
    switch (CombineMode)
    {
    case EAsyncRTCombineMode::SingleRT:
    {
        if (!WorldContextObject || !RT || !RT->GetResource())
        {
            UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Invalid WorldContextObject or RenderTarget"));
            OnSaveRenderTarget.Broadcast();
            SetReadyToDestroy();
            break;
        }

        FTextureRenderTarget2DResource* TextureResource = (FTextureRenderTarget2DResource*)RT->GetResource();
        check(TextureResource);
        check(TextureResource->GetRenderTargetTexture());

        StartFrame = GFrameCounter;

        ENQUEUE_RENDER_COMMAND(FCopyRTAsync)([bFlushRHI = bFlushRHI, AsyncReadPtr = TWeakObjectPtr<UAsyncScreenshotRTAction>(this), TextureRHI = TextureResource->GetRenderTargetTexture(), ReadData = ReadRTData](FRHICommandListImmediate& RHICmdList)
            {
                check(IsInRenderingThread());
                check(TextureRHI.IsValid());

                FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("AsyncScreenshotRTReadback"));

                SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT", FColor::Magenta);

                FTextureRHIRef IORHITextureCPU;
                {
                    SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::CreateCopyTexture", FColor::Magenta);

                    int32 Width, Height;
                    Width = TextureRHI->GetSizeX();
                    Height = TextureRHI->GetSizeY();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 2
                    FRHITextureCreateDesc TextureDesc = FRHITextureCreateDesc::Create2D(TEXT("AsyncScreenshotRTReadback"), Width, Height, TextureRHI->GetFormat());
                    TextureDesc.AddFlags(ETextureCreateFlags::CPUReadback);
                    TextureDesc.InitialState = ERHIAccess::CopyDest;
#if ENGINE_MINOR_VERSION > 3
                    IORHITextureCPU = RHICmdList.CreateTexture(TextureDesc);

#else // ENGINE_MINOR_VERSION
                    IORHITextureCPU = GDynamicRHI->RHICreateTexture(TextureDesc);
#endif // ENGINE_MINOR_VERSION
#else
                    FRHIResourceCreateInfo CreateInfo(TEXT("AsyncRTReadback"));
                    IORHITextureCPU = RHICreateTexture2D(Width, Height, TextureRHI->GetFormat(), 1, 1, TexCreate_CPUReadback, ERHIAccess::CopyDest, CreateInfo);
#endif

                    FRHICopyTextureInfo CopyTextureInfo;
                    CopyTextureInfo.Size = FIntVector(Width, Height, 1);
                    CopyTextureInfo.SourceMipIndex = 0;
                    CopyTextureInfo.DestMipIndex = 0;
                    CopyTextureInfo.SourcePosition = FIntVector(0, 0, 0);
                    CopyTextureInfo.DestPosition = FIntVector(0, 0, 0);

                    RHICmdList.Transition(FRHITransitionInfo(TextureRHI, ERHIAccess::Unknown, ERHIAccess::CopySrc));
                    RHICmdList.CopyTexture(TextureRHI, IORHITextureCPU, CopyTextureInfo);

                    RHICmdList.Transition(FRHITransitionInfo(IORHITextureCPU, ERHIAccess::CopyDest, ERHIAccess::CopySrc));
                    RHICmdList.WriteGPUFence(Fence);
                }
                check(Fence.IsValid());

                ReadData->Texture = IORHITextureCPU;
                ReadData->TextureFence = Fence;

                // If we flush the RHI then we can just go ahead and read the mapped texture asap
                if (bFlushRHI)
                {
                    PollRTRead(RHICmdList, ReadData, AsyncReadPtr, bFlushRHI);
                }
            });

        WorldContextObject->GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UAsyncScreenshotRTAction::OnNextFrame);
        break;
    }
    case EAsyncRTCombineMode::MultiplyAlpha:
    {
        if (!WorldContextObject || !RT || !RT->GetResource() || !AlphaRT || !AlphaRT->GetResource())
        {
            UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Invalid WorldContextObject or RenderTargets"));
            OnSaveRenderTarget.Broadcast();
            SetReadyToDestroy();
            break;
        }

        StartFrame = GFrameCounter;

        auto EnqueueRead = [&](UTextureRenderTarget2D* Target, TSharedPtr<FAsyncReadEntireRTData> Data)
            {
                FTextureRenderTarget2DResource* Res = static_cast<FTextureRenderTarget2DResource*>(Target->GetResource());

                ENQUEUE_RENDER_COMMAND(ReadRT)(
                    [Res, Data, bFlushRHI = bFlushRHI](FRHICommandListImmediate& RHICmdList)
                    {
                        FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("RTReadFence"));
                        FTextureRHIRef Src = Res->GetRenderTargetTexture();
                        int32 W = Src->GetSizeX();
                        int32 H = Src->GetSizeY();

                        FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("RT_CPU"), W, H, Src->GetFormat());
                        Desc.AddFlags(ETextureCreateFlags::CPUReadback);
                        Desc.InitialState = ERHIAccess::CopyDest;
                        FTextureRHIRef CPUTexture = RHICmdList.CreateTexture(Desc);

                        RHICmdList.Transition(FRHITransitionInfo(Src, ERHIAccess::Unknown, ERHIAccess::CopySrc));
                        RHICmdList.CopyTexture(Src, CPUTexture, {});

                        RHICmdList.Transition(FRHITransitionInfo(CPUTexture, ERHIAccess::CopyDest, ERHIAccess::CopySrc));
                        RHICmdList.WriteGPUFence(Fence);

                        Data->Texture = CPUTexture;
                        Data->TextureFence = Fence;

                        if (bFlushRHI)
                        {
                            PollRTRead(RHICmdList, Data, nullptr, true);
                        }
                    });
            };

        EnqueueRead(RT, CombinedData->ColorRT);
        EnqueueRead(AlphaRT, CombinedData->AlphaRT);

        WorldContextObject->GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UAsyncScreenshotRTAction::OnNextFrame);
        break;
    }
    }
}
