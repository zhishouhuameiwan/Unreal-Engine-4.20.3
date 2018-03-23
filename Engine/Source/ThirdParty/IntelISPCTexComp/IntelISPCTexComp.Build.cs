// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IntelISPCTexComp : ModuleRules
{
	public IntelISPCTexComp(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string LibraryPath = Target.UEThirdPartySourceDirectory + "IntelISPCTexComp/ispc_texcomp/";
        string BinaryFolder = Target.UEThirdPartyBinariesDirectory + "IntelISPCTexComp/";
		PublicIncludePaths.Add(LibraryPath);

        //NOTE: If you change bUseDebugBuild, you must also change FTextureFormatIntelISPCTexCompModule.GetTextureFormat() to load the corresponding DLL
        bool bUseDebugBuild = false;

		if ( (Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Win32) )
		{
            string platformName = (Target.Platform == UnrealTargetPlatform.Win64) ? "Win64" : "Win32";
			string configName = bUseDebugBuild ? "Debug" : "Release";
            string LibFolder = LibraryPath + "lib/" + platformName + "-" + configName;
            string DLLFolder = BinaryFolder + platformName + "-" + configName;
            string DLLFilePath = DLLFolder + "/ispc_texcomp.dll";
            PublicLibraryPaths.Add(LibFolder);
            PublicLibraryPaths.Add(DLLFolder);
            PublicAdditionalLibraries.Add("ispc_texcomp.lib");
			PublicDelayLoadDLLs.Add("ispc_texcomp.dll");
            RuntimeDependencies.Add(DLLFilePath);
		}
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            string BinaryLibraryFolder = BinaryFolder + "Mac64-Release";
            string LibraryFilePath = BinaryLibraryFolder + "/libispc_texcomp.dylib";
            PublicAdditionalLibraries.Add(LibraryFilePath);
            PublicDelayLoadDLLs.Add(LibraryFilePath);
            PublicAdditionalShadowFiles.Add(LibraryFilePath);
            RuntimeDependencies.Add(LibraryFilePath);
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) && Target.Architecture.StartsWith("x86_64"))
        {
            string BinaryLibraryFolder = BinaryFolder + "Linux64-Release";
            string LibraryFilePath = BinaryLibraryFolder + "/libispc_texcomp.so";
            PublicAdditionalLibraries.Add(LibraryFilePath);
            PublicDelayLoadDLLs.Add("libispc_texcomp.so");
            RuntimeDependencies.Add(LibraryFilePath);
        }
    }
}
