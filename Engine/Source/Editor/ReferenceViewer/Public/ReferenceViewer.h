// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ModuleManager.h"
#include "AssetData.h"

/**
 * The public interface to this module
 */
class IReferenceViewerModule : public IModuleInterface
{

public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IReferenceViewerModule& Get()
	{
		return FModuleManager::LoadModuleChecked< IReferenceViewerModule >( "ReferenceViewer" );
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded( "ReferenceViewer" );
	}

	/** Invokes a major tab with a reference viewer within it */
	virtual void InvokeReferenceViewerTab(const TArray<FName>& GraphRootPackageNames)
	{
		TArray<FAssetIdentifier> Identifiers;
		for (FName Name : GraphRootPackageNames)
		{
			Identifiers.Add(FAssetIdentifier(Name));
		}

		InvokeReferenceViewerTab(Identifiers);
	}

	/** Invokes a major tab with a reference viewer within it */
	virtual void InvokeReferenceViewerTab(const TArray<FAssetIdentifier>& GraphRootIdentifiers) = 0;
};

