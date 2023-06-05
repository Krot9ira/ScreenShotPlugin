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
    HDC hwindowDC = GetDC(0);
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




void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name)
{
   
    if (Name == "")
    {
        Name = "Blank";
    }
    
    /* Unreal engine async method
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [] {
        GdiplusStartupInput GDIplusStartupInput;
        ULONG_PTR GDIplusToken;
        GdiplusStartup(&GDIplusToken, &GDIplusStartupInput, NULL);
        // get the bitmap handle to the bitmap screenshot
        HWND hWnd = GetForegroundWindow();
        HBITMAP hBmp = GdiPlusScreenCapture(hWnd);

        // save as png to memory 
        std::vector<BYTE> data;
        std::string dataFormat = "bmp";

        if (SaveToMemory(&hBmp, data, dataFormat))
        {
            // save from memory to file
            std::ofstream fout("C:\\Users\\kahas\\Pictures\\Camera Roll\\Screenshot-m1." + dataFormat, std::ios::binary);
            fout.write((char*)data.data(), data.size());
        }
        GdiplusShutdown(GDIplusToken);

        });

    
    */
    std::thread my_thread([PathToSave, Name] {
        
        GdiplusStartupInput GDIplusStartupInput;
        ULONG_PTR GDIplusToken;
        GdiplusStartup(&GDIplusToken, &GDIplusStartupInput, NULL);

        FString FullPath = PathToSave + FString("\\") + Name;
   
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
    
}
#endif

#if !PLATFORM_WINDOWS
void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name)
{
}
#endif
