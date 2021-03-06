// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OnlineTitleFileInterface.h"

class FTestTitleFileInterface
{
public:
	void Test(class UWorld* InWorld);

	/**
	 * Sets the subsystem name to test
	 *
	 * @param InSubsystemName the subsystem to test
	 */
	FTestTitleFileInterface(const FString& InSubsystemName);

private:
	void FinishTest();

	/** The subsystem that was requested to be tested or the default if empty */
	const FString SubsystemName;

	/** The online interface to use for testing */
	IOnlineTitleFilePtr OnlineTitleFile;

	/** Title file list enumeration complete delegate */
	FOnEnumerateFilesCompleteDelegate OnEnumerateFilesCompleteDelegate;
	/** Title file download complete delegate */
	FOnReadFileCompleteDelegate OnReadFileCompleteDelegate;
	/** OnEnumerateFilesComplete delegate handle */
	FDelegateHandle OnEnumerateFilesCompleteDelegateHandle;
	/** OnReadFileComplete delegate handle */
	FDelegateHandle OnReadFileCompleteDelegateHandle;

	void OnEnumerateFilesComplete(bool bSuccess);

	void OnReadFileComplete(bool bSuccess, const FString& Filename);

	/** Async file reads still pending completion */
	int32 NumPendingFileReads;
};