// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

#pragma once
#include "AsyncScreenShotBPLibrary.h"
#include "AsyncScreenShot.h"
#include "Async/TaskGraphInterfaces.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "RenderUtils.h"
#include "Widgets/SWindow.h"
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


int CaptureAnImage(HWND hWnd, string path)
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
    hbmScreen = CreateCompatibleBitmap(hdcWindow, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);

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
    
    hFile = CreateFile(sw,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);

    // Add the size of the headers to the size of the bitmap to get the total file size.
    dwSizeofDIB = dwBmpSize + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    // Offset to where the actual bitmap bits start.
    bmfHeader.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER);

    // Size of the file.
    bmfHeader.bfSize = dwSizeofDIB;

    // bfType must always be BM for Bitmaps.
    bmfHeader.bfType = 0x4D42; // BM.

    WriteFile(hFile, (LPSTR)&bmfHeader, sizeof(BITMAPFILEHEADER), &dwBytesWritten, NULL);
    WriteFile(hFile, (LPSTR)&bi, sizeof(BITMAPINFOHEADER), &dwBytesWritten, NULL);
    WriteFile(hFile, (LPSTR)lpbitmap, dwBmpSize, &dwBytesWritten, NULL);

    // Unlock and Free the DIB from the heap.
    GlobalUnlock(hDIB);
    GlobalFree(hDIB);

    // Close the handle for the file that was created.
    CloseHandle(hFile);

    // Clean up.

    DeleteObject(hbmScreen);
    DeleteObject(hdcMemDC);
    ReleaseDC(NULL, hdcScreen);
    ReleaseDC(hWnd, hdcWindow);

    return 0;
}




void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name)
{
   
    if (Name == "")
    {
        Name = "Blank";
    }
    
    
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [PathToSave, Name] {
        GdiplusStartupInput GDIplusStartupInput;
        ULONG_PTR GDIplusToken;
        GdiplusStartup(&GDIplusToken, &GDIplusStartupInput, NULL);

        FString FullPath = PathToSave  + Name + FString(".bmp");
        string FolderPath = std::string(TCHAR_TO_UTF8(*PathToSave));
        FolderPath.pop_back();
        std::wstring stemp = std::wstring(FolderPath.begin(), FolderPath.end());
        LPCWSTR sw = stemp.c_str();
        // get the bitmap handle to the bitmap screenshot
        HWND hWnd = static_cast<HWND>(GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle());

        //If folder not exist image will not save, so i make folder to be sure
        filesystem::create_directories(FolderPath);
        CaptureAnImage(hWnd, std::string(TCHAR_TO_UTF8(*FullPath)));
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

void UAsyncScreenShotBPLibrary::SaveRenderTarget(UTextureRenderTarget2D* RenderTarget, FString PathToSave, FString Name)
{
    if (!RenderTarget || RenderTarget->GetTextureFormatForConversionToTexture2D() != ETextureSourceFormat::TSF_RGBA16F)
    {
        return;
    }
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [RenderTarget, PathToSave, Name]() {
    int32 InSizeX = RenderTarget->SizeX;
    int32 InSizeY = RenderTarget->SizeY;

    struct FCopyBufferData {
        TPromise<void> Promise;
        TArray<FFloat16Color> StructDestBuffer;
    };

    TArray<FFloat16Color> PixelArray;
    int32 sizeOfImage = InSizeX * InSizeY;
    PixelArray.SetNum(sizeOfImage);

    using FCommandDataPtr = TSharedPtr<FCopyBufferData, ESPMode::ThreadSafe>;
    FCommandDataPtr CommandData = MakeShared<FCopyBufferData, ESPMode::ThreadSafe>();
    CommandData->StructDestBuffer.SetNum(sizeOfImage);

    auto Future = CommandData->Promise.GetFuture();

    //CopyTextureToArray(InTexture, PixelArray);
    ENQUEUE_RENDER_COMMAND(CopyTexture2DToArray)([Data = CommandData, RenderTarget, bFlush = false](FRHICommandListImmediate& RHICmdList)
        {
    if (RenderTarget && RenderTarget->GetResource() && RenderTarget->GetResource()->TextureRHI.IsValid() && RenderTarget->GetResource()->TextureRHI->GetTexture2D()->IsValid()) {
        UE_LOG(LogTemp, Warning, TEXT("Valid, Starting Read"));
        const uint32 NumBytes = CalculateImageBytes(RenderTarget->SizeX, RenderTarget->SizeY, 0, RenderTarget->GetFormat());

            
         uint32 DestStride = 0;
         FFloat16Color* DestBuffer = static_cast<FFloat16Color*>(RHILockTexture2D(RenderTarget->GetResource()->TextureRHI->GetTexture2D(), 0, EResourceLockMode::RLM_ReadOnly, DestStride, false));

         FMemory::Memcpy(Data->StructDestBuffer.GetData(), DestBuffer, NumBytes);

         RHIUnlockTexture2D(RenderTarget->GetResource()->TextureRHI->GetTexture2D(), 0, false);
         Data->Promise.SetValue();
    }
            });

    

    Future.Get();
    PixelArray = std::move(CommandData->StructDestBuffer);
    int32 size = PixelArray.Num();

    FString FullPath = PathToSave + Name + FString(".png");
    string FolderPath = std::string(TCHAR_TO_UTF8(*FullPath));

    std::vector<uint8> data;
    FString pixString;
    for (int i = 0; i < size; i++) {
        data.push_back(PixelArray[i].GetFloats().ToFColor(true).R);
        data.push_back(PixelArray[i].GetFloats().ToFColor(true).G);
        data.push_back(PixelArray[i].GetFloats().ToFColor(true).B);
        data.push_back(PixelArray[i].GetFloats().ToFColor(true).A);
    }
    stbi_write_png(FolderPath.data(), RenderTarget->SizeX, RenderTarget->SizeY, 4, static_cast<void*>(data.data()), 4 * RenderTarget->SizeX); });
    
}

#endif
FString UAsyncScreenShotBPLibrary::GetScreenshotSavePath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir());
}
#if !PLATFORM_WINDOWS
void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name)
{
}
#endif
