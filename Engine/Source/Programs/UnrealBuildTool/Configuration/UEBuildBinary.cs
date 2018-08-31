// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Xml;
using System.Globalization;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// All binary types generated by UBT
	/// </summary>
	enum UEBuildBinaryType
	{
		/// <summary>
		/// An executable
		/// </summary>
		Executable,

		/// <summary>
		/// A dynamic library (.dll, .dylib, or .so)
		/// </summary>
		DynamicLinkLibrary,

		/// <summary>
		/// A static library (.lib or .a)
		/// </summary>
		StaticLibrary,
	}

	/// <summary>
	/// A binary built by UBT.
	/// </summary>
	class UEBuildBinary
	{
		/// <summary>
		/// The type of binary to build
		/// </summary>
		public UEBuildBinaryType Type;

		/// <summary>
		/// Output directory for this binary
		/// </summary>
		public DirectoryReference OutputDir;

		/// <summary>
		/// The output file path. This must be set before a binary can be built using it.
		/// </summary>
		public List<FileReference> OutputFilePaths;

		/// <summary>
		/// Returns the OutputFilePath if there is only one entry in OutputFilePaths
		/// </summary>
		public FileReference OutputFilePath
		{
			get
			{
				if (OutputFilePaths.Count != 1)
				{
					throw new BuildException("Attempted to use UEBuildBinaryConfiguration.OutputFilePath property, but there are multiple (or no) OutputFilePaths. You need to handle multiple in the code that called this (size = {0})", OutputFilePaths.Count);
				}
				return OutputFilePaths[0];
			}
		}

		/// <summary>
		/// Original output filepath. This is the original binary name before hot-reload suffix has been appended to it.
		/// </summary>
		public List<FileReference> OriginalOutputFilePaths;

		/// <summary>
		/// Returns the OriginalOutputFilePath if there is only one entry in OriginalOutputFilePaths
		/// </summary>
		public FileReference OriginalOutputFilePath
		{
			get
			{
				if (OriginalOutputFilePaths.Count != 1)
				{
					throw new BuildException("Attempted to use UEBuildBinaryConfiguration.OriginalOutputFilePath property, but there are multiple (or no) OriginalOutputFilePaths. You need to handle multiple in the code that called this (size = {0})", OriginalOutputFilePaths.Count);
				}
				return OriginalOutputFilePaths[0];
			}
		}

		/// <summary>
		/// The intermediate directory for this binary. Modules should create separate intermediate directories below this. Must be set before a binary can be built using it.
		/// </summary>
		public DirectoryReference IntermediateDirectory;

		/// <summary>
		/// If true, build exports lib
		/// </summary>
		public bool bAllowExports = false;

		/// <summary>
		/// If true, create a separate import library
		/// </summary>
		public bool bCreateImportLibrarySeparately = false;

		/// <summary>
		/// If true, creates an additional console application. Hack for Windows, where it's not possible to conditionally inherit a parent's console Window depending on how
		/// the application is invoked; you have to link the same executable with a different subsystem setting.
		/// </summary>
		public bool bBuildAdditionalConsoleApp = false;

		/// <summary>
		/// 
		/// </summary>
		public bool bUsePrecompiled;

		/// <summary>
		/// The primary module that this binary was constructed for. For executables, this is typically the launch module.
		/// </summary>
		public readonly UEBuildModuleCPP PrimaryModule;

		/// <summary>
		/// List of modules to link together into this executable
		/// </summary>
		public readonly List<UEBuildModule> Modules = new List<UEBuildModule>();

		/// <summary>
		/// Cached list of dependent link libraries.
		/// </summary>
		private List<string> DependentLinkLibraries;

		/// <summary>
		/// Create an instance of the class with the given configuration data
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="OutputFilePaths"></param>
		/// <param name="IntermediateDirectory"></param>
		/// <param name="bAllowExports"></param>
		/// <param name="PrimaryModule"></param>
		/// <param name="bUsePrecompiled"></param>
		public UEBuildBinary(
				UEBuildBinaryType Type,
				IEnumerable<FileReference> OutputFilePaths,
				DirectoryReference IntermediateDirectory,
				bool bAllowExports,
				UEBuildModuleCPP PrimaryModule,
				bool bUsePrecompiled
			)
		{
			this.Type = Type;
			this.OutputDir = OutputFilePaths.First().Directory;
			this.OutputFilePaths = new List<FileReference>(OutputFilePaths);
			this.IntermediateDirectory = IntermediateDirectory;
			this.bAllowExports = bAllowExports;
			this.PrimaryModule = PrimaryModule;
			this.bUsePrecompiled = bUsePrecompiled;
			
			Modules.Add(PrimaryModule);
		}

		/// <summary>
		/// Creates all the modules referenced by this target.
		/// </summary>
		public void CreateAllDependentModules(UEBuildModule.CreateModuleDelegate CreateModule)
		{
			foreach (UEBuildModule Module in Modules)
			{
				Module.RecursivelyCreateModules(CreateModule, "Target");
			}
		}

		/// <summary>
		/// Builds the binary.
		/// </summary>
		/// <param name="Target">Rules for the target being built</param>
		/// <param name="ToolChain">The toolchain which to use for building</param>
		/// <param name="CompileEnvironment">The environment to compile the binary in</param>
		/// <param name="LinkEnvironment">The environment to link the binary in</param>
		/// <param name="SharedPCHs">List of templates for shared PCHs</param>
		/// <param name="WorkingSet">The working set of source files</param>
		/// <param name="ExeDir">Directory containing the output executable</param>
		/// <param name="ActionGraph">Graph to add build actions to</param>
		/// <returns>Set of built products</returns>
		public IEnumerable<FileItem> Build(ReadOnlyTargetRules Target, UEToolChain ToolChain, CppCompileEnvironment CompileEnvironment, LinkEnvironment LinkEnvironment, List<PrecompiledHeaderTemplate> SharedPCHs, ISourceFileWorkingSet WorkingSet, DirectoryReference ExeDir, ActionGraph ActionGraph)
		{
			// Return nothing if we're using precompiled binaries
			if(bUsePrecompiled)
			{
				return new List<FileItem>();
			}

			// Setup linking environment.
			LinkEnvironment BinaryLinkEnvironment = SetupBinaryLinkEnvironment(Target, ToolChain, LinkEnvironment, CompileEnvironment, SharedPCHs, WorkingSet, ExeDir, ActionGraph);

			// If we're generating projects, we only need include paths and definitions, there is no need to run the linking logic.
			if (ProjectFileGenerator.bGenerateProjectFiles)
			{
				return BinaryLinkEnvironment.InputFiles;
			}

			// If linking is disabled, our build products are just the compiled object files
			if (Target.bDisableLinking)
			{
				return BinaryLinkEnvironment.InputFiles;
			}

			// Generate import libraries as a separate step
			List<FileItem> OutputFiles = new List<FileItem>();
			if (bCreateImportLibrarySeparately)
			{
				// Mark the link environment as cross-referenced.
				BinaryLinkEnvironment.bIsCrossReferenced = true;

				if (BinaryLinkEnvironment.Platform != CppPlatform.Mac && BinaryLinkEnvironment.Platform != CppPlatform.Linux)
				{
					// Create the import library.
					OutputFiles.AddRange(ToolChain.LinkAllFiles(BinaryLinkEnvironment, true, ActionGraph));
				}
			}

			// Link the binary.
			FileItem[] Executables = ToolChain.LinkAllFiles(BinaryLinkEnvironment, false, ActionGraph);
			OutputFiles.AddRange(Executables);

			// Produce additional console app if requested
			if (bBuildAdditionalConsoleApp)
			{
				// Produce additional binary but link it as a console app
				LinkEnvironment ConsoleAppLinkEvironment = new LinkEnvironment(BinaryLinkEnvironment);
				ConsoleAppLinkEvironment.bIsBuildingConsoleApplication = true;
				ConsoleAppLinkEvironment.WindowsEntryPointOverride = "WinMainCRTStartup";		// For WinMain() instead of "main()" for Launch module
				ConsoleAppLinkEvironment.OutputFilePaths = ConsoleAppLinkEvironment.OutputFilePaths.Select(Path => GetAdditionalConsoleAppPath(Path)).ToList();

				// Link the console app executable
				OutputFiles.AddRange(ToolChain.LinkAllFiles(ConsoleAppLinkEvironment, false, ActionGraph));
			}

			foreach (FileItem Executable in Executables)
			{
				OutputFiles.AddRange(ToolChain.PostBuild(Executable, BinaryLinkEnvironment, ActionGraph));
			}

			return OutputFiles;
		}

		/// <summary>
		/// Gets all the runtime dependencies Copies all the runtime dependencies from any modules in 
		/// </summary>
		/// <param name="RuntimeDependencies">The output list of runtime dependencies, mapping target file to type</param>
		/// <param name="TargetFileToSourceFile">Map of target files to source files that need to be copied</param>
		/// <param name="ExeDir">Output directory for the executable</param>
		public void PrepareRuntimeDependencies(List<RuntimeDependency> RuntimeDependencies, Dictionary<FileReference, FileReference> TargetFileToSourceFile, DirectoryReference ExeDir)
		{
			foreach(UEBuildModule Module in Modules)
			{
				foreach (ModuleRules.RuntimeDependency Dependency in Module.Rules.RuntimeDependencies.Inner)
				{
					if(Dependency.SourcePath == null)
					{
						// Expand the target path
						string ExpandedPath = Module.ExpandPathVariables(Dependency.Path, OutputDir, ExeDir);
						if (FileFilter.FindWildcardIndex(ExpandedPath) == -1)
						{
							RuntimeDependencies.Add(new RuntimeDependency(new FileReference(ExpandedPath), Dependency.Type));
						}
						else
						{
							RuntimeDependencies.AddRange(FileFilter.ResolveWildcard(ExpandedPath).Select(x => new RuntimeDependency(x, Dependency.Type)));
						}
					}
					else
					{
						// Parse the source and target patterns
						FilePattern SourcePattern = new FilePattern(UnrealBuildTool.EngineSourceDirectory, Module.ExpandPathVariables(Dependency.SourcePath, OutputDir, ExeDir));
						FilePattern TargetPattern = new FilePattern(UnrealBuildTool.EngineSourceDirectory, Module.ExpandPathVariables(Dependency.Path, OutputDir, ExeDir));

						// Resolve all the wildcards between the source and target paths
						Dictionary<FileReference, FileReference> Mapping;
						try
						{
							Mapping = FilePattern.CreateMapping(null, ref SourcePattern, ref TargetPattern);
						}
						catch(FilePatternException Ex)
						{
							throw new BuildException(Ex, "While creating runtime dependencies for module {0}", Module.Name);
						}

						// Add actions to copy everything
						foreach(KeyValuePair<FileReference, FileReference> Pair in Mapping)
						{
							FileReference ExistingSourceFile;
							if(!TargetFileToSourceFile.TryGetValue(Pair.Key, out ExistingSourceFile))
							{
								TargetFileToSourceFile[Pair.Key] = Pair.Value;
								RuntimeDependencies.Add(new RuntimeDependency(Pair.Key, Dependency.Type));
							}
							else if(ExistingSourceFile != Pair.Value)
							{
								throw new BuildException("Runtime dependency '{0}' is configured to be staged from '{1}' and '{2}'", Pair.Key, Pair.Value, ExistingSourceFile);
							}
						}
					}
				}
			}
		}

		/// <summary>
		/// Called to allow the binary to modify the link environment of a different binary containing 
		/// a module that depends on a module in this binary.
		/// </summary>
		/// <param name="DependentLinkEnvironment">The link environment of the dependency</param>
		public void SetupDependentLinkEnvironment(LinkEnvironment DependentLinkEnvironment)
		{
			// Cache the list of libraries in the dependent link environment between calls. We typically run this code path many times for each module.
			if (DependentLinkLibraries == null)
			{
				DependentLinkLibraries = new List<string>();
				foreach (FileReference OutputFilePath in OutputFilePaths)
				{
					FileReference LibraryFileName;
					if (Type == UEBuildBinaryType.StaticLibrary || DependentLinkEnvironment.Platform == CppPlatform.Mac || DependentLinkEnvironment.Platform == CppPlatform.Linux)
					{
						LibraryFileName = OutputFilePath;
					}
					else
					{
						LibraryFileName = FileReference.Combine(IntermediateDirectory, OutputFilePath.GetFileNameWithoutExtension() + ".lib");
					}
					DependentLinkLibraries.Add(LibraryFileName.FullName);
				}
			}
			DependentLinkEnvironment.AdditionalLibraries.AddRange(DependentLinkLibraries);
		}

		/// <summary>
		/// Called to allow the binary to to determine if it matches the Only module "short module name".
		/// </summary>
		/// <param name="OnlyModules"></param>
		/// <returns>The OnlyModule if found, null if not</returns>
		public OnlyModule FindOnlyModule(List<OnlyModule> OnlyModules)
		{
			foreach (UEBuildModule Module in Modules)
			{
				foreach (OnlyModule OnlyModule in OnlyModules)
				{
					if (OnlyModule.OnlyModuleName.ToLower() == Module.Name.ToLower())
					{
						return OnlyModule;
					}
				}
			}
			return null;
		}

		/// <summary>
		/// Called to allow the binary to find game modules.
		/// </summary>
		/// <returns>The OnlyModule if found, null if not</returns>
		public List<UEBuildModule> FindGameModules()
		{
			List<UEBuildModule> GameModules = new List<UEBuildModule>();
			foreach (UEBuildModule Module in Modules)
			{
				if (!UnrealBuildTool.IsUnderAnEngineDirectory(Module.ModuleDirectory))
				{
					GameModules.Add(Module);
				}
			}
			return GameModules;
		}

		/// <summary>
		/// Generates a list of all modules referenced by this binary
		/// </summary>
		/// <param name="bIncludeDynamicallyLoaded">True if dynamically loaded modules (and all of their dependent modules) should be included.</param>
		/// <param name="bForceCircular">True if circular dependencies should be process</param>
		/// <returns>List of all referenced modules</returns>
		public List<UEBuildModule> GetAllDependencyModules(bool bIncludeDynamicallyLoaded, bool bForceCircular)
		{
			List<UEBuildModule> ReferencedModules = new List<UEBuildModule>();
			HashSet<UEBuildModule> IgnoreReferencedModules = new HashSet<UEBuildModule>();

			foreach (UEBuildModule Module in Modules)
			{
				if (!IgnoreReferencedModules.Contains(Module))
				{
					IgnoreReferencedModules.Add(Module);

					Module.GetAllDependencyModules(ReferencedModules, IgnoreReferencedModules, bIncludeDynamicallyLoaded, bForceCircular, bOnlyDirectDependencies: false);

					ReferencedModules.Add(Module);
				}
			}

			return ReferencedModules;
		}

		/// <summary>
		/// Generates a list of all modules referenced by this binary
		/// </summary>
		/// <param name="ReferencedBy">Map of module to the module that referenced it</param>
		/// <returns>List of all referenced modules</returns>
		public void FindModuleReferences(Dictionary<UEBuildModule, UEBuildModule> ReferencedBy)
		{
			List<UEBuildModule> ReferencedModules = new List<UEBuildModule>();
			foreach(UEBuildModule Module in Modules)
			{
				ReferencedModules.Add(Module);
				ReferencedBy.Add(Module, null);
			}

			List<UEBuildModule> DirectlyReferencedModules = new List<UEBuildModule>();
			HashSet<UEBuildModule> VisitedModules = new HashSet<UEBuildModule>();
			for(int Idx = 0; Idx < ReferencedModules.Count; Idx++)
			{
				UEBuildModule SourceModule = ReferencedModules[Idx];

				// Find all the direct references from this module
				DirectlyReferencedModules.Clear();
				SourceModule.GetAllDependencyModules(DirectlyReferencedModules, VisitedModules, false, false, true);

				// Set up the references for all the new modules
				foreach(UEBuildModule DirectlyReferencedModule in DirectlyReferencedModules)
				{
					if(!ReferencedBy.ContainsKey(DirectlyReferencedModule))
					{
						ReferencedBy.Add(DirectlyReferencedModule, SourceModule);
						ReferencedModules.Add(DirectlyReferencedModule);
					}
				}
			}
		}

		/// <summary>
		/// Sets whether to create a separate import library to resolve circular dependencies for this binary
		/// </summary>
		/// <param name="bInCreateImportLibrarySeparately">True to create a separate import library</param>
		public void SetCreateImportLibrarySeparately(bool bInCreateImportLibrarySeparately)
		{
			bCreateImportLibrarySeparately = bInCreateImportLibrarySeparately;
		}

		/// <summary>
		/// Adds a module to the binary.
		/// </summary>
		/// <param name="Module">The module to add</param>
		public void AddModule(UEBuildModule Module)
		{
			if (!Modules.Contains(Module))
			{
				Modules.Add(Module);
			}
		}

		/// <summary>
		/// Gets all build products produced by this binary
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <param name="ToolChain">The platform toolchain</param>
		/// <param name="BuildProducts">Mapping of produced build product to type</param>
		/// <param name="bCreateDebugInfo">Whether debug info is enabled for this binary</param>
		public void GetBuildProducts(ReadOnlyTargetRules Target, UEToolChain ToolChain, Dictionary<FileReference, BuildProductType> BuildProducts, bool bCreateDebugInfo)
		{
			// Add all the precompiled outputs
			if(Target.bPrecompile)
			{
				foreach(UEBuildModuleCPP Module in Modules.OfType<UEBuildModuleCPP>())
				{
					if(Module.Rules.bPrecompile)
					{
						if(Module.GeneratedCodeDirectory != null && DirectoryReference.Exists(Module.GeneratedCodeDirectory))
						{
							foreach(FileReference GeneratedCodeFile in DirectoryReference.EnumerateFiles(Module.GeneratedCodeDirectory))
							{
								// Exclude timestamp files, since they're always updated and cause collisions between builds
								if(!GeneratedCodeFile.GetFileName().Equals("Timestamp", StringComparison.OrdinalIgnoreCase))
								{
									BuildProducts.Add(GeneratedCodeFile, BuildProductType.BuildResource);
								}
							}
						}
						if(Target.LinkType == TargetLinkType.Monolithic)
						{
							FileReference PrecompiledManifestLocation = Module.PrecompiledManifestLocation;
							BuildProducts.Add(PrecompiledManifestLocation, BuildProductType.BuildResource);

							PrecompiledManifest ModuleManifest = PrecompiledManifest.Read(PrecompiledManifestLocation);
							foreach(FileReference OutputFile in ModuleManifest.OutputFiles)
							{
								if(!BuildProducts.ContainsKey(OutputFile))
								{
									BuildProducts.Add(OutputFile, BuildProductType.BuildResource);
								}
							}
						}
					}
				}
			}

			// Add all the binary outputs
			if(!Target.bDisableLinking)
			{
				// Get the type of build products we're creating
				BuildProductType OutputType = BuildProductType.RequiredResource;
				switch (Type)
				{
					case UEBuildBinaryType.Executable:
						OutputType = BuildProductType.Executable;
						break;
					case UEBuildBinaryType.DynamicLinkLibrary:
						OutputType = BuildProductType.DynamicLibrary;
						break;
					case UEBuildBinaryType.StaticLibrary:
						OutputType = BuildProductType.BuildResource;
						break;
				}

				// Add the primary build products
				string[] DebugExtensions = UEBuildPlatform.GetBuildPlatform(Target.Platform).GetDebugInfoExtensions(Target, Type);
				foreach (FileReference OutputFilePath in OutputFilePaths)
				{
					AddBuildProductAndDebugFiles(OutputFilePath, OutputType, DebugExtensions, BuildProducts, ToolChain, bCreateDebugInfo);
				}

				// Add the console app, if there is one
				if (Type == UEBuildBinaryType.Executable && bBuildAdditionalConsoleApp)
				{
					foreach (FileReference OutputFilePath in OutputFilePaths)
					{
						AddBuildProductAndDebugFiles(GetAdditionalConsoleAppPath(OutputFilePath), OutputType, DebugExtensions, BuildProducts, ToolChain, bCreateDebugInfo);
					}
				}

				// Add any additional build products from the modules in this binary, including additional bundle resources/dylibs on Mac.
				List<string> Libraries = new List<string>();
				List<UEBuildBundleResource> BundleResources = new List<UEBuildBundleResource>();
				GatherAdditionalResources(Libraries, BundleResources);

				// Add any extra files from the toolchain
				ToolChain.ModifyBuildProducts(Target, this, Libraries, BundleResources, BuildProducts);
			}
		}

		/// <summary>
		/// Adds a build product and its associated debug file to a receipt.
		/// </summary>
		/// <param name="OutputFile">Build product to add</param>
		/// <param name="OutputType">The type of built product</param>
		/// <param name="DebugExtensions">Extensions for the matching debug file (may be null).</param>
		/// <param name="BuildProducts">Map of build products to their type</param>
		/// <param name="ToolChain">The toolchain used to build these binaries</param>
		/// <param name="bCreateDebugInfo">Whether creating debug info is enabled</param>
		static void AddBuildProductAndDebugFiles(FileReference OutputFile, BuildProductType OutputType, string[] DebugExtensions, Dictionary<FileReference, BuildProductType> BuildProducts, UEToolChain ToolChain, bool bCreateDebugInfo)
		{
			BuildProducts.Add(OutputFile, OutputType);

			foreach (string DebugExtension in DebugExtensions)
			{
				if (!String.IsNullOrEmpty(DebugExtension) && ToolChain.ShouldAddDebugFileToReceipt(OutputFile, OutputType) && bCreateDebugInfo)
				{
					BuildProducts.Add(OutputFile.ChangeExtension(DebugExtension), BuildProductType.SymbolFile);
				}
			}
		}

		/// <summary>
		/// Enumerates resources which the toolchain may need may produced additional build products from. Some platforms (eg. Mac, Linux) can link directly 
		/// against .so/.dylibs, but they are also copied to the output folder by the toolchain.
		/// </summary>
		/// <param name="Libraries">List to which libraries required by this module are added</param>
		/// <param name="BundleResources">List of bundle resources required by this module</param>
		public void GatherAdditionalResources(List<string> Libraries, List<UEBuildBundleResource> BundleResources)
		{
			foreach(UEBuildModule Module in Modules)
			{
				Module.GatherAdditionalResources(Libraries, BundleResources);
			}
		}

		/// <summary>
		/// Helper function to get the console app BinaryName-Cmd.exe filename based on the binary filename.
		/// </summary>
		/// <param name="BinaryPath">Full path to the binary exe.</param>
		/// <returns></returns>
		public static FileReference GetAdditionalConsoleAppPath(FileReference BinaryPath)
		{
			return FileReference.Combine(BinaryPath.Directory, BinaryPath.GetFileNameWithoutExtension() + "-Cmd" + BinaryPath.GetExtension());
		}

		/// <summary>
		/// Checks whether the binary output paths are appropriate for the distribution
		/// level of its direct module dependencies
		/// </summary>
		public bool CheckRestrictedFolders(DirectoryReference ProjectDir, Dictionary<UEBuildModule, Dictionary<RestrictedFolder, DirectoryReference>> ModuleRestrictedFolderCache)
		{
			// Find all the modules we depend on
			Dictionary<UEBuildModule, UEBuildModule> ModuleReferencedBy = new Dictionary<UEBuildModule, UEBuildModule>();
			FindModuleReferences(ModuleReferencedBy);

			// Loop through each of the output binaries and check them separately
			bool bResult = true;
			foreach (FileReference OutputFilePath in OutputFilePaths)
			{
				// Find the base directory for this binary
				DirectoryReference BaseDir;
				if(OutputFilePath.IsUnderDirectory(UnrealBuildTool.EngineDirectory))
				{
					BaseDir = UnrealBuildTool.EngineDirectory;
				}
				else if(ProjectDir != null && OutputFilePath.IsUnderDirectory(ProjectDir))
				{
					BaseDir = ProjectDir;
				}
				else
				{
					continue;
				}

				// Find the restricted folders under the base directory
				List<RestrictedFolder> BinaryFolders = RestrictedFolders.FindRestrictedFolders(BaseDir, OutputFilePath.Directory);

				// Check all the dependent modules
				foreach(UEBuildModule Module in ModuleReferencedBy.Keys)
				{
					// Find the restricted folders for this module
					Dictionary<RestrictedFolder, DirectoryReference> ModuleRestrictedFolders;
					if (!ModuleRestrictedFolderCache.TryGetValue(Module, out ModuleRestrictedFolders))
					{
						ModuleRestrictedFolders = Module.FindRestrictedFolderReferences(ProjectDir);
						ModuleRestrictedFolderCache.Add(Module, ModuleRestrictedFolders);
					}

					// Write errors for any missing paths in the output files
					foreach(KeyValuePair<RestrictedFolder, DirectoryReference> Pair in ModuleRestrictedFolders)
					{
						if(!BinaryFolders.Contains(Pair.Key))
						{
							List<string> ReferenceChain = new List<string>();
							for(UEBuildModule ReferencedModule = Module; ReferencedModule != null; ReferencedModule = ModuleReferencedBy[ReferencedModule])
							{
								ReferenceChain.Insert(0, ReferencedModule.Name);
							}
							Log.TraceError("ERROR: Output binary \"{0}\" is not in a {1} folder, but references \"{2}\" via {3}.", OutputFilePath, Pair.Key.ToString(), Pair.Value, String.Join(" -> ", ReferenceChain));
							bResult = false;
						}
					}
				}
			}
			return bResult;
		}

		/// <summary>
		/// Write information about this binary to a JSON file
		/// </summary>
		/// <param name="Writer">Writer for this binary's data</param>
		public virtual void ExportJson(JsonWriter Writer)
		{
			Writer.WriteValue("File", OutputFilePath.FullName);
			Writer.WriteValue("Type", Type.ToString());

			Writer.WriteArrayStart("Modules");
			foreach(UEBuildModule Module in Modules)
			{
				Writer.WriteValue(Module.Name);
			}
			Writer.WriteArrayEnd();
		}

		// UEBuildBinary interface.

		bool IsBuildingDll(UEBuildBinaryType Type)
		{
			return Type == UEBuildBinaryType.DynamicLinkLibrary;
		}

		bool IsBuildingLibrary(UEBuildBinaryType Type)
		{
			return Type == UEBuildBinaryType.StaticLibrary;
		}

		public void GatherDataForProjectFiles(ReadOnlyTargetRules Target, CppCompileEnvironment GlobalCompileEnvironment)
		{
			CppCompileEnvironment BinaryCompileEnvironment = CreateBinaryCompileEnvironment(GlobalCompileEnvironment);
			foreach(UEBuildModuleCPP Module in Modules.OfType<UEBuildModuleCPP>())
			{
				ProjectFile ProjectFileForIDE;
				if (ProjectFileGenerator.ModuleToProjectFileMap.TryGetValue(Module.Name, out ProjectFileForIDE))
				{
					Module.GatherDataForProjectFile(Target, BinaryCompileEnvironment, ProjectFileForIDE);
				}
			}
		}

		private CppCompileEnvironment CreateBinaryCompileEnvironment(CppCompileEnvironment GlobalCompileEnvironment)
		{
			CppCompileEnvironment BinaryCompileEnvironment = new CppCompileEnvironment(GlobalCompileEnvironment);
			BinaryCompileEnvironment.bIsBuildingDLL = IsBuildingDll(Type);
			BinaryCompileEnvironment.bIsBuildingLibrary = IsBuildingLibrary(Type);

			// @todo: This should be in some Windows code somewhere...
			// Set the original file name macro; used in PCLaunch.rc to set the binary metadata fields.
			string OriginalFilename = (OriginalOutputFilePaths != null) ?
				OriginalOutputFilePaths[0].GetFileName() :
				OutputFilePaths[0].GetFileName();
			BinaryCompileEnvironment.Definitions.Add("ORIGINAL_FILE_NAME=\"" + OriginalFilename + "\"");

			return BinaryCompileEnvironment;
		}

		private LinkEnvironment SetupBinaryLinkEnvironment(ReadOnlyTargetRules Target, UEToolChain ToolChain, LinkEnvironment LinkEnvironment, CppCompileEnvironment CompileEnvironment, List<PrecompiledHeaderTemplate> SharedPCHs, ISourceFileWorkingSet WorkingSet, DirectoryReference ExeDir, ActionGraph ActionGraph)
		{
			LinkEnvironment BinaryLinkEnvironment = new LinkEnvironment(LinkEnvironment);
			HashSet<UEBuildModule> LinkEnvironmentVisitedModules = new HashSet<UEBuildModule>();
			List<UEBuildBinary> BinaryDependencies = new List<UEBuildBinary>();

			CppCompileEnvironment BinaryCompileEnvironment = CreateBinaryCompileEnvironment(CompileEnvironment);
			
			foreach (UEBuildModule Module in Modules)
			{
				List<FileItem> LinkInputFiles;
				if (Module.Binary == null || Module.Binary == this)
				{
					// Compile each module.
					Log.TraceVerbose("Compile module: " + Module.Name);
					LinkInputFiles = Module.Compile(Target, ToolChain, BinaryCompileEnvironment, SharedPCHs, WorkingSet, ActionGraph);

					// NOTE: Because of 'Shared PCHs', in monolithic builds the same PCH file may appear as a link input
					// multiple times for a single binary.  We'll check for that here, and only add it once.  This avoids
					// a linker warning about redundant .obj files. 
					foreach (FileItem LinkInputFile in LinkInputFiles)
					{
						if (!BinaryLinkEnvironment.InputFiles.Contains(LinkInputFile))
						{
							BinaryLinkEnvironment.InputFiles.Add(LinkInputFile);
						}
					}
				}
				else
				{
					BinaryDependencies.Add(Module.Binary);
				}

				// Allow the module to modify the link environment for the binary.
				Module.SetupPrivateLinkEnvironment(this, BinaryLinkEnvironment, BinaryDependencies, LinkEnvironmentVisitedModules, ExeDir);
			}


			// Allow the binary dependencies to modify the link environment.
			foreach (UEBuildBinary BinaryDependency in BinaryDependencies)
			{
				BinaryDependency.SetupDependentLinkEnvironment(BinaryLinkEnvironment);
			}

			// Remove the default resource file on Windows (PCLaunch.rc) if the user has specified their own
			if (BinaryLinkEnvironment.InputFiles.Select(Item => Path.GetFileName(Item.AbsolutePath).ToLower()).Any(Name => Name.EndsWith(".res") && !Name.EndsWith(".inl.res") && Name != "pclaunch.rc.res"))
			{
				BinaryLinkEnvironment.InputFiles.RemoveAll(x => Path.GetFileName(x.AbsolutePath).ToLower() == "pclaunch.rc.res");
			}

			// Set the link output file.
			BinaryLinkEnvironment.OutputFilePaths = OutputFilePaths.ToList();

			// Set whether the link is allowed to have exports.
			BinaryLinkEnvironment.bHasExports = bAllowExports;

			// Set the output folder for intermediate files
			BinaryLinkEnvironment.IntermediateDirectory = IntermediateDirectory;

			// Put the non-executable output files (PDB, import library, etc) in the same directory as the production
			BinaryLinkEnvironment.OutputDirectory = OutputFilePaths[0].Directory;

			// Setup link output type
			BinaryLinkEnvironment.bIsBuildingDLL = IsBuildingDll(Type);
			BinaryLinkEnvironment.bIsBuildingLibrary = IsBuildingLibrary(Type);

			// If we don't have any resource file, use the default or compile a custom one for this module
			if(BinaryLinkEnvironment.Platform == CppPlatform.Win32 || BinaryLinkEnvironment.Platform == CppPlatform.Win64)
			{
				if (!BinaryLinkEnvironment.InputFiles.Any(x => x.Location.HasExtension(".res")))
				{
					if(BinaryLinkEnvironment.DefaultResourceFiles.Count > 0)
					{
						// Use the default resource file if possible
						BinaryLinkEnvironment.InputFiles.AddRange(BinaryLinkEnvironment.DefaultResourceFiles);
					}
					else
					{
						// Create a compile environment for resource files
						CppCompileEnvironment ResourceCompileEnvironment = new CppCompileEnvironment(BinaryCompileEnvironment);

						// @todo: This should be in some Windows code somewhere...
						// Set the original file name macro; used in PCLaunch.rc to set the binary metadata fields.
						string OriginalFilename = (OriginalOutputFilePaths != null) ? OriginalOutputFilePaths[0].GetFileName() : OutputFilePaths[0].GetFileName();
						ResourceCompileEnvironment.Definitions.Add("ORIGINAL_FILE_NAME=\"" + OriginalFilename + "\"");

						// Set the other version fields
						ResourceCompileEnvironment.Definitions.Add(String.Format("BUILT_FROM_CHANGELIST={0}", Target.Version.Changelist));
						ResourceCompileEnvironment.Definitions.Add(String.Format("BUILD_VERSION={0}", Target.BuildVersion));

						// Otherwise compile the default resource file per-binary, so that it gets the correct ORIGINAL_FILE_NAME macro.
						FileItem DefaultResourceFile = FileItem.GetItemByFileReference(FileReference.Combine(UnrealBuildTool.EngineSourceDirectory, "Runtime", "Launch", "Resources", "Windows", "PCLaunch.rc"));
						CPPOutput DefaultResourceOutput = ToolChain.CompileRCFiles(BinaryCompileEnvironment, new List<FileItem> { DefaultResourceFile }, ((UEBuildModuleCPP)Modules.First()).IntermediateDirectory, ActionGraph);
						BinaryLinkEnvironment.InputFiles.AddRange(DefaultResourceOutput.ObjectFiles);
					}
				}
			}

			// Add all the common resource files
			BinaryLinkEnvironment.InputFiles.AddRange(BinaryLinkEnvironment.CommonResourceFiles);

			return BinaryLinkEnvironment;
		}

		/// <summary>
		/// ToString implementation
		/// </summary>
		/// <returns>Returns the OutputFilePath for this binary</returns>
		public override string ToString()
		{
			return OutputFilePath.FullName;
		}
	}
}
