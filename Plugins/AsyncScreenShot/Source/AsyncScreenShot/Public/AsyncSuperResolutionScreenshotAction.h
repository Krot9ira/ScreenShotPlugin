// Copyright Grigoryev Daniil. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncScreenshotTypes.h"
#include "AsyncSuperResolutionScreenshotAction.generated.h"

class AActor;
class UAsyncScreenshotRTAction;
class USceneCaptureComponent2D;
class UTexture2D;
class UTextureRenderTarget2D;

/**
 * Captures the active player's view at a multiple of the current viewport resolution by rendering it
 * through a temporary SceneCaptureComponent2D, then saves it through UAsyncScreenshotRTAction.
 *
 * The multiplier is clamped to what the RHI can allocate: a 4x shot of a 4K viewport is already a 530 MB
 * render target.
 */
UCLASS()
class ASYNCSCREENSHOT_API UAsyncSuperResolutionScreenshotAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "AsyncScreenShot")
	static UAsyncSuperResolutionScreenshotAction* CaptureSuperResolutionScreenshot(UObject* WorldContextObject, float ResolutionMultiplier, FString PathToSave, FString Name, bool bAutoUniqueName = false);

	virtual void Activate() override;

	/** The file that was written. */
	UPROPERTY(BlueprintAssignable)
	FAsyncScreenshotFilePin OnSaved;

	/** Fired with an empty path when there is no player view to capture, or the save failed. */
	UPROPERTY(BlueprintAssignable)
	FAsyncScreenshotFilePin OnFailed;

	UPROPERTY()
	TObjectPtr<UObject> WorldContextObject;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> CaptureRT;

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> CaptureComponent;

	UPROPERTY()
	TObjectPtr<AActor> CaptureActor;

	// Keeps the inner readback rooted until it finishes, since it is driven manually rather than through
	// the Blueprint node compiler.
	UPROPERTY()
	TObjectPtr<UAsyncScreenshotRTAction> InnerAction;

	float ResolutionMultiplier = 1.0f;
	FString SavedPathToSave;
	FString SavedName;
	bool bAutoUniqueName = false;

private:
	UFUNCTION()
	void HandleInnerSaved(const FString& FullPath, UTexture2D* CapturedTexture);

	UFUNCTION()
	void HandleInnerFailed(const FString& FullPath, UTexture2D* CapturedTexture);

	/** Destroys the temporary capture actor and fires the matching output pin. */
	void Finish(bool bSuccess, const FString& FullPath);
};
