// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "CollectionManagerPrivatePCH.h"
#include "ISourceControlModule.h"

#define LOCTEXT_NAMESPACE "CollectionManager"

struct FCollectionUtils
{
	static void AppendCollectionToArray(const TSet<FName>& InObjectSet, TArray<FName>& OutObjectArray)
	{
		OutObjectArray.Reserve(OutObjectArray.Num() + InObjectSet.Num());
		for (const FName& ObjectName : InObjectSet)
		{
			OutObjectArray.Add(ObjectName);
		}
	}
};

FCollection::FCollection(const FString& InFilename, bool InUseSCC)
{
	ensure(InFilename.Len() > 0);

	bUseSCC = InUseSCC;
	SourceFilename = InFilename;
	CollectionName = FName(*FPaths::GetBaseFilename(InFilename));

	CollectionGuid = FGuid::NewGuid();

	// Initialize the file version to the most recent
	FileVersion = ECollectionVersion::CurrentVersion;
}

TSharedRef<FCollection> FCollection::Clone(const FString& InFilename, bool InUseSCC, ECollectionCloneMode InCloneMode) const
{
	TSharedRef<FCollection> NewCollection = MakeShareable(new FCollection(*this));

	// Set the new collection name and path
	NewCollection->bUseSCC = InUseSCC;
	NewCollection->SourceFilename = InFilename;
	NewCollection->CollectionName = FName(*FPaths::GetBaseFilename(InFilename));

	// Create a new GUID?
	if (InCloneMode == ECollectionCloneMode::Unique)
	{
		NewCollection->CollectionGuid = FGuid::NewGuid();
	}

	return NewCollection;
}

bool FCollection::Load(FText& OutError)
{
	ObjectSet.Empty();
	DiskSnapshot = FCollectionSnapshot();

	FString FullFileContentsString;
	if (!FFileHelper::LoadFileToString(FullFileContentsString, *SourceFilename))
	{
		OutError = FText::Format(LOCTEXT("LoadError_FailedToLoadFile", "Failed to load the collection '{0}' from disk."), FText::FromString(SourceFilename));
		return false;
	}

	// Normalize line endings and parse into array
	TArray<FString> FileContents;
	FullFileContentsString.ReplaceInline(TEXT("\r"), TEXT(""));
	FullFileContentsString.ParseIntoArray(FileContents, TEXT("\n"), /*bCullEmpty=*/false);

	if ( FileContents.Num() == 0 )
	{
		// Empty file, assume static collection with no items
		return true;
	}

	// Load the header from the contents array
	TMap<FString,FString> HeaderPairs;
	while ( FileContents.Num() )
	{
		// Pop the 0th element from the contents array and read it
		const FString Line = FileContents[0].Trim().TrimTrailing();
		FileContents.RemoveAt(0);

		if (Line.Len() == 0)
		{
			// Empty line. Done reading headers.
			break;
		}

		FString Key;
		FString Value;
		if ( Line.Split(TEXT(":"), &Key, &Value) )
		{
			HeaderPairs.Add(Key, Value);
		}
	}

	// Now process the header pairs to prepare and validate this collection
	if ( !LoadHeaderPairs(HeaderPairs) )
	{
		// Bad header
		OutError = FText::Format(LOCTEXT("LoadError_BadHeader", "The collection file '{0}' contains a bad header and could not be loaded."), FText::FromString(SourceFilename));
		return false;
	}

	// Now load the content if the header load was successful
	if ( IsDynamic() )
	{
		// @todo collection Load dynamic collections
	}
	else
	{
		// Static collection, a flat list of asset paths
		for (FString Line : FileContents)
		{
			Line.Trim();
			Line.TrimTrailing();

			if ( Line.Len() )
			{
				AddObjectToCollection(FName(*Line));
			}
		}
	}

	DiskSnapshot.TakeSnapshot(*this);

	return true;
}

bool FCollection::Save(FText& OutError)
{
	if ( !ensure(SourceFilename.Len()) )
	{
		OutError = LOCTEXT("Error_Internal", "There was an internal error.");
		return false;
	}

	// Store the start time for profiling reasons
	double SaveStartTime = FPlatformTime::Seconds();

	// Keep track of save progress to update the slow task dialog
	const int32 SaveProgressDenominator = 3;
	int32 SaveProgressNumerator = 0;


	GWarn->BeginSlowTask( FText::Format( LOCTEXT("SavingCollection", "Saving Collection {0}"), FText::FromName( CollectionName ) ), true);
	GWarn->UpdateProgress(SaveProgressNumerator++, SaveProgressDenominator);

	if ( bUseSCC )
	{
		// Checkout the file
		if ( !CheckoutCollection(OutError) )
		{
			UE_LOG(LogCollectionManager, Error, TEXT("Failed to check out a collection file: %s"), *CollectionName.ToString());
			GWarn->EndSlowTask();
			return false;
		}
	}

	GWarn->UpdateProgress(SaveProgressNumerator++, SaveProgressDenominator);

	// Generate a string with the file contents
	FString FileOutput;

	// Start with the header
	TMap<FString,FString> HeaderPairs;
	SaveHeaderPairs(HeaderPairs);
	for (const auto& HeaderPair : HeaderPairs)
	{
		FileOutput += HeaderPair.Key + TEXT(":") + HeaderPair.Value + LINE_TERMINATOR;
	}
	FileOutput += LINE_TERMINATOR;

	// Now for the content
	if ( IsDynamic() )
	{
		// @todo Dynamic collections
	}
	else
	{
		// Write out the set as a sorted array to keep things in a known order for diffing
		TArray<FName> ObjectList = ObjectSet.Array();
		ObjectList.Sort();

		// Static collection. Save a flat list of all objects in the collection.
		for (const FName& ObjectName : ObjectList)
		{
			FileOutput += ObjectName.ToString() + LINE_TERMINATOR;
		}
	}

	// Attempt to save the file
	bool bSaveSuccessful = false;
	if ( ensure(FileOutput.Len()) )
	{
		// We have some output, write it to file
		if ( FFileHelper::SaveStringToFile(FileOutput, *SourceFilename) )
		{
			bSaveSuccessful = true;
		}
		else
		{
			OutError = FText::Format(LOCTEXT("Error_WriteFailed", "Failed to write to collection file: {0}"), FText::FromString(SourceFilename));
			UE_LOG(LogCollectionManager, Error, TEXT("%s"), *OutError.ToString());
		}
	}
	else
	{
		OutError = LOCTEXT("Error_Internal", "There was an internal error.");
	}

	GWarn->UpdateProgress(SaveProgressNumerator++, SaveProgressDenominator);

	if ( bSaveSuccessful )
	{
		if ( bUseSCC )
		{
			// Check in the file if the save was successful
			if ( bSaveSuccessful )
			{
				if ( !CheckinCollection(OutError) )
				{
					UE_LOG(LogCollectionManager, Error, TEXT("Failed to check in a collection successfully saving: %s"), *CollectionName.ToString());
					bSaveSuccessful = false;
				}
			}
		
			// If the save was not successful or the checkin failed, revert
			if ( !bSaveSuccessful )
			{
				FText Unused;
				if ( !RevertCollection(Unused) )
				{
					// The revert failed... file will be left on disk as it was saved.
					// DiskAssetList will still hold the version of the file when this collection was last loaded or saved successfully so nothing will be out of sync.
					// If the user closes the editor before successfully saving, this file may not be exactly what was seen at the time the editor closed.
					UE_LOG(LogCollectionManager, Warning, TEXT("Failed to revert a checked out collection after failing to save or checkin: %s"), *CollectionName.ToString());
				}
			}
		}
	}

	GWarn->UpdateProgress(SaveProgressNumerator++, SaveProgressDenominator);

	if ( bSaveSuccessful )
	{
		// Files are always saved at the latest version as loading should take care of data upgrades
		FileVersion = ECollectionVersion::CurrentVersion;

		DiskSnapshot.TakeSnapshot(*this);
	}

	GWarn->EndSlowTask();

	UE_LOG(LogCollectionManager, Verbose, TEXT("Saved collection %s in %0.6f seconds"), *CollectionName.ToString(), FPlatformTime::Seconds() - SaveStartTime);

	return bSaveSuccessful;
}

bool FCollection::DeleteSourceFile(FText& OutError)
{
	bool bSuccessfullyDeleted = false;

	if ( SourceFilename.Len() )
	{
		if ( bUseSCC )
		{
			bSuccessfullyDeleted = DeleteFromSourceControl(OutError);
		}
		else
		{
			bSuccessfullyDeleted = IFileManager::Get().Delete(*SourceFilename);
			if ( !bSuccessfullyDeleted )
			{
				OutError = FText::Format(LOCTEXT("Error_DiskDeleteFailed", "Failed to delete the collection file: {0}"), FText::FromString(SourceFilename));
			}
		}
	}
	else
	{
		// No source file. Since it doesn't exist we will say it is deleted.
		bSuccessfullyDeleted = true;
	}

	if ( bSuccessfullyDeleted )
	{
		DiskSnapshot = FCollectionSnapshot();
	}

	return bSuccessfullyDeleted;
}

bool FCollection::AddObjectToCollection(FName ObjectPath)
{
	if (!ObjectSet.Contains(ObjectPath))
	{
		ObjectSet.Add(ObjectPath);
		return true;
	}

	return false;
}

bool FCollection::RemoveObjectFromCollection(FName ObjectPath)
{
	return ObjectSet.Remove(ObjectPath) > 0;
}

void FCollection::GetAssetsInCollection(TArray<FName>& Assets) const
{
	for (const FName& ObjectName : ObjectSet)
	{
		if (!ObjectName.ToString().StartsWith(TEXT("/Script/")))
		{
			Assets.Add(ObjectName);
		}
	}
}

void FCollection::GetClassesInCollection(TArray<FName>& Classes) const
{
	for (const FName& ObjectName : ObjectSet)
	{
		if (ObjectName.ToString().StartsWith(TEXT("/Script/")))
		{
			Classes.Add(ObjectName);
		}
	}
}

void FCollection::GetObjectsInCollection(TArray<FName>& Objects) const
{
	FCollectionUtils::AppendCollectionToArray(ObjectSet, Objects);
}

bool FCollection::IsObjectInCollection(FName ObjectPath) const
{
	return ObjectSet.Contains(ObjectPath);
}

bool FCollection::IsRedirectorInCollection(FName ObjectPath) const
{
	// Redirectors are fixed up in-memory once the asset registry has finished loading, 
	// so we need to test our on-disk set of objects rather than our in-memory set of objects
	return DiskSnapshot.ObjectSet.Contains(ObjectPath);
}

bool FCollection::IsDynamic() const
{
	// @todo collection Dynamic collections
	return false;
}

bool FCollection::IsEmpty() const
{
	return ObjectSet.Num() == 0;
}

void FCollection::PrintCollection() const
{
	if ( IsDynamic() )
	{
		// @todo collection Printing Dynamic collections
	}
	else
	{
		UE_LOG(LogCollectionManager, Log, TEXT("    Printing static elements of collection %s"), *CollectionName.ToString());
		UE_LOG(LogCollectionManager, Log, TEXT("    ============================="));

		// Print the set as a sorted array to keep things in a sane order
		TArray<FName> ObjectList = ObjectSet.Array();
		ObjectList.Sort();

		for (const FName& ObjectName : ObjectList)
		{
			UE_LOG(LogCollectionManager, Log, TEXT("        %s"), *ObjectName.ToString());
		}
	}
}

void FCollection::SaveHeaderPairs(TMap<FString,FString>& OutHeaderPairs) const
{
	// These pairs will appear at the top of the file followed by a newline
	OutHeaderPairs.Add(TEXT("FileVersion"), FString::FromInt(ECollectionVersion::CurrentVersion)); // Files are always saved at the latest version as loading should take care of data upgrades
	OutHeaderPairs.Add(TEXT("Type"), IsDynamic() ? TEXT("Dynamic") : TEXT("Static"));
	OutHeaderPairs.Add(TEXT("Guid"), CollectionGuid.ToString(EGuidFormats::DigitsWithHyphens));
	OutHeaderPairs.Add(TEXT("ParentGuid"), ParentCollectionGuid.ToString(EGuidFormats::DigitsWithHyphens));
}

bool FCollection::LoadHeaderPairs(const TMap<FString,FString>& InHeaderPairs)
{
	// These pairs will appeared at the top of the file being loaded
	// First find all the known pairs
	const FString* Version = InHeaderPairs.Find(TEXT("FileVersion"));
	if ( !Version )
	{
		// FileVersion is required
		return false;
	}

	const FString* Type = InHeaderPairs.Find(TEXT("Type"));
	if ( !Type )
	{
		// Type is required
		return false;
	}

	FileVersion = (ECollectionVersion::Type)FCString::Atoi(**Version);

	if (FileVersion >= ECollectionVersion::AddedCollectionGuid)
	{
		const FString* GuidStr = InHeaderPairs.Find(TEXT("Guid"));
		if ( !GuidStr || !FGuid::Parse(*GuidStr, CollectionGuid) )
		{
			// Guid is required
			return false;
		}

		const FString* ParentGuidStr = InHeaderPairs.Find(TEXT("ParentGuid"));
		if ( !ParentGuidStr || !FGuid::Parse(*ParentGuidStr, ParentCollectionGuid) )
		{
			ParentCollectionGuid = FGuid();
		}
	}

	if ( *Type == TEXT("Dynamic") )
	{
		// @todo Set this file up to be dynamic
	}

	return FileVersion > 0 && FileVersion <= ECollectionVersion::CurrentVersion;
}

void FCollection::MergeWithCollection(const FCollection& Other)
{
	if ( IsDynamic() )
	{
		// @todo collection Merging dynamic collections?
	}
	else
	{
		// Gather the differences from the file on disk
		TArray<FName> ObjectsAdded;
		TArray<FName> ObjectsRemoved;
		GetObjectDifferencesFromDisk(ObjectsAdded, ObjectsRemoved);

		// Copy asset list from other collection
		DiskSnapshot = Other.DiskSnapshot;
		ObjectSet = DiskSnapshot.ObjectSet;

		// Add the objects that were added before the merge
		for (const FName& AddedObjectName : ObjectsAdded)
		{
			ObjectSet.Add(AddedObjectName);
		}

		// Remove the objects that were removed before the merge
		for (const FName& RemovedObjectName : ObjectsRemoved)
		{
			ObjectSet.Remove(RemovedObjectName);
		}
	}
}

void FCollection::GetObjectDifferencesFromDisk(TArray<FName>& ObjectsAdded, TArray<FName>& ObjectsRemoved)
{
	// Find the objects that were removed since the disk list
	for (const FName& DiskObjectName : DiskSnapshot.ObjectSet)
	{
		if (!ObjectSet.Contains(DiskObjectName))
		{
			ObjectsRemoved.Add(DiskObjectName);
		}
	}

	// Find the objects that were added since the disk list
	for (const FName& MemoryObjectName : ObjectSet)
	{
		if (!DiskSnapshot.ObjectSet.Contains(MemoryObjectName))
		{
			ObjectsAdded.Add(MemoryObjectName);
		}
	}
}

bool FCollection::CheckoutCollection(FText& OutError)
{
	if ( !ensure(SourceFilename.Len()) )
	{
		OutError = LOCTEXT("Error_Internal", "There was an internal error.");
		return false;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if ( !ISourceControlModule::Get().IsEnabled() )
	{
		OutError = LOCTEXT("Error_SCCDisabled", "Source control is not enabled. Enable source control in the preferences menu.");
		return false;
	}

	if ( !SourceControlProvider.IsAvailable() )
	{
		OutError = LOCTEXT("Error_SCCNotAvailable", "Source control is currently not available. Check your connection and try again.");
		return false;
	}

	const FString AbsoluteFilename = FPaths::ConvertRelativePathToFull(SourceFilename);
	FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(AbsoluteFilename, EStateCacheUsage::ForceUpdate);

	bool bSuccessfullyCheckedOut = false;

	if (SourceControlState.IsValid() && SourceControlState->IsDeleted())
	{
		// Revert our delete
		if ( !RevertCollection(OutError) )
		{
			return false;
		}

		// Make sure we get a fresh state from the server
		SourceControlState = SourceControlProvider.GetState(AbsoluteFilename, EStateCacheUsage::ForceUpdate);
	}

	// If not at the head revision, sync up
	if (SourceControlState.IsValid() && !SourceControlState->IsCurrent())
	{
		if ( SourceControlProvider.Execute(ISourceControlOperation::Create<FSync>(), AbsoluteFilename) == ECommandResult::Failed )
		{
			// Could not sync up with the head revision
			OutError = FText::Format(LOCTEXT("Error_SCCSync", "Failed to sync collection '{0}' to the head revision."), FText::FromName(CollectionName));
			return false;
		}

		// Check to see if the file exists at the head revision
		if ( IFileManager::Get().FileExists(*SourceFilename) )
		{
			// File found! Load it and merge with our local changes
			FText LoadErrorText;
			FCollection NewCollection(SourceFilename, false);
			if ( !NewCollection.Load(LoadErrorText) )
			{
				// Failed to load the head revision file so it isn't safe to delete it
				OutError = FText::Format(LOCTEXT("Error_SCCBadHead", "Failed to load the collection '{0}' at the head revision. {1}"), FText::FromName(CollectionName), LoadErrorText);
				return false;
			}

			// Loaded the head revision, now merge up so the files are in a consistent state
			MergeWithCollection(NewCollection);
		}

		// Make sure we get a fresh state from the server
		SourceControlState = SourceControlProvider.GetState(AbsoluteFilename, EStateCacheUsage::ForceUpdate);
	}

	if(SourceControlState.IsValid())
	{
		if(!SourceControlState->IsSourceControlled())
		{
			// Not yet in the depot. We'll add it when we call CheckinCollection
			bSuccessfullyCheckedOut = true;
		}
		else if(SourceControlState->IsAdded() || SourceControlState->IsCheckedOut())
		{
			// Already checked out or opened for add
			bSuccessfullyCheckedOut = true;
		}
		else if(SourceControlState->CanCheckout())
		{
			// In depot and needs to be checked out
			bSuccessfullyCheckedOut = (SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), AbsoluteFilename) == ECommandResult::Succeeded);
			if (!bSuccessfullyCheckedOut)
			{
				OutError = FText::Format(LOCTEXT("Error_SCCCheckout", "Failed to check out collection '{0}'"), FText::FromName(CollectionName));
			}
		}
		else if(!SourceControlState->IsCurrent())
		{
			OutError = FText::Format(LOCTEXT("Error_SCCNotCurrent", "Collection '{0}' is not at head revision after sync."), FText::FromName(CollectionName));
		}
		else if(SourceControlState->IsCheckedOutOther())
		{
			OutError = FText::Format(LOCTEXT("Error_SCCCheckedOutOther", "Collection '{0}' is checked out by another user."), FText::FromName(CollectionName));
		}
		else
		{
			OutError = FText::Format(LOCTEXT("Error_SCCUnknown", "Could not determine source control state for collection '{0}'"), FText::FromName(CollectionName));
		}
	}
	else
	{
		OutError = LOCTEXT("Error_SCCInvalid", "Source control state is invalid.");
	}

	return bSuccessfullyCheckedOut;
}

bool FCollection::CheckinCollection(FText& OutError)
{
	if ( !ensure(SourceFilename.Len()) )
	{
		OutError = LOCTEXT("Error_Internal", "There was an internal error.");
		return false;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if ( !ISourceControlModule::Get().IsEnabled() )
	{
		OutError = LOCTEXT("Error_SCCDisabled", "Source control is not enabled. Enable source control in the preferences menu.");
		return false;
	}

	if ( !SourceControlProvider.IsAvailable() )
	{
		OutError = LOCTEXT("Error_SCCNotAvailable", "Source control is currently not available. Check your connection and try again.");
		return false;
	}

	const FString AbsoluteFilename = FPaths::ConvertRelativePathToFull(SourceFilename);
	FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(AbsoluteFilename, EStateCacheUsage::ForceUpdate);

	if (SourceControlState.IsValid() && !SourceControlState->IsSourceControlled())
	{
		// Not yet in the depot. Add it.
		const bool bWasAdded = (SourceControlProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), AbsoluteFilename) == ECommandResult::Succeeded);
		if (!bWasAdded)
		{
			OutError = FText::Format(LOCTEXT("Error_SCCAdd", "Failed to add collection '{0}' to source control."), FText::FromName(CollectionName));
			return false;
		}
		SourceControlState = SourceControlProvider.GetState(AbsoluteFilename, EStateCacheUsage::ForceUpdate);
	}

	if ( SourceControlState.IsValid() && !(SourceControlState->IsCheckedOut() || SourceControlState->IsAdded()) )
	{
		OutError = FText::Format(LOCTEXT("Error_SCCNotCheckedOut", "Collection '{0}' not checked out or open for add."), FText::FromName(CollectionName));
		return false;
	}

	// Form an appropriate summary for the changelist
	const FText CollectionNameText = FText::FromName( CollectionName );
	FTextBuilder ChangelistDescBuilder;

	if (SourceControlState.IsValid() && SourceControlState->IsAdded())
	{
		ChangelistDescBuilder.AppendLineFormat(LOCTEXT("CollectionAddedNewDesc", "Added collection '{0}'"), CollectionNameText);
	}
	else
	{
		if (IsDynamic())
		{
			// @todo collection Change description for dynamic collections
		}
		else
		{
			// Gather differences from disk
			TArray<FName> ObjectsAdded;
			TArray<FName> ObjectsRemoved;
			GetObjectDifferencesFromDisk(ObjectsAdded, ObjectsRemoved);

			ObjectsAdded.Sort();
			ObjectsRemoved.Sort();

			// Report added files
			FFormatNamedArguments Args;
			Args.Add(TEXT("FirstObjectAdded"), ObjectsAdded.Num() > 0 ? FText::FromName(ObjectsAdded[0]) : NSLOCTEXT("Core", "None", "None"));
			Args.Add(TEXT("NumberAdded"), FText::AsNumber(ObjectsAdded.Num()));
			Args.Add(TEXT("FirstObjectRemoved"), ObjectsRemoved.Num() > 0 ? FText::FromName(ObjectsRemoved[0]) : NSLOCTEXT("Core", "None", "None"));
			Args.Add(TEXT("NumberRemoved"), FText::AsNumber(ObjectsRemoved.Num()));
			Args.Add(TEXT("CollectionName"), CollectionNameText);

			if (ObjectsAdded.Num() == 1)
			{
				ChangelistDescBuilder.AppendLineFormat(LOCTEXT("CollectionAddedSingleDesc", "Added '{FirstObjectAdded}' to collection '{CollectionName}'"), Args);
			}
			else if (ObjectsAdded.Num() > 1)
			{
				ChangelistDescBuilder.AppendLineFormat(LOCTEXT("CollectionAddedMultipleDesc", "Added {NumberAdded} objects to collection '{CollectionName}':"), Args);

				ChangelistDescBuilder.Indent();
				for (const FName& AddedObjectName : ObjectsAdded)
				{
					ChangelistDescBuilder.AppendLine(FText::FromName(AddedObjectName));
				}
				ChangelistDescBuilder.Unindent();
			}

			if ( ObjectsRemoved.Num() == 1 )
			{
				ChangelistDescBuilder.AppendLineFormat(LOCTEXT("CollectionRemovedSingleDesc", "Removed '{FirstObjectRemoved}' from collection '{CollectionName}'"), Args);
			}
			else if (ObjectsRemoved.Num() > 1)
			{
				ChangelistDescBuilder.AppendLineFormat(LOCTEXT("CollectionRemovedMultipleDesc", "Removed {NumberRemoved} objects from collection '{CollectionName}'"), Args);

				ChangelistDescBuilder.Indent();
				for (const FName& RemovedObjectName : ObjectsRemoved)
				{
					ChangelistDescBuilder.AppendLine(FText::FromName(RemovedObjectName));
				}
				ChangelistDescBuilder.Unindent();
			}
		}

		// Parent change?
		if (DiskSnapshot.ParentCollectionGuid != ParentCollectionGuid)
		{
			ChangelistDescBuilder.AppendLineFormat(LOCTEXT("CollectionChangedParentDesc", "Changed the parent of collection '{0}'"), CollectionNameText);
		}

		// Version bump?
		if (FileVersion < ECollectionVersion::CurrentVersion)
		{
			ChangelistDescBuilder.AppendLineFormat(LOCTEXT("CollectionUpgradedDesc", "Upgraded collection '{0}' (was version {1}, now version {2})"), CollectionNameText, FText::AsNumber(FileVersion), FText::AsNumber(ECollectionVersion::CurrentVersion));
		}
	}

	FText ChangelistDesc = ChangelistDescBuilder.ToText();
	if (ChangelistDesc.IsEmpty())
	{
		// No changes could be detected
		ChangelistDesc = FText::Format(LOCTEXT("CollectionNotModifiedDesc", "Collection '{0}' not modified"), CollectionNameText);
	}

	// Finally check in the file
	TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckInOperation = ISourceControlOperation::Create<FCheckIn>();
	CheckInOperation->SetDescription( ChangelistDesc );
	if ( SourceControlProvider.Execute( CheckInOperation, AbsoluteFilename ) )
	{
		return true;
	}
	else 
	{
		OutError = FText::Format(LOCTEXT("Error_SCCCheckIn", "Failed to check in collection '{0}'."), FText::FromName(CollectionName));
		return false;
	}
}

bool FCollection::RevertCollection(FText& OutError)
{
	if ( !ensure(SourceFilename.Len()) )
	{
		OutError = LOCTEXT("Error_Internal", "There was an internal error.");
		return false;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if ( !ISourceControlModule::Get().IsEnabled() )
	{
		OutError = LOCTEXT("Error_SCCDisabled", "Source control is not enabled. Enable source control in the preferences menu.");
		return false;
	}

	if ( !SourceControlProvider.IsAvailable() )
	{
		OutError = LOCTEXT("Error_SCCNotAvailable", "Source control is currently not available. Check your connection and try again.");
		return false;
	}

	FString AbsoluteFilename = FPaths::ConvertRelativePathToFull(SourceFilename);
	FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(AbsoluteFilename, EStateCacheUsage::ForceUpdate);

	if ( SourceControlState.IsValid() && !(SourceControlState->IsCheckedOut() || SourceControlState->IsAdded()) )
	{
		OutError = FText::Format(LOCTEXT("Error_SCCNotCheckedOut", "Collection '{0}' not checked out or open for add."), FText::FromName(CollectionName));
		return false;
	}

	if ( SourceControlProvider.Execute(ISourceControlOperation::Create<FRevert>(), AbsoluteFilename) == ECommandResult::Succeeded)
	{
		return true;
	}
	else
	{
		OutError = FText::Format(LOCTEXT("Error_SCCRevert", "Could not revert collection '{0}'"), FText::FromName(CollectionName));
		return false;
	}
}

bool FCollection::DeleteFromSourceControl(FText& OutError)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if ( !ISourceControlModule::Get().IsEnabled() )
	{
		OutError = LOCTEXT("Error_SCCDisabled", "Source control is not enabled. Enable source control in the preferences menu.");
		return false;
	}

	if ( !SourceControlProvider.IsAvailable() )
	{
		OutError = LOCTEXT("Error_SCCNotAvailable", "Source control is currently not available. Check your connection and try again.");
		return false;
	}

	bool bDeletedSuccessfully = false;

	const int32 DeleteProgressDenominator = 2;
	int32 DeleteProgressNumerator = 0;

	const FText CollectionNameText = FText::FromName( CollectionName );

	FFormatNamedArguments Args;
	Args.Add( TEXT("CollectionName"), CollectionNameText );
	const FText StatusUpdate = FText::Format( LOCTEXT("DeletingCollection", "Deleting Collection {CollectionName}"), Args );

	GWarn->BeginSlowTask( StatusUpdate, true );
	GWarn->UpdateProgress(DeleteProgressNumerator++, DeleteProgressDenominator);

	FString AbsoluteFilename = FPaths::ConvertRelativePathToFull(SourceFilename);
	FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(AbsoluteFilename, EStateCacheUsage::ForceUpdate);
	
	GWarn->UpdateProgress(DeleteProgressNumerator++, DeleteProgressDenominator);

	// If checked out locally for some reason, revert
	if (SourceControlState.IsValid() && (SourceControlState->IsAdded() || SourceControlState->IsCheckedOut() || SourceControlState->IsDeleted()))
	{
		if ( !RevertCollection(OutError) )
		{
			// Failed to revert, just bail out
			GWarn->EndSlowTask();
			return false;
		}

		// Make sure we get a fresh state from the server
		SourceControlState = SourceControlProvider.GetState(AbsoluteFilename, EStateCacheUsage::ForceUpdate);
	}

	// If not at the head revision, sync up
	if (SourceControlState.IsValid() && !SourceControlState->IsCurrent())
	{
		if ( SourceControlProvider.Execute(ISourceControlOperation::Create<FSync>(), AbsoluteFilename) == ECommandResult::Failed)
		{
			// Could not sync up with the head revision
			GWarn->EndSlowTask();
			OutError = FText::Format(LOCTEXT("Error_SCCSync", "Failed to sync collection '{0}' to the head revision."), FText::FromName(CollectionName));
			return false;
		}

		// Check to see if the file exists at the head revision
		if ( !IFileManager::Get().FileExists(*SourceFilename) )
		{
			// File was already deleted, consider this a success
			GWarn->EndSlowTask();
			return true;
		}
			
		FCollection NewCollection(SourceFilename, false);
		FText LoadErrorText;
		if ( !NewCollection.Load(LoadErrorText) )
		{
			// Failed to load the head revision file so it isn't safe to delete it
			GWarn->EndSlowTask();
			OutError = FText::Format(LOCTEXT("Error_SCCBadHead", "Failed to load the collection '{0}' at the head revision. {1}"), FText::FromName(CollectionName), LoadErrorText);
			return false;
		}

		// Loaded the head revision, now merge up so the files are in a consistent state
		MergeWithCollection(NewCollection);

		// Make sure we get a fresh state from the server
		SourceControlState = SourceControlProvider.GetState(AbsoluteFilename, EStateCacheUsage::ForceUpdate);
	}

	GWarn->UpdateProgress(DeleteProgressNumerator++, DeleteProgressDenominator);

	if(SourceControlState.IsValid())
	{
		if(SourceControlState->IsAdded() || SourceControlState->IsCheckedOut())
		{
			OutError = FText::Format(LOCTEXT("Error_SCCDeleteWhileCheckedOut", "Failed to delete collection '{0}' in source control because it is checked out or open for add."), FText::FromName(CollectionName));
		}
		else if(SourceControlState->CanCheckout())
		{
			if ( SourceControlProvider.Execute(ISourceControlOperation::Create<FDelete>(), AbsoluteFilename) == ECommandResult::Succeeded )
			{
				// Now check in the delete
				const FText ChangelistDesc = FText::Format( LOCTEXT("CollectionDeletedDesc", "Deleted collection: {CollectionName}"), CollectionNameText );
				TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckInOperation = ISourceControlOperation::Create<FCheckIn>();
				CheckInOperation->SetDescription(ChangelistDesc);
				if ( SourceControlProvider.Execute( CheckInOperation, AbsoluteFilename ) )
				{
					// Deleted successfully!
					bDeletedSuccessfully = true;
				}
				else
				{
					FText Unused;
					if ( !RevertCollection(Unused) )
					{
						UE_LOG(LogCollectionManager, Warning, TEXT("Failed to revert collection '%s' after failing to check in the file that was marked for delete."), *CollectionName.ToString());
					}

					OutError = FText::Format(LOCTEXT("Error_SCCCheckIn", "Failed to check in collection '{0}'."), FText::FromName(CollectionName));
				}
			}
			else
			{
				OutError = FText::Format(LOCTEXT("Error_SCCDeleteFailed", "Failed to delete collection '{0}' in source control."), FText::FromName(CollectionName));
			}
		}
		else if(!SourceControlState->IsSourceControlled())
		{
			// Not yet in the depot or deleted. We can just delete it from disk.
			bDeletedSuccessfully = IFileManager::Get().Delete(*AbsoluteFilename);
			if ( !bDeletedSuccessfully )
			{
				OutError = FText::Format(LOCTEXT("Error_DiskDeleteFailed", "Failed to delete the collection file: {0}"), FText::FromString(AbsoluteFilename));
			}
		}
		else if (!SourceControlState->IsCurrent())
		{
			OutError = FText::Format(LOCTEXT("Error_SCCNotCurrent", "Collection '{0}' is not at head revision after sync."), FText::FromName(CollectionName));
		}
		else if(SourceControlState->IsCheckedOutOther())
		{
			OutError = FText::Format(LOCTEXT("Error_SCCCheckedOutOther", "Collection '{0}' is checked out by another user."), FText::FromName(CollectionName));
		}
		else
		{
			OutError = FText::Format(LOCTEXT("Error_SCCUnknown", "Could not determine source control state for collection '{0}'"), FText::FromName(CollectionName));
		}
	}
	else
	{
		OutError = LOCTEXT("Error_SCCInvalid", "Source control state is invalid.");
	}

	GWarn->UpdateProgress(DeleteProgressNumerator++, DeleteProgressDenominator);

	GWarn->EndSlowTask();

	return bDeletedSuccessfully;
}


#undef LOCTEXT_NAMESPACE
