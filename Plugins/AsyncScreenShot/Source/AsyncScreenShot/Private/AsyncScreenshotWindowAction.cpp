// Copyright Grigoryev Daniil. All Rights Reserved.

#include "AsyncScreenshotWindowAction.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

#if PLATFORM_WINDOWS
#include "AsyncScreenshotWinCapture.h"
#endif

UAsyncScreenshotWindowAction* UAsyncScreenshotWindowAction::CaptureGameScreen(FString PathToSave, FString Name, EImageFormat ImageFormat, int32 Quality, bool bAutoUniqueName,
	int32 CropX, int32 CropY, int32 CropWidth, int32 CropHeight, float DownscaleFactor)
{
	UAsyncScreenshotWindowAction* Node = NewObject<UAsyncScreenshotWindowAction>();
	Node->PathToSave = PathToSave;
	Node->Name = Name.IsEmpty() ? TEXT("Blank") : Name;
	Node->ImageFormat = ImageFormat;
	Node->Quality = Quality;
	Node->bAutoUniqueName = bAutoUniqueName;
	Node->CropX = CropX;
	Node->CropY = CropY;
	Node->CropWidth = CropWidth;
	Node->CropHeight = CropHeight;
	Node->DownscaleFactor = DownscaleFactor;

	// This node has no WorldContextObject pin, but it already needs the game viewport for the window
	// handle, so the game instance can come from there. Without registering, nothing keeps the node alive
	// while the capture runs on a background thread and the pins never fire.
	if (GEngine && GEngine->GameViewport)
	{
		Node->RegisterWithGameInstance(GEngine->GameViewport->GetGameInstance());
	}

	return Node;
}

void UAsyncScreenshotWindowAction::Activate()
{
#if PLATFORM_WINDOWS
	// HWND must be fetched on the game thread: GEngine->GameViewport is not thread-safe.
	HWND hWnd = AsyncScreenShot::Private::GetActiveGameWindow();
	if (!hWnd)
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: No valid game window HWND, aborting CaptureGameScreen"));
		OnFailed.Broadcast();
		SetReadyToDestroy();
		return;
	}

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis = TWeakObjectPtr<UAsyncScreenshotWindowAction>(this), hWnd,
		PathToSave = PathToSave, Name = Name, ImageFormat = ImageFormat, Quality = Quality, bAutoUniqueName = bAutoUniqueName,
		CropX = CropX, CropY = CropY, CropWidth = CropWidth, CropHeight = CropHeight, DownscaleFactor = DownscaleFactor] {

		FString OutFullPath;
		const bool bSuccess = AsyncScreenShot::Private::CaptureWindowToFile(hWnd, PathToSave, Name, ImageFormat, Quality, bAutoUniqueName,
			CropX, CropY, CropWidth, CropHeight, DownscaleFactor, OutFullPath);

		AsyncTask(ENamedThreads::GameThread, [WeakThis, bSuccess, OutFullPath] {
			if (!WeakThis.IsValid())
			{
				return;
			}
			if (bSuccess)
			{
				WeakThis->OnSaved.Broadcast(OutFullPath);
			}
			else
			{
				WeakThis->OnFailed.Broadcast();
			}
			WeakThis->SetReadyToDestroy();
		});
	});
#else
	OnFailed.Broadcast();
	SetReadyToDestroy();
#endif
}
