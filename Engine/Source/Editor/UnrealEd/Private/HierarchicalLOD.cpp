// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"
#include "HierarchicalLOD.h"

#include "MessageLog.h"
#include "UObjectToken.h"
#include "MapErrors.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#include "Editor/UnrealEd/Public/Editor.h"
#include "Engine/LODActor.h"
#include "Editor/UnrealEd/Classes/Factories/Factory.h"
#include "ObjectTools.h"
#endif // WITH_EDITOR

#include "GameFramework/WorldSettings.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "HierarchicalLODVolume.h"

DEFINE_LOG_CATEGORY_STATIC(LogLODGenerator, Warning, All);

#define LOCTEXT_NAMESPACE "HierarchicalLOD"
#define CM_TO_METER		0.01f
#define METER_TO_CM		100.0f

FHierarchicalLODBuilder::FHierarchicalLODBuilder(UWorld* InWorld)
:	World(InWorld)
{}

#if WITH_HOT_RELOAD_CTORS
FHierarchicalLODBuilder::FHierarchicalLODBuilder()
	: World(nullptr)
{
	EnsureRetrievingVTablePtrDuringCtor(TEXT("FHierarchicalLODBuilder()"));
}
#endif // WITH_HOT_RELOAD_CTORS

void FHierarchicalLODBuilder::Build()
{
	check (World)
	const TArray<ULevel*>& Levels = World->GetLevels();
	for (const auto& LevelIter : Levels)
	{
		BuildClusters(LevelIter, true);
	}
}

void FHierarchicalLODBuilder::PreviewBuild()
{
	check(World);
	const TArray<ULevel*>& Levels = World->GetLevels();
	for (const auto& LevelIter : Levels)
	{
		BuildClusters(LevelIter, false);
	}
}

void FHierarchicalLODBuilder::BuildClusters(ULevel* InLevel, const bool bCreateMeshes)
{	
	SCOPE_LOG_TIME(TEXT("STAT_HLOD_BuildClusters"), nullptr);

	// I'm using stack mem within this scope of the function
	// so we need this
	FMemMark Mark(FMemStack::Get());

	// you still have to delete all objects just in case they had it and didn't want it anymore
	TArray<UObject*> AssetsToDelete;
	for (int32 ActorId=InLevel->Actors.Num()-1; ActorId >= 0; --ActorId)
	{
		ALODActor* LodActor = Cast<ALODActor>(InLevel->Actors[ActorId]);
		if (LodActor)
		{
			for (auto& Asset: LodActor->SubObjects)
			{
				// @TOOD: This is not permanent fix
				if (Asset)
				{
					AssetsToDelete.Add(Asset);
				}
			}
			World->DestroyActor(LodActor);
		}
	}

	ULevel::BuildStreamingData(InLevel->OwningWorld, InLevel);

	for (auto& Asset : AssetsToDelete)
	{
		Asset->MarkPendingKill();
		ObjectTools::DeleteSingleObject(Asset, false);
	}
	
	// garbage collect
	CollectGarbage( GARBAGE_COLLECTION_KEEPFLAGS, true );

	// only build if it's enabled
	if(InLevel->GetWorld()->GetWorldSettings()->bEnableHierarchicalLODSystem)
	{
		// Handle HierachicalLOD volumes first
		HandleHLODVolumes(InLevel);

		AWorldSettings* WorldSetting = InLevel->GetWorld()->GetWorldSettings();		
		const int32 TotalNumLOD = InLevel->GetWorld()->GetWorldSettings()->HierarchicalLODSetup.Num();
		for(int32 LODId=0; LODId<TotalNumLOD; ++LODId)
		{
			
			// we use meter for bound. Otherwise it's very easy to get to overflow and have problem with filling ratio because
			// bound is too huge
			const float DesiredBoundRadius = WorldSetting->HierarchicalLODSetup[LODId].DesiredBoundRadius * CM_TO_METER;
			const float DesiredFillingRatio = WorldSetting->HierarchicalLODSetup[LODId].DesiredFillingPercentage * 0.01f;
			ensure(DesiredFillingRatio!=0.f);
			const float HighestCost = FMath::Pow(DesiredBoundRadius, 3) / (DesiredFillingRatio);
			const int32 MinNumActors = WorldSetting->HierarchicalLODSetup[LODId].MinNumberOfActorsToBuild;
			check (MinNumActors > 0);
			// test parameter I was playing with to cull adding to the array
			// intialization can have too many elements, decided to cull
			// the problem can be that we can create disconnected tree
			// my assumption is that if the merge cost is too high, then it's not worth merge anyway
			static int32 CullMultiplier=1;

			// since to show progress of initialization, I'm scoping it
			{
				FString LevelName = FPackageName::GetShortName(InLevel->GetOutermost()->GetName());
				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("LODIndex"), FText::AsNumber(LODId+1));
				Arguments.Add(TEXT("LevelName"), FText::FromString(LevelName));

				FScopedSlowTask SlowTask(100, FText::Format(LOCTEXT("HierarchicalLOD_InitializeCluster", "Initializing Clusters for LOD {LODIndex} of {LevelName}..."), Arguments));
				SlowTask.MakeDialog();

				// initialize Clusters
				InitializeClusters(InLevel, LODId, HighestCost*CullMultiplier);

				// move a half way - I know we can do this better but as of now this is small progress
				SlowTask.EnterProgressFrame(50);

				// now we have all pair of nodes
				FindMST();
			}

			// now we have to calculate merge clusters and build actors
			MergeClustersAndBuildActors(InLevel, LODId, HighestCost, MinNumActors, bCreateMeshes);
		}
	}
	else
	{
		// Fire map check warnings if HLOD System is not enabled
		FMessageLog MapCheck("MapCheck");
		MapCheck.Warning()
			->AddToken(FUObjectToken::Create(InLevel->GetWorld()->GetWorldSettings()))
			->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_HLODSystemNotEnabled", "Hierarchical LOD System is disabled, unable to build LOD actors.")))
			->AddToken(FMapErrorToken::Create(FMapErrors::HLODSystemNotEnabled));
	}

	// Clear Clusters. It is using stack mem, so it won't be good after this
	Clusters.Empty();
	Clusters.Shrink();
}

void FHierarchicalLODBuilder::InitializeClusters(ULevel* InLevel, const int32 LODIdx, float CullCost)
{
	SCOPE_LOG_TIME(TEXT("STAT_HLOD_InitializeClusters"), nullptr);
	if (InLevel->Actors.Num() > 0)
	{
		if (LODIdx == 0)
		{
			Clusters.Empty();

			TArray<AActor*> GenerationActors;			
			for (int32 ActorId = 0; ActorId < InLevel->Actors.Num(); ++ActorId)
			{
				AActor* Actor = InLevel->Actors[ActorId];
				if (ShouldGenerateCluster(Actor))
				{
					// Check whether or not this actor falls within a HierarchicalLODVolume, if so add to the Volume's cluster and exclude from normal process
					bool bAdded = false;
					for (auto& Cluster : HLODVolumeClusters)
					{
						if (Cluster.Key->EncompassesPoint(Actor->GetActorLocation(), 0.0f, nullptr))
						{
							FLODCluster ActorCluster(Actor);
							Cluster.Value += ActorCluster;
							bAdded = true;
							break;
						}
					}

					if (!bAdded)
					{
						GenerationActors.Add(Actor);
					}
				}
			}
			
			// Create clusters using actor pairs
			for (int32 ActorId = 0; ActorId<GenerationActors.Num(); ++ActorId)
			{
				AActor* Actor1 = GenerationActors[ActorId];

				for (int32 SubActorId = ActorId + 1; SubActorId<GenerationActors.Num(); ++SubActorId)
				{
					AActor* Actor2 = GenerationActors[SubActorId];

					FLODCluster NewClusterCandidate = FLODCluster(Actor1, Actor2);
					float NewClusterCost = NewClusterCandidate.GetCost();

					if ( NewClusterCost <= CullCost)
					{
						Clusters.Add(NewClusterCandidate);
					}
				}
			}
		}
		else // at this point we only care for LODActors
		{
			Clusters.Empty();

			// we filter the LOD index first
			TArray<AActor*> Actors;
			for(int32 ActorId=0; ActorId<InLevel->Actors.Num(); ++ActorId)
			{
				AActor* Actor = (InLevel->Actors[ActorId]);

				if (Actor)
				{
					if (Actor->IsA(ALODActor::StaticClass()))
					{
						ALODActor* LODActor = CastChecked<ALODActor>(Actor);
						if (LODActor->LODLevel == LODIdx)
						{
							Actors.Add(Actor);
						}
					}
					else if (ShouldGenerateCluster(Actor))
					{
						Actors.Add(Actor);
					}
				}				
			}
			
			// first we generate graph with 2 pair nodes
			// this is very expensive when we have so many actors
			// so we'll need to optimize later @todo
			for(int32 ActorId=0; ActorId<Actors.Num(); ++ActorId)
			{
				AActor* Actor1 = (Actors[ActorId]);
				for(int32 SubActorId=ActorId+1; SubActorId<Actors.Num(); ++SubActorId)
				{
					AActor* Actor2 = Actors[SubActorId];

					// create new cluster
					FLODCluster NewClusterCandidate = FLODCluster(Actor1, Actor2);
					Clusters.Add(NewClusterCandidate);
				}
			}

			// shrink after adding actors
			// LOD 0 has lots of actors, and subsequence LODs tend to have a lot less actors
			// so this should save a lot more. 
			Clusters.Shrink();
		}
	}
}

void FHierarchicalLODBuilder::FindMST() 
{
	SCOPE_LOG_TIME(TEXT("STAT_HLOD_FindMST"), nullptr);
	if (Clusters.Num() > 0)
	{
		// now sort edge in the order of weight
		struct FCompareCluster
		{
			FORCEINLINE bool operator()(const FLODCluster& A, const FLODCluster& B) const
			{
				return (A.GetCost() < B.GetCost());
			}
		};

		Clusters.HeapSort(FCompareCluster());
	}
}

void FHierarchicalLODBuilder::HandleHLODVolumes(ULevel* InLevel)
{	
	for (int32 ActorId = 0; ActorId < InLevel->Actors.Num(); ++ActorId)
	{
		AActor* Actor = InLevel->Actors[ActorId];
		if (Actor && Actor->IsA( AHierarchicalLODVolume::StaticClass()))
		{
			// Came across a HLOD volume			
			FLODCluster& NewCluster = HLODVolumeClusters.Add(CastChecked<AHierarchicalLODVolume>(Actor));

			//FVector Origin, Extent;
			//Actor->GetActorBounds(false, Origin, Extent);			
			//NewCluster.Bound = FSphere(Origin * CM_TO_METER, Extent.GetAbsMax() * CM_TO_METER);
		}
	}
}

bool FHierarchicalLODBuilder::ShouldGenerateCluster(AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	if (!Actor->bEnableAutoLODGeneration)
	{
		return false;
	}

	FVector Origin, Extent;
	Actor->GetActorBounds(false, Origin, Extent);
	if (Extent.SizeSquared() <= 0.1)
	{
		return false;
	}

	// for now only consider staticmesh - I don't think skel mesh would work with simplygon merge right now @fixme
	TArray<UStaticMeshComponent*> Components;
	Actor->GetComponents<UStaticMeshComponent>(Components);
	// TODO: support instanced static meshes
	Components.RemoveAll([](UStaticMeshComponent* Val){ return Val->IsA(UInstancedStaticMeshComponent::StaticClass()); });

	int32 ValidComponentCount = 0;
	// now make sure you check parent primitive, so that we don't build for the actor that already has built. 
	if (Components.Num() > 0)
	{
		for (auto& ComponentIter : Components)
		{
			if (ComponentIter->GetLODParentPrimitive())
			{
				return false;
			}

			// see if we should generate it
			if (ComponentIter->ShouldGenerateAutoLOD())
			{
				++ValidComponentCount;
				break;
			}
		}
	}

	return (ValidComponentCount > 0);
}

void FHierarchicalLODBuilder::MergeClustersAndBuildActors(ULevel* InLevel, const int32 LODIdx, float HighestCost, int32 MinNumActors, const bool bCreateMeshes)
{	
	if (Clusters.Num() > 0)
	{
		FString LevelName = FPackageName::GetShortName(InLevel->GetOutermost()->GetName());
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("LODIndex"), FText::AsNumber(LODIdx+1));
		Arguments.Add(TEXT("LevelName"), FText::FromString(LevelName));
		// merge clusters first
		{
			SCOPE_LOG_TIME(TEXT("HLOD_MergeClusters"), nullptr);
			static int32 TotalIteration=3;
			const int32 TotalCluster = Clusters.Num();

			FScopedSlowTask SlowTask(TotalIteration*TotalCluster, FText::Format( LOCTEXT("HierarchicalLOD_BuildClusters", "Building Clusters for LOD {LODIndex} of {LevelName}..."), Arguments) );
			SlowTask.MakeDialog();

			for(int32 Iteration=0; Iteration<TotalIteration; ++Iteration)
			{
				// now we have minimum Clusters
				for(int32 ClusterId=0; ClusterId < TotalCluster; ++ClusterId)
				{
					auto& Cluster = Clusters[ClusterId];
					UE_LOG(LogLODGenerator, Verbose, TEXT("%d. %0.2f {%s}"), ClusterId+1, Cluster.GetCost(), *Cluster.ToString());

					// progress bar update
					SlowTask.EnterProgressFrame();

					if(Cluster.IsValid())
					{
						for(int32 MergedClusterId=0; MergedClusterId < ClusterId; ++MergedClusterId)
						{
							// compare with previous clusters
							auto& MergedCluster = Clusters[MergedClusterId];
							// see if it's valid, if it contains, check the cost
							if(MergedCluster.IsValid())
							{
								if(MergedCluster.Contains(Cluster))
								{
									// if valid, see if it contains any of this actors
									// merge whole clusters
									FLODCluster NewCluster = Cluster + MergedCluster;
									float MergeCost = NewCluster.GetCost();

									// merge two clusters
									if(MergeCost <= HighestCost)
									{
										UE_LOG(LogLODGenerator, Log, TEXT("Merging of Cluster (%d) and (%d) with merge cost (%0.2f) "), ClusterId+1, MergedClusterId+1, MergeCost);

										MergedCluster = NewCluster;
										// now this cluster is invalid
										Cluster.Invalidate();
										break;
									}
									else
									{
										Cluster -= MergedCluster;
									}
								}
							}
						}

						UE_LOG(LogLODGenerator, Verbose, TEXT("Processed(%s): %0.2f {%s}"), Cluster.IsValid()? TEXT("Valid"):TEXT("Invalid"), Cluster.GetCost(), *Cluster.ToString());
					}
				}
			}
		}


		if (LODIdx == 0)
		{
			for (auto& Cluster : HLODVolumeClusters)
			{
				Clusters.Add(Cluster.Value);
			}
		}
		

		// debug flag, so that I can just see clustered data since no visualization in the editor yet
		//static bool bBuildActor=true;

		//if (bBuildActors)
		{
			SCOPE_LOG_TIME(TEXT("HLOD_BuildActors"), nullptr);
			// print data
			int32 TotalValidCluster=0;
			for(auto& Cluster: Clusters)
			{
				if(Cluster.IsValid())
				{
					++TotalValidCluster;
				}
			}

			FScopedSlowTask SlowTask(TotalValidCluster, FText::Format( LOCTEXT("HierarchicalLOD_MergeActors", "Merging Actors for LOD {LODIndex} of {LevelName}..."), Arguments) );
			SlowTask.MakeDialog();

			for(auto& Cluster: Clusters)
			{
				if(Cluster.IsValid())
				{
					SlowTask.EnterProgressFrame();

					if (Cluster.Actors.Num() >= MinNumActors)
					{
						Cluster.BuildActor(InLevel, LODIdx, bCreateMeshes);
					}
				}
			}
		}
	}
}
#undef LOCTEXT_NAMESPACE 
