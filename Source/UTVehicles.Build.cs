// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UTVehicles : ModuleRules
	{
		public UTVehicles(TargetInfo Target)
		{
			PrivateIncludePaths.Add("UTVehicles/Private");
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			PublicIncludePaths.AddRange(new string[] {
				"UTVehicles/Public"
			});
			PrivateIncludePaths.AddRange(new string[] {
				"UnrealTournament/Private",
				"UnrealTournament/Classes"
			});

			PublicDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UnrealTournament",
				"InputCore",
				"PhysXVehicles"
			});

			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"Slate"
			});
		}
	}
}
