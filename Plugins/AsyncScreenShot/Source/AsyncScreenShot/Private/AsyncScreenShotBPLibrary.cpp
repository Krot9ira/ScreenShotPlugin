// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

#include "AsyncScreenShotBPLibrary.h"
#include "AsyncScreenShot.h"
#include "AsyncScreenshotInternal.h"
#include "AsyncScreenshotWinCapture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Async/TaskGraphInterfaces.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "RenderUtils.h"
#include "GameFramework/GameUserSettings.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Widgets/SWindow.h"
#include "Runtime/Launch/Resources/Version.h"
#include "TimerManager.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "Math/Float16Color.h"
#include "Engine/World.h"

// This translation unit owns the stb_image_write implementation; every other file just includes the
// header for the declarations. The macro is undefined again so it cannot leak into the next file of a
// unity build and emit the implementation a second time.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#undef STB_IMAGE_WRITE_IMPLEMENTATION

#include <vector>
#include <stdio.h>
#include <filesystem>
namespace fs = std::filesystem;

UAsyncScreenShotBPLibrary::UAsyncScreenShotBPLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

namespace AsyncScreenShot::Private
{

	void PngWriteCallback(void* context, void* data, int size)
	{
		FILE* file = static_cast<FILE*>(context);
		fwrite(data, 1, size, file);
	}

	// Platform-agnostic helpers shared by both the GDI window capture path (Windows-only) and the RHI render-target
	// readback path (portable). Keeping these outside the PLATFORM_WINDOWS guard is what lets the RT-based screenshot
	// pipeline compile and run on non-Windows platforms too.
	bool PathExists(const FString& Path)
	{
#if PLATFORM_WINDOWS
		return fs::exists(std::wstring(TCHAR_TO_WCHAR(*Path)));
#else
		return fs::exists(std::string(TCHAR_TO_UTF8(*Path)));
#endif
	}

	void CreateDirectoriesForFile(const FString& FilePath)
	{
#if PLATFORM_WINDOWS
		fs::create_directories(fs::path(std::wstring(TCHAR_TO_WCHAR(*FilePath))).parent_path());
#else
		fs::create_directories(fs::path(std::string(TCHAR_TO_UTF8(*FilePath))).parent_path());
#endif
	}

	FILE* OpenFileForWrite(const FString& Path)
	{
		FILE* File = nullptr;
#if PLATFORM_WINDOWS
		std::wstring WidePath(TCHAR_TO_WCHAR(*Path));
		_wfopen_s(&File, WidePath.c_str(), L"wb");
#else
		File = fopen(TCHAR_TO_UTF8(*Path), "wb");
#endif
		return File;
	}

	// When bAutoUniqueName is true and FullPath already exists, appends a numeric suffix (_0001, _0002, ...)
	// until a free path is found, instead of silently overwriting an existing screenshot.
	FString MakeUniquePath(const FString& FullPath, bool bAutoUniqueName)
	{
		if (!bAutoUniqueName || !PathExists(FullPath))
		{
			return FullPath;
		}

		const FString Dir = FPaths::GetPath(FullPath);
		const FString BaseName = FPaths::GetBaseFilename(FullPath);
		const FString Ext = FPaths::GetExtension(FullPath, true);

		for (int32 Suffix = 1; Suffix < 100000; ++Suffix)
		{
			FString Candidate = FPaths::Combine(Dir, FString::Printf(TEXT("%s_%04d"), *BaseName, Suffix)) + Ext;
			if (!PathExists(Candidate))
			{
				return Candidate;
			}
		}
		return FullPath;
	}

	// Crops (optional) and downscales (optional, nearest-neighbor) an FColor pixel buffer in place.
	// Used by the render-target readback path. Width/Height are updated to reflect the new dimensions.
	static void CropAndDownscaleColors(TArray<FColor>& Pixels, int32& Width, int32& Height,
		int32 CropX, int32 CropY, int32 CropWidth, int32 CropHeight, float DownscaleFactor)
	{
		if (CropWidth > 0 && CropHeight > 0 && Width > 0 && Height > 0)
		{
			const int32 SrcX = FMath::Clamp(CropX, 0, Width - 1);
			const int32 SrcY = FMath::Clamp(CropY, 0, Height - 1);
			const int32 SrcW = FMath::Clamp(CropWidth, 1, Width - SrcX);
			const int32 SrcH = FMath::Clamp(CropHeight, 1, Height - SrcY);

			TArray<FColor> Cropped;
			Cropped.SetNumUninitialized(SrcW * SrcH);
			for (int32 Y = 0; Y < SrcH; Y++)
			{
				FMemory::Memcpy(Cropped.GetData() + Y * SrcW, Pixels.GetData() + (SrcY + Y) * Width + SrcX, SrcW * sizeof(FColor));
			}
			Pixels = MoveTemp(Cropped);
			Width = SrcW;
			Height = SrcH;
		}

		if (DownscaleFactor > 0.f && DownscaleFactor < 1.0f && Width > 0 && Height > 0)
		{
			const int32 NewWidth = FMath::Max(1, FMath::RoundToInt(Width * DownscaleFactor));
			const int32 NewHeight = FMath::Max(1, FMath::RoundToInt(Height * DownscaleFactor));

			TArray<FColor> Resized;
			Resized.SetNumUninitialized(NewWidth * NewHeight);
			for (int32 Y = 0; Y < NewHeight; Y++)
			{
				const int32 SrcY = FMath::Min(Height - 1, FMath::FloorToInt((Y + 0.5f) * Height / NewHeight));
				for (int32 X = 0; X < NewWidth; X++)
				{
					const int32 SrcX = FMath::Min(Width - 1, FMath::FloorToInt((X + 0.5f) * Width / NewWidth));
					Resized[Y * NewWidth + X] = Pixels[SrcY * Width + SrcX];
				}
			}
			Pixels = MoveTemp(Resized);
			Width = NewWidth;
			Height = NewHeight;
		}
	}

} // namespace AsyncScreenShot::Private

#if PLATFORM_WINDOWS

void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name, EAsyncScreenshotImageFormat ImageFormat, int32 Quality, bool bAutoUniqueName)
{
	if (Name.IsEmpty())
	{
		Name = TEXT("Blank");
	}

	// HWND must be fetched on the game thread: GEngine->GameViewport is not thread-safe.
	HWND hWnd = AsyncScreenShot::Private::GetActiveGameWindow();
	if (!hWnd)
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: No valid game window HWND, aborting SaveGameScreen"));
		return;
	}

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [PathToSave, Name, ImageFormat, Quality, bAutoUniqueName, hWnd] {
		FString OutFullPath;
		AsyncScreenShot::Private::CaptureWindowToFile(hWnd, PathToSave, Name, ImageFormat, Quality, bAutoUniqueName, -1, -1, -1, -1, 1.0f, OutFullPath);
		});
}

#else // !PLATFORM_WINDOWS

void UAsyncScreenShotBPLibrary::SaveGameScreen(FString PathToSave, FString Name, EAsyncScreenshotImageFormat ImageFormat, int32 Quality, bool bAutoUniqueName)
{
	UE_LOG(LogTemp, Warning, TEXT("AsyncScreenshot: SaveGameScreen (GDI window capture) is only implemented on Windows"));
}

#endif // PLATFORM_WINDOWS


namespace AsyncScreenShot::Private
{

	// Marks a readback as having produced nothing usable. Order matters: ReadFailed is published before
	// FinishedRead, which is the flag the game thread spins on.
	static void FailReadback(const TSharedPtr<FAsyncReadEntireRTData, ESPMode::ThreadSafe>& ReadData)
	{
		ReadData->PixelColors.Empty();
		ReadData->LinearColors.Empty();
		ReadData->ReadFailed = true;
		ReadData->FinishedRead = true;
	}

	void PollRTRead(FRHICommandListImmediate& RHICmdList, TSharedPtr<FAsyncReadEntireRTData, ESPMode::ThreadSafe> ReadData, TWeakObjectPtr<UAsyncScreenshotRTAction> ReadAction, bool bFlushRHI)
	{
		SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::PollRTRead", FColor::Magenta);

		check(IsInRenderingThread());


		// If we didn't flush the RHI then make sure the previous rendering commands got done
		if (!bFlushRHI)
		{
			// Return if we haven't finished the texture commands
			if (!ReadData->TextureFence.IsValid() || !ReadData->TextureFence->Poll() || ReadData->StartReading)
			{
				return;
			}
		}
		ReadData->FinishedRead = false;
		SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::MapTexture", FColor::Magenta);
		void* OutputBuffer = nullptr;
		int32 RowPitchInPixels = 0;
		int32 Height = 0;

		if (bFlushRHI)
		{
			// This flushes the command list
			RHICmdList.MapStagingSurface(ReadData->Texture, ReadData->TextureFence, OutputBuffer, RowPitchInPixels, Height);
		}
		else
		{
#if (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2)
			GDynamicRHI->RHIMapStagingSurface_RenderThread(RHICmdList, ReadData->Texture, INDEX_NONE, ReadData->TextureFence, OutputBuffer, RowPitchInPixels, Height);
#else
			GDynamicRHI->RHIMapStagingSurface_RenderThread(RHICmdList, ReadData->Texture, ReadData->TextureFence, OutputBuffer, RowPitchInPixels, Height);
#endif
		}
		ReadData->StartReading = true;

		if (OutputBuffer == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Could not map the staging surface for readback"));
			FailReadback(ReadData);
			return;
		}

		const int32 Width = ReadData->Texture->GetSizeX();
		check(RowPitchInPixels >= Width);
		check(Height == ReadData->Texture->GetSizeY());
		const EPixelFormat Format = ReadData->Texture->GetFormat();

		// Reject unsupported formats before allocating: both branches below write every pixel, which is what
		// makes SetNumUninitialized safe. An unsupported format used to fall through leaving the buffer
		// untouched, so a PNG of uninitialized memory ended up on disk.
		if (Format != EPixelFormat::PF_B8G8R8A8 && Format != EPixelFormat::PF_FloatRGBA)
		{
			UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Unsupported render target format %d (supported: PF_B8G8R8A8, PF_FloatRGBA); nothing will be written"), static_cast<int32>(Format));
			RHICmdList.UnmapStagingSurface(ReadData->Texture);
			FailReadback(ReadData);
			return;
		}

		ReadData->PixelColors.SetNumUninitialized(Width * Height);

		if (Format == EPixelFormat::PF_B8G8R8A8)
		{
			const int32 BytesPerPixel = GPixelFormats[Format].BlockBytes; // = 4 для B8G8R8A8
			uint8* Src = static_cast<uint8*>(OutputBuffer);
			FColor* Dst = ReadData->PixelColors.GetData();

			for (int32 Y = 0; Y < Height; Y++)
			{
				// Смещение в байтах с учётом pitch
				uint8* RowSrc = Src + Y * RowPitchInPixels * BytesPerPixel;
				FMemory::Memcpy(Dst + Y * Width, RowSrc, Width * BytesPerPixel);
			}
		}
		else if (Format == EPixelFormat::PF_FloatRGBA)
		{
			FFloat16Color* Src = static_cast<FFloat16Color*>(OutputBuffer);
			FColor* Dst = ReadData->PixelColors.GetData();

			if (ReadData->bWantsLinearColor)
			{
				ReadData->LinearColors.SetNumUninitialized(Width * Height);
			}

			for (int32 Y = 0; Y < Height; Y++)
			{
				FFloat16Color* RowSrc = Src + Y * RowPitchInPixels;
				for (int32 X = 0; X < Width; X++)
				{
					const FFloat16Color& P = RowSrc[X];
					FLinearColor Lin(P.R.GetFloat(), P.G.GetFloat(), P.B.GetFloat(), P.A.GetFloat());
					Dst[Y * Width + X] = Lin.ToFColor(false);
					if (ReadData->bWantsLinearColor)
					{
						ReadData->LinearColors[Y * Width + X] = Lin;
					}
				}
			}
		}

		RHICmdList.UnmapStagingSurface(ReadData->Texture);
		ReadData->FinishedRead = true;
	}

} // namespace AsyncScreenShot::Private

UAsyncScreenshotRTAction* UAsyncScreenshotRTAction::SaveRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* RenderTarget, FString PathToSave, FString Name, bool bFlushRHI,
	bool bAutoUniqueName, bool bExportHDRForFloatRT, bool bSaveToDisk, bool bReturnAsTexture,
	int32 CropX, int32 CropY, int32 CropWidth, int32 CropHeight, float DownscaleFactor)
{
	UAsyncScreenshotRTAction* BlueprintNode = NewObject<UAsyncScreenshotRTAction>();
	BlueprintNode->WorldContextObject = WorldContextObject;
	BlueprintNode->RT = RenderTarget;
	BlueprintNode->bFlushRHI = bFlushRHI;
	BlueprintNode->ReadRTData = MakeShared<FAsyncReadEntireRTData, ESPMode::ThreadSafe>();
	BlueprintNode->ReadRTData->bWantsLinearColor = bExportHDRForFloatRT;
	BlueprintNode->SavedPathToSave = PathToSave;
	BlueprintNode->SavedName = Name;
	BlueprintNode->bAutoUniqueName = bAutoUniqueName;
	BlueprintNode->bExportHDRForFloatRT = bExportHDRForFloatRT;
	BlueprintNode->bSaveToDisk = bSaveToDisk;
	BlueprintNode->bReturnAsTexture = bReturnAsTexture;
	BlueprintNode->CropX = CropX;
	BlueprintNode->CropY = CropY;
	BlueprintNode->CropWidth = CropWidth;
	BlueprintNode->CropHeight = CropHeight;
	BlueprintNode->DownscaleFactor = DownscaleFactor;
	BlueprintNode->RegisterWithGameInstance(WorldContextObject);

	// TODO(hdr-crop): the .hdr export path writes the full, uncropped/undownscaled linear buffer -
	// CropAndDownscaleColors only runs on the PNG/texture path. Warn instead of silently ignoring the request.
	const bool bWantsCrop = (CropWidth > 0 && CropHeight > 0);
	const bool bWantsDownscale = (DownscaleFactor > 0.f && DownscaleFactor < 1.0f);
	if (bExportHDRForFloatRT && (bWantsCrop || bWantsDownscale))
	{
		UE_LOG(LogTemp, Warning, TEXT("AsyncScreenshot: Crop/Downscale are not yet supported together with bExportHDRForFloatRT; the .hdr file will be written at full render target resolution."));
	}

	return BlueprintNode;
}

UAsyncScreenshotRTAction* UAsyncScreenshotRTAction::SaveRenderTargetsMultiplyAlpha(UObject* WorldContextObject, UTextureRenderTarget2D* ColorRT, UTextureRenderTarget2D* AlphaRT, FString PathToSave, FString Name, bool bFlushRHI, bool bAutoUniqueName)
{
	UAsyncScreenshotRTAction* Node = NewObject<UAsyncScreenshotRTAction>();
	Node->WorldContextObject = WorldContextObject;
	Node->RT = ColorRT;
	Node->AlphaRT = AlphaRT;
	Node->CombineMode = EAsyncRTCombineMode::MultiplyAlpha;
	Node->bFlushRHI = bFlushRHI;
	Node->SavedPathToSave = PathToSave;
	Node->SavedName = Name;
	Node->bAutoUniqueName = bAutoUniqueName;
	Node->CombinedData = MakeShared<FAsyncReadCombinedRTData, ESPMode::ThreadSafe>();
	Node->CombinedData->ColorRT = MakeShared<FAsyncReadEntireRTData, ESPMode::ThreadSafe>();
	Node->CombinedData->AlphaRT = MakeShared<FAsyncReadEntireRTData, ESPMode::ThreadSafe>();
	Node->CombinedData->Mode = Node->CombineMode;
	Node->RegisterWithGameInstance(WorldContextObject);
	return Node;
}

namespace AsyncScreenShot::Private
{

	// FColor is BGRA in memory; stb wants RGBA. Swapping the two channels in place turns the pixel array
	// itself into the byte buffer the encoder needs, rather than building a second one of the same size.
	static void SwapRedAndBlue(TArray<FColor>& Pixels)
	{
		for (FColor& Pixel : Pixels)
		{
			Swap(Pixel.R, Pixel.B);
		}
	}

	// Pixel buffers are big - a 4K capture is 33 MB - so ownership moves into a shared pointer that the
	// background write and the game thread completion both capture by handle. This used to copy the array
	// into a local, again into the write lambda, again into a std::vector of bytes, and once more into the
	// completion lambda: four copies of the same 33 MB, three of them avoidable.
	void WritePixelsToFile(FString PathToSave, FString Name, TArray<FColor>&& PixelsIn, int32 InWidth, int32 InHeight, TWeakObjectPtr<UAsyncScreenshotRTAction> Action,
		bool bAutoUniqueName, bool bSaveToDisk, bool bReturnAsTexture)
	{
		TSharedPtr<TArray<FColor>, ESPMode::ThreadSafe> Pixels = MakeShared<TArray<FColor>, ESPMode::ThreadSafe>(MoveTemp(PixelsIn));

		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Action, PathToSave, Name, Pixels, InWidth, InHeight, bAutoUniqueName, bSaveToDisk, bReturnAsTexture]()
		{
			SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::WriteImage", FColor::Magenta);

			FString FullPath;
			bool bSucceeded = true;

			if (bSaveToDisk)
			{
				FullPath = MakeUniquePath(FPaths::Combine(PathToSave, Name) + TEXT(".png"), bAutoUniqueName);
				CreateDirectoriesForFile(FullPath);

				SwapRedAndBlue(*Pixels);

				if (FILE* File = OpenFileForWrite(FullPath))
				{
					bSucceeded = stbi_write_png_to_func(PngWriteCallback, File, InWidth, InHeight, 4, Pixels->GetData(), InWidth * 4) != 0;
					fclose(File);

					if (!bSucceeded)
					{
						UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Failed to encode PNG: %s"), *FullPath);
					}
				}
				else
				{
					bSucceeded = false;
					UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Failed to open file for writing: %s"), *FullPath);
				}

				if (bReturnAsTexture)
				{
					SwapRedAndBlue(*Pixels);
				}
			}

			AsyncTask(ENamedThreads::GameThread, [Action, Pixels, InWidth, InHeight, bReturnAsTexture, bSucceeded, FullPath]()
			{
				if (!Action.IsValid())
				{
					return;
				}

				UTexture2D* Texture = nullptr;
				if (bReturnAsTexture)
				{
					Texture = UTexture2D::CreateTransient(InWidth, InHeight, PF_B8G8R8A8, NAME_None,
						TConstArrayView64<uint8>(reinterpret_cast<const uint8*>(Pixels->GetData()), (int64)Pixels->Num() * sizeof(FColor)));
					if (Texture)
					{
						Texture->UpdateResource();
					}
				}

				Action->FinishSave(bSucceeded, FullPath, Texture);
			});
		});
	}

	// Writes the raw (non-tonemapped) linear float pixels of a PF_FloatRGBA render target to a Radiance .hdr
	// file, preserving the full HDR range. stb already ships an HDR encoder, so this costs no new dependency.
	// FLinearColor is four contiguous floats and stb's writer reads the first three of every four, so the
	// array can be handed to it as-is - the intermediate std::vector<float> this used to build was pure copy.
	void WriteLinearPixelsToHDRFile(FString PathToSave, FString Name, TArray<FLinearColor>&& PixelsIn, int32 InWidth, int32 InHeight, TWeakObjectPtr<UAsyncScreenshotRTAction> Action, bool bAutoUniqueName)
	{
		TSharedPtr<TArray<FLinearColor>, ESPMode::ThreadSafe> Pixels = MakeShared<TArray<FLinearColor>, ESPMode::ThreadSafe>(MoveTemp(PixelsIn));

		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Action, PathToSave, Name, Pixels, InWidth, InHeight, bAutoUniqueName]()
		{
			SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::WriteHDR", FColor::Magenta);

			FString FullPath = MakeUniquePath(FPaths::Combine(PathToSave, Name) + TEXT(".hdr"), bAutoUniqueName);
			CreateDirectoriesForFile(FullPath);
			bool bSucceeded = true;

			if (FILE* File = OpenFileForWrite(FullPath))
			{
				bSucceeded = stbi_write_hdr_to_func(PngWriteCallback, File, InWidth, InHeight, 4,
					reinterpret_cast<const float*>(Pixels->GetData())) != 0;
				fclose(File);

				if (!bSucceeded)
				{
					UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Failed to encode HDR: %s"), *FullPath);
				}
			}
			else
			{
				bSucceeded = false;
				UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Failed to open file for writing: %s"), *FullPath);
			}

			AsyncTask(ENamedThreads::GameThread, [Action, bSucceeded, FullPath]()
			{
				if (Action.IsValid())
				{
					Action->FinishSave(bSucceeded, FullPath, nullptr);
				}
			});
		});
	}

} // namespace AsyncScreenShot::Private



FString UAsyncScreenShotBPLibrary::GetScreenshotSavePath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir());
}

void UAsyncScreenShotBPLibrary::SetPngCompressionLevel(int32 Level)
{
	stbi_write_png_compression_level = FMath::Clamp(Level, 0, 9);
}

bool UAsyncScreenShotBPLibrary::SaveScreenshotMetadata(FString PathToSave, FString Name, const TMap<FString, FString>& Metadata)
{
	FString Json = TEXT("{\n");
	Json += FString::Printf(TEXT("  \"Timestamp\": \"%s\""), *FDateTime::Now().ToIso8601());
	for (const TPair<FString, FString>& Pair : Metadata)
	{
		const FString EscapedValue = Pair.Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
		Json += FString::Printf(TEXT(",\n  \"%s\": \"%s\""), *Pair.Key, *EscapedValue);
	}
	Json += TEXT("\n}\n");

	const FString FullPath = FPaths::Combine(PathToSave, Name) + TEXT(".json");
	AsyncScreenShot::Private::CreateDirectoriesForFile(FullPath);

	if (!FFileHelper::SaveStringToFile(Json, *FullPath))
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Failed to write metadata sidecar: %s"), *FullPath);
		return false;
	}

	return true;
}

// Safety net: if the GPU fence never signals (RT destroyed, GPU hang), bail out instead of polling forever.
static constexpr uint64 MaxWaitFrames = 300;

void UAsyncScreenshotRTAction::FinishSave(bool bSuccess, const FString& FullPath, UTexture2D* CapturedTexture)
{
	check(IsInGameThread());

	if (bSuccess)
	{
		OnSaved.Broadcast(FullPath, CapturedTexture);
	}
	else
	{
		OnFailed.Broadcast(FString(), nullptr);
	}

	SetReadyToDestroy();
}

bool UAsyncScreenshotRTAction::ScheduleNextFrame()
{
	if (UWorld* World = CachedWorld.Get())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &UAsyncScreenshotRTAction::OnNextFrame);
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: World went away, abandoning the render target readback"));
	FinishSave(false, FString(), nullptr);
	return false;
}

void UAsyncScreenshotRTAction::OnNextFrame()
{
	check(IsInGameThread());

	switch (CombineMode)
	{
	case EAsyncRTCombineMode::SingleRT:
	{

		check(ReadRTData.IsValid());

		if (ReadRTData->FinishedRead)
		{
			if (ReadRTData->ReadFailed)
			{
				UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Render target readback failed, nothing was written"));
				FinishSave(false, FString(), nullptr);
				return;
			}

			int32 Width = ReadRTData->Texture->GetSizeX();
			int32 Height = ReadRTData->Texture->GetSizeY();

			if (bExportHDRForFloatRT && ReadRTData->LinearColors.Num() == Width * Height)
			{
				AsyncScreenShot::Private::WriteLinearPixelsToHDRFile(SavedPathToSave, SavedName, MoveTemp(ReadRTData->LinearColors), Width, Height, this, bAutoUniqueName);
			}
			else
			{
				AsyncScreenShot::Private::CropAndDownscaleColors(ReadRTData->PixelColors, Width, Height, CropX, CropY, CropWidth, CropHeight, DownscaleFactor);
				AsyncScreenShot::Private::WritePixelsToFile(SavedPathToSave, SavedName, MoveTemp(ReadRTData->PixelColors), Width, Height, this, bAutoUniqueName, bSaveToDisk, bReturnAsTexture);
			}
			return;
		}
		else
		{
			if (GFrameCounter - StartFrame > MaxWaitFrames)
			{
				UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Timed out waiting for render target readback"));
				FinishSave(false, FString(), nullptr);
				return;
			}

			ENQUEUE_RENDER_COMMAND(FReadRTAsync)([WeakThis = TWeakObjectPtr<UAsyncScreenshotRTAction>(this), ReadRTData = ReadRTData](FRHICommandListImmediate& RHICmdList)
				{
					AsyncScreenShot::Private::PollRTRead(RHICmdList, ReadRTData, WeakThis, false);
				});



			ScheduleNextFrame();
		}
		break;
	}
	case EAsyncRTCombineMode::MultiplyAlpha:
	{
		if (!CombinedData->ColorRT->FinishedRead || !CombinedData->AlphaRT->FinishedRead)
		{
			if (GFrameCounter - StartFrame > MaxWaitFrames)
			{
				UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Timed out waiting for render target readback"));
				FinishSave(false, FString(), nullptr);
				return;
			}

			ENQUEUE_RENDER_COMMAND(PollRTs)(
				[ColorData = CombinedData->ColorRT,
				AlphaData = CombinedData->AlphaRT,
				bFlushRHI = bFlushRHI,
				WeakThis = TWeakObjectPtr<UAsyncScreenshotRTAction>(this)]
				(FRHICommandListImmediate& RHICmdList)
				{
					AsyncScreenShot::Private::PollRTRead(RHICmdList, ColorData, WeakThis, bFlushRHI);
					AsyncScreenShot::Private::PollRTRead(RHICmdList, AlphaData, WeakThis, bFlushRHI);
				});
			ScheduleNextFrame();
			return;
		}

		if (CombinedData->ColorRT->ReadFailed || CombinedData->AlphaRT->ReadFailed)
		{
			UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Render target readback failed, nothing was written"));
			FinishSave(false, FString(), nullptr);
			return;
		}

		TArray<FColor>& Color = CombinedData->ColorRT->PixelColors;
		TArray<FColor>& Alpha = CombinedData->AlphaRT->PixelColors;

		if (Color.Num() != Alpha.Num())
		{
			UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: Color/Alpha RT size mismatch (%d vs %d)"), Color.Num(), Alpha.Num());
			FinishSave(false, FString(), nullptr);
			return;
		}

		for (int32 i = 0; i < Color.Num(); ++i)
		{
			const float ColorAlpha = Color[i].A / 255.f;
			const float MaskAlphaInv = (255 - Alpha[i].A) / 255.f;
			Color[i].A = (uint8)(FMath::Clamp(ColorAlpha * MaskAlphaInv, 0.f, 1.f) * 255);
		}

		AsyncScreenShot::Private::WritePixelsToFile(SavedPathToSave, SavedName, MoveTemp(Color), CombinedData->ColorRT->Texture->GetSizeX(), CombinedData->ColorRT->Texture->GetSizeY(), this, bAutoUniqueName, true, false);
		return;
		break;
	}
	}

}

void UAsyncScreenshotRTAction::Activate()
{
	CachedWorld = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;

	switch (CombineMode)
	{
	case EAsyncRTCombineMode::SingleRT:
	{
		if (!CachedWorld.IsValid() || !RT || !RT->GetResource())
		{
			UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: No world for the given context object, or an invalid render target"));
			FinishSave(false, FString(), nullptr);
			break;
		}

		FTextureRenderTarget2DResource* TextureResource = (FTextureRenderTarget2DResource*)RT->GetResource();
		check(TextureResource);
		check(TextureResource->GetRenderTargetTexture());

		StartFrame = GFrameCounter;

		ENQUEUE_RENDER_COMMAND(FCopyRTAsync)([bFlushRHI = bFlushRHI, AsyncReadPtr = TWeakObjectPtr<UAsyncScreenshotRTAction>(this), TextureRHI = TextureResource->GetRenderTargetTexture(), ReadData = ReadRTData](FRHICommandListImmediate& RHICmdList)
			{
				check(IsInRenderingThread());
				check(TextureRHI.IsValid());

				FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("AsyncScreenshotRTReadback"));

				SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT", FColor::Magenta);

				FTextureRHIRef IORHITextureCPU;
				{
					SCOPED_NAMED_EVENT_TEXT("AsyncScreenshot::AsyncReadRT::CreateCopyTexture", FColor::Magenta);

					int32 Width, Height;
					Width = TextureRHI->GetSizeX();
					Height = TextureRHI->GetSizeY();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 2
					FRHITextureCreateDesc TextureDesc = FRHITextureCreateDesc::Create2D(TEXT("AsyncScreenshotRTReadback"), Width, Height, TextureRHI->GetFormat());
					TextureDesc.AddFlags(ETextureCreateFlags::CPUReadback);
					TextureDesc.InitialState = ERHIAccess::CopyDest;
#if ENGINE_MINOR_VERSION > 3
					IORHITextureCPU = RHICmdList.CreateTexture(TextureDesc);

#else // ENGINE_MINOR_VERSION
					IORHITextureCPU = GDynamicRHI->RHICreateTexture(TextureDesc);
#endif // ENGINE_MINOR_VERSION
#else
					FRHIResourceCreateInfo CreateInfo(TEXT("AsyncRTReadback"));
					IORHITextureCPU = RHICreateTexture2D(Width, Height, TextureRHI->GetFormat(), 1, 1, TexCreate_CPUReadback, ERHIAccess::CopyDest, CreateInfo);
#endif

					FRHICopyTextureInfo CopyTextureInfo;
					CopyTextureInfo.Size = FIntVector(Width, Height, 1);
					CopyTextureInfo.SourceMipIndex = 0;
					CopyTextureInfo.DestMipIndex = 0;
					CopyTextureInfo.SourcePosition = FIntVector(0, 0, 0);
					CopyTextureInfo.DestPosition = FIntVector(0, 0, 0);

					RHICmdList.Transition(FRHITransitionInfo(TextureRHI, ERHIAccess::Unknown, ERHIAccess::CopySrc));
					RHICmdList.CopyTexture(TextureRHI, IORHITextureCPU, CopyTextureInfo);

					RHICmdList.Transition(FRHITransitionInfo(IORHITextureCPU, ERHIAccess::CopyDest, ERHIAccess::CopySrc));
					RHICmdList.WriteGPUFence(Fence);
				}
				check(Fence.IsValid());

				ReadData->Texture = IORHITextureCPU;
				ReadData->TextureFence = Fence;

				// If we flush the RHI then we can just go ahead and read the mapped texture asap
				if (bFlushRHI)
				{
					AsyncScreenShot::Private::PollRTRead(RHICmdList, ReadData, AsyncReadPtr, bFlushRHI);
				}
			});

		ScheduleNextFrame();
		break;
	}
	case EAsyncRTCombineMode::MultiplyAlpha:
	{
		if (!CachedWorld.IsValid() || !RT || !RT->GetResource() || !AlphaRT || !AlphaRT->GetResource())
		{
			UE_LOG(LogTemp, Error, TEXT("AsyncScreenshot: No world for the given context object, or an invalid render target"));
			FinishSave(false, FString(), nullptr);
			break;
		}

		StartFrame = GFrameCounter;

		auto EnqueueRead = [&](UTextureRenderTarget2D* Target, TSharedPtr<FAsyncReadEntireRTData> Data)
			{
				FTextureRenderTarget2DResource* Res = static_cast<FTextureRenderTarget2DResource*>(Target->GetResource());

				ENQUEUE_RENDER_COMMAND(ReadRT)(
					[Res, Data, bFlushRHI = bFlushRHI](FRHICommandListImmediate& RHICmdList)
					{
						FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("RTReadFence"));
						FTextureRHIRef Src = Res->GetRenderTargetTexture();
						int32 W = Src->GetSizeX();
						int32 H = Src->GetSizeY();

						FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("RT_CPU"), W, H, Src->GetFormat());
						Desc.AddFlags(ETextureCreateFlags::CPUReadback);
						Desc.InitialState = ERHIAccess::CopyDest;
						FTextureRHIRef CPUTexture = RHICmdList.CreateTexture(Desc);

						RHICmdList.Transition(FRHITransitionInfo(Src, ERHIAccess::Unknown, ERHIAccess::CopySrc));
						RHICmdList.CopyTexture(Src, CPUTexture, {});

						RHICmdList.Transition(FRHITransitionInfo(CPUTexture, ERHIAccess::CopyDest, ERHIAccess::CopySrc));
						RHICmdList.WriteGPUFence(Fence);

						Data->Texture = CPUTexture;
						Data->TextureFence = Fence;

						if (bFlushRHI)
						{
							AsyncScreenShot::Private::PollRTRead(RHICmdList, Data, nullptr, true);
						}
					});
			};

		EnqueueRead(RT, CombinedData->ColorRT);
		EnqueueRead(AlphaRT, CombinedData->AlphaRT);

		ScheduleNextFrame();
		break;
	}
	}
}
