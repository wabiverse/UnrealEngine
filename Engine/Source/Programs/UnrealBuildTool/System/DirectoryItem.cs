// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Stores the state of a directory. May or may not exist.
	/// </summary>
	class DirectoryItem
	{
		/// <summary>
		/// Full path to the directory on disk
		/// </summary>
		public readonly DirectoryReference Location;

		/// <summary>
		/// Cached value for whether the directory exists
		/// </summary>
		DirectoryInfo Info;

		/// <summary>
		/// Cached map of name to subdirectory item
		/// </summary>
		List<DirectoryItem> Directories;

		/// <summary>
		/// Cached map of name to file
		/// </summary>
		List<FileItem> Files;

		/// <summary>
		/// Global map of location to item
		/// </summary>
		static ConcurrentDictionary<DirectoryReference, DirectoryItem> LocationToItem = new ConcurrentDictionary<DirectoryReference, DirectoryItem>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Location">Path to this directory</param>
		/// <param name="Info">Information about this directory</param>
		private DirectoryItem(DirectoryReference Location, DirectoryInfo Info)
		{
			this.Location = Location;
			this.Info = Info;
		}

		/// <summary>
		/// The name of this directory
		/// </summary>
		public string Name
		{
			get { return Info.Name; }
		}

		/// <summary>
		/// Whether the directory exists or not
		/// </summary>
		public bool Exists
		{
			get { return Info.Exists; }
		}

		/// <summary>
		/// The last write time of the file.
		/// </summary>
		public DateTime LastWriteTimeUtc
		{
			get { return Info.LastWriteTimeUtc; }
		}

		/// <summary>
		/// Gets the parent directory item
		/// </summary>
		public DirectoryItem GetParentDirectoryItem()
		{
			if(Info.Parent == null)
			{
				return null;
			}
			else
			{
				return GetItemByDirectoryInfo(Info.Parent);
			}
		}

		/// <summary>
		/// Gets a new directory item by combining the existing directory item with the given path fragments
		/// </summary>
		/// <param name="BaseDirectory">Base directory to append path fragments to</param>
		/// <param name="Fragments">The path fragments to append</param>
		/// <returns>Directory item corresponding to the combined path</returns>
		public static DirectoryItem Combine(DirectoryItem BaseDirectory, params string[] Fragments)
		{
			return DirectoryItem.GetItemByDirectoryReference(DirectoryReference.Combine(BaseDirectory.Location, Fragments));
		}

		/// <summary>
		/// Finds or creates a directory item from its location
		/// </summary>
		/// <param name="Location">Path to the directory</param>
		/// <returns>The directory item for this location</returns>
		public static DirectoryItem GetItemByPath(string Location)
		{
			return GetItemByDirectoryReference(new DirectoryReference(Location));
		}

		/// <summary>
		/// Finds or creates a directory item from its location
		/// </summary>
		/// <param name="Location">Path to the directory</param>
		/// <returns>The directory item for this location</returns>
		public static DirectoryItem GetItemByDirectoryReference(DirectoryReference Location)
		{
			DirectoryItem Result;
			if(!LocationToItem.TryGetValue(Location, out Result))
			{
				DirectoryItem NewItem = new DirectoryItem(Location, new DirectoryInfo(Location.FullName));
				if(LocationToItem.TryAdd(Location, NewItem))
				{
					Result = NewItem;
				}
				else
				{
					Result = LocationToItem[Location];
				}
			}
			return Result;
		}

		/// <summary>
		/// Finds or creates a directory item from a DirectoryInfo object
		/// </summary>
		/// <param name="Info">Path to the directory</param>
		/// <returns>The directory item for this location</returns>
		public static DirectoryItem GetItemByDirectoryInfo(DirectoryInfo Info)
		{
			DirectoryReference Location = new DirectoryReference(Info);

			DirectoryItem Result;
			if(!LocationToItem.TryGetValue(Location, out Result))
			{
				DirectoryItem NewItem = new DirectoryItem(Location, Info);
				if(LocationToItem.TryAdd(Location, NewItem))
				{
					Result = NewItem;
				}
				else
				{
					Result = LocationToItem[Location];
				}
			}
			return Result;
		}

		/// <summary>
		/// Reset the contents of the directory and allow them to be fetched again
		/// </summary>
		public void Refresh()
		{
			Directories = null;
			Files = null;
		}

		/// <summary>
		/// Caches the subdirectories of this directories
		/// </summary>
		public void CacheDirectories()
		{
			if(Directories == null)
			{
				List<DirectoryItem> NewDirectories = new List<DirectoryItem>();
				if(Info.Exists)
				{
					foreach(DirectoryInfo SubDirectoryInfo in Info.EnumerateDirectories())
					{
						if(SubDirectoryInfo.Name.Length == 1 && SubDirectoryInfo.Name[0] == '.')
						{
							continue;
						}
						else if(SubDirectoryInfo.Name.Length == 2 && SubDirectoryInfo.Name[0] == '.' && SubDirectoryInfo.Name[1] == '.')
						{
							continue;
						}
						else
						{
							NewDirectories.Add(DirectoryItem.GetItemByDirectoryInfo(SubDirectoryInfo));
						}
					}
				}
				Directories = NewDirectories;
			}
		}

		/// <summary>
		/// Enumerates all the subdirectories
		/// </summary>
		/// <returns>Sequence of subdirectory items</returns>
		public IEnumerable<DirectoryItem> EnumerateDirectories()
		{
			CacheDirectories();
			return Directories;
		}

		/// <summary>
		/// Attempts to get a sub-directory by name
		/// </summary>
		/// <param name="Name">Name of the directory</param>
		/// <param name="OutDirectory">If successful receives the matching directory item with this name</param>
		/// <returns>True if the file exists, false otherwise</returns>
		public bool TryGetDirectory(string Name, out DirectoryItem OutDirectory)
		{
			if(Name.Equals(".", StringComparison.Ordinal))
			{
				OutDirectory = this;
				return true;
			}

			if(Name.Equals("..", StringComparison.Ordinal))
			{
				OutDirectory = GetParentDirectoryItem();
				return OutDirectory != null;
			}

			CacheDirectories();

			foreach(DirectoryItem Directory in Directories)
			{
				if(String.Equals(Directory.Name, Name, DirectoryReference.Comparison))
				{
					OutDirectory = Directory;
					return true;
				}
			}

			OutDirectory = null;
			return false;
		}

		/// <summary>
		/// Caches the files in this directory
		/// </summary>
		public void CacheFiles()
		{
			if(Files == null)
			{
				List<FileItem> NewFiles = new List<FileItem>();
				if(Info.Exists)
				{
					foreach(FileInfo FileInfo in Info.EnumerateFiles())
					{
						NewFiles.Add(FileItem.GetItemByFileInfo(FileInfo));
					}
				}
				Files = NewFiles;
			}
		}

		/// <summary>
		/// Enumerates all the files
		/// </summary>
		/// <returns>Sequence of FileItems</returns>
		public IEnumerable<FileItem> EnumerateFiles()
		{
			CacheFiles();
			return Files;
		}

		/// <summary>
		/// Attempts to get a file from this directory by name. Unlike creating a file item and checking whether it exists, this does not
		/// cause any I/O to check whether it exists, nor does it create a permanent FileItem object.
		/// </summary>
		/// <param name="Name">Name of the file</param>
		/// <param name="OutFile">If successful receives the matching file item with this name</param>
		/// <returns>True if the file exists, false otherwise</returns>
		public bool TryGetFile(string Name, out FileItem OutFile)
		{
			CacheFiles();

			foreach(FileItem File in Files)
			{
				if(String.Equals(File.Name, Name, FileReference.Comparison))
				{
					OutFile = File;
					return true;
				}
			}

			OutFile = null;
			return false;
		}

		/// <summary>
		/// Formats this object as a string for debugging
		/// </summary>
		/// <returns>Location of the directory</returns>
		public override string ToString()
		{
			return Location.FullName;
		}
	}

	/// <summary>
	/// Helper functions for serialization
	/// </summary>
	static class DirectoryItemExtensionMethods
	{
		/// <summary>
		/// Read a directory item from a binary archive
		/// </summary>
		/// <param name="Reader">Reader to serialize data from</param>
		/// <returns>Instance of the serialized directory item</returns>
		public static DirectoryItem ReadDirectoryItem(this BinaryArchiveReader Reader)
		{
			return Reader.ReadObjectReference<DirectoryItem>(() => DirectoryItem.GetItemByDirectoryReference(Reader.ReadDirectoryReference()));
		}

		/// <summary>
		/// Write a directory item to a binary archive
		/// </summary>
		/// <param name="Writer">Writer to serialize data to</param>
		/// <param name="DirectoryItem">Directory item to write</param>
		public static void WriteDirectoryItem(this BinaryArchiveWriter Writer, DirectoryItem DirectoryItem)
		{
			Writer.WriteObjectReference<DirectoryItem>(DirectoryItem, () => Writer.WriteDirectoryReference(DirectoryItem.Location));
		}
	}
}
