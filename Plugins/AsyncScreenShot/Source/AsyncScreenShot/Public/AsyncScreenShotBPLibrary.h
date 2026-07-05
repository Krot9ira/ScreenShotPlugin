// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncScreenShotBPLibrary.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAsyncReadEntireRTOutputPin);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsyncRTTextureOutputPin, UTexture2D*, ResultTexture);
/*
*	Function library class.
*	Each function in it is expected to be static and represents blueprint node that can be called in any blueprint.
*
*	When declaring function you can define metadata for the node. Key function specifiers will be BlueprintPure and BlueprintCallable.
*	BlueprintPure - means the function does not affect the owning object in any way and thus creates a node without Exec pins.
*	BlueprintCallable - makes a function which can be executed in Blueprints - Thus it has Exec pins.
*	DisplayName - full name of the node, shown when you mouse over the node and in the blueprint drop down menu.
*				Its lets you name the node using characters not allowed in C++ function names.
*	CompactNodeTitle - the word(s) that appear on the node.
*	Keywords -	the list of keywords that helps you to find node when you search for it using Blueprint drop-down menu.
*				Good example is "Print String" node which you can find also by using keyword "log".
*	Category -	the category your node will be under in the Blueprint drop-down menu.
*
*	For more info on custom blueprint nodes visit documentation:
*	https://wiki.unrealengine.com/Custom_Blueprint_Node_Creation
*/
UENUM(BlueprintType)
enum class EImageFormat : uint8
{
	bpm UMETA(DisplayName = "BMP"),
	jpg UMETA(DisplayName = "JPG"),
	png UMETA(DisplayName = "PNG")
};

UENUM()
enum class EAsyncRTCombineMode : uint8
{
	SingleRT,
	MultiplyAlpha
};


class UTextureRenderTarget2D;

struct FAsyncReadEntireRTData
{
	FGPUFenceRHIRef TextureFence;
	FTextureRHIRef Texture;
	TAtomic<bool> FinishedRead;
	bool StartReading = false;
	TArray<FColor> PixelColors;

	// When true, PollRTRead additionally fills LinearColors with the raw (non-tonemapped) float values.
	// Used for true HDR export of PF_FloatRGBA render targets.
	bool bWantsLinearColor = false;
	TArray<FLinearColor> LinearColors;
};

struct FAsyncReadCombinedRTData
{
	TSharedPtr<FAsyncReadEntireRTData, ESPMode::ThreadSafe> ColorRT;
	TSharedPtr<FAsyncReadEntireRTData, ESPMode::ThreadSafe> AlphaRT;

	EAsyncRTCombineMode Mode = EAsyncRTCombineMode::SingleRT;
};

UCLASS()
class UAsyncScreenShotBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()
	/*When specifying the path, use / , for example C : / Users / Krot9ira /
	* Quality is used only for jpg in range from 1 to 100
	* bAutoUniqueName: when true and a file with the same name already exists, a numeric suffix (_0001, _0002, ...) is appended instead of overwriting it. Defaults to false to keep existing overwrite behavior.
	*/
	UFUNCTION(BlueprintCallable, meta = (Category = "ScreenshotTaker Functionality"))
	static void SaveGameScreen(FString PathToSave, FString Name, EImageFormat ImageFormat, int Quality = 80, bool bAutoUniqueName = false);


//Returning default path for saving screenshot
	UFUNCTION(BlueprintPure, meta = (Category = "ScreenshotTaker Functionality"))
	static FString GetScreenshotSavePath();

	// Sets the zlib compression level (0 = fastest/largest, 9 = slowest/smallest) used by all subsequent PNG writes made by this plugin. Applies plugin-wide, not per-call.
	UFUNCTION(BlueprintCallable, meta = (Category = "ScreenshotTaker Functionality"))
	static void SetPngCompressionLevel(int32 Level);

	// Writes a small .json sidecar file (Name.json) next to an already-saved screenshot with arbitrary key/value metadata (e.g. camera transform, timestamp).
	UFUNCTION(BlueprintCallable, meta = (Category = "ScreenshotTaker Functionality"))
	static void SaveScreenshotMetadata(FString PathToSave, FString Name, const TMap<FString, FString>& Metadata);

	// TODO(capture-without-ui): Not exposed yet. A prior attempt only toggled the classic Canvas AHUD
	// (bShowHUD) and left UMG widgets on screen, which doesn't deliver on "hide UI for this screenshot" -
	// UGameViewportClient's UMG overlay (ViewportOverlayWidget) has no public accessor to hide as a whole.
	// Revisit once we have a real plan (e.g. a small engine-side accessor, or a documented per-widget
	// convention) instead of shipping a half-working toggle.

};
UCLASS()
class UAsyncScreenshotRTAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "ScreenshotTaker Functionality")
	static UAsyncScreenshotRTAction* SaveRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* RenderTarget, FString PathToSave, FString Name, bool bFlushRHI,
		bool bAutoUniqueName = false, bool bExportHDRForFloatRT = false, bool bSaveToDisk = true, bool bReturnAsTexture = false,
		int32 CropX = -1, int32 CropY = -1, int32 CropWidth = -1, int32 CropHeight = -1, float DownscaleFactor = 1.0f);
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "ScreenshotTaker Functionality")
	static UAsyncScreenshotRTAction* SaveRenderTargetsMultiplyAlpha(UObject* WorldContextObject, UTextureRenderTarget2D* ColorRT, UTextureRenderTarget2D* AlphaRT, FString PathToSave, FString Name, bool bFlushRHI,
		bool bAutoUniqueName = false);


	// UBlueprintAsyncActionBase interface
	virtual void Activate() override;
	//~UBlueprintAsyncActionBase interface

	UPROPERTY()
	UObject* WorldContextObject;

	UPROPERTY()
	UTextureRenderTarget2D* RT = nullptr;

	UPROPERTY()
	UTextureRenderTarget2D* AlphaRT = nullptr;

	EAsyncRTCombineMode CombineMode = EAsyncRTCombineMode::SingleRT;

	TSharedPtr<FAsyncReadCombinedRTData, ESPMode::ThreadSafe> CombinedData;

	int32 X;
	int32 Y;
	bool bFlushRHI;

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

	UPROPERTY(BlueprintAssignable)
	FAsyncReadEntireRTOutputPin OnSaveRenderTarget;

	UPROPERTY(BlueprintAssignable)
	FAsyncRTTextureOutputPin OnCapturedTexture;

	FString SavedPathToSave;
	FString SavedName;

protected:
	UFUNCTION()
	void OnNextFrame();

	uint64 StartFrame;
};
