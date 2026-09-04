// Copyright Grigoryev Daniil. All Rights Reserved.

#include "AsyncSuperResolutionScreenshotAction.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "RenderUtils.h"

UAsyncSuperResolutionScreenshotAction* UAsyncSuperResolutionScreenshotAction::CaptureSuperResolutionScreenshot(UObject* WorldContextObject, float ResolutionMultiplier, FString PathToSave, FString Name, bool bAutoUniqueName)
{
	UAsyncSuperResolutionScreenshotAction* Node = NewObject<UAsyncSuperResolutionScreenshotAction>();
	Node->WorldContextObject = WorldContextObject;
	Node->ResolutionMultiplier = FMath::Max(ResolutionMultiplier, 0.1f);
	Node->SavedPathToSave = PathToSave;
	Node->SavedName = Name;
	Node->bAutoUniqueName = bAutoUniqueName;
	Node->RegisterWithGameInstance(WorldContextObject);

	return Node;
}

void UAsyncSuperResolutionScreenshotAction::Activate()
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;

	if (!World || !PC || !PC->PlayerCameraManager)
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: No valid world/player controller/camera manager for super-resolution capture"));
		Finish(false, FString());
		return;
	}

	FVector2D ViewportSize(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}

	// Clamp against what the RHI can actually allocate. A 4x shot of a 4K viewport is already a 530 MB
	// render target before the readback buffers on top, and nothing above capped the multiplier.
	const int32 MaxDimension = (int32)GetMax2DTextureDimension();
	const int32 Width = FMath::Clamp(FMath::RoundToInt(ViewportSize.X * ResolutionMultiplier), 1, MaxDimension);
	const int32 Height = FMath::Clamp(FMath::RoundToInt(ViewportSize.Y * ResolutionMultiplier), 1, MaxDimension);

	if (Width != FMath::RoundToInt(ViewportSize.X * ResolutionMultiplier)
		|| Height != FMath::RoundToInt(ViewportSize.Y * ResolutionMultiplier))
	{
		UE_LOG(LogTemp, Warning, TEXT("AsyncScreenshot: Requested super-resolution size exceeds the %dpx texture limit, clamping to %dx%d"), MaxDimension, Width, Height);
	}

	CaptureActor = World->SpawnActor<AActor>();
	if (!CaptureActor)
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Failed to spawn temporary capture actor"));
		Finish(false, FString());
		return;
	}

	CaptureComponent = NewObject<USceneCaptureComponent2D>(CaptureActor);
	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;
	CaptureComponent->RegisterComponentWithWorld(World);

	CaptureRT = NewObject<UTextureRenderTarget2D>();
	CaptureRT->RenderTargetFormat = RTF_RGBA8;
	CaptureRT->InitAutoFormat(Width, Height);
	CaptureRT->UpdateResourceImmediate(true);

	FVector CamLoc;
	FRotator CamRot;
	PC->PlayerCameraManager->GetCameraViewPoint(CamLoc, CamRot);

	CaptureComponent->SetWorldLocationAndRotation(CamLoc, CamRot);
	CaptureComponent->FOVAngle = PC->PlayerCameraManager->GetFOVAngle();

	// Without this the capture renders with the component's default post processing, so exposure, bloom
	// and colour grading do not match the frame the player is looking at.
	CaptureComponent->PostProcessSettings = PC->PlayerCameraManager->GetCameraCacheView().PostProcessSettings;
	CaptureComponent->PostProcessBlendWeight = PC->PlayerCameraManager->GetCameraCacheView().PostProcessBlendWeight;
	CaptureComponent->TextureTarget = CaptureRT;
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	CaptureComponent->CaptureScene();

	// CaptureScene() has queued the render; the readback's own fence orders the copy after it, so there is
	// no reason to stall the game thread on a full RHI flush in a plugin whose whole point is not to.
	InnerAction = UAsyncScreenshotRTAction::SaveRenderTarget(WorldContextObject, CaptureRT, SavedPathToSave, SavedName, /*bFlushRHI=*/false, bAutoUniqueName);
	InnerAction->OnSaved.AddDynamic(this, &UAsyncSuperResolutionScreenshotAction::HandleInnerSaved);
	InnerAction->OnFailed.AddDynamic(this, &UAsyncSuperResolutionScreenshotAction::HandleInnerFailed);
	InnerAction->Activate();
}

void UAsyncSuperResolutionScreenshotAction::HandleInnerSaved(const FString& FullPath, UTexture2D* CapturedTexture)
{
	Finish(true, FullPath);
}

void UAsyncSuperResolutionScreenshotAction::HandleInnerFailed(const FString& FullPath, UTexture2D* CapturedTexture)
{
	Finish(false, FString());
}

void UAsyncSuperResolutionScreenshotAction::Finish(bool bSuccess, const FString& FullPath)
{
	if (CaptureActor)
	{
		CaptureActor->Destroy();
		CaptureActor = nullptr;
	}
	CaptureComponent = nullptr;
	CaptureRT = nullptr;
	InnerAction = nullptr;

	if (bSuccess)
	{
		OnSaved.Broadcast(FullPath);
	}
	else
	{
		OnFailed.Broadcast(FString());
	}

	SetReadyToDestroy();
}
