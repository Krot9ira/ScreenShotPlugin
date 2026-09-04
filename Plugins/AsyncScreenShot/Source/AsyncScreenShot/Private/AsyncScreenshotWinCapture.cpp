// Copyright Grigoryev Daniil. All Rights Reserved.

#include "AsyncScreenshotWinCapture.h"

#if PLATFORM_WINDOWS

#include "AsyncScreenshotInternal.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
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
// Converts the bottom-up 32bpp BGRA rows GDI returns into a top-down RGBA buffer. Alpha is forced to
// opaque: PrintWindow does not fill in meaningful alpha for ordinary (non-layered) windows, so trusting
// what it hands back usually produces a fully transparent image.
static void Bgra2Rgba(const uint8* Src, int32 Width, int32 Height, uint8* Dst)
{
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const uint8* SrcRow = Src + (size_t)(Height - 1 - Y) * Width * 4;
		uint8* DstRow = Dst + (size_t)Y * Width * 4;
		for (int32 X = 0; X < Width; ++X)
		{
			DstRow[X * 4 + 0] = SrcRow[X * 4 + 2];
			DstRow[X * 4 + 1] = SrcRow[X * 4 + 1];
			DstRow[X * 4 + 2] = SrcRow[X * 4 + 0];
			DstRow[X * 4 + 3] = 255;
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
	if (!hWnd)
	{
		return false;
	}

	RECT ClientRect;
	if (!GetClientRect(hWnd, &ClientRect))
	{
		return false;
	}

	int32 Width = ClientRect.right - ClientRect.left;
	int32 Height = ClientRect.bottom - ClientRect.top;
	if (Width <= 0 || Height <= 0)
	{
		return false;
	}

	HDC WindowDC = GetDC(hWnd);
	if (!WindowDC)
	{
		return false;
	}
	ON_SCOPE_EXIT{ ReleaseDC(hWnd, WindowDC); };

	HDC MemoryDC = CreateCompatibleDC(WindowDC);
	if (!MemoryDC)
	{
		return false;
	}
	ON_SCOPE_EXIT{ DeleteDC(MemoryDC); };

	HBITMAP Bitmap = CreateCompatibleBitmap(WindowDC, Width, Height);
	if (!Bitmap)
	{
		return false;
	}

	// A bitmap that is still selected into a DC cannot be deleted, so the DC's original bitmap has to go
	// back first. Without this DeleteObject quietly fails and every capture leaks a GDI object.
	HGDIOBJ PreviousBitmap = SelectObject(MemoryDC, Bitmap);
	ON_SCOPE_EXIT
	{
		SelectObject(MemoryDC, PreviousBitmap);
		DeleteObject(Bitmap);
	};

	if (!PrintWindow(hWnd, MemoryDC, PW_RENDERFULLCONTENT))
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: PrintWindow failed for the game window"));
		return false;
	}

	BITMAPINFOHEADER BitmapInfo = {};
	BitmapInfo.biSize = sizeof(BITMAPINFOHEADER);
	BitmapInfo.biWidth = Width;
	BitmapInfo.biHeight = Height;
	BitmapInfo.biPlanes = 1;
	BitmapInfo.biBitCount = 32;
	BitmapInfo.biCompression = BI_RGB;

	// 32bpp DIB rows are always DWORD aligned, so the stride is exactly Width * 4 and a plain array will do -
	// no need for the GlobalAlloc/GlobalLock pair this used to carry, whose results were never checked.
	TArray<uint8> Dib;
	Dib.SetNumUninitialized(Width * Height * 4);

	if (GetDIBits(MemoryDC, Bitmap, 0, Height, Dib.GetData(), reinterpret_cast<BITMAPINFO*>(&BitmapInfo), DIB_RGB_COLORS) == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: GetDIBits failed for the captured window"));
		return false;
	}

	std::vector<unsigned char> Rgba((size_t)Width * Height * 4);
	Bgra2Rgba(Dib.GetData(), Width, Height, Rgba.data());

	CropAndDownscaleBytes(Rgba, Width, Height, 4, CropX, CropY, CropWidth, CropHeight, DownscaleFactor);

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
		bWriteOk = stbi_write_jpg_to_func(PngWriteCallback, File, Width, Height, 4, Rgba.data(), Quality) != 0;
		break;
	case EImageFormat::png:
		bWriteOk = stbi_write_png_to_func(PngWriteCallback, File, Width, Height, 4, Rgba.data(), Width * 4) != 0;
		break;
	case EImageFormat::bpm:
	default:
		bWriteOk = WriteBmpFromRgba(File, Rgba, Width, Height);
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
