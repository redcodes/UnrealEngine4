// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealAudioWasapi : ModuleRules
{
	public UnrealAudioWasapi(TargetInfo Target)
	{
		PrivateIncludePaths.Add("Runtime/UnrealAudio/Private");
		PublicIncludePaths.Add("Runtime/UnrealAudio/Public");


		PrivateIncludePathModuleNames.Add("TargetPlatform");

		PrivateDependencyModuleNames.AddRange(
		new string[] {
				"Core",
				"CoreUObject",
			}
		);

		PublicDependencyModuleNames.Add("UnrealAudio");

	}
}
