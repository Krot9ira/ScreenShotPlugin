// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncScreenShotBPLibrary.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAsyncReadEntireRTOutputPin);
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
	jpg UMETA(DisplayName = "JPG")
};



class UTextureRenderTarget2D;

struct FAsyncReadEntireRTData
{
	FGPUFenceRHIRef TextureFence;
	FTexture2DRHIRef Texture;
	TAtomic<bool> FinishedRead;
	bool StartReading = false;
	TArray<FColor> PixelColors;

};

UCLASS()
class UAsyncScreenShotBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()
	/*When specifying the path, use / , for example C : / Users / Krot9ira /
	* Quality is used only for jpg in range from 1 to 100
	*/
	UFUNCTION(BlueprintCallable, meta = (Category = "ScreenshotTaker Functionality"))
	static void SaveGameScreen(FString PathToSave, FString Name, EImageFormat ImageFormat, int Quality = 80);
	//TODO Add selecting file extention


//Returning default path for saving screenshot
	UFUNCTION(BlueprintPure, meta = (Category = "ScreenshotTaker Functionality"))
	static FString GetScreenshotSavePath();


};
UCLASS()
class UAsyncScreenshotRTAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "ScreenshotTaker Functionality")
	static UAsyncScreenshotRTAction* SaveRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* RenderTarget, FString PathToSave, FString Name, bool bFlushRHI);

	// UBlueprintAsyncActionBase interface
	virtual void Activate() override;
	//~UBlueprintAsyncActionBase interface

	UPROPERTY()
	UObject* WorldContextObject;

	UPROPERTY()
	UTextureRenderTarget2D* RT;

	int32 X;
	int32 Y;
	bool bFlushRHI;

	TSharedPtr<FAsyncReadEntireRTData, ESPMode::ThreadSafe> ReadRTData;

	UPROPERTY(BlueprintAssignable)
	FAsyncReadEntireRTOutputPin OnSaveRenderTarget;

	FString SavedPathToSave;
	FString SavedName;

protected:
	UFUNCTION()
	void OnNextFrame();

	uint64 StartFrame;
};
