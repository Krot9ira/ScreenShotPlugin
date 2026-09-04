// Copyright (c) 2026 Daniil Grigoryev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Shared by the GDI window capture path (AsyncScreenshotWinCapture.cpp, Windows only) and the RHI render
// target readback path (AsyncScreenShotBPLibrary.cpp, portable).
namespace AsyncScreenShot::Private
{
	/** stb write callback: appends the encoded bytes to the TArray<uint8> passed as context. */
	void AppendEncodedBytes(void* Context, void* Data, int32 Size);

	/**
	 * Applies the plugin-wide PNG compression level and runs Encode.
	 *
	 * stb keeps that level in a global, so concurrent captures would otherwise race over it. Encoding is
	 * already off the game thread, so serialising the encodes costs nothing that matters here.
	 */
	bool EncodeWithPngCompressionLevel(TFunctionRef<bool()> Encode);

	/** True if the file or directory exists. */
	ASYNCSCREENSHOT_API bool PathExists(const FString& Path);

	/** Creates the directory tree the file will live in. */
	bool CreateDirectoriesForFile(const FString& FilePath);

	/**
	 * Writes Bytes to FullPath through the engine's file system, creating directories as needed.
	 * Logs and returns false on failure, which the raw fwrite this replaced could not report.
	 */
	bool WriteFile(const FString& FullPath, const TArray<uint8>& Bytes);

	/**
	 * When bAutoUniqueName is true and FullPath already exists, appends a numeric suffix (_0001, _0002,
	 * ...) until a free path is found, instead of silently overwriting an existing screenshot.
	 */
	ASYNCSCREENSHOT_API FString MakeUniquePath(const FString& FullPath, bool bAutoUniqueName);

	/**
	 * Crops (optionally) and then downscales (optionally, nearest neighbour) a pixel buffer in place.
	 * Width and Height are updated to the new dimensions. A crop rectangle that runs off the edge is
	 * clamped rather than rejected; a DownscaleFactor of 1 or more is ignored, this never upscales.
	 */
	ASYNCSCREENSHOT_API void CropAndDownscaleColors(TArray<FColor>& Pixels, int32& Width, int32& Height,
		int32 CropX, int32 CropY, int32 CropWidth, int32 CropHeight, float DownscaleFactor);
}
