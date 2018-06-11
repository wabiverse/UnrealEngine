// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "ReplicationGraphTypes.h"

#include "ReplicationGraph.h"
#include "Engine/World.h"

#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Net/DataReplication.h"
#include "Engine/ActorChannel.h"
#include "Engine/NetworkObjectList.h"
#include "Net/RepLayout.h"
#include "GameFramework/SpectatorPawn.h"
#include "Engine/LevelStreamingKismet.h"
#include "GameFramework/SpectatorPawnMovement.h"
#include "Net/UnrealNetwork.h"
#include "Net/NetworkProfiler.h"
#include "HAL/LowLevelMemTracker.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Level.h"
#include "Templates/UnrealTemplate.h"
#include "Misc/CoreDelegates.h"


DEFINE_LOG_CATEGORY( LogReplicationGraph );

// --------------------------------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------------------------------
// Actor List Allocator
// --------------------------------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------------------------------

/// This could be simplified by using TChunkedArray?

template<uint32 NumListsPerBlock, uint32 MaxNumPools>
struct TActorListAllocator
{
	FActorRepList& RequestList(int32 ExpectedMaxSize)
	{
		return GetOrCreatePoolForListSize(ExpectedMaxSize).RequestList();
	}

	void ReleaseList(FActorRepList& List)
	{
		// Check we are at 0 refs, reset length to 0, and reset the used bit
		checkfSlow(List.RefCount == 0, TEXT("TActorListAllocator::ReleaseList called on list with RefCount %d"), List.RefCount);
		List.UsedBitRef = false;
		List.Num = 0;
	}

	void PreAllocateLists(int32 ListSize, int32 NumLists)
	{
		GetOrCreatePoolForListSize(ListSize, true).PreAllocateLists(NumLists);
	}

	/** Logs stats about this entire allocator. mode = level of detail */
	void LogStats(int32 Mode, FOutputDevice& Ar=*GLog);
	/** Logs details about a specific list */
	void LogDetails(int32 PoolSize, int32 BlockIdx, int32 ListIdx, FOutputDevice& Ar=*GLog);

private:

	/** A pool of lists of the same (max) size. Starts with a single FBlock of NumListsPerBlock lists. More blocks allocated as needed.  */
	struct FPool
	{
		FPool(int32 InListSize) : ListSize(InListSize), Block(InListSize) { }
		
		int32 ListSize;
		bool operator==(const int32& InSize) const { return InSize == ListSize; }

		/** Represents a block of allocated lists. Contains NumListsPerBlock. */
		struct FBlock
		{
			/** Construct a block for given list size. All lists within the block are preallocated and initialized here. */
			FBlock(const int32& InListSize)
			{
				UsedListsBitArray.Init(false, NumListsPerBlock);
				for (int32 i=0; i < NumListsPerBlock; ++i)
				{
					FActorRepList* List = AllocList(InListSize);
					List->RefCount = 0;
					List->Max = InListSize;
					List->Num = 0;
					new (&List->UsedBitRef) FBitReference(UsedListsBitArray[i]); // This is the only way to reassign the bitref (assignment op copies the value from the rhs bitref)
					Lists.Add(List);
				}
			}

			/** Returns next free list on this block of forwards to the next */
			FActorRepList& RequestList(const int32& ReqListSize)
			{
				// Note: we flip the used bit immediately even though the list's refcount==0.
				// FActorRepListView should be the only thing inc/dec refcount. If someone
				// wanted to use TActorListAllocator without ref counting, they could by simply
				// sticking to RequestList/ReleaseList.
				int32 FreeIdx = UsedListsBitArray.FindAndSetFirstZeroBit();
				if (FreeIdx == INDEX_NONE)
				{
					return GetNext(ReqListSize)->RequestList(ReqListSize);
				}

				return Lists[FreeIdx];
			}

			/** Returns next FBlock, allocating it if necessary */
			FBlock* GetNext(const int32& NextListSize)
			{
				if (!Next.IsValid())
				{
					Next = MakeUnique<FBlock>(NextListSize);
				}
				return Next.Get();
			}
			
			/** Pointers to all the lists we have allocated. This will free allocated FActorRepLists when it is destroyed */
			TIndirectArray<FActorRepList, TFixedAllocator<NumListsPerBlock>>	Lists;

			/** BitArray to track which lists are free. false == free */
			TBitArray<TFixedAllocator<NumListsPerBlock>> UsedListsBitArray;

			/** Pointer to next block, only allocated if necessary */
			TUniquePtr<FBlock> Next;
		};

		/** The head block that we start off with */
		FBlock Block;

		/** Get a free list from this pool */
		FActorRepList& RequestList() { return Block.RequestList(ListSize); }
		
		/** Preallocate (at least) NumLists by allocating (NumLists/NumListsPerBlock) FBlocks */
		void PreAllocateLists(int32 NumLists)
		{
			FBlock* CurrentBlock = &Block;
			while(NumLists > NumListsPerBlock)
			{
				CurrentBlock = CurrentBlock->GetNext(ListSize);
				NumLists -= NumListsPerBlock;
			}
		}
	};

	/** Fixed size pools. Note that due to inline allocation and the way we store the FBitReference, elements in this container MUST stay stable! A fixed allocator is the best here. It could be a indirect array (slower) or we could make FBlock::Block not inlined (use TUniquePtr)  */
	TArray<FPool, TFixedAllocator<MaxNumPools>>	PoolTable;

	static FActorRepList* AllocList(int32 DataNum)
	{
		uint32 NumBytes = sizeof(FActorRepList) + (DataNum * sizeof(FActorRepListType));
		FActorRepList* NewList = (FActorRepList*)FMemory::Malloc(NumBytes);
		NewList->Max = DataNum;
		NewList->Num = 0;
		NewList->RefCount = 0;
		return NewList;
	}

	FPool& GetOrCreatePoolForListSize(int32 ExpectedMaxSize, bool ForPreAllocation=false)
	{
		FPool* Pool = PoolTable.FindByPredicate([&ExpectedMaxSize](const FPool& InPool) { return ExpectedMaxSize <= InPool.ListSize; });
		if (!Pool)
		{
			if (!ForPreAllocation)
			{
				UE_LOG(LogReplicationGraph, Warning, TEXT("No pool big enough for requested list size %d. Creating a new pool. (You may want to preallocate a pool of this size or investigate why this size is needed)"), ExpectedMaxSize);
				ExpectedMaxSize = FMath::RoundUpToPowerOfTwo(ExpectedMaxSize); // Round up because this size was not preallocated
			}
			checkf(PoolTable.Num() < MaxNumPools, TEXT("You cannot allocate anymore pools! Consider preallocating a pool of the max list size you will need."));
			Pool = new (PoolTable) FPool( ExpectedMaxSize );
		}
		return *Pool;
	}
};

#ifndef REP_LISTS_PER_BLOCK
#define REP_LISTS_PER_BLOCK 128
#endif
#ifndef REP_LISTS_MAX_NUM_POOLS
#define REP_LISTS_MAX_NUM_POOLS 12
#endif
TActorListAllocator<REP_LISTS_PER_BLOCK, REP_LISTS_MAX_NUM_POOLS> GActorListAllocator;

void FActorRepList::Release()
{
	if (RefCount-- == 1)
	{
		GActorListAllocator.ReleaseList(*this);
	}
}

void FActorRepListRefView::RequestNewList(int32 NewSize, bool CopyExistingContent)
{
	FActorRepList* NewList = &GActorListAllocator.RequestList(NewSize > 0 ? NewSize : InitialListSize);
	if (CopyExistingContent)
	{
		FMemory::Memcpy((uint8*)NewList->Data, (uint8*)CachedData, CachedNum * sizeof(FActorRepListType) );
		NewList->Num = CachedNum;
	}
	else
	{
		repCheck(NewList->Num == 0);
		CachedNum = 0;
	}
	RepList = NewList;
	CachedData = RepList->Data;
	CachedMax = RepList->Max;
}

void FActorRepListRefView::CopyContentsFrom(const FActorRepListRefView& Source)
{
	const int32 NewNum = Source.CachedNum;

	FActorRepList* NewList = &GActorListAllocator.RequestList(Source.Num());
	FMemory::Memcpy((uint8*)NewList->Data, (uint8*)Source.CachedData, NewNum * sizeof(FActorRepListType) );
	NewList->Num = NewNum;

	RepList = NewList;	

	CachedData = NewList->Data;
	CachedMax = NewList->Max;
	CachedNum = NewNum;
}

bool FActorRepListRefView::VerifyContents_Slow() const
{
	for (FActorRepListType Actor : *this)
	{
		if (IsActorValidForReplication(Actor) == false)
		{
			UE_LOG(LogReplicationGraph, Warning, TEXT("Actor %s not valid for replication"), *GetActorRepListTypeDebugString(Actor));
			return false;
		}
	

		TWeakObjectPtr<AActor> WeakPtr(Actor);
		if (WeakPtr.Get() == nullptr)
		{
			UE_LOG(LogReplicationGraph, Warning, TEXT("Actor %s failed WeakObjectPtr resolve"), *GetActorRepListTypeDebugString(Actor));
			return false;
		}
	}

	return true;
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void PrintRepListStats(int32 mode)
{
	GActorListAllocator.LogStats(mode);
}
void PrintRepListStatsAr(int32 mode, FOutputDevice& Ar)
{
	GActorListAllocator.LogStats(mode, Ar);
}
void PrintRepListDetails(int32 PoolSize, int32 BlockIdx, int32 ListIdx)
{
	GActorListAllocator.LogDetails(PoolSize, BlockIdx, ListIdx);
}
#endif

void PreAllocateRepList(int32 ListSize, int32 NumLists)
{
	GActorListAllocator.PreAllocateLists(ListSize, NumLists);
}

// --------------------------------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------------------------------
// Stats, Logging, Debugging
// --------------------------------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------------------------------

void LogListDetails(FActorRepList& RepList, FOutputDevice& Ar)
{
	FString ListContentString;
	for ( int32 i=0; i < RepList.Num; ++i)
	{
		ListContentString += GetActorRepListTypeDebugString(RepList.Data[i]);
		if (i < RepList.Num - 1) ListContentString += TEXT(" ");
	}
	Ar.Logf(TEXT("Num: %d. Ref: %d [%s]"), RepList.Num, RepList.RefCount, *ListContentString);
	Ar.Logf(TEXT(""));
}

template<uint32 NumListsPerBlock, uint32 MaxNumPools>
void TActorListAllocator<NumListsPerBlock, MaxNumPools>::LogStats(int32 Mode, FOutputDevice& Ar)
{
	uint32 NumPools = PoolTable.Num();
	uint32 NumBlocks = 0;
	uint32 NumUsedLists = 0;
	uint32 NumElements = 0;
	uint32 NumListBytes = 0;
	for (int32 PoolIdx=0; PoolIdx < PoolTable.Num(); ++PoolIdx)
	{
		FPool& Pool = PoolTable[PoolIdx];

		typename FPool::FBlock* B = &Pool.Block;
		uint32 NumBlocksThisPool = 0;
		uint32 NumUsedThisPool = 0;

		FString BlockBinaryStr;

		while(B)
		{
			NumBlocksThisPool++;
			NumElements += (NumListsPerBlock * Pool.ListSize);
			NumListBytes += NumListsPerBlock * (sizeof(FActorRepList) + (Pool.ListSize * sizeof(FActorRepListType)));

			// Block Details
			if (Mode >= 2)
			{
				for (int32 i=0; i < B->UsedListsBitArray.Num(); ++i)
				{
					BlockBinaryStr += B->UsedListsBitArray[i] ? *LexToString(B->Lists[i].RefCount) : TEXT("0");
				}
				BlockBinaryStr += TEXT(" ");
			}

			uint32 NumUsedThisBlock = 0;
			for (TConstSetBitIterator<TFixedAllocator<NumListsPerBlock>> It(B->UsedListsBitArray); It; ++It)
			{
				NumUsedThisBlock++;

				// List Details
				if (Mode >= 3)
				{
					LogListDetails(B->Lists[It.GetIndex()], Ar);
				}
			}
				
			NumUsedLists += NumUsedThisBlock;
			NumUsedThisPool += NumUsedThisBlock;				

			B = B->Next.Get();
		}

		if (Mode > 1)
		{
			Ar.Logf(TEXT("%s"), *BlockBinaryStr);
		}

		// Pool Details
		if (Mode >= 1)
		{
			Ar.Logf(TEXT("Pool[%d] ListSize: %d. NumBlocks: %d NumUsedLists: %d"), PoolIdx, Pool.ListSize, NumBlocksThisPool, NumUsedThisPool);
		}
		NumUsedLists += NumUsedThisPool;
		NumBlocks += NumBlocksThisPool;
	}

	// All Details
	Ar.Logf(TEXT(""));
	Ar.Logf(TEXT("[TOTAL] NumPools: %u. NumBlocks: %u. NumUsedLists: %u NumElements: %u ListBytes: %u"), NumPools, NumBlocks, NumUsedLists, NumElements, NumListBytes);
}

template<uint32 NumListsPerBlock, uint32 MaxNumPools>
void TActorListAllocator<NumListsPerBlock, MaxNumPools>::LogDetails(int32 PoolSize, int32 BlockIdx, int32 ListIdx, FOutputDevice& Ar)
{
	FPool* Pool = PoolTable.FindByPredicate([&PoolSize](const FPool& InPool) { return PoolSize <= InPool.ListSize; });
	if (!Pool)
	{
		Ar.Logf(TEXT("Could not find suitable PoolSize %d"), PoolSize);
		return;
	}

	if (ListIdx > NumListsPerBlock)
	{
		Ar.Logf(TEXT("ListIdx %d too big. Should be <= %d."), ListIdx, NumListsPerBlock);
		return;
	}

	typename FPool::FBlock* B = &Pool->Block;
	while(B && ListIdx-- > 0)
	{
		B = B->Next.Get();
	}

	if (B)
	{
		if (ListIdx < 0)
		{
			for (TConstSetBitIterator<TFixedAllocator<NumListsPerBlock>> It(B->UsedListsBitArray); It; ++It)
			{
				LogListDetails(B->Lists[It.GetIndex()], Ar);
			}
		}
		else
		{
			LogListDetails(B->Lists[ListIdx], Ar);
		}
	}
}

