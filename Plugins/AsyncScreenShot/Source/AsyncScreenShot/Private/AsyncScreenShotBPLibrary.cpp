// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncScreenShotBPLibrary.h"
#include "AsyncScreenShot.h"

UAsyncScreenShotBPLibrary::UAsyncScreenShotBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}


#if PLATFORM_WINDOWS
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <tchar.h>
#include <thread>
#include <stdio.h>
#include <fstream>
#include <iostream>


using namespace Gdiplus;
using namespace std;

#pragma comment(lib,"gdiplus.lib")


/**
 * Create a Bitmap file header..
 *
 * @param hwindowDC : window handle.
 * @param widht	    : image width.
 * @param height    : image height.
 *
 * @return Bitmap header.
 */
BITMAPINFOHEADER createBitmapHeader(int width, int height)
{
    BITMAPINFOHEADER  bi;

    // create a bitmap
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;  //this is the line that makes it draw upside down or not
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    return bi;
}

/**
 * Capture a screen and return the handle to its bitmap.
 *
 * @param hwnd : window handle.
 */
HBITMAP GdiPlusScreenCapture(HWND hWnd)
{
    // get handles to a device context (DC)

    HDC hwindowDC = GetDC(hWnd);
    
    HDC hwindowCompatibleDC = CreateCompatibleDC(hwindowDC);
    SetStretchBltMode(hwindowCompatibleDC, COLORONCOLOR);

    // define scale, height and width
    int scale = 1;
    int screenx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screeny = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    RECT rect;
    if (GetWindowRect(hWnd, &rect))
    {
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
    }
    
    // create a bitmap
    HBITMAP hbwindow = CreateCompatibleBitmap(hwindowDC, width, height);
    BITMAPINFOHEADER bi = createBitmapHeader(width, height);

    // use the previously created device context with the bitmap
    SelectObject(hwindowCompatibleDC, hbwindow);

    // Starting with 32-bit Windows, GlobalAlloc and LocalAlloc are implemented as wrapper functions that call HeapAlloc using a handle to the process's default heap. 
    // Therefore, GlobalAlloc and LocalAlloc have greater overhead than HeapAlloc.
    DWORD dwBmpSize = ((width * bi.biBitCount + 31) / 32) * 4 * height;
    HANDLE hDIB = GlobalAlloc(GHND, dwBmpSize);
    char* lpbitmap = (char*)GlobalLock(hDIB);

    // copy from the window device context to the bitmap device context
    StretchBlt(hwindowCompatibleDC, 0, 0, width, height, hwindowDC, screenx, screeny, width, height, SRCCOPY);   //change SRCCOPY to NOTSRCCOPY for wacky colors !
    GetDIBits(hwindowCompatibleDC, hbwindow, 0, height, lpbitmap, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    // avoid memory leak
    DeleteDC(hwindowCompatibleDC);
    ReleaseDC(hWnd, hwindowDC);
    //delete lpbitmap;
    
    return hbwindow;
}

/**
 * Save a bitmap to memory using its handle.
 *
 * @param hbitmap    : pointer to a bitmap handle.
 * @param data       : pointer to a vector of bytes.
 * @param dataformat : format of datatype to save data according to it.
 *
 * @return boolean representing whether the saving successful was or not.
 */
bool SaveToMemory(HBITMAP* hbitmap, std::vector<BYTE>& data, std::string dataFormat = "png")
{
    Gdiplus::Bitmap bmp(*hbitmap, nullptr);
    // write to IStream
    IStream* istream = nullptr;
    CreateStreamOnHGlobal(NULL, TRUE, &istream);

    // define encoding
    CLSID clsid;
    if (dataFormat.compare("bmp") == 0) { CLSIDFromString(L"{557cf400-1a04-11d3-9a73-0000f81ef32e}", &clsid); }
    else if (dataFormat.compare("jpg") == 0) { CLSIDFromString(L"{557cf401-1a04-11d3-9a73-0000f81ef32e}", &clsid); }
    else if (dataFormat.compare("gif") == 0) { CLSIDFromString(L"{557cf402-1a04-11d3-9a73-0000f81ef32e}", &clsid); }
    else if (dataFormat.compare("tif") == 0) { CLSIDFromString(L"{557cf405-1a04-11d3-9a73-0000f81ef32e}", &clsid); }
    else if (dataFormat.compare("png") == 0) { CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &clsid); }

    Gdiplus::Status status = bmp.Save(istream, &clsid, NULL);
    if (status != Gdiplus::Status::Ok)
        return false;

    // get memory handle associated with istream
    HGLOBAL hg = NULL;
    GetHGlobalFromStream(istream, &hg);

    // copy IStream to buffer
    int bufsize = GlobalSize(hg);
    data.resize(bufsize);

    // lock & unlock memory
    LPVOID pimage = GlobalLock(hg);
    memcpy(&data[0], pimage, bufsize);
    
    GlobalUnlock(hg);
    istream->Release();
    return true;
}

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
    GetClientRect(hWnd, &rcClient);
    WINDOWPLACEMENT WindowPosition = {};
    WindowPosition.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hWnd, &WindowPosition);
    // This is the best stretch mode.
    SetStretchBltMode(hdcWindow, COLORONCOLOR);
    
    // The source DC is the entire screen, and the destination DC is the current window (HWND).
    if (!StretchBlt(hdcWindow,
        0, 0,
        rcClient.right, rcClient.bottom,
        hdcScreen,
        WindowPosition.rcNormalPosition.left, WindowPosition.rcNormalPosition.top,
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

       

        FString FullPath = PathToSave + FString("\\") + Name + FString(".bmp");

        // get the bitmap handle to the bitmap screenshot
        HWND hWnd = static_cast<HWND>(GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle());

        CaptureAnImage(hWnd, std::string(TCHAR_TO_UTF8(*FullPath)));
        // save as png to memory 
       

        GdiplusShutdown(GDIplusToken);

        });

    
    /* Unreal engine async method
    std::thread my_thread([PathToSave, Name] {
        

        GdiplusStartupInput GDIplusStartupInput;
        ULONG_PTR GDIplusToken;
        GdiplusStartup(&GDIplusToken, &GDIplusStartupInput, NULL);

        FString FullPath = PathToSave + FString("\\") + Name + FString(".");
   
        // get the bitmap handle to the bitmap screenshot
        HWND hWnd = static_cast<HWND>(GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle());
        HBITMAP hBmp = GdiPlusScreenCapture(hWnd);

        // save as png to memory 
        std::vector<BYTE> data;
        std::string dataFormat = "bmp";

        if (SaveToMemory(&hBmp, data, dataFormat))
        {
            // save from memory to file
            std::ofstream fout(std::string(TCHAR_TO_UTF8(*FullPath))+dataFormat, std::ios::binary);
            fout.write((char*)data.data(), data.size());
        }

        GdiplusShutdown(GDIplusToken);

        });
   
    my_thread.detach();
    */
}
#endif

#if !PLATFORM_WINDOWS
void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name)
{
}
#endif
