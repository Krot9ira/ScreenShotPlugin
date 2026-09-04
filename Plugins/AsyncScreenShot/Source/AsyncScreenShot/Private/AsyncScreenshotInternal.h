// Copyright Grigoryev Daniil. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include <stdio.h>

// Platform-agnostic file helpers shared by the GDI window capture path (AsyncScreenshotWinCapture.cpp,
// Windows-only) and the RHI render target readback path (AsyncScreenShotBPLibrary.cpp, portable).
namespace AsyncScreenShot::Private
{
	// stb write callback: forwards the encoded bytes to the FILE* passed as context.
	void PngWriteCallback(void* Context, void* Data, int Size);

	bool PathExists(const FString& Path);
	void CreateDirectoriesForFile(const FString& FilePath);
	FILE* OpenFileForWrite(const FString& Path);

	// When bAutoUniqueName is true and FullPath already exists, appends a numeric suffix (_0001, _0002, ...)
	// until a free path is found, instead of silently overwriting an existing screenshot.
	FString MakeUniquePath(const FString& FullPath, bool bAutoUniqueName);
}
