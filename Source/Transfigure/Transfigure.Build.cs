// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Transfigure : ModuleRules
{
    public Transfigure(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[] {
            ModuleDirectory,
            Path.Combine(ModuleDirectory, "Character"),
            Path.Combine(ModuleDirectory, "StateMachine"),
            Path.Combine(ModuleDirectory, "States"),
            Path.Combine(ModuleDirectory, "Grapple"),
            Path.Combine(ModuleDirectory, "Transfiguration"),
            Path.Combine(ModuleDirectory, "Combat"),
            Path.Combine(ModuleDirectory, "Enemy"),
            Path.Combine(ModuleDirectory, "Destruction"),
            Path.Combine(ModuleDirectory, "Temporal"),
            Path.Combine(ModuleDirectory, "RingWorld"),
            Path.Combine(ModuleDirectory, "City"),
        });

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "PhysicsCore",
            "Niagara",
            "ProceduralMeshComponent",
            "UMG",
            "AIModule",
            "GameplayTasks",
            "NavigationSystem",
            "DeveloperSettings",
            "GeometryCore",
            "GeometryFramework",
        });

        PrivateDependencyModuleNames.AddRange(new string[] { });
    }
}

//PrivateDependencyModuleNames.AddRange(new string[] {  });

// Uncomment if you are using Slate UI
// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

// Uncomment if you are using online features
// PrivateDependencyModuleNames.Add("OnlineSubsystem");

// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
//	}
//}