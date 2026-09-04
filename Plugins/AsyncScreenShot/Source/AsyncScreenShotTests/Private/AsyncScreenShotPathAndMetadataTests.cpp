// Copyright (c) 2023 Grigoryev Daniil. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Json.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "AsyncScreenShotBPLibrary.h"

/**
 * The non-GPU surface of AsyncScreenShot: where screenshots land, and the metadata sidecar
 * format tooling reads back.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBScreenshotSavePathIsUsable, "AsyncScreenShot.Path.SavePathIsAbsoluteUnderSaved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FUBScreenshotSavePathIsUsable::RunTest(const FString& Parameters)
{
	// When: the default screenshot folder is resolved
	const FString Path = UAsyncScreenShotBPLibrary::GetScreenshotSavePath();

	// Then: it is a full path inside the project's Saved area - something a Blueprint can
	// concatenate a file name onto without further work.
	TestFalse(TEXT("path is non-empty"), Path.IsEmpty());
	TestFalse(TEXT("path is absolute, not relative"), FPaths::IsRelative(Path));
	TestTrue(TEXT("path lives under Saved"), Path.Contains(TEXT("/Saved/")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBScreenshotMetadataRoundtrip, "AsyncScreenShot.Metadata.WriterProducesReadableJson",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FUBScreenshotMetadataRoundtrip::RunTest(const FString& Parameters)
{
	// Given: an output folder under Saved and a couple of metadata pairs, one of which
	// contains characters that must be escaped inside JSON strings
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("AutomationScreenMeta");
	IFileManager::Get().DeleteDirectory(*Directory, /*RequireExists*/false, /*Tree*/true);

	TMap<FString, FString> Metadata;
	Metadata.Add(TEXT("MapName"), TEXT("L_Test"));
	Metadata.Add(TEXT("Note"), TEXT("quote\" and \\ slash"));

	// When: metadata is written
	UAsyncScreenShotBPLibrary::SaveScreenshotMetadata(Directory, TEXT("meta"), Metadata);

	const FString FullPath = Directory / TEXT("meta.json");
	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.FileExists(*FullPath))
	{
		AddError(FString::Printf(TEXT("metadata file '%s' was not created"), *FullPath));
		// try to leave the workspace clean anyway
		FileManager.DeleteDirectory(*Directory, false, true);
		return false;
	}

	// Then: the file parses as JSON and carries every pair back verbatim.
	FString JsonText;
	FFileHelper::LoadFileToString(JsonText, *FullPath);

	TSharedPtr<FJsonObject> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		AddError(TEXT("metadata file is not valid JSON"));
		FileManager.DeleteDirectory(*Directory, false, true);
		return false;
	}

	TestTrue(TEXT("timestamp recorded"), Parsed->HasField(TEXT("Timestamp")));
	TestEqual(TEXT("plain value survives"), Parsed->GetStringField(TEXT("MapName")), FString(TEXT("L_Test")));
	TestEqual(TEXT("escaped value survives"), Parsed->GetStringField(TEXT("Note")), FString(TEXT("quote\" and \\ slash")));

	// And: automation leaves no litter behind.
	FileManager.DeleteDirectory(*Directory, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
