// Copyright (c) 2026 Daniil Grigoryev. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Json.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AsyncScreenShotBPLibrary.h"
#include "AsyncScreenshotInternal.h"

/**
 * The non-GPU surface of AsyncScreenShot: where screenshots land, the metadata sidecar format tooling
 * reads back, how a name is made unique, and the crop/downscale maths.
 */

namespace
{
	/** A scratch directory under Saved that a test owns for its lifetime. */
	struct FScopedTestDirectory
	{
		FString Path;

		explicit FScopedTestDirectory(const TCHAR* Name)
			: Path(FPaths::ProjectSavedDir() / Name)
		{
			IFileManager::Get().DeleteDirectory(*Path, /*RequireExists*/ false, /*Tree*/ true);
			IFileManager::Get().MakeDirectory(*Path, /*Tree*/ true);
		}

		~FScopedTestDirectory()
		{
			IFileManager::Get().DeleteDirectory(*Path, /*RequireExists*/ false, /*Tree*/ true);
		}

		FString File(const TCHAR* Name) const { return Path / Name; }
	};

	/** A Width x Height buffer whose every pixel encodes its own coordinates, so moves are visible. */
	TArray<FColor> MakeCoordinateGrid(int32 Width, int32 Height)
	{
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				Pixels[Y * Width + X] = FColor((uint8)X, (uint8)Y, 0, 255);
			}
		}
		return Pixels;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncScreenShotSavePathIsUsable, "AsyncScreenShot.Path.SavePathIsAbsoluteUnderSaved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FAsyncScreenShotSavePathIsUsable::RunTest(const FString& Parameters)
{
	// When: the default screenshot folder is resolved
	const FString Path = UAsyncScreenShotBPLibrary::GetScreenshotSavePath();

	// Then: it is a full path inside the project's Saved area - something a Blueprint can concatenate a
	// file name onto without further work.
	TestFalse(TEXT("path is non-empty"), Path.IsEmpty());
	TestFalse(TEXT("path is absolute, not relative"), FPaths::IsRelative(Path));
	TestTrue(TEXT("path lives under Saved"), Path.Contains(TEXT("/Saved/")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncScreenShotMetadataRoundtrip, "AsyncScreenShot.Metadata.WriterProducesReadableJson",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FAsyncScreenShotMetadataRoundtrip::RunTest(const FString& Parameters)
{
	// Given: metadata whose keys and values contain everything JSON has to escape. The hand-rolled writer
	// this replaced escaped quotes and backslashes in values only, so each of these broke the file.
	const FScopedTestDirectory Directory(TEXT("AutomationScreenMeta"));

	TMap<FString, FString> Metadata;
	Metadata.Add(TEXT("MapName"), TEXT("L_Test"));
	Metadata.Add(TEXT("Note"), TEXT("quote\" and \\ slash"));
	Metadata.Add(TEXT("Multiline"), TEXT("first\nsecond\ttabbed"));
	Metadata.Add(TEXT("Key \"quoted\""), TEXT("value"));

	// When: the sidecar is written
	if (!TestTrue(TEXT("writer reports success"), UAsyncScreenShotBPLibrary::SaveScreenshotMetadata(Directory.Path, TEXT("meta"), Metadata)))
	{
		return false;
	}

	const FString FullPath = Directory.File(TEXT("meta.json"));
	if (!TestTrue(TEXT("sidecar exists on disk"), IFileManager::Get().FileExists(*FullPath)))
	{
		return false;
	}

	// Then: it parses, and every pair survives verbatim.
	FString JsonText;
	FFileHelper::LoadFileToString(JsonText, *FullPath);

	TSharedPtr<FJsonObject> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!TestTrue(TEXT("sidecar is valid JSON"), FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("timestamp recorded"), Parsed->HasField(TEXT("Timestamp")));
	for (const TPair<FString, FString>& Pair : Metadata)
	{
		TestEqual(*FString::Printf(TEXT("value survives for key '%s'"), *Pair.Key), Parsed->GetStringField(Pair.Key), Pair.Value);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncScreenShotUniqueNaming, "AsyncScreenShot.Path.UniqueNamingAvoidsOverwrites",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FAsyncScreenShotUniqueNaming::RunTest(const FString& Parameters)
{
	using namespace AsyncScreenShot::Private;

	const FScopedTestDirectory Directory(TEXT("AutomationScreenNaming"));
	const FString Requested = Directory.File(TEXT("Shot.png"));

	// A free path is returned untouched, whether or not uniquing was asked for.
	TestEqual(TEXT("free path is left alone"), MakeUniquePath(Requested, /*bAutoUniqueName*/ true), Requested);
	TestEqual(TEXT("free path is left alone when disabled"), MakeUniquePath(Requested, /*bAutoUniqueName*/ false), Requested);

	// With the file present, opting out still overwrites - that is the documented default.
	FFileHelper::SaveStringToFile(TEXT("x"), *Requested);
	TestEqual(TEXT("existing path is overwritten when disabled"), MakeUniquePath(Requested, /*bAutoUniqueName*/ false), Requested);

	// Opting in yields a suffixed sibling in the same folder, with the extension preserved.
	const FString First = MakeUniquePath(Requested, /*bAutoUniqueName*/ true);
	TestNotEqual(TEXT("existing path is avoided"), First, Requested);
	TestEqual(TEXT("stays in the same folder"), FPaths::GetPath(First), FPaths::GetPath(Requested));
	TestEqual(TEXT("keeps the extension"), FPaths::GetExtension(First), FString(TEXT("png")));
	TestTrue(TEXT("suffix is appended to the base name"), FPaths::GetBaseFilename(First).StartsWith(TEXT("Shot_")));

	// A second request while the first is still only reserved must not hand back the same name; two
	// captures fired in one frame would otherwise race for one file.
	FFileHelper::SaveStringToFile(TEXT("x"), *First);
	TestNotEqual(TEXT("consecutive requests differ"), MakeUniquePath(Requested, /*bAutoUniqueName*/ true), First);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncScreenShotCropAndDownscale, "AsyncScreenShot.Image.CropAndDownscale",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FAsyncScreenShotCropAndDownscale::RunTest(const FString& Parameters)
{
	using namespace AsyncScreenShot::Private;

	// Nothing requested means nothing changes.
	{
		int32 Width = 8, Height = 4;
		TArray<FColor> Pixels = MakeCoordinateGrid(Width, Height);
		CropAndDownscaleColors(Pixels, Width, Height, -1, -1, -1, -1, 1.0f);
		TestEqual(TEXT("width untouched"), Width, 8);
		TestEqual(TEXT("height untouched"), Height, 4);
		TestEqual(TEXT("pixel count untouched"), Pixels.Num(), 32);
	}

	// A crop takes exactly the requested rectangle, and the pixels come from the right place.
	{
		int32 Width = 8, Height = 4;
		TArray<FColor> Pixels = MakeCoordinateGrid(Width, Height);
		CropAndDownscaleColors(Pixels, Width, Height, 2, 1, 3, 2, 1.0f);
		TestEqual(TEXT("cropped width"), Width, 3);
		TestEqual(TEXT("cropped height"), Height, 2);
		TestEqual(TEXT("cropped buffer resized"), Pixels.Num(), 6);
		TestEqual(TEXT("top-left comes from (2,1)"), Pixels[0], FColor(2, 1, 0, 255));
		TestEqual(TEXT("bottom-right comes from (4,2)"), Pixels[5], FColor(4, 2, 0, 255));
	}

	// A rectangle running off the edge is clamped rather than reading out of bounds.
	{
		int32 Width = 8, Height = 4;
		TArray<FColor> Pixels = MakeCoordinateGrid(Width, Height);
		CropAndDownscaleColors(Pixels, Width, Height, 6, 3, 100, 100, 1.0f);
		TestEqual(TEXT("clamped width"), Width, 2);
		TestEqual(TEXT("clamped height"), Height, 1);
		TestEqual(TEXT("clamped buffer resized"), Pixels.Num(), 2);
	}

	// Downscaling halves the dimensions; upscaling is not a thing this does.
	{
		int32 Width = 8, Height = 4;
		TArray<FColor> Pixels = MakeCoordinateGrid(Width, Height);
		CropAndDownscaleColors(Pixels, Width, Height, -1, -1, -1, -1, 0.5f);
		TestEqual(TEXT("halved width"), Width, 4);
		TestEqual(TEXT("halved height"), Height, 2);
		TestEqual(TEXT("downscaled buffer resized"), Pixels.Num(), 8);
	}
	{
		int32 Width = 8, Height = 4;
		TArray<FColor> Pixels = MakeCoordinateGrid(Width, Height);
		CropAndDownscaleColors(Pixels, Width, Height, -1, -1, -1, -1, 4.0f);
		TestEqual(TEXT("no upscale"), Width, 8);
		TestEqual(TEXT("no upscale, height"), Height, 4);
	}

	// Crop then downscale compose, in that order.
	{
		int32 Width = 8, Height = 8;
		TArray<FColor> Pixels = MakeCoordinateGrid(Width, Height);
		CropAndDownscaleColors(Pixels, Width, Height, 0, 0, 4, 4, 0.5f);
		TestEqual(TEXT("cropped then halved, width"), Width, 2);
		TestEqual(TEXT("cropped then halved, height"), Height, 2);
		TestEqual(TEXT("cropped then halved, count"), Pixels.Num(), 4);
	}

	// A degenerate buffer must not crash or produce a negative size.
	{
		int32 Width = 0, Height = 0;
		TArray<FColor> Pixels;
		CropAndDownscaleColors(Pixels, Width, Height, 0, 0, 4, 4, 0.5f);
		TestEqual(TEXT("empty stays empty"), Pixels.Num(), 0);
		TestTrue(TEXT("dimensions stay non-negative"), Width >= 0 && Height >= 0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
