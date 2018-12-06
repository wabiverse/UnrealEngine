// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Base class for platform-specific project generators
	/// </summary>
	class LuminProjectGenerator : UEPlatformProjectGenerator
	{
		static bool VSSupportChecked = false;       // Don't want to check multiple times
		static bool VSDebuggingEnabled = false;

		private bool IsVSLuminSupportInstalled(VCProjectFileFormat ProjectFileFormat)
		{
			if (!VSSupportChecked)
			{
				// TODO: add a registry check or file exists check to confirm if MLExtension is installed on the given ProjectFileFormat.
				VSDebuggingEnabled = (ProjectFileFormat == VCProjectFileFormat.VisualStudio2015 || ProjectFileFormat == VCProjectFileFormat.VisualStudio2017);
				VSSupportChecked = true;
			}
            return VSDebuggingEnabled;
		}
		
		/// <summary>
		/// Register the platform with the UEPlatformProjectGenerator class
		/// </summary>
		public override void RegisterPlatformProjectGenerator()
		{
			Log.TraceVerbose("        Registering for {0}", UnrealTargetPlatform.Lumin.ToString());
			UEPlatformProjectGenerator.RegisterPlatformProjectGenerator(UnrealTargetPlatform.Lumin, this);
		}

		/// <summary>
		/// Whether this build platform has native support for VisualStudio
		/// </summary>
		/// <param name="InPlatform">  The UnrealTargetPlatform being built</param>
		/// <param name="InConfiguration"> The UnrealTargetConfiguration being built</param>
		/// <param name="ProjectFileFormat"> Which version of VS</param>
		/// <returns>bool    true if native VisualStudio support (or custom VSI) is available</returns>
		public override bool HasVisualStudioSupport(UnrealTargetPlatform InPlatform, UnrealTargetConfiguration InConfiguration, VCProjectFileFormat ProjectFileFormat)
		{
			// TODO: Returning true from here is resulting in VS erroring out when trying to build a game project for Lumin.
			return false; //IsVSLuminSupportInstalled(ProjectFileFormat);
		}

		/// <summary>
		/// Return the platform toolset string to write into the project configuration
		/// </summary>
		/// <param name="InPlatform">  The UnrealTargetPlatform being built</param>
		/// <param name="InConfiguration"> The UnrealTargetConfiguration being built</param>
		/// <param name="InProjectFileFormat"> The verison of VS</param> 
		/// <param name="ProjectFileBuilder">String builder for the project file</param>
		/// <returns>string    The custom configuration section for the project file; Empty string if it doesn't require one</returns>
		public override void GetVisualStudioPlatformToolsetString(UnrealTargetPlatform InPlatform, UnrealTargetConfiguration InConfiguration, VCProjectFileFormat InProjectFileFormat, StringBuilder ProjectFileBuilder)
		{
			ProjectFileBuilder.AppendLine("    <PlatformToolset>" + VCProjectFileGenerator.GetProjectFilePlatformToolsetVersionString(InProjectFileFormat) + "</PlatformToolset>");
		}

		/// <summary>
		/// Return any custom paths for VisualStudio this platform requires
		/// This include ReferencePath, LibraryPath, LibraryWPath, IncludePath and ExecutablePath.
		/// </summary>
		/// <param name="InPlatform">The UnrealTargetPlatform being built</param>
		/// <param name="InConfiguration">The configuration being built</param>
		/// <param name="TargetType">The type of target (game or program)</param>
		/// <param name="TargetRulesPath">Path to the target.cs file</param>
		/// <param name="ProjectFilePath">Path to the project file</param>
		/// <param name="NMakeOutputPath"></param>
		/// <param name="InProjectFileFormat">Format for the generated project files</param>
		/// <param name="ProjectFileBuilder">The project file content</param>
		/// <returns>The custom path lines for the project file; Empty string if it doesn't require one</returns>
		public override void GetVisualStudioPathsEntries(UnrealTargetPlatform InPlatform, UnrealTargetConfiguration InConfiguration, TargetType TargetType, FileReference TargetRulesPath, FileReference ProjectFilePath, FileReference NMakeOutputPath, VCProjectFileFormat InProjectFileFormat, StringBuilder ProjectFileBuilder)
		{
			base.GetVisualStudioPathsEntries(InPlatform, InConfiguration, TargetType, TargetRulesPath, ProjectFilePath, NMakeOutputPath, InProjectFileFormat, ProjectFileBuilder);

			if (IsVSLuminSupportInstalled(InProjectFileFormat) && TargetType == TargetType.Game && InPlatform == UnrealTargetPlatform.Lumin)
			{
				string MLSDK = Utils.CleanDirectorySeparators(Environment.GetEnvironmentVariable("MLSDK"), '\\');

				// TODO: Check if MPK name can be other than the project name.
				string GameName = TargetRulesPath.GetFileNameWithoutExtension();
				GameName = Path.GetFileNameWithoutExtension(GameName);

				string PackageFile = Utils.MakePathRelativeTo(NMakeOutputPath.Directory.FullName, ProjectFilePath.Directory.FullName);
				string PackageName = GameName;
				if (InConfiguration != UnrealTargetConfiguration.Development)
				{
					PackageName = GameName + "-" + InPlatform.ToString() + "-" + InConfiguration.ToString();
				}
				PackageFile = Path.Combine(PackageFile, PackageName + ".mpk");

                // TODO: Fix NMakeOutput to have the the correct architecture name and then set ELFFile to $(NMakeOutput)
                string ELFFile = Utils.MakePathRelativeTo(NMakeOutputPath.Directory.FullName, ProjectFilePath.Directory.FullName);
                ELFFile = Path.Combine(ELFFile, "..\\..\\Intermediate\\Lumin\\Mabu\\Packaged\\bin\\" + GameName);
				string DebuggerFlavor = "MLDebugger";

				string SymFile = Utils.MakePathRelativeTo(NMakeOutputPath.Directory.FullName, ProjectFilePath.Directory.FullName);
				SymFile = Path.Combine(SymFile, "..\\..\\Intermediate\\Lumin\\Mabu\\Binaries", Path.ChangeExtension(NMakeOutputPath.GetFileName(), ".sym"));

				// following are defaults for debugger options
				string Attach = "false";
				string EnableAutoStop = "true";
				string AutoStopAtFunction = "main";
				string EnablePrettyPrinting = "true";
				string MLDownloadOnStart = "true";

				string CustomPathEntriesTemplate = "<MLSDK>{0}</MLSDK>" + ProjectFileGenerator.NewLine +
													"<PackageFile>{1}</PackageFile>" + ProjectFileGenerator.NewLine +
													"<ELFFile>{2}</ELFFile>" + ProjectFileGenerator.NewLine +
													"<DebuggerFlavor>{3}</DebuggerFlavor>" + ProjectFileGenerator.NewLine +
													"<Attach>{4}</Attach>" + ProjectFileGenerator.NewLine +
													"<EnableAutoStop>{5}</EnableAutoStop>" + ProjectFileGenerator.NewLine +
													"<AutoStopAtFunction>{6}</AutoStopAtFunction>" + ProjectFileGenerator.NewLine +
													"<EnablePrettyPrinting>{7}</EnablePrettyPrinting>" + ProjectFileGenerator.NewLine +
													"<MLDownloadOnStart>{8}</MLDownloadOnStart>" + ProjectFileGenerator.NewLine +
													"<SymFile>{9}</SymFile>" + ProjectFileGenerator.NewLine;
				ProjectFileBuilder.Append(ProjectFileGenerator.NewLine + string.Format(CustomPathEntriesTemplate, MLSDK, PackageFile, ELFFile, DebuggerFlavor, Attach, EnableAutoStop, AutoStopAtFunction, EnablePrettyPrinting, MLDownloadOnStart, SymFile));
			}
		}

		public override string GetBuildCommandEntry(ProjectFile ProjectFile, DirectoryReference BatchFileDirectory, ScriptArguments FileArguments, FileReference OnlyGameProject)
		{
			if (OnlyGameProject != null && File.Exists(OnlyGameProject.FullName))
			{
				return ProjectFile.EscapePath(ProjectFile.NormalizeProjectPath(FileReference.Combine(BatchFileDirectory, "RunUAT.bat"))) + GetBatchFileArguments(FileArguments);
			}
			return ProjectFile.EscapePath(ProjectFile.NormalizeProjectPath(FileReference.Combine(BatchFileDirectory, "Build.bat"))) + base.GetBatchFileArguments(FileArguments);
		}

		public override string GetReBuildCommandEntry(ProjectFile ProjectFile, DirectoryReference BatchFileDirectory, ScriptArguments FileArguments, FileReference OnlyGameProject)
		{
			if (OnlyGameProject != null && File.Exists(OnlyGameProject.FullName))
			{
				return GetBuildCommandEntry(ProjectFile, BatchFileDirectory, FileArguments, OnlyGameProject) + " -clean";
			}
			return ProjectFile.EscapePath(ProjectFile.NormalizeProjectPath(FileReference.Combine(BatchFileDirectory, "Rebuild.bat"))) + base.GetBatchFileArguments(FileArguments);
		}

		public override string GetCleanCommandEntry(ProjectFile ProjectFile, DirectoryReference BatchFileDirectory, ScriptArguments FileArguments, FileReference OnlyGameProject)
		{
			return ProjectFile.EscapePath(ProjectFile.NormalizeProjectPath(FileReference.Combine(BatchFileDirectory, "Clean.bat"))) + base.GetBatchFileArguments(FileArguments);
		}

		public override string GetBatchFileArguments(ScriptArguments FileArguments)
		{
			return String.Format(" BuildCookRun -nocompileeditor -nop4 -project=\"$(SolutionDir)$(ProjectName).uproject\" -cook -fastcook -stage -archive -archivedirectory=\"$(SolutionDir)..\\..\\Binaries\\Lumin\" -package -clientconfig={0} -ue4exe=UE4Editor-CMD.exe -SkipCookingEditorContent -pak -prereqs -targetplatform=Lumin -build -utf8output -compile", FileArguments.ConfigurationName);
		}

		public override FileReference GetNMakeOutputPath(FileReference InNMakePath)
		{
			return new FileReference(Path.Combine(InNMakePath.Directory.FullName, InNMakePath.GetFileNameWithoutExtension()+"-arm64-lumin"+InNMakePath.GetExtension()));
		}
	}
}
