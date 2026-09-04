// Copyright (c) 2026 Daniil Grigoryev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AsyncScreenshotTypes.generated.h"

class UTexture2D;

/**
 * Output image format.
 *
 * Deliberately not called EImageFormat: the engine's ImageWrapper module already declares a global
 * enum by that name, so any consumer translation unit that included both failed to compile.
 */
UENUM(BlueprintType)
enum class EAsyncScreenshotImageFormat : uint8
{
	Bmp UMETA(DisplayName = "BMP"),
	Jpg UMETA(DisplayName = "JPG"),
	Png UMETA(DisplayName = "PNG")
};

/**
 * Result of a capture that writes a single file.
 *
 * FullPath is the file that was actually written, which is not necessarily the path that was requested:
 * bAutoUniqueName appends a numeric suffix rather than overwriting. It is empty when nothing was written.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsyncScreenshotFilePin, const FString&, FullPath);

/**
 * Result of a render target capture.
 *
 * FullPath is empty when the capture was asked not to save to disk; CapturedTexture is null unless the
 * capture was asked to return one. On failure both are empty.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAsyncScreenshotRenderTargetPin, const FString&, FullPath, UTexture2D*, CapturedTexture);
