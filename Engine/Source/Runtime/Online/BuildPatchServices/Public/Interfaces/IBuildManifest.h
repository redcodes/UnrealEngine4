// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IBuildManifest.h: Declares the IBuildManifest and IManifestField interfaces.
	This defines the functionality provided by a Build Manifest of a specific
	App, and also the interface to creating and reading custom fields.
=============================================================================*/

#pragma once

typedef TSharedPtr< class IManifestField, ESPMode::ThreadSafe > IManifestFieldPtr;
typedef TSharedRef< class IManifestField, ESPMode::ThreadSafe > IManifestFieldRef;
typedef TSharedPtr< class IBuildManifest, ESPMode::ThreadSafe > IBuildManifestPtr;
typedef TSharedRef< class IBuildManifest, ESPMode::ThreadSafe > IBuildManifestRef;

/**
 * Interface to a manifest field, which is used for accessing custom fields in the manifest
 */
class IManifestField
{
public:
	/**
	 * Virtual destructor.
	 */
	virtual ~IManifestField() { }
	
	/**
	 * Get the fields value as an FString.
	 * @return	The string value.
	 */
	virtual FString AsString() const = 0;
	
	/**
	 * Get the fields value as a double.
	 * @return	The double value.
	 */
	virtual double AsDouble() const = 0;
	
	/**
	 * Get the fields value as an int.
	 * @return	The int value.
	 */
	virtual int64 AsInteger() const = 0;
};

/**
 * Interface to a Build Manifest.
 */
class IBuildManifest
{
public:
	/**
	 * Virtual destructor.
	 */
	virtual ~IBuildManifest() { }
	
	/**
	 * Get the App ID that this manifest belongs to
	 * @return		the app ID
	 */
	virtual uint32 GetAppID() const = 0;
	
	/**
	 * Get the name of the App that this manifest belongs to
	 * @return		the app name
	 */
	virtual const FString& GetAppName() const = 0;
	
	/**
	 * Get the string App version that this manifest describes
	 * @return		the version string
	 */
	virtual const FString& GetVersionString() const = 0;
	
	/**
	 * Get the local install path to the exe that launches the App
	 * @return		local path to the launch exe
	 */
	virtual const FString& GetLaunchExe() const = 0;
	
	/**
	 * Get the command line arguments that the launch exe should be ran with
	 * @return		the launch command line
	 */
	virtual const FString& GetLaunchCommand() const = 0;
	
	/**
	 * Get the name of the prerequisites installer for the app
	 * @return		the prerequisites installer name
	 */
	virtual const FString& GetPrereqName() const = 0;
	
	/**
	 * Get the path to the prerequisites installer exe
	 * @return		local path to the prerequisites installer
	 */
	virtual const FString& GetPrereqPath() const = 0;
	
	/**
	 * Get the command line arguments that should be passed to the prerequisites installer
	 * @return		the prerequisites installer command line arguments
	 */
	virtual const FString& GetPrereqArgs() const = 0;

	/**
	 * Get the size of this download, assuming fresh install
	 * @return		the total download size in bytes
	 */
	virtual int64 GetDownloadSize() const = 0;

	/**
	 * Get the size of this build
	 * @return		the build size in bytes
	 */
	virtual int64 GetBuildSize() const = 0;

	/**
	 * Get the list of files in this build. Filenames are all relative to an install directory
	 * @return		an array containing build files
	 */
	virtual TArray<FString> GetBuildFileList() const = 0;

	/**
	 * Gets a list of files that were installed with the Old Manifest, but no longer required by this Manifest.
	 * @param OldManifest	IN		The Build Manifest that is currently installed.
	 * @param RemovableFiles	OUT		A list to receive the files that may be removed.
	 */
	virtual void GetRemovableFiles(IBuildManifestRef OldManifest, TArray< FString >& RemovableFiles) const = 0;

	/**
	 * Gets a list of files that are installed in InstallPath, but no longer required by this Manifest.
	 * @param InstallPath	IN		The path to the currently installed files.
	 * @param RemovableFiles	OUT		A list to receive the files that may be removed.
	 */
	virtual void GetRemovableFiles(const TCHAR* InstallPath, TArray< FString >& RemovableFiles) const = 0;

	/**
	 * Checks the manifest format version to see if this manifest was loaded from latest data
	 * @return True if the manifest was created from the latest format
	 */
	virtual bool NeedsResaving() const = 0;

	/**
	 * Copy the custom fields from another manifest into this one. If this manifest has custom fields, matching keys will be overwritten but extras will remain
	 * @param Other		The manifest to copy from
	 * @param bClobber	Whether to overwrite any already existing fields
	 */
	virtual void CopyCustomFields(IBuildManifestRef Other, bool bClobber) = 0;

	/**
	 * Get a custom field from the manifest
	 * @param	FieldName	The name of the custom field
	 * @return An interface to the field
	 */
	virtual const IManifestFieldPtr GetCustomField(const FString& FieldName) const = 0;

	/**
	 * Various functions for setting a custom field in the manifest
	 * @param	FieldName	The name of the custom field
	 * @param	Value		The value for the field
	 * @return An interface to the field that was created
	 */
	virtual const IManifestFieldPtr SetCustomField(const FString& FieldName, const FString& Value) = 0;
	virtual const IManifestFieldPtr SetCustomField(const FString& FieldName, const double& Value) = 0;
	virtual const IManifestFieldPtr SetCustomField(const FString& FieldName, const int64& Value) = 0;
	
	/**
	 * Duplicated this manifest to create a copy. Should be used if storing a received manifest as an installed
	 * manifest which would then be unique
	 * @return A shared ref to the new manifest
	 */
	virtual IBuildManifestRef Duplicate() const = 0;
};
