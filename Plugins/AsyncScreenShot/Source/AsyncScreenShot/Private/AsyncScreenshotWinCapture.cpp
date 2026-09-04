// Copyright Grigoryev Daniil. All Rights Reserved.

#include "AsyncScreenshotWinCapture.h"

#if PLATFORM_WINDOWS

#include "AsyncScreenshotInternal.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "Widgets/SWindow.h"

#include "stb_image_write.h"

#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace AsyncScreenShot::Private
{
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
}

#endif // PLATFORM_WINDOWS
