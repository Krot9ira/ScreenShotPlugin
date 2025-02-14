// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

#pragma once
#include "AsyncScreenShotBPLibrary.h"
#include "AsyncScreenShot.h"
#include "Async/TaskGraphInterfaces.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "RenderUtils.h"
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

int CaptureAnImage(HWND hWnd, string path, EImageFormat ImageFormat, int quality)
{
    HDC hdcScreen;
    HDC hdcWindow;
    HDC hdcMemDC = NULL;
    HBITMAP hbmScreen = NULL;
    BITMAP bmpScreen;
    DWORD dwBytesWritten = 0;
    DWORD dwSizeofDIB = 0;
    HANDLE hFile = NULL;
    char* lpbitmap = NULL;
    HANDLE hDIB = NULL;
    DWORD dwBmpSize = 0;

    // Retrieve the handle to a display device context for the client 
    // area of the window. 
    hdcScreen = GetDC(0);
    hdcWindow = GetDC(hWnd);

    // Create a compatible DC, which is used in a BitBlt from the window DC.
    hdcMemDC = CreateCompatibleDC(hdcWindow);

    if (!hdcMemDC)
    {
        MessageBox(hWnd, L"CreateCompatibleDC has failed", L"Failed", MB_OK);
        DeleteObject(hbmScreen);
        DeleteObject(hdcMemDC);
        ReleaseDC(NULL, hdcScreen);
        ReleaseDC(hWnd, hdcWindow);

        return 0;
    }

    // Get the client area for size calculation.
    RECT rcClient;
    RECT rcWindow;
    GetClientRect(hWnd, &rcClient);
    GetWindowRect(hWnd, &rcWindow);
    // This is the best stretch mode.
    SetStretchBltMode(hdcWindow, COLORONCOLOR);
    // TakeAllScreen
    if (!StretchBlt(hdcWindow,
        0, 0,
        rcClient.right, rcClient.bottom,
        hdcScreen,
        rcWindow.left, rcWindow.top,
        rcClient.right, rcClient.bottom,
        SRCCOPY))
    {
        MessageBox(hWnd, L"StretchBlt has failed", L"Failed", MB_OK);
        DeleteObject(hbmScreen);
        DeleteObject(hdcMemDC);
        ReleaseDC(NULL, hdcScreen);
        ReleaseDC(hWnd, hdcWindow);

        return 0;
    }




    // Create a compatible bitmap from the Window DC.
    long x = rcClient.right - rcClient.left;
    long y = rcClient.bottom - rcClient.top;
    hbmScreen = CreateCompatibleBitmap(hdcWindow, x, y);

    if (!hbmScreen)
    {
        MessageBox(hWnd, L"CreateCompatibleBitmap Failed", L"Failed", MB_OK);
        DeleteObject(hbmScreen);
        DeleteObject(hdcMemDC);
        ReleaseDC(NULL, hdcScreen);
        ReleaseDC(hWnd, hdcWindow);

        return 0;
    }

    // Select the compatible bitmap into the compatible memory DC.
    SelectObject(hdcMemDC, hbmScreen);

    // Bit block transfer into our compatible memory DC.
    if (!BitBlt(hdcMemDC,
        0, 0,
        rcClient.right - rcClient.left, rcClient.bottom - rcClient.top,
        hdcWindow,
        0, 0,
        SRCCOPY))
    {
        MessageBox(hWnd, L"BitBlt has failed", L"Failed", MB_OK);
        DeleteObject(hbmScreen);
        DeleteObject(hdcMemDC);
        ReleaseDC(NULL, hdcScreen);
        ReleaseDC(hWnd, hdcWindow);

        return 0;
    }

    // Get the BITMAP from the HBITMAP.
    GetObject(hbmScreen, sizeof(BITMAP), &bmpScreen);

    BITMAPFILEHEADER   bmfHeader;
    BITMAPINFOHEADER   bi;

    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmpScreen.bmWidth;
    bi.biHeight = bmpScreen.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    dwBmpSize = ((bmpScreen.bmWidth * bi.biBitCount + 31) / 32) * 4 * bmpScreen.bmHeight;

    // Starting with 32-bit Windows, GlobalAlloc and LocalAlloc are implemented as wrapper functions that 
    // call HeapAlloc using a handle to the process's default heap. Therefore, GlobalAlloc and LocalAlloc 
    // have greater overhead than HeapAlloc.
    hDIB = GlobalAlloc(GHND, dwBmpSize);
    lpbitmap = (char*)GlobalLock(hDIB);

    // Gets the "bits" from the bitmap, and copies them into a buffer 
    // that's pointed to by lpbitmap.
    GetDIBits(hdcWindow, hbmScreen, 0,
        (UINT)bmpScreen.bmHeight,
        lpbitmap,
        (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    std::wstring stemp = std::wstring(path.begin(), path.end());
    LPCWSTR sw = stemp.c_str();
    // A file is created, this is where we will save the screen capture.
    if (ImageFormat == EImageFormat::bpm)
    {
        hFile = CreateFile(sw,
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
    }


    // Add the size of the headers to the size of the bitmap to get the total file size.
    dwSizeofDIB = dwBmpSize + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    // Offset to where the actual bitmap bits start.
    bmfHeader.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER);

    // Size of the file.
    bmfHeader.bfSize = dwSizeofDIB;

    // bfType must always be BM for Bitmaps.
    bmfHeader.bfType = 0x4D42; // BM.
    unsigned char* rgb;
    switch (ImageFormat)
    {
    case EImageFormat::bpm:
        WriteFile(hFile, (LPSTR)&bmfHeader, sizeof(BITMAPFILEHEADER), &dwBytesWritten, NULL);
        WriteFile(hFile, (LPSTR)&bi, sizeof(BITMAPINFOHEADER), &dwBytesWritten, NULL);
        WriteFile(hFile, (LPSTR)lpbitmap, dwBmpSize, &dwBytesWritten, NULL);
        break;
    case EImageFormat::jpg:
        rgb = new unsigned char[x * abs(y) * 32 / 8];
        Bgra2Rgb((unsigned char*)lpbitmap, x, y, 32 / 8, rgb);

        stbi_write_jpg(path.data(), x, y, 3, rgb, quality);
        break;
    default:
        break;
    }


    // Unlock and Free the DIB from the heap.
    GlobalUnlock(hDIB);
    GlobalFree(hDIB);

    // Close the handle for the file that was created.
    if (ImageFormat == EImageFormat::bpm)
    {
        CloseHandle(hFile);
    }
    // Clean up.

    DeleteObject(hbmScreen);
    DeleteObject(hdcMemDC);
    ReleaseDC(NULL, hdcScreen);
    ReleaseDC(hWnd, hdcWindow);

    return 0;
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

        //If folder not exist image will not save, so i make folder to be sure
        if (ImageFormat == EImageFormat::bpm)
        {
            filesystem::create_directories(FolderPath);
        }

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
        FMemory::Memmove(ReadData->PixelColors.GetData(), OutputBuffer, NumBytes);
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
            data.push_back(255);
        }
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
}
#if !PLATFORM_WINDOWS
void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name)
{
}
#endif

void UAsyncScreenshotRTAction::Activate()
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
                IORHITextureCPU = GDynamicRHI->RHICreateTexture(FRHICommandListExecutor::GetImmediateCommandList(), TextureDesc);
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
}