// Copyright Grigoryev Daniil. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncScreenshotTypes.h"
#include "RHIFwd.h"
#include "AsyncScreenShotBPLibrary.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;
class UWorld;

UENUM()
enum class EAsyncRTCombineMode : uint8
{
	SingleRT,
	MultiplyAlpha
};

/** One render target readback in flight. Shared between the game thread and the rendering thread. */
struct FAsyncReadEntireRTData
{
	FGPUFenceRHIRef TextureFence;
	FTextureRHIRef Texture;

	// Written on the rendering thread, polled on the game thread.
	TAtomic<bool> FinishedRead{ false };

	// Published on the rendering thread *before* FinishedRead, so a game thread that has observed
	// FinishedRead == true is guaranteed to see this too. True means the readback produced nothing
	// usable - the staging surface could not be mapped, or the render target uses a pixel format this
	// plugin cannot convert. The colour buffers are empty in that case and nothing may be written.
	TAtomic<bool> ReadFailed{ false };

	bool StartReading = false;
	TArray<FColor> PixelColors;

	// When true, PollRTRead additionally fills LinearColors with the raw (non-tonemapped) float values.
	// Used for true HDR export of PF_FloatRGBA render targets.
	bool bWantsLinearColor = false;
	TArray<FLinearColor> LinearColors;
};

/** The colour and alpha readbacks behind SaveRenderTargetsMultiplyAlpha. */
struct FAsyncReadCombinedRTData
{
	TSharedPtr<FAsyncReadEntireRTData, ESPMode::ThreadSafe> ColorRT;
	TSharedPtr<FAsyncReadEntireRTData, ESPMode::ThreadSafe> AlphaRT;

	EAsyncRTCombineMode Mode = EAsyncRTCombineMode::SingleRT;
};

/** Blueprint entry points that do not need to run across several frames. */
UCLASS()
class ASYNCSCREENSHOT_API UAsyncScreenShotBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	/**
	 * Captures the game window and writes it to PathToSave/Name.<ext>. Fire and forget: use the
	 * Capture Game Screen node instead if you need to know whether it worked or where it landed.
	 *
	 * Paths use forward slashes, e.g. C:/Users/Name/Screenshots. Quality applies to JPG only (1..100).
	 * bAutoUniqueName appends a numeric suffix (_0001, _0002, ...) instead of overwriting an existing file.
	 */
	UFUNCTION(BlueprintCallable, Category = "AsyncScreenShot")
	static void SaveGameScreen(FString PathToSave, FString Name, EAsyncScreenshotImageFormat ImageFormat, int32 Quality = 80, bool bAutoUniqueName = false);

	/** The project's default screenshot folder, as an absolute path. */
	UFUNCTION(BlueprintPure, Category = "AsyncScreenShot")
	static FString GetScreenshotSavePath();

	/**
	 * Sets the zlib compression level (0 = fastest and largest, 9 = slowest and smallest) for PNG writes.
	 * Applies to every subsequent PNG this plugin writes, not just the next one.
	 */
	UFUNCTION(BlueprintCallable, Category = "AsyncScreenShot")
	static void SetPngCompressionLevel(int32 Level);

	/**
	 * Writes a Name.json sidecar next to an already-saved screenshot holding arbitrary key/value metadata,
	 * for instance the camera transform or a build number. A timestamp is always included.
	 */
	UFUNCTION(BlueprintCallable, Category = "AsyncScreenShot")
	static bool SaveScreenshotMetadata(FString PathToSave, FString Name, const TMap<FString, FString>& Metadata);

	// TODO(capture-without-ui): Not exposed yet. A prior attempt only toggled the classic Canvas AHUD
	// (bShowHUD) and left UMG widgets on screen, which doesn't deliver on "hide UI for this screenshot" -
	// UGameViewportClient's UMG overlay (ViewportOverlayWidget) has no public accessor to hide as a whole.
	// Revisit once we have a real plan (e.g. a small engine-side accessor, or a documented per-widget
	// convention) instead of shipping a half-working toggle.
};

/**
 * Reads a render target back on the GPU's schedule and writes it out, without stalling the game thread.
 *
 * The readback is started in Activate() and polled once per frame until the GPU fence signals, so a
 * capture costs a few frames of latency instead of a hitch.
 */
UCLASS()
class ASYNCSCREENSHOT_API UAsyncScreenshotRTAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/**
	 * Saves RenderTarget as a PNG (or .hdr, see bExportHDRForFloatRT) and/or hands it back as a texture.
	 *
	 * bFlushRHI trades the whole point of this node for immediacy: leave it off unless you need the
	 * pixels this frame. Crop values below zero mean "no crop"; DownscaleFactor only scales down.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "AsyncScreenShot")
	static UAsyncScreenshotRTAction* SaveRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* RenderTarget, FString PathToSave, FString Name, bool bFlushRHI,
		bool bAutoUniqueName = false, bool bExportHDRForFloatRT = false, bool bSaveToDisk = true, bool bReturnAsTexture = false,
		int32 CropX = -1, int32 CropY = -1, int32 CropWidth = -1, int32 CropHeight = -1, float DownscaleFactor = 1.0f);

	/**
	 * Saves ColorRT with its alpha multiplied by the inverse of AlphaRT's alpha, for compositing a capture
	 * over another image. Both render targets must have the same dimensions.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "AsyncScreenShot")
	static UAsyncScreenshotRTAction* SaveRenderTargetsMultiplyAlpha(UObject* WorldContextObject, UTextureRenderTarget2D* ColorRT, UTextureRenderTarget2D* AlphaRT, FString PathToSave, FString Name, bool bFlushRHI,
		bool bAutoUniqueName = false);

	// UBlueprintAsyncActionBase interface
	virtual void Activate() override;
	//~UBlueprintAsyncActionBase interface

	/** Fired once the capture is on disk and/or available as a texture. */
	UPROPERTY(BlueprintAssignable)
	FAsyncScreenshotRenderTargetPin OnSaved;

	/** Fired if the readback or the write failed. The reason is in the log. */
	UPROPERTY(BlueprintAssignable)
	FAsyncScreenshotRenderTargetPin OnFailed;

	UPROPERTY()
	TObjectPtr<UObject> WorldContextObject;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> RT;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> AlphaRT;

	EAsyncRTCombineMode CombineMode = EAsyncRTCombineMode::SingleRT;

	TSharedPtr<FAsyncReadCombinedRTData, ESPMode::ThreadSafe> CombinedData;

	bool bFlushRHI = false;
	bool bAutoUniqueName = false;
	bool bExportHDRForFloatRT = false;
	bool bSaveToDisk = true;
	bool bReturnAsTexture = false;

	int32 CropX = -1;
	int32 CropY = -1;
	int32 CropWidth = -1;
	int32 CropHeight = -1;
	float DownscaleFactor = 1.0f;

	TSharedPtr<FAsyncReadEntireRTData, ESPMode::ThreadSafe> ReadRTData;

	// Resolved once in Activate(). WorldContextObject may be an object that never had a world, and the
	// world can be torn down while a readback is still in flight, so it is not safe to walk back through
	// it every frame.
	TWeakObjectPtr<UWorld> CachedWorld;

	FString SavedPathToSave;
	FString SavedName;

	/** Fires exactly one output pin and retires the node. Game thread only. */
	void FinishSave(bool bSuccess, const FString& FullPath, UTexture2D* CapturedTexture);

protected:
	UFUNCTION()
	void OnNextFrame();

	// Queues the next OnNextFrame tick. Finishes the action and returns false if the world is gone.
	bool ScheduleNextFrame();

	uint64 StartFrame = 0;
};
