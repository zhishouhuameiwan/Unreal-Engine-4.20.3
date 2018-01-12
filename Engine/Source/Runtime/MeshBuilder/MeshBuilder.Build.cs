// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MeshBuilder : ModuleRules
	{
		public MeshBuilder(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateIncludePaths.Add("Runtime/MeshBuilder/Private");
            PublicIncludePaths.Add("Runtime/MeshBuilder/Public");

            PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Engine",
                    "RenderCore",
                    "MeshDescription",
                    "MeshReductionInterface",
                    "RenderCore",
                    "RawMesh"
                }
			);

            AddEngineThirdPartyPrivateStaticDependencies(Target, "nvTriStrip");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "ForsythTriOptimizer");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "nvTessLib");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "MikkTSpace");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "QuadricMeshReduction");

            if (Target.bCompileSimplygon == true)
            {
                AddEngineThirdPartyPrivateDynamicDependencies(Target, "SimplygonMeshReduction");

                if (Target.bCompileSimplygonSSF == true)
                {
                    DynamicallyLoadedModuleNames.AddRange(
                        new string[] {
                    "SimplygonSwarm"
                    }
                    );
                }
            }
        }
	}
}