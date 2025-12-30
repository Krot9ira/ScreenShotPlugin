// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

#pragma once
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
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "Async/Async.h"

UAsyncScreenShotBPLibrary::UAsyncScreenShotBPLibrary(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{

}


#if PLATFORM_WINDOWS
#include <windows.h>
THIRD_PARTY_INCLUDES_START
#include <gdiplus.h>
THIRD_PARTY_INCLUDES_END
#include <vector>
#include <tchar.h>
#include <thread>
#include <stdio.h>
#include <fstream>
#include <iostream>
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
/*
#if __cplusplus < 201703L // If the version of C++ is less than 17
#include <experimental/filesystem>
// It was still in the experimental:: namespace
namespace fs = std::experimental::filesystem;
*/
//#else
#include <filesystem>
namespace fs = std::filesystem;
//#endif


using namespace Gdiplus;
using namespace std;

#pragma comment(lib,"gdiplus.lib")


void Bgra2Rgb(const unsigned char* src, int w, int h, int d, unsigned char* dst)
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
            pTempDst += 3;
            pTempSrc += d;
        }
    }
}

int CaptureAnImage(HWND hWnd, const std::string& path, EImageFormat ImageFormat, int quality)
{
    if (!hWnd) return 0;

    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    int width_px = rcClient.right - rcClient.left;
    int height_px = rcClient.bottom - rcClient.top;

    HDC hdcWindow = GetDC(hWnd);
    HDC hdcMemDC = CreateCompatibleDC(hdcWindow);
    if (!hdcMemDC) { ReleaseDC(hWnd, hdcWindow); return 0; }

    HBITMAP hbmScreen = CreateCompatibleBitmap(hdcWindow, width_px, height_px);
    if (!hbmScreen) { DeleteDC(hdcMemDC); ReleaseDC(hWnd, hdcWindow); return 0; }

    SelectObject(hdcMemDC, hbmScreen);

    if (!PrintWindow(hWnd, hdcMemDC, PW_RENDERFULLCONTENT))
    {
        DeleteObject(hbmScreen);
        DeleteDC(hdcMemDC);
        ReleaseDC(hWnd, hdcWindow);
        return 0;
    }

    BITMAP bmpScreen;
    GetObject(hbmScreen, sizeof(BITMAP), &bmpScreen);

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

    if (ImageFormat == EImageFormat::jpg)
    {
        unsigned char* rgb = new unsigned char[width_px * height_px * 3];
        Bgra2Rgb((unsigned char*)lpbitmap, width_px, height_px, 4, rgb);
        stbi_write_jpg(path.c_str(), width_px, height_px, 3, rgb, quality);
        delete[] rgb;
    }
    else if (ImageFormat == EImageFormat::bpm)
    {
        BITMAPFILEHEADER bmfHeader = {};
        bmfHeader.bfType = 0x4D42;
        bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        bmfHeader.bfSize = dwBmpSize + bmfHeader.bfOffBits;

        HANDLE hFile = CreateFile(std::wstring(path.begin(), path.end()).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile)
        {
            DWORD dwBytesWritten;
            WriteFile(hFile, &bmfHeader, sizeof(BITMAPFILEHEADER), &dwBytesWritten, NULL);
            WriteFile(hFile, &bi, sizeof(BITMAPINFOHEADER), &dwBytesWritten, NULL);
            WriteFile(hFile, lpbitmap, dwBmpSize, &dwBytesWritten, NULL);
            CloseHandle(hFile);
        }
    }

    GlobalUnlock(hDIB);
    GlobalFree(hDIB);
    DeleteObject(hbmScreen);
    DeleteDC(hdcMemDC);
    ReleaseDC(hWnd, hdcWindow);

    return 1;
}





void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name, EImageFormat ImageFormat, int Quality)
{

    if (Name == "")
    {
        Name = "Blank";
    }


    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [PathToSave, Name, ImageFormat, Quality] {
        GdiplusStartupInput GDIplusStartupInput;
        ULONG_PTR GDIplusToken;
        GdiplusStartup(&GDIplusToken, &GDIplusStartupInput, NULL);
        FString FormatEnd;
        switch (ImageFormat)
        {
        case EImageFormat::bpm:
            FormatEnd = ".bmp";
            break;
        case EImageFormat::jpg:
            FormatEnd = ".jpg";
            break;
        default:
            break;
        }
        FString FullPath = PathToSave + Name + FormatEnd;
        string FolderPath = std::string(TCHAR_TO_UTF8(*PathToSave));
        FolderPath.pop_back();
        std::wstring stemp = std::wstring(FolderPath.begin(), FolderPath.end());
        LPCWSTR sw = stemp.c_str();
        // get the bitmap handle to the bitmap screenshot
        HWND hWnd = static_cast<HWND>(GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle());


        fs::create_directories(FolderPath);

        CaptureAnImage(hWnd, std::string(TCHAR_TO_UTF8(*FullPath)), ImageFormat, Quality);
        // save as png to memory 


        GdiplusShutdown(GDIplusToken);

        });


    /* Native c++ multihread
    std::thread my_thread([PathToSave, Name] {


        GdiplusStartupInput GDIplusStartupInput;
        ULONG_PTR GDIplusToken;
        GdiplusStartup(&GDIplusToken, &GDIplusStartupInput, NULL);



        FString FullPath = PathToSave + FString("\\") + Name + FString(".bmp");

        // get the bitmap handle to the bitmap screenshot
        HWND hWnd = static_cast<HWND>(GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle());

        CaptureAnImage(hWnd, std::string(TCHAR_TO_UTF8(*FullPath)));
        // save as png to memory


        GdiplusShutdown(GDIplusToken);

        });

    my_thread.detach();
    */
}
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
    const int32 SrcPitch = RowPitchInPixels * GPixelFormats[ReadData->Texture->GetFormat()].BlockBytes;
    const uint32 NumBytes = CalculateImageBytes(Width, Height, 0, ReadData->Texture->GetFormat());
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
    /*
    for (int32 YIndex = 0; YIndex < Height; YIndex++)
    {
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 PixelOffset = X + (YIndex * RowPitchInPixels);

            FLinearColor& OutColor = ReadData->PixelColors.AddDefaulted_GetRef();
            switch (Format)
            {
            case EPixelFormat::PF_FloatRGBA:
            {
                FFloat16Color* OutputColor = reinterpret_cast<FFloat16Color*>(OutputBuffer) + PixelOffset;
                OutColor.R = OutputColor->R;
                OutColor.G = OutputColor->G;
                OutColor.B = OutputColor->B;
                OutColor.A = OutputColor->A;
                break;
            }
            case EPixelFormat::PF_B8G8R8A8:
            {
                FColor* OutputColor = reinterpret_cast<FColor*>(OutputBuffer) + PixelOffset;
                OutColor.R = OutputColor->R;
                OutColor.G = OutputColor->G;
                OutColor.B = OutputColor->B;
                OutColor.A = OutputColor->A;
                OutColor /= 255.f;
                break;
            }
            default:
                UE_LOG(LogTemp, Warning, TEXT("AsyncScreenshot: Unsupported RT format! Format: %d"), static_cast<int32>(Format)); // Unsupported, add a new switch statement.
            }
        }
    }
    */
    RHICmdList.UnmapStagingSurface(ReadData->Texture);
    ReadData->FinishedRead = true;
}
UAsyncScreenshotRTAction* UAsyncScreenshotRTAction::SaveRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* RenderTarget, FString PathToSave, FString Name, bool bFlushRHI)
{
    UAsyncScreenshotRTAction* BlueprintNode = NewObject<UAsyncScreenshotRTAction>();
    BlueprintNode->WorldContextObject = WorldContextObject;
    BlueprintNode->RT = RenderTarget;
    BlueprintNode->bFlushRHI = bFlushRHI;
    BlueprintNode->ReadRTData = MakeShared<FAsyncReadEntireRTData, ESPMode::ThreadSafe>();
    BlueprintNode->ReadRTData->FinishedRead = false;
    BlueprintNode->SavedPathToSave = PathToSave;
    BlueprintNode->SavedName = Name;
    return BlueprintNode;



}

UAsyncScreenshotRTAction* UAsyncScreenshotRTAction::SaveRenderTargetsMultiplyAlpha(UObject* WorldContextObject, UTextureRenderTarget2D* ColorRT, UTextureRenderTarget2D* AlphaRT, FString PathToSave, FString Name, bool bFlushRHI)
{
    UAsyncScreenshotRTAction* Node = NewObject<UAsyncScreenshotRTAction>();
    Node->WorldContextObject = WorldContextObject;
    Node->RT = ColorRT;
    Node->AlphaRT = AlphaRT;
    Node->CombineMode = EAsyncRTCombineMode::MultiplyAlpha;
    Node->bFlushRHI = bFlushRHI;
    Node->SavedPathToSave = PathToSave;
    Node->SavedName = Name;
    Node->CombinedData = MakeShared<FAsyncReadCombinedRTData, ESPMode::ThreadSafe>();
    Node->CombinedData->ColorRT = MakeShared<FAsyncReadEntireRTData, ESPMode::ThreadSafe>();
    Node->CombinedData->AlphaRT = MakeShared<FAsyncReadEntireRTData, ESPMode::ThreadSafe>();
    Node->CombinedData->Mode = Node->CombineMode;
    return Node;
}

void WritePixelsToFile(FTextureRHIRef RenderTarget, FString PathToSave, FString Name, TArray<FColor>& PixelToCopy, TWeakObjectPtr<UAsyncScreenshotRTAction> Action)
{

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Action, RenderTarget, PathToSave, Name, &PixelToCopy]() {
        int32 InSizeX = RenderTarget->GetSizeX();
        int32 InSizeY = RenderTarget->GetSizeY();
        SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::WriteImage", FColor::Magenta);


        TArray<FColor> PixelArray;
        int32 sizeOfImage = InSizeX * InSizeY;
        PixelArray.SetNum(sizeOfImage);


        PixelArray = std::move(PixelToCopy);
        int32 size = PixelArray.Num();

        FString FullPath = PathToSave + Name + FString(".png");
        string FolderPath = std::string(TCHAR_TO_UTF8(*FullPath));

        std::vector<uint8> data;
        FString pixString;
        for (int i = 0; i < size; i++) {
            data.push_back(PixelArray[i].R);
            data.push_back(PixelArray[i].G);
            data.push_back(PixelArray[i].B);
            data.push_back(PixelArray[i].A);
        }
        fs::create_directories(fs::path(FolderPath).parent_path());
        stbi_write_png(FolderPath.data(), InSizeX, InSizeY, 4, static_cast<void*>(data.data()), 4 * InSizeX);

        AsyncTask(ENamedThreads::GameThread, [Action]() {
            if (Action.IsValid())
            {
                Action->OnSaveRenderTarget.Broadcast();
                Action->SetReadyToDestroy();
            }
            });

        });
}

#endif
FString UAsyncScreenShotBPLibrary::GetScreenshotSavePath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir());
}
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
            WritePixelsToFile(ReadRTData->Texture, SavedPathToSave, SavedName, ReadRTData->PixelColors, this);
            return;
        }
        else
        {
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

        for (int32 i = 0; i < Color.Num(); ++i)
        {
            uint8 A1 = Color[i].A / 255;
            uint8 A2 = (255 - Alpha[i].A) / 255;
            Color[i].A = uint8(FMath::Clamp(A1 * A2, 0.f, 1.f) * 255);
        }

        WritePixelsToFile(CombinedData->ColorRT->Texture, SavedPathToSave, SavedName, Color, this);
        return;
        break;
    }
    }
    
}
#if !PLATFORM_WINDOWS
void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name)
{
}
#endif

void UAsyncScreenshotRTAction::Activate()
{
    switch (CombineMode)
    {
    case EAsyncRTCombineMode::SingleRT:
    {
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
                        FTextureRHIRef CPUTexture = RHICmdList.CreateTexture(Desc);

                        RHICmdList.CopyTexture(Src, CPUTexture, {});
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

