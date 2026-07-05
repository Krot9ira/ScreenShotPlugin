// Copyright Grigoryev Daniil. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncScreenShotBPLibrary.h"
#include "AsyncSuperResolutionScreenshotAction.generated.h"

class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class AActor;

// Captures the active player's camera view at a resolution multiplier above the current viewport size
// (e.g. 4x) by rendering it through a temporary SceneCaptureComponent2D, then saves it via UAsyncScreenshotRTAction.
UCLASS()
class UAsyncSuperResolutionScreenshotAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "ScreenshotTaker Functionality")
	static UAsyncSuperResolutionScreenshotAction* CaptureSuperResolutionScreenshot(UObject* WorldContextObject, float ResolutionMultiplier, FString PathToSave, FString Name, bool bAutoUniqueName = false);

	virtual void Activate() override;

	UPROPERTY()
	UObject* WorldContextObject = nullptr;

	UPROPERTY()
	UTextureRenderTarget2D* CaptureRT = nullptr;

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComponent = nullptr;

	UPROPERTY()
	AActor* CaptureActor = nullptr;

	// Keeps the inner readback action rooted (referenced) until it finishes, since it is driven manually rather than through the Blueprint node compiler.
	UPROPERTY()
	UAsyncScreenshotRTAction* InnerAction = nullptr;

	float ResolutionMultiplier = 1.0f;
	FString SavedPathToSave;
	FString SavedName;
	bool bAutoUniqueName = false;

	UPROPERTY(BlueprintAssignable)
	FAsyncReadEntireRTOutputPin OnSaved;

	UFUNCTION()
	void HandleInnerSaveComplete();
};
