// Copyright Grigoryev Daniil. All Rights Reserved.

#include "AsyncSuperResolutionScreenshotAction.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

UAsyncSuperResolutionScreenshotAction* UAsyncSuperResolutionScreenshotAction::CaptureSuperResolutionScreenshot(UObject* WorldContextObject, float ResolutionMultiplier, FString PathToSave, FString Name, bool bAutoUniqueName)
{
	UAsyncSuperResolutionScreenshotAction* Node = NewObject<UAsyncSuperResolutionScreenshotAction>();
	Node->WorldContextObject = WorldContextObject;
	Node->ResolutionMultiplier = FMath::Max(ResolutionMultiplier, 0.1f);
	Node->SavedPathToSave = PathToSave;
	Node->SavedName = Name;
	Node->bAutoUniqueName = bAutoUniqueName;

	return Node;
}

void UAsyncSuperResolutionScreenshotAction::Activate()
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;

	if (!World || !PC || !PC->PlayerCameraManager)
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: No valid world/player controller/camera manager for super-resolution capture"));
		OnSaved.Broadcast();
		SetReadyToDestroy();
		return;
	}

	FVector2D ViewportSize(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}

	const int32 Width = FMath::Max(1, FMath::RoundToInt(ViewportSize.X * ResolutionMultiplier));
	const int32 Height = FMath::Max(1, FMath::RoundToInt(ViewportSize.Y * ResolutionMultiplier));

	CaptureActor = World->SpawnActor<AActor>();
	if (!CaptureActor)
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Failed to spawn temporary capture actor"));
		OnSaved.Broadcast();
		SetReadyToDestroy();
		return;
	}

	CaptureComponent = NewObject<USceneCaptureComponent2D>(CaptureActor);
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
	CaptureComponent->TextureTarget = CaptureRT;
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	CaptureComponent->CaptureScene();

	InnerAction = UAsyncScreenshotRTAction::SaveRenderTarget(WorldContextObject, CaptureRT, SavedPathToSave, SavedName, /*bFlushRHI=*/true, bAutoUniqueName);
	InnerAction->OnSaveRenderTarget.AddDynamic(this, &UAsyncSuperResolutionScreenshotAction::HandleInnerSaveComplete);
	InnerAction->Activate();
}

void UAsyncSuperResolutionScreenshotAction::HandleInnerSaveComplete()
{
	OnSaved.Broadcast();

	if (CaptureActor)
	{
		CaptureActor->Destroy();
		CaptureActor = nullptr;
	}
	CaptureComponent = nullptr;
	CaptureRT = nullptr;
	InnerAction = nullptr;

	SetReadyToDestroy();
}
