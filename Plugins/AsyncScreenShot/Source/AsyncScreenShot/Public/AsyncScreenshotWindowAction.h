// Copyright Grigoryev Daniil. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncScreenshotTypes.h"
#include "AsyncScreenshotWindowAction.generated.h"

/**
 * Captures the game window through the OS rather than the renderer, so the result includes everything on
 * screen, UMG widgets included. The encode and the file write run on a background thread.
 *
 * Same capture as UAsyncScreenShotBPLibrary::SaveGameScreen, but as a proper async node: it reports where
 * the file landed, says so when it fails, and can crop and downscale.
 */
UCLASS()
class ASYNCSCREENSHOT_API UAsyncScreenshotWindowAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Crop values below zero mean "no crop". DownscaleFactor only scales down; values >= 1 are ignored. */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"), Category = "AsyncScreenShot")
	static UAsyncScreenshotWindowAction* CaptureGameScreen(FString PathToSave, FString Name, EAsyncScreenshotImageFormat ImageFormat, int32 Quality = 80, bool bAutoUniqueName = false,
		int32 CropX = -1, int32 CropY = -1, int32 CropWidth = -1, int32 CropHeight = -1, float DownscaleFactor = 1.0f);

	virtual void Activate() override;

	/** The file that was written. Not necessarily the requested name: bAutoUniqueName may have suffixed it. */
	UPROPERTY(BlueprintAssignable)
	FAsyncScreenshotFilePin OnSaved;

	/** Fired with an empty path when the window could not be captured or the file could not be written. */
	UPROPERTY(BlueprintAssignable)
	FAsyncScreenshotFilePin OnFailed;

	FString PathToSave;
	FString Name;
	EAsyncScreenshotImageFormat ImageFormat = EAsyncScreenshotImageFormat::Png;
	int32 Quality = 80;
	bool bAutoUniqueName = false;
	int32 CropX = -1;
	int32 CropY = -1;
	int32 CropWidth = -1;
	int32 CropHeight = -1;
	float DownscaleFactor = 1.0f;
};
