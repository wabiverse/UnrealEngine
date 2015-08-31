// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Runtime.Serialization;

namespace UnrealBuildTool
{
	[Serializable]
	public class UEBuildGame : UEBuildTarget
	{
		public UEBuildGame(SerializationInfo Info, StreamingContext Context)
			: base(Info, Context)
		{
		}

		public UEBuildGame(TargetDescriptor InDesc, TargetRules InRulesObject, RulesAssembly InRulesAssembly, string InTargetCsFilename)
			: base(InDesc, InRulesObject, InRulesAssembly, "UE4", InTargetCsFilename)
		{
			if (ShouldCompileMonolithic())
			{
				if (!UnrealBuildTool.IsDesktopPlatform(Platform) || Platform == UnrealTargetPlatform.WinRT || Platform == UnrealTargetPlatform.WinRT_ARM)
				{
					// We are compiling for a console...
					// We want the output to go into the <GAME>\Binaries folder
					if (!InRulesObject.bOutputToEngineBinaries)
					{
						OutputPaths = OutputPaths.Select(Path => Path.Replace("Engine\\Binaries", InDesc.TargetName + "\\Binaries")).ToList();
					}
				}
			}
		}

		//
		// UEBuildTarget interface.
		//


		protected override void SetupModules()
		{
			base.SetupModules();
		}


		/// <summary>
		/// Setup the binaries for this target
		/// </summary>
		protected override void SetupBinaries()
		{
			base.SetupBinaries();

			{
				// Make the game executable.
				UEBuildBinaryConfiguration Config = new UEBuildBinaryConfiguration( InType: UEBuildBinaryType.Executable,
																					InOutputFilePaths: OutputPaths,
																					InIntermediateDirectory: EngineIntermediateDirectory,
																					bInCreateImportLibrarySeparately: (ShouldCompileMonolithic() ? false : true),
																					bInAllowExports:!ShouldCompileMonolithic(),
																					InModuleNames: new List<string>() { "Launch" } );

				AppBinaries.Add( new UEBuildBinaryCPP( this, Config ) );
			}

			// Add the other modules that we want to compile along with the executable.  These aren't necessarily
			// dependencies to any of the other modules we're building, so we need to opt in to compile them.
			{
				// Modules should properly identify the 'extra modules' they need now.
				// There should be nothing here!
			}
		}

		public override void SetupDefaultGlobalEnvironment(
			TargetInfo Target,
			ref LinkEnvironmentConfiguration OutLinkEnvironmentConfiguration,
			ref CPPEnvironmentConfiguration OutCPPEnvironmentConfiguration
			)
		{
			UEBuildConfiguration.bCompileLeanAndMeanUE = true;

			// Do not include the editor
			UEBuildConfiguration.bBuildEditor = false;
			UEBuildConfiguration.bBuildWithEditorOnlyData = false;

			// Require cooked data
			UEBuildConfiguration.bBuildRequiresCookedData = true;

			// Compile the engine
			UEBuildConfiguration.bCompileAgainstEngine = true;

			// Tag it as a 'Game' build
			OutCPPEnvironmentConfiguration.Definitions.Add("UE_GAME=1");

			// no exports, so no need to verify that a .lib and .exp file was emitted by the linker.
			OutLinkEnvironmentConfiguration.bHasExports = false;
		}
	}
}
