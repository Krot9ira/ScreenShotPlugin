// Copyright Grigoryev Daniil. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncScreenShotBPLibrary.h"
#include "AsyncScreenshotWindowAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsyncWindowCapturedPin, FString, FullPath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAsyncWindowFailedPin);

// Captures the game window (same underlying capture as UAsyncScreenShotBPLibrary::SaveGameScreen) but as a proper
// async Blueprint node with success/failure output pins, plus optional region-crop and downscale.
UCLASS()
class UAsyncScreenshotWindowAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"), Category = "ScreenshotTaker Functionality")
	static UAsyncScreenshotWindowAction* CaptureGameScreen(FString PathToSave, FString Name, EImageFormat ImageFormat, int32 Quality = 80, bool bAutoUniqueName = false,
		int32 CropX = -1, int32 CropY = -1, int32 CropWidth = -1, int32 CropHeight = -1, float DownscaleFactor = 1.0f);

	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FAsyncWindowCapturedPin OnSaved;

	UPROPERTY(BlueprintAssignable)
	FAsyncWindowFailedPin OnFailed;

	FString PathToSave;
	FString Name;
	EImageFormat ImageFormat;
	int32 Quality = 80;
	bool bAutoUniqueName = false;
	int32 CropX = -1;
	int32 CropY = -1;
	int32 CropWidth = -1;
	int32 CropHeight = -1;
	float DownscaleFactor = 1.0f;
};
