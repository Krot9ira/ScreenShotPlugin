// Copyright Grigoryev Daniil. All Rights Reserved.

#pragma once

#include "AsyncScreenShotBPLibrary.h"

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

// Captures the given window's client area (optionally cropped/downscaled) and writes it to disk as PNG/JPG/BMP.
// OutFullPath receives the path actually written to (may differ from the requested one if bAutoUniqueName avoided an overwrite).
// Returns true on success.
bool CaptureWindowToFile(HWND hWnd, const FString& PathToSave, const FString& Name, EImageFormat ImageFormat, int32 Quality, bool bAutoUniqueName,
	int32 CropX, int32 CropY, int32 CropWidth, int32 CropHeight, float DownscaleFactor, FString& OutFullPath);

// Fetches the native HWND of the game's main viewport window. Must be called on the game thread
// (GEngine->GameViewport is not thread-safe). Returns nullptr if no valid window is available.
HWND GetActiveGameWindow();

#endif // PLATFORM_WINDOWS
