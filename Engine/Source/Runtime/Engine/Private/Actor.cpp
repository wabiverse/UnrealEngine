// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


#include "EnginePrivate.h"
#include "Engine/InputDelegateBinding.h"
#include "Engine/LevelStreamingPersistent.h"
#include "PhysicsPublic.h"
#include "ParticleDefinitions.h"
#include "Sound/SoundCue.h"
#include "LatentActions.h"
#include "MessageLog.h"
#include "Net/UnrealNetwork.h"
#include "DisplayDebugHelpers.h"
#include "Matinee/MatineeActor.h"
#include "Matinee/InterpGroup.h"
#include "Matinee/InterpGroupInst.h"
#include "Particles/ParticleSystemComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "Animation/AnimInstance.h"
#include "Engine/DemoNetDriver.h"
#include "GameFramework/Pawn.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "Components/TimelineComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

//DEFINE_LOG_CATEGORY_STATIC(LogActor, Log, All);
DEFINE_LOG_CATEGORY(LogActor);

DEFINE_STAT(STAT_GetComponentsTime);

#if UE_BUILD_SHIPPING
#define DEBUG_CALLSPACE(Format, ...)
#else
#define DEBUG_CALLSPACE(Format, ...) UE_LOG(LogNet, VeryVerbose, Format, __VA_ARGS__);
#endif

#define LOCTEXT_NAMESPACE "Actor"

FMakeNoiseDelegate AActor::MakeNoiseDelegate = FMakeNoiseDelegate::CreateStatic(&AActor::MakeNoiseImpl);

#if !UE_BUILD_SHIPPING
FOnProcessEvent AActor::ProcessEventDelegate;
#endif

AActor::AActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	// Default to no tick function, but if we set 'never ticks' to false (so there is a tick function) it is enabled by default
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = true;

	CustomTimeDilation = 1.0f;

	Role = ROLE_Authority;
	RemoteRole = ROLE_None;
	bReplicates = false;
	NetPriority = 1.0f;
	NetUpdateFrequency = 100.0f;
	bNetLoadOnClient = true;
#if WITH_EDITORONLY_DATA
	bEditable = true;
	bListedInSceneOutliner = true;
	bHiddenEdLayer = false;
	bHiddenEdTemporary = false;
	bHiddenEdLevel = false;
	bActorLabelEditable = true;
	SpriteScale = 1.0f;
#endif // WITH_EDITORONLY_DATA
	NetCullDistanceSquared = 225000000.0f;
	NetDriverName = NAME_GameNetDriver;
	NetDormancy = DORM_Awake;
	// will be updated in PostInitProperties
	bActorEnableCollision = true;
	bActorSeamlessTraveled = false;
	InputConsumeOption_DEPRECATED = ICO_ConsumeBoundKeys;
	bBlockInput = false;
	bCanBeDamaged = true;
	bPendingKillPending = false;
	bFindCameraComponentWhenViewTarget = true;
	bAllowReceiveTickEventOnDedicatedServer = true;
	AnimUpdateRateShiftTag = 0;
	AnimUpdateRateFrameCount = 0;
}

// Global counter to spread SkinnedMeshComponent tick updates.
static uint32 SkinnedMeshUpdateRateGroupCount = 0;

uint32 AActor::GetAnimUpdateRateShiftTag()
{
	// If hasn't been initialized yet, pick a unique ID, to spread population over frames.
	if (AnimUpdateRateShiftTag == 0)
	{
		AnimUpdateRateShiftTag = ++SkinnedMeshUpdateRateGroupCount;
	}

	return AnimUpdateRateShiftTag;
}

void FActorTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && !Target->HasAnyFlags(RF_PendingKill | RF_Unreachable))
	{
		FScopeCycleCounterUObject ActorScope(Target);
		Target->TickActor(DeltaTime*Target->CustomTimeDilation, TickType, *this);	
	}
}

FString FActorTickFunction::DiagnosticMessage()
{
	return Target->GetFullName() + TEXT("[TickActor]");
}

bool AActor::CheckDefaultSubobjectsInternal()
{
	bool Result = Super::CheckDefaultSubobjectsInternal();
	if (Result)
	{
		Result = CheckActorComponents();
	}
	return Result;
}

bool AActor::CheckActorComponents()
{
	DEFINE_LOG_CATEGORY_STATIC(LogCheckComponents, Warning, All);

	bool bResult = true;
	TArray<UActorComponent*> Components;
	GetComponents(Components);

	for (int32 Index = 0; Index < Components.Num(); Index++)
	{
		UActorComponent* Inner = Components[Index];
		if (!Inner)
		{
			continue;
		}
		if (Inner->IsPendingKill())
		{
			UE_LOG(LogCheckComponents, Warning, TEXT("Component is pending kill. Me = %s, Component = %s"), *this->GetFullName(), *Inner->GetFullName());
		}
		if (Inner->IsTemplate() && !IsTemplate())
		{
			UE_LOG(LogCheckComponents, Error, TEXT("Component is a template but I am not. Me = %s, Component = %s"), *this->GetFullName(), *Inner->GetFullName());
			bResult = false;
		}
		UObject* Archetype = Inner->GetArchetype();
		if (Archetype != Inner->GetClass()->GetDefaultObject())
		{
			if (Archetype != GetClass()->GetDefaultSubobjectByName(Inner->GetFName()))
			{
				UE_LOG(LogCheckComponents, Error, TEXT("Component archetype is not the CDO nor a default subobject of my class. Me = %s, Component = %s, Archetype = %s"), *this->GetFullName(), *Inner->GetFullName(), *Archetype->GetFullName());
				bResult = false;
			}
		}
	}
	for (int32 Index = 0; Index < SerializedComponents.Num(); Index++)
	{
		UActorComponent* Inner = SerializedComponents[Index];
		if (!Inner)
		{
			continue;
		}
		if (Inner->GetOuter() != this)
		{
			UE_LOG(LogCheckComponents, Error, TEXT("SerializedComponent does not have me as an outer. Me = %s, Component = %s"), *this->GetFullName(), *Inner->GetFullName());
			bResult = false;
		}
		if (Inner->IsPendingKill())
		{
			UE_LOG(LogCheckComponents, Warning, TEXT("SerializedComponent is pending kill. Me = %s, Component = %s"), *this->GetFullName(), *Inner->GetFullName());
		}
		if (Inner->IsTemplate() && !IsTemplate())
		{
			UE_LOG(LogCheckComponents, Error, TEXT("SerializedComponent is a template but I am not. Me = %s, Component = %s"), *this->GetFullName(), *Inner->GetFullName());
			bResult = false;
		}
		UObject* Archetype = Inner->GetArchetype();
		if (Archetype != Inner->GetClass()->GetDefaultObject())
		{
			if (Archetype != GetClass()->GetDefaultSubobjectByName(Inner->GetFName()))
			{
				UE_LOG(LogCheckComponents, Error, TEXT("SerializedComponent archetype is not the CDO nor a default subobject of my class. Me = %s, Component = %s, Archetype = %s"), *this->GetFullName(), *Inner->GetFullName(), *Archetype->GetFullName());
				bResult = false;
			}
		}
	}
	return bResult;
}

void AActor::ResetOwnedComponents()
{
	TArray<UObject*> Children;
	OwnedComponents.Empty();
	ReplicatedComponents.Empty();
	GetObjectsWithOuter(this, Children, true, RF_PendingKill);

	for (UObject* Child : Children)
	{
		UActorComponent* Component = Cast<UActorComponent>(Child);
		if (Component)
		{
			OwnedComponents.Add(Component);

			if (Component->GetIsReplicated())
			{
				ReplicatedComponents.Add(Component);
			}
		}
	}
}

void AActor::PostInitProperties()
{
	Super::PostInitProperties();
	RegisterAllActorTickFunctions(true,false); // component will be handled when they are registered
	RemoteRole = (bReplicates ? ROLE_SimulatedProxy : ROLE_None);

	// Make sure the OwnedComponents list correct.  
	// Under some circumstances sub-object instancing can result in bogus/duplicate entries.
	// This is not necessary for CDOs
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		ResetOwnedComponents();
	}
}

void AActor::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	AActor* This = CastChecked<AActor>(InThis);
	Collector.AddReferencedObjects(This->OwnedComponents);
	Super::AddReferencedObjects(InThis, Collector);
}

UWorld* AActor::GetWorld() const
{
	// CDO objects do not belong to a world
	// If the actors outer is destroyed or unreachable we are shutting down and the world should be NULL
	return (!HasAnyFlags(RF_ClassDefaultObject) && !GetOuter()->HasAnyFlags(RF_BeginDestroyed|RF_Unreachable) ? GetLevel()->OwningWorld : NULL);
}

FTimerManager& AActor::GetWorldTimerManager() const
{
	return GetWorld()->GetTimerManager();
}

UGameInstance* AActor::GetGameInstance() const
{
	return GetWorld()->GetGameInstance();
}

bool AActor::IsNetStartupActor() const
{
	return bNetStartup;
}

FVector AActor::GetVelocity() const
{
	if ( RootComponent )
	{
		return RootComponent->GetComponentVelocity();
	}

	return FVector::ZeroVector;
}

void AActor::ClearCrossLevelReferences()
{
	if(RootComponent && GetRootComponent()->AttachParent && (GetOutermost() != GetRootComponent()->AttachParent->GetOutermost()))
	{
		GetRootComponent()->DetachFromParent();
	}
}

bool AActor::TeleportTo( const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest, bool bNoCheck )
{
	SCOPE_CYCLE_COUNTER(STAT_TeleportToTime);

	if(RootComponent == NULL)
	{
		return false;
	}

	// Can't move non-movable actors during play
	if( (RootComponent->Mobility == EComponentMobility::Static) && GetWorld()->AreActorsInitialized() )
	{
		return false;
	}

	if ( bIsATest && (GetActorLocation() == DestLocation) )
	{
		return true;
	}

	FVector const PrevLocation = GetActorLocation();
	FVector NewLocation = DestLocation;
	bool bTeleportSucceeded = true;
	UPrimitiveComponent* ActorPrimComp = Cast<UPrimitiveComponent>(RootComponent);
	if ( ActorPrimComp )
	{
		if (!bNoCheck && (ActorPrimComp->IsCollisionEnabled() || (bCollideWhenPlacing && (GetNetMode() != NM_Client))) )
		{
			// Apply the pivot offset to the desired location
			FVector PivotOffset = GetRootComponent()->Bounds.Origin - PrevLocation;
			NewLocation = NewLocation + PivotOffset;

			// check if able to find an acceptable destination for this actor that doesn't embed it in world geometry
			bTeleportSucceeded = GetWorld()->FindTeleportSpot(this, NewLocation, DestRotation);
			NewLocation = NewLocation - PivotOffset;
		}

		if ( bTeleportSucceeded )
		{
			// check whether this actor unacceptably encroaches on any other actors.
			if ( bIsATest || bNoCheck )
			{
				ActorPrimComp->SetWorldLocationAndRotation(NewLocation, DestRotation);
			}
			else
			{
				FVector const Delta = NewLocation - PrevLocation;
				bTeleportSucceeded = ActorPrimComp->MoveComponent(Delta, DestRotation, false);
			}
			if( bTeleportSucceeded )
			{
				TeleportSucceeded(bIsATest);
			}
		}
	}
	else if (RootComponent)
	{
		// not a primitivecomponent, just set directly
		GetRootComponent()->SetWorldLocationAndRotation(NewLocation, DestRotation);
	}

	return bTeleportSucceeded; 
}


bool AActor::K2_TeleportTo( FVector DestLocation, FRotator DestRotation )
{
	return TeleportTo(DestLocation, DestRotation, false, false);
}

void AActor::SetTickPrerequisite(AActor* PrerequisiteActor)
{
	AddTickPrerequisiteActor(PrerequisiteActor);
}

void AActor::AddTickPrerequisiteActor(AActor* PrerequisiteActor)
{
	if (PrimaryActorTick.bCanEverTick && PrerequisiteActor && PrerequisiteActor->PrimaryActorTick.bCanEverTick)
	{
		PrimaryActorTick.AddPrerequisite(PrerequisiteActor, PrerequisiteActor->PrimaryActorTick);
	}
}

void AActor::AddTickPrerequisiteComponent(UActorComponent* PrerequisiteComponent)
{
	if (PrimaryActorTick.bCanEverTick && PrerequisiteComponent && PrerequisiteComponent->PrimaryComponentTick.bCanEverTick)
	{
		PrimaryActorTick.AddPrerequisite(PrerequisiteComponent, PrerequisiteComponent->PrimaryComponentTick);
	}
}

void AActor::RemoveTickPrerequisiteActor(AActor* PrerequisiteActor)
{
	if (PrerequisiteActor)
	{
		PrimaryActorTick.RemovePrerequisite(PrerequisiteActor, PrerequisiteActor->PrimaryActorTick);
	}
}

void AActor::RemoveTickPrerequisiteComponent(UActorComponent* PrerequisiteComponent)
{
	if (PrerequisiteComponent)
	{
		PrimaryActorTick.RemovePrerequisite(PrerequisiteComponent, PrerequisiteComponent->PrimaryComponentTick);
	}
}

void AActor::SetTickableWhenPaused(bool bTickableWhenPaused)
{
	PrimaryActorTick.bTickEvenWhenPaused = bTickableWhenPaused;
}

void AActor::SetTickEnabled(bool bEnable)
{
	if (CanEverTick())
	{
		PrimaryActorTick.SetTickFunctionEnable(bEnable);
	}
	else if (bEnable)
	{
		UE_LOG(LogActor, Warning, TEXT("Attempting to enable ticking for '%s' when the actor is explicitly prevented from doing so (check CanEverTick)."), *GetName());
	}
}

void AActor::AddControllingMatineeActor( AMatineeActor& InMatineeActor )
{
	if (RootComponent)
	{
		RootComponent->PrimaryComponentTick.AddPrerequisite(&InMatineeActor, InMatineeActor.PrimaryActorTick);
	}

	PrimaryActorTick.AddPrerequisite(&InMatineeActor, InMatineeActor.PrimaryActorTick);
	ControllingMatineeActors.AddUnique(&InMatineeActor);
}

void AActor::RemoveControllingMatineeActor( AMatineeActor& InMatineeActor )
{
	if (RootComponent)
	{
		RootComponent->PrimaryComponentTick.RemovePrerequisite(&InMatineeActor, InMatineeActor.PrimaryActorTick);
	}

	PrimaryActorTick.RemovePrerequisite(&InMatineeActor, InMatineeActor.PrimaryActorTick);
	ControllingMatineeActors.RemoveSwap(&InMatineeActor);
}

void AActor::BeginDestroy()
{
	UnregisterAllComponents();
	Super::BeginDestroy();
}

bool AActor::IsReadyForFinishDestroy()
{
	return Super::IsReadyForFinishDestroy() && DetachFence.IsFenceComplete();
}

void AActor::PostLoad()
{
	Super::PostLoad();

	// add ourselves to our Owner's Children array
	if (Owner != NULL)
	{
		checkSlow(!Owner->Children.Contains(this));
		Owner->Children.Add(this);
	}

	// For components created before we added the bCreatedByConstructionScript flag, we assume if you are not a default subobject, you can from a construction script
	if(GetLinkerUE4Version() < VER_UE4_ADD_CREATEDBYCONSTRUCTIONSCRIPT)
	{
		TArray<UActorComponent*> Components;
		GetComponents(Components);

		for(int32 i=0; i<Components.Num(); i++)
		{
			UActorComponent* Component = Components[i];
			if (!Component->IsDefaultSubobject())
			{
				Component->bCreatedByConstructionScript = true;
			}
		}
	}

	// For actors created before SimpleConstructionScript's default RootComponent, and the default BadBlueprintSprite are flagged as RF_Transactional and bCreatedByConstructionScript
	if(GetLinkerUE4Version() < VER_UE4_DEFAULT_ROOT_COMP_TRANSACTIONAL)
	{
		if(RootComponent != NULL && !RootComponent->IsDefaultSubobject())
		{
			if(RootComponent->GetClass()==USceneComponent::StaticClass())
			{
				RootComponent->SetFlags(RF_Transactional);
				RootComponent->bCreatedByConstructionScript = true;
			}
			else if(RootComponent->GetClass()==UBillboardComponent::StaticClass())
			{
				static const FName BadSpriteName(TEXT("BadBlueprintSprite"));
				UBillboardComponent* SpriteRootComponent = static_cast<UBillboardComponent*>(RootComponent);
				if(SpriteRootComponent->Sprite->GetFName() == BadSpriteName)
				{
					SpriteRootComponent->SetFlags(RF_Transactional);
					SpriteRootComponent->bCreatedByConstructionScript = true;
				}
			}
		}
	}

	if (GetLinkerUE4Version() < VER_UE4_CONSUME_INPUT_PER_BIND)
	{
		bBlockInput = (InputConsumeOption_DEPRECATED == ICO_ConsumeAll);
	}

	if (GetLinkerUE4Version() < VER_UE4_PRIVATE_REMOTE_ROLE)
	{
		bReplicates = (RemoteRole != ROLE_None);
	}

	if ( GIsEditor )
	{
#if WITH_EDITORONLY_DATA
		// Propagate the hidden at editor startup flag to the transient hidden flag
		bHiddenEdTemporary = bHiddenEd;

		// Check/warning when loading actors in editor. Should never load IsPendingKill() Actors!
		if ( IsPendingKill() )
		{
			UE_LOG(LogActor, Log,  TEXT("Loaded Actor (%s) with IsPendingKill() == true"), *GetName() );
		}
#endif // WITH_EDITORONLY_DATA
	}
}

void AActor::PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph)
{
	USceneComponent* OldRoot = RootComponent;
	bool bHadRoot = !!OldRoot;
	FRotator OldRotation;
	FVector OldTranslation;
	FVector OldScale;
	if (bHadRoot)
	{
		OldRotation = OldRoot->RelativeRotation;
		OldTranslation = OldRoot->RelativeLocation;
		OldScale = OldRoot->RelativeScale3D;
	}

	Super::PostLoadSubobjects(OuterInstanceGraph);

	ResetOwnedComponents();

	if (RootComponent && bHadRoot && OldRoot != RootComponent)
	{
		UE_LOG(LogActor, Log, TEXT("Root component has changed, relocating new root component to old position %s->%s"), *OldRoot->GetFullName(), *GetRootComponent()->GetFullName());
		GetRootComponent()->RelativeRotation = OldRotation;
		GetRootComponent()->RelativeLocation = OldTranslation;
		GetRootComponent()->RelativeScale3D = OldScale;
		
		// Migrate any attachment to the new root
		if(OldRoot->AttachParent)
		{
			RootComponent->AttachTo(OldRoot->AttachParent);
			OldRoot->DetachFromParent();
		}

		// Reset the transform on the old component
		OldRoot->RelativeRotation = FRotator::ZeroRotator;
		OldRoot->RelativeLocation = FVector::ZeroVector;
		OldRoot->RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
	}
}

void AActor::ProcessEvent(UFunction* Function, void* Parameters)
{
	#if WITH_EDITOR
	static const FName CallInEditorMeta(TEXT("CallInEditor"));
	const bool bAllowScriptExecution = GAllowActorScriptExecutionInEditor || Function->GetBoolMetaData(CallInEditorMeta);
	#else
	const bool bAllowScriptExecution = GAllowActorScriptExecutionInEditor;
	#endif
	if( ((GetWorld() && (GetWorld()->AreActorsInitialized() || bAllowScriptExecution)) || HasAnyFlags(RF_ClassDefaultObject)) && !GIsGarbageCollecting )
	{
#if !UE_BUILD_SHIPPING
		if (!ProcessEventDelegate.IsBound() || !ProcessEventDelegate.Execute(this, Function, Parameters))
		{
			Super::ProcessEvent(Function, Parameters);
		}
#else
		Super::ProcessEvent(Function, Parameters);
#endif
	}
}


void AActor::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
	// Do not shift child components
	if (RootComponent != NULL && RootComponent->AttachParent == NULL)
	{
		RootComponent->ApplyWorldOffset(InOffset, bWorldShift);
	}
}


static AActor* GTestRegisterTickFunctions = NULL;

void AActor::RegisterActorTickFunctions(bool bRegister)
{
	check(!IsTemplate());

	if(bRegister)
	{
		if(PrimaryActorTick.bCanEverTick)
		{
			PrimaryActorTick.Target = this;
			PrimaryActorTick.SetTickFunctionEnable(PrimaryActorTick.bStartWithTickEnabled);
			PrimaryActorTick.RegisterTickFunction(GetLevel());
		}
	}
	else
	{
		if(PrimaryActorTick.IsTickFunctionRegistered())
		{
			PrimaryActorTick.UnRegisterTickFunction();			
		}
	}

	GTestRegisterTickFunctions = this; // we will verify the super call chain is intact. Don't copy and paste this to another actor class!
}

void AActor::RegisterAllActorTickFunctions(bool bRegister, bool bDoComponents)
{
	if(!IsTemplate())
	{
		check(GTestRegisterTickFunctions == NULL);
		RegisterActorTickFunctions(bRegister);
		checkf(GTestRegisterTickFunctions == this, TEXT("Failed to route Actor RegisterTickFunctions (%s)"), *GetFullName());
		GTestRegisterTickFunctions = NULL;

		if (bDoComponents)
		{
			TArray<UActorComponent*> Components;
			GetComponents(Components);

			for (int32 Index = 0; Index < Components.Num(); Index++)
			{
				Components[Index]->RegisterAllComponentTickFunctions(bRegister);
			}
		}
	}
}

void AActor::SetActorTickEnabled(bool bEnabled)
{
	if (!IsTemplate() && PrimaryActorTick.bCanEverTick)
	{
		PrimaryActorTick.SetTickFunctionEnable(bEnabled);
	}
}

bool AActor::IsActorTickEnabled() const
{
	return PrimaryActorTick.IsTickFunctionEnabled();
}

bool AActor::Rename( const TCHAR* InName, UObject* NewOuter, ERenameFlags Flags )
{
	if (NewOuter)
	{
		RegisterAllActorTickFunctions(false, true); // unregister all tick functions
		UnregisterAllComponents();
	}

	bool bSuccess = Super::Rename( InName, NewOuter, Flags );

	if (NewOuter && NewOuter->IsA<ULevel>())
	{
		UWorld* World = NewOuter->GetWorld();
		if (World && World->bIsWorldInitialized)
		{
			RegisterAllComponents();
		}
		RegisterAllActorTickFunctions(true, true); // register all tick functions
	}
	return bSuccess;
}

UNetConnection* AActor::GetNetConnection()
{
	return Owner ? Owner->GetNetConnection() : NULL;
}

class UPlayer* AActor::GetNetOwningPlayer()
{
	// We can only replicate RPCs to the owning player
	if (Role == ROLE_Authority)
	{
		if (Owner)
		{
			return Owner->GetNetOwningPlayer();
		}
	}
	return NULL;
}

void AActor::TickActor( float DeltaSeconds, ELevelTick TickType, FActorTickFunction& ThisTickFunction )
{
	//root of tick hierarchy

	// Non-player update.
	const bool bShouldTick = ((TickType!=LEVELTICK_ViewportsOnly) || ShouldTickIfViewportsOnly());
	if(bShouldTick)
	{
		// If an Actor has been Destroyed or its level has been unloaded don't execute any queued ticks
		if (!IsPendingKill() && GetWorld())
		{
			Tick(DeltaSeconds);	// perform any tick functions unique to an actor subclass
		}
	}
}

void AActor::Tick( float DeltaSeconds )
{
	// Blueprint code outside of the construction script should not run in the editor
	// Allow tick if we are not a dedicated server, or we allow this tick on dedicated servers
	if (GetWorldSettings() != NULL && (bAllowReceiveTickEventOnDedicatedServer || !IsRunningDedicatedServer()))
	{
		ReceiveTick(DeltaSeconds);
	}

	// Update any latent actions we have for this actor
	GetWorld()->GetLatentActionManager().ProcessLatentActions(this, DeltaSeconds);

	if (bAutoDestroyWhenFinished)
	{
		bool bOKToDestroy = true;

		// @todo: naive implementation, needs improved
		TArray<UActorComponent*> Components;
		GetComponents(Components);

		for (int32 CompIdx=0; CompIdx<Components.Num(); ++CompIdx)
		{
			UActorComponent* const Comp = Components[CompIdx];

			UParticleSystemComponent* const PSC = Cast<UParticleSystemComponent>(Comp);
			if ( PSC && (PSC->bIsActive || !PSC->bWasCompleted) )
			{
				bOKToDestroy = false;
				break;
			}

			UAudioComponent* const AC = Cast<UAudioComponent>(Comp);
			if (AC && AC->IsPlaying())
			{
				bOKToDestroy = false;
				break;
			}

			UTimelineComponent* const TL = Cast<UTimelineComponent>(Comp);
			if (TL && TL->IsPlaying())
			{
				bOKToDestroy = false;
				break;
			}
		}

		// die!
		if (bOKToDestroy)
		{
			Destroy();
		}
	}
}


/** If true, actor is ticked even if TickType==LEVELTICK_ViewportsOnly */
bool AActor::ShouldTickIfViewportsOnly() const
{
	return false;
}

void AActor::PreReplication( IRepChangedPropertyTracker & ChangedPropertyTracker )
{
	if ( bReplicateMovement || AttachmentReplication.AttachParent )
	{
		GatherCurrentMovement();
	}

	DOREPLIFETIME_ACTIVE_OVERRIDE( AActor, ReplicatedMovement, bReplicateMovement );
}

void AActor::PostActorCreated()
{
	// nothing at the moment
}

void AActor::GetComponentsBoundingCylinder(float& OutCollisionRadius, float& OutCollisionHalfHeight, bool bNonColliding) const
{
	bool bIgnoreRegistration = false;

#if WITH_EDITOR
	if(IsTemplate())
	{
		// Editor code calls this function on default objects when placing them in the viewport, so no components will be registered in those cases.
		if (!GetWorld() || !GetWorld()->IsGameWorld())
		{
			bIgnoreRegistration = true;
		}
		else
		{
			UE_LOG(LogActor, Log, TEXT("WARNING AActor::GetComponentsBoundingCylinder : Called on default object '%s'. Will likely return zero size."), *this->GetPathName());
		}
	}
#elif !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (IsTemplate())
	{
		UE_LOG(LogActor, Log, TEXT("WARNING AActor::GetComponentsBoundingCylinder : Called on default object '%s'. Will likely return zero size."), *this->GetPathName());
	}
#endif

	float Radius = 0.f;
	float HalfHeight = 0.f;

	TArray<UPrimitiveComponent*> Components;
	GetComponents(Components);

	for (int32 CompIdx = 0; CompIdx < Components.Num(); CompIdx++)
	{
		UPrimitiveComponent* PrimComp = Components[CompIdx];

		// Only use collidable components to find collision bounding box.
		if ((bIgnoreRegistration || PrimComp->IsRegistered()) && (bNonColliding || PrimComp->IsCollisionEnabled()))
		{
			float TestRadius, TestHalfHeight;
			PrimComp->CalcBoundingCylinder(TestRadius, TestHalfHeight);
			Radius = FMath::Max(Radius, TestRadius);
			HalfHeight = FMath::Max(HalfHeight, TestHalfHeight);
		}
	}

	OutCollisionRadius = Radius;
	OutCollisionHalfHeight = HalfHeight;
}


void AActor::GetSimpleCollisionCylinder(float& CollisionRadius, float& CollisionHalfHeight) const
{
	if (IsRootComponentCollisionRegistered())
	{
		RootComponent->CalcBoundingCylinder(CollisionRadius, CollisionHalfHeight);
	}
	else
	{
		GetComponentsBoundingCylinder(CollisionRadius, CollisionHalfHeight, false);
	}
}

bool AActor::IsRootComponentCollisionRegistered() const
{
	return RootComponent != NULL && RootComponent->IsRegistered() && RootComponent->IsCollisionEnabled();
}

bool AActor::IsAttachedTo(const AActor* Other) const
{
	return (RootComponent && Other && Other->RootComponent) ? RootComponent->IsAttachedTo(Other->RootComponent) : false;
}

bool AActor::IsBasedOnActor(const AActor* Other) const
{
	return IsAttachedTo(Other);
}

bool AActor::Modify( bool bAlwaysMarkDirty/*=true*/ )
{
	// Any properties that reference a blueprint constructed component needs to avoid creating a reference to the component from the transaction
	// buffer, so we temporarily switch the property to non-transactional while the modify occurs
	TArray<UObjectProperty*> TemporarilyNonTransactionalProperties;
	if (GUndo)
	{
		for (TFieldIterator<UObjectProperty> PropertyIt(GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			UObjectProperty* ObjProp = *PropertyIt;
			if (!ObjProp->HasAllPropertyFlags(CPF_NonTransactional))
			{
				UActorComponent* ActorComponent = Cast<UActorComponent>(ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(this)));
				if (ActorComponent && ActorComponent->bCreatedByConstructionScript)
				{
					ObjProp->SetPropertyFlags(CPF_NonTransactional);
					TemporarilyNonTransactionalProperties.Add(ObjProp);
				}
			}
		}
	}

	bool bSavedToTransactionBuffer = UObject::Modify( bAlwaysMarkDirty );

	for (UObjectProperty* ObjProp : TemporarilyNonTransactionalProperties)
	{
		ObjProp->ClearPropertyFlags(CPF_NonTransactional);
	}

	// If the root component is blueprint constructed we don't save it to the transaction buffer
	if( RootComponent && !RootComponent->bCreatedByConstructionScript )
	{
		bSavedToTransactionBuffer = RootComponent->Modify( bAlwaysMarkDirty ) || bSavedToTransactionBuffer;
	}

	return bSavedToTransactionBuffer;
}

FBox AActor::GetComponentsBoundingBox(bool bNonColliding) const
{
	FBox Box(0);

	TArray<UPrimitiveComponent*> Components;
	GetComponents(Components);

	for(int32 CompIdx = 0; CompIdx < Components.Num(); CompIdx++)
	{
		UPrimitiveComponent* PrimComp = Components[CompIdx];

		// Only use collidable components to find collision bounding box.
		if( PrimComp->IsRegistered() && (bNonColliding || PrimComp->IsCollisionEnabled()) )
		{
			Box += PrimComp->Bounds.GetBox();
		}
	}

	return Box;
}

bool AActor::CheckStillInWorld()
{
	// check the variations of KillZ
	AWorldSettings* WorldSettings = GetWorld()->GetWorldSettings( true );

	if (!WorldSettings->bEnableWorldBoundsChecks)
	{
		return true;
	}

	if( GetActorLocation().Z < WorldSettings->KillZ )
	{
		UDamageType const* const DmgType = WorldSettings->KillZDamageType ? WorldSettings->KillZDamageType->GetDefaultObject<UDamageType>() : GetDefault<UDamageType>();
		FellOutOfWorld(*DmgType);
		return false;
	}
	// Check if box has poked outside the world
	else if( ( RootComponent != NULL ) && ( GetRootComponent()->IsRegistered() == true ) )
	{
		const FBox&	Box = GetRootComponent()->Bounds.GetBox();
		if(	Box.Min.X < -HALF_WORLD_MAX || Box.Max.X > HALF_WORLD_MAX ||
			Box.Min.Y < -HALF_WORLD_MAX || Box.Max.Y > HALF_WORLD_MAX ||
			Box.Min.Z < -HALF_WORLD_MAX || Box.Max.Z > HALF_WORLD_MAX )
		{
			UE_LOG(LogActor, Warning, TEXT("%s is outside the world bounds!"), *GetName());
			OutsideWorldBounds();
			// not safe to use physics or collision at this point
			SetActorEnableCollision(false);
			DisableComponentsSimulatePhysics();
			return false;
		}
	}

	return true;
}

void AActor::SetTickGroup(ETickingGroup NewTickGroup)
{
	PrimaryActorTick.TickGroup = NewTickGroup;
}

void AActor::UpdateOverlaps(bool bDoNotifies)
{
	// just update the root component, which will cascade down to the children
	USceneComponent* const RootComp = GetRootComponent();
	if (RootComp)
	{
		RootComp->UpdateOverlaps(NULL, bDoNotifies);
	}
}

bool AActor::IsOverlappingActor(const AActor* Other) const
{
	// using a stack to walk this actor's attached component tree
	TArray<USceneComponent*> ComponentStack;

	USceneComponent const* CurrentComponent = GetRootComponent();

	while (CurrentComponent)
	{
		// push children on the stack so they get tested later
		ComponentStack.Append(CurrentComponent->AttachChildren);

		UPrimitiveComponent const* const PrimComp = Cast<const UPrimitiveComponent>(CurrentComponent);
		if (PrimComp && PrimComp->IsOverlappingActor(Other))
		{
			// found one, finished
			return true;
		}

		CurrentComponent = (ComponentStack.Num() > 0) ? ComponentStack.Pop() : NULL;
	}

	return false;
}


void AActor::GetOverlappingActors(TArray<AActor*>& OverlappingActors, UClass* ClassFilter) const
{
	// prepare output
	OverlappingActors.Empty();

	TArray<USceneComponent*> ComponentStack;

	USceneComponent const* CurrentComponent = GetRootComponent();

	while (CurrentComponent)
	{
		// push children on the stack so they get tested later
		ComponentStack.Append(CurrentComponent->AttachChildren);

		// get list of actors from the component
		UPrimitiveComponent const* const PrimComp = Cast<const UPrimitiveComponent>(CurrentComponent);
		if (PrimComp)
		{
			TArray<AActor*> OverlappingActorsForCurrentComponent;
			PrimComp->GetOverlappingActors(OverlappingActorsForCurrentComponent, ClassFilter);

			// then merge it into our final list
			for (auto CompIt = OverlappingActorsForCurrentComponent.CreateIterator(); CompIt; ++CompIt)
			{
				AActor* OverlappingActor = *CompIt;
				if(OverlappingActor != this)
				{
					OverlappingActors.AddUnique(OverlappingActor);
				}
			}
		}

		// advance to next component
		CurrentComponent = (ComponentStack.Num() > 0) ? ComponentStack.Pop() : NULL;
	}
}

void AActor::GetOverlappingComponents(TArray<UPrimitiveComponent*>& OutOverlappingComponents) const
{
	OutOverlappingComponents.Empty();

	TArray<USceneComponent*> ComponentStack;

	USceneComponent* CurrentComponent = GetRootComponent();

	while (CurrentComponent)
	{
		// push children on the stack so they get tested later
		ComponentStack.Append(CurrentComponent->AttachChildren);

		UPrimitiveComponent const* const PrimComp = Cast<const UPrimitiveComponent>(CurrentComponent);
		if (PrimComp)
		{
			// get list of components from the component
			TArray<UPrimitiveComponent*> OverlappingComponentsForCurrentComponent;
			PrimComp->GetOverlappingComponents(OverlappingComponentsForCurrentComponent);

			// then merge it into our final list
			for (auto CompIt = OverlappingComponentsForCurrentComponent.CreateIterator(); CompIt; ++CompIt)
			{
				OutOverlappingComponents.AddUnique(*CompIt);
			}
		}

		// advance to next component
		CurrentComponent = (ComponentStack.Num() > 0) ? ComponentStack.Pop() : NULL;
	}
}

/** marks all PrimitiveComponents for which their Owner is relevant for visibility as dirty because the Owner of some Actor in the chain has changed
 * @param TheActor the actor to mark components dirty for
 */
static void MarkOwnerRelevantComponentsDirty(AActor* TheActor)
{
	TArray<UPrimitiveComponent*> Components;
	TheActor->GetComponents(Components);

	for (int32 i = 0; i < Components.Num(); i++)
	{
		UPrimitiveComponent* Primitive = Components[i];
		if (Primitive->IsRegistered() && (Primitive->bOnlyOwnerSee || Primitive->bOwnerNoSee))
		{
			Primitive->MarkRenderStateDirty();
		}
	}

	// recurse over children of this Actor
	for (int32 i = 0; i < TheActor->Children.Num(); i++)
	{
		AActor* Child = TheActor->Children[i];
		if (Child != NULL && !Child->IsPendingKill())
		{
			MarkOwnerRelevantComponentsDirty(Child);
		}
	}
}

float AActor::GetLastRenderTime() const
{
	// return most recent of Components' LastRenderTime
	// @todo UE4 maybe check base component and components attached to it instead?
	TArray<UPrimitiveComponent*> Components;
	GetComponents(Components);

	float LastRenderTime = -1000.f;
	for( int32 i=0; i<Components.Num(); i++ )
	{
		UPrimitiveComponent* PrimComp = Components[i];
		if(PrimComp->IsRegistered())
		{
			LastRenderTime = FMath::Max(LastRenderTime, PrimComp->LastRenderTime);
		}
	}
	return LastRenderTime;
}

void AActor::SetOwner( AActor *NewOwner )
{
	if (Owner != NewOwner && !IsPendingKill())
	{
		if (NewOwner != NULL && NewOwner->IsOwnedBy(this))
		{
			UE_LOG(LogActor, Error, TEXT("SetOwner(): Failed to set '%s' owner of '%s' because it would cause an Owner loop"), *NewOwner->GetName(), *GetName());
			return;
		}

		// Sets this actor's parent to the specified actor.
		AActor* OldOwner = Owner;
		if( Owner != NULL )
		{
			// remove from old owner's Children array
			verifySlow(Owner->Children.Remove(this) == 1);
		}

		Owner = NewOwner;

		if( Owner != NULL )
		{
			// add to new owner's Children array
			checkSlow(!Owner->Children.Contains(this));
			Owner->Children.Add(this);
		}

		// mark all components for which Owner is relevant for visibility to be updated
		MarkOwnerRelevantComponentsDirty(this);
	}
}

AActor* AActor::GetOwner() const
{ 
	return Owner; 
}

bool AActor::HasNetOwner() const
{
	if (Owner == NULL)
	{
		// all basic AActors are unable to call RPCs without special AActors as their owners (ie APlayerController)
		return false;
	}

	// Find the topmost actor in this owner chain
	AActor* TopOwner = NULL;
	for (TopOwner = Owner; TopOwner->Owner; TopOwner = TopOwner->Owner)
	{
	}

	return TopOwner->HasNetOwner();
}

void AActor::K2_AttachRootComponentTo(USceneComponent* InParent, FName InSocketName, EAttachLocation::Type AttachLocationType /*= EAttachLocation::KeepRelativeOffset */, bool bWeldSimulatedBodies /*=true*/)
{
	AttachRootComponentTo(InParent, InSocketName, AttachLocationType, bWeldSimulatedBodies);
}

void AActor::AttachRootComponentTo(USceneComponent* InParent, FName InSocketName, EAttachLocation::Type AttachLocationType /*= EAttachLocation::KeepRelativeOffset */, bool bWeldSimulatedBodies /*=false*/)
{
	if(RootComponent && InParent)
	{
		RootComponent->AttachTo(InParent, InSocketName, AttachLocationType, bWeldSimulatedBodies);


		AttachmentReplication.AttachParent = InParent->GetAttachmentRootActor();
		AttachmentReplication.LocationOffset = RootComponent->RelativeLocation;
		AttachmentReplication.RotationOffset = RootComponent->RelativeRotation;
		AttachmentReplication.RelativeScale3D = RootComponent->RelativeScale3D;
		AttachmentReplication.AttachSocket = InSocketName;
		AttachmentReplication.AttachComponent = InParent;
	}
}

void AActor::OnRep_AttachmentReplication()
{
	if (AttachmentReplication.AttachParent)
	{
		if (RootComponent)
		{
			USceneComponent* ParentComponent = AttachmentReplication.AttachParent->GetRootComponent();
			
			if (AttachmentReplication.AttachComponent != NULL)
			{
				ParentComponent = AttachmentReplication.AttachComponent;
			}

			if (ParentComponent)
			{
				RootComponent->AttachTo(ParentComponent, AttachmentReplication.AttachSocket);
				RootComponent->RelativeLocation = AttachmentReplication.LocationOffset;
				RootComponent->RelativeRotation = AttachmentReplication.RotationOffset;
				RootComponent->RelativeScale3D = AttachmentReplication.RelativeScale3D;

				RootComponent->UpdateComponentToWorld();

			}
		}
	}
	else
	{
		DetachRootComponentFromParent();
	}
}

void AActor::K2_AttachRootComponentToActor(AActor* InParentActor, FName InSocketName /*= NAME_None*/, EAttachLocation::Type AttachLocationType /*= EAttachLocation::KeepRelativeOffset */, bool bWeldSimulatedBodies /*=true*/)
{
	AttachRootComponentToActor(InParentActor, InSocketName, AttachLocationType, bWeldSimulatedBodies);
}

void AActor::AttachRootComponentToActor(AActor* InParentActor, FName InSocketName /*= NAME_None*/, EAttachLocation::Type AttachLocationType /*= EAttachLocation::KeepRelativeOffset */, bool bWeldSimulatedBodies /*=false*/)
{
	if (RootComponent && InParentActor)
	{
		USceneComponent* ParentRootComponent = InParentActor->GetRootComponent();
		if (ParentRootComponent)
		{
			RootComponent->AttachTo(ParentRootComponent, InSocketName, AttachLocationType, bWeldSimulatedBodies );


			AttachmentReplication.AttachParent = InParentActor;
			AttachmentReplication.LocationOffset = RootComponent->RelativeLocation;
			AttachmentReplication.RotationOffset = RootComponent->RelativeRotation;
			AttachmentReplication.RelativeScale3D = RootComponent->RelativeScale3D;
			AttachmentReplication.AttachSocket = InSocketName;
			AttachmentReplication.AttachComponent = NULL;
		}
	}
}

void AActor::SnapRootComponentTo(AActor* InParentActor, FName InSocketName/* = NAME_None*/)
{
	if (RootComponent && InParentActor)
	{
		USceneComponent* ParentRootComponent = InParentActor->GetRootComponent();
		if (ParentRootComponent)
		{
			RootComponent->SnapTo(ParentRootComponent, InSocketName);
		}
	}
}

void AActor::DetachRootComponentFromParent(bool bMaintainWorldPosition)
{
	if(RootComponent)
	{
		USceneComponent * RootComponent = GetRootComponent();


		GetRootComponent()->DetachFromParent(bMaintainWorldPosition);

		AttachmentReplication.AttachParent = NULL;
	}
}

void AActor::DetachSceneComponentsFromParent(USceneComponent* InParentComponent, bool bMaintainWorldPosition)
{
	if (InParentComponent != NULL)
	{
		TArray<USceneComponent*> Components;
		GetComponents(Components);

		for (int32 Index = 0; Index < Components.Num(); ++Index)
		{
			USceneComponent* SceneComp = Components[Index];
			if (SceneComp->GetAttachParent() == InParentComponent)
			{
				SceneComp->DetachFromParent(bMaintainWorldPosition);
			}
		}
	}
}

AActor* AActor::GetAttachParentActor() const
{
	if(RootComponent != NULL)
	{
		for( const USceneComponent* Test=GetRootComponent()->AttachParent; Test!=NULL; Test=Test->AttachParent )
		{
			AActor* TestOwner = Test->GetOwner();
			if( TestOwner != this )
			{
				return TestOwner;
			}
		}
	}

	return NULL;
}

FName AActor::GetAttachParentSocketName() const
{
	if(RootComponent != NULL)
	{
		for( const USceneComponent* Test=GetRootComponent(); Test!=NULL; Test=Test->AttachParent )
		{
			AActor* TestOwner = Test->GetOwner();
			if( TestOwner != this )
			{
				return Test->AttachSocketName;
			}
		}
	}

	return NAME_None;
}

void AActor::GetAttachedActors(TArray<class AActor*>& OutActors) const
{
	OutActors.Empty();
	if (RootComponent != NULL)
	{
		// Current set of components to check
		TArray< USceneComponent*, TInlineAllocator<8> > CompsToCheck;

		// Set of all components we have checked
		TArray< USceneComponent*, TInlineAllocator<8> > CheckedComps;

		CompsToCheck.Push(RootComponent);

		// While still work left to do
		while(CompsToCheck.Num() > 0)
		{
			// Get the next off the queue
			USceneComponent* SceneComp = CompsToCheck.Pop();

			// Add it to the 'checked' set, should not already be there!
			if (!CheckedComps.Contains(SceneComp))
			{
				CheckedComps.Add(SceneComp);

				AActor* CompOwner = SceneComp->GetOwner();
				if (CompOwner != NULL)
				{
					if (CompOwner != this)
					{
						// If this component has a different owner, add that owner to our output set and do nothing more
						OutActors.AddUnique(CompOwner);
					}
					else
					{
						// This component is owned by us, we need to add its children
						for (int32 i = 0; i < SceneComp->AttachChildren.Num(); ++i)
						{
							USceneComponent* ChildComp = SceneComp->AttachChildren[i];

							// Add any we have not explored yet to the set to check
							if ((ChildComp != NULL) && !CheckedComps.Contains(ChildComp))
							{
								CompsToCheck.Push(ChildComp);
							}
						}
					}
				}
			}
		}
	}
}

bool AActor::ActorHasTag(FName Tag) const
{
	return (Tag != NAME_None) && Tags.Contains(Tag);
}

bool AActor::IsInLevel(const ULevel *TestLevel) const
{
	return (GetOuter() == TestLevel);
}

ULevel* AActor::GetLevel() const
{
	return CastChecked<ULevel>( GetOuter() );
}

bool AActor::IsInPersistentLevel(bool bIncludeLevelStreamingPersistent) const
{
	ULevel* MyLevel = GetLevel();
	UWorld* World = GetWorld();
	return ( (MyLevel == World->PersistentLevel) || ( bIncludeLevelStreamingPersistent && World->StreamingLevels.Num() > 0 &&
														Cast<ULevelStreamingPersistent>(World->StreamingLevels[0]) != NULL &&
														World->StreamingLevels[0]->GetLoadedLevel() == MyLevel ) );
}


bool AActor::IsMatineeControlled() const 
{
	bool bMovedByMatinee = false;
	for(auto It(ControllingMatineeActors.CreateConstIterator()); It; It++)
	{
		AMatineeActor* ControllingMatineeActor = *It;
		if(ControllingMatineeActor != NULL)
		{
			UInterpGroupInst* GroupInst = ControllingMatineeActor->FindGroupInst(this);
			if(GroupInst != NULL)
			{
				if(GroupInst->Group && GroupInst->Group->HasMoveTrack())
				{
					bMovedByMatinee = true;
					break;
				}
			}
			else
			{
				UE_LOG(LogActor, Log, TEXT("IsMatineeControlled: ControllingMatineeActor is set but no GroupInstance (%s)"), *GetPathName());
			}
		}
	}
	return bMovedByMatinee;
}

bool AActor::IsRootComponentStatic() const
{
	return(RootComponent != NULL && RootComponent->Mobility == EComponentMobility::Static);
}

bool AActor::IsRootComponentStationary() const
{
	return(RootComponent != NULL && RootComponent->Mobility == EComponentMobility::Stationary);
}

bool AActor::IsRootComponentMovable() const
{
	return(RootComponent != NULL && RootComponent->Mobility == EComponentMobility::Movable);
}

FVector AActor::GetTargetLocation(AActor* RequestedBy) const
{
	return GetActorLocation();
}


bool AActor::IsRelevancyOwnerFor(AActor* ReplicatedActor, AActor* ActorOwner, AActor* ConnectionActor)
{
	return (ActorOwner == this);
}

void AActor::SetNetUpdateTime(float NewUpdateTime)
{
	NetUpdateTime = NewUpdateTime;
}

void AActor::ForceNetUpdate()
{
	if (NetDormancy > DORM_Awake)
	{
		FlushNetDormancy(); 
	}

	SetNetUpdateTime(FMath::Min(NetUpdateTime, GetWorld()->TimeSeconds - 0.01f));
}

void AActor::SetNetDormancy(ENetDormancy NewDormancy)
{
	if (GetNetMode() == NM_Client)
	{
		return;
	}
	
	UNetDriver* NetDriver = GEngine->FindNamedNetDriver(GetWorld(), NetDriverName);
	if (NetDriver)
	{
		NetDormancy = NewDormancy;

		// If not dormant, flush actor from NetDriver's dormant list
		if (NewDormancy <= DORM_Awake)
		{
			// Since we are coming out of dormancy, make sure we are on the network actor list
			GetWorld()->AddNetworkActor( this );

			NetDriver->FlushActorDormancy(this);
		}
	}
}

/** Removes the actor from the NetDriver's dormancy list: forcing at least one more update. */
void AActor::FlushNetDormancy()
{
	if (GetNetMode() == NM_Client || NetDormancy <= DORM_Awake)
	{
		return;
	}

	if (NetDormancy == DORM_Initial)
	{
		// No longer initially dormant
		NetDormancy = DORM_DormantAll;
	}

	// Don't proceed with network operations if not actually set to replicate
	if (!bReplicates)
	{
		return;
	}

	// Add to network actors list if needed
	GetWorld()->AddNetworkActor( this );
	
	UNetDriver* NetDriver = GetNetDriver();
	if (NetDriver)
	{
		NetDriver->FlushActorDormancy(this);
	}
}

void AActor::PostRenderFor(APlayerController *PC, UCanvas *Canvas, FVector CameraPosition, FVector CameraDir) {}

void AActor::PrestreamTextures( float Seconds, bool bEnableStreaming, int32 CinematicTextureGroups )
{
	// This only handles non-location-based streaming. Location-based streaming is handled by SeqAct_StreamInTextures::UpdateOp.
	float Duration = 0.0;
	if ( bEnableStreaming )
	{
		// A Seconds==0.0f, it means infinite (e.g. 30 days)
		Duration = FMath::IsNearlyZero(Seconds) ? (60.0f*60.0f*24.0f*30.0f) : Seconds;
	}

	// Iterate over all components of that actor
	TArray<UMeshComponent*> Components;
	GetComponents(Components);

	for (int32 ComponentIndex=0; ComponentIndex < Components.Num(); ComponentIndex++)
	{
		// If its a static mesh component, with a static mesh
		UMeshComponent* MeshComponent = Components[ComponentIndex];
		if ( MeshComponent->IsRegistered() )
		{
			MeshComponent->PrestreamTextures( Duration, false, CinematicTextureGroups );
		}
	}
}

void AActor::OnRep_Instigator() {}

void AActor::RouteEndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bActorInitialized)
	{
		UninitializeComponents();

		UWorld* World = GetWorld();
		if (World && World->HasBegunPlay())
		{
			EndPlay(EndPlayReason);
		}
	}
}

void AActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Dispatch the blueprint events
	ReceiveEndPlay(EndPlayReason);
	OnEndPlay.Broadcast(EndPlayReason);

	// Behaviors specific to an actor being unloaded due to a streaming level removal
	if (EndPlayReason == EEndPlayReason::RemovedFromWorld)
	{
		bActorInitialized = false;
		GetWorld()->RemoveNetworkActor(this);

		// Clear any ticking lifespan timers
		if (InitialLifeSpan > 0.f)
		{
			SetLifeSpan(0.f);
		}
	}

	UNavigationSystem::OnActorUnregistered(this);
}

FVector AActor::GetPlacementExtent() const
{
	FVector Extent(0.f);
	if( (RootComponent && GetRootComponent()->ShouldCollideWhenPlacing()) && bCollideWhenPlacing) 
	{
		TArray<USceneComponent*> Components;
		GetComponents(Components);

		FBox ActorBox(0.f);
		for (int32 ComponentID=0; ComponentID<Components.Num(); ++ComponentID)
		{
			USceneComponent * SceneComp = Components[ComponentID];
			if (SceneComp->ShouldCollideWhenPlacing() )
			{
				ActorBox += SceneComp->GetPlacementExtent().GetBox();
			}
		}

		// Get box extent, adjusting for any difference between the center of the box and the actor pivot
		FVector AdjustedBoxExtent = ActorBox.GetExtent() - ActorBox.GetCenter();
		float CollisionRadius = FMath::Sqrt((AdjustedBoxExtent.X * AdjustedBoxExtent.X) + (AdjustedBoxExtent.Y * AdjustedBoxExtent.Y));
		Extent = FVector(CollisionRadius, CollisionRadius, AdjustedBoxExtent.Z);
	}
	return Extent;
}

FTransform AActor::ActorToWorld() const
{
	FTransform Result = FTransform::Identity;
	if( RootComponent != NULL )
	{
		Result = RootComponent->ComponentToWorld;
	}
	else
	{
		UE_LOG(LogActor, Log, TEXT("AActor::ActorToWorld (%s) No RootComponent!"), *GetPathName());
	}

	return Result;
}

class UClass* AActor::GetActorClass() const
{
	return GetClass();
}
FTransform AActor::GetTransform() const
{
	return ActorToWorld();
}

void AActor::Destroyed()
{
	RouteEndPlay(EEndPlayReason::ActorDestroyed);

	ReceiveDestroyed();
	OnDestroyed.Broadcast();
	GetWorld()->RemoveNetworkActor(this);
}

void AActor::TornOff() {}
void AActor::Reset() {}

void AActor::FellOutOfWorld(const UDamageType& dmgType)
{
	DisableComponentsSimulatePhysics();
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	Destroy();
}

void AActor::MakeNoise(float Loudness, APawn* NoiseInstigator, FVector NoiseLocation)
{
	NoiseInstigator = NoiseInstigator ? NoiseInstigator : Instigator;
	if ((GetNetMode() != NM_Client) && NoiseInstigator)
	{
		AActor::MakeNoiseDelegate.Execute(this, Loudness, NoiseInstigator
			, NoiseLocation.IsZero() ? GetActorLocation() : NoiseLocation);
	}
}

void AActor::MakeNoiseImpl(AActor* NoiseMaker, float Loudness, APawn* NoiseInstigator, const FVector& NoiseLocation)
{
	check(NoiseMaker);

	UPawnNoiseEmitterComponent* NoiseEmitterComponent = NoiseInstigator->GetPawnNoiseEmitterComponent();
	if (NoiseEmitterComponent)
	{
		NoiseEmitterComponent->MakeNoise( NoiseMaker, Loudness, NoiseLocation );
	}
}

void AActor::SetMakeNoiseDelegate(const FMakeNoiseDelegate& NewDelegate)
{
	if (NewDelegate.IsBound())
	{
		MakeNoiseDelegate = NewDelegate;
	}
}

float AActor::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	float ActualDamage = DamageAmount;

	UDamageType const* const DamageTypeCDO = DamageEvent.DamageTypeClass ? DamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>() : GetDefault<UDamageType>();
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		// point damage event, pass off to helper function
		FPointDamageEvent* const PointDamageEvent = (FPointDamageEvent*) &DamageEvent;
		ActualDamage = InternalTakePointDamage(ActualDamage, *PointDamageEvent, EventInstigator, DamageCauser);

		// K2 notification for this actor
		if (ActualDamage != 0.f)
		{
			ReceivePointDamage(ActualDamage, DamageTypeCDO, PointDamageEvent->HitInfo.ImpactPoint, PointDamageEvent->HitInfo.ImpactNormal, PointDamageEvent->HitInfo.Component.Get(), PointDamageEvent->HitInfo.BoneName, PointDamageEvent->ShotDirection, EventInstigator, DamageCauser);
			OnTakePointDamage.Broadcast(ActualDamage, EventInstigator, PointDamageEvent->HitInfo.ImpactPoint, PointDamageEvent->HitInfo.Component.Get(), PointDamageEvent->HitInfo.BoneName, PointDamageEvent->ShotDirection, DamageTypeCDO, DamageCauser);

			// Notify the component
			UPrimitiveComponent* const PrimComp = PointDamageEvent->HitInfo.Component.Get();
			if (PrimComp)
			{
				PrimComp->ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
			}
		}
	}
	else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		// radial damage event, pass off to helper function
		FRadialDamageEvent* const RadialDamageEvent = (FRadialDamageEvent*) &DamageEvent;
		ActualDamage = InternalTakeRadialDamage(ActualDamage, *RadialDamageEvent, EventInstigator, DamageCauser);

		// K2 notification for this actor
		if (ActualDamage != 0.f)
		{
			FHitResult const& Hit = (RadialDamageEvent->ComponentHits.Num() > 0) ? RadialDamageEvent->ComponentHits[0] : FHitResult();
			ReceiveRadialDamage(ActualDamage, DamageTypeCDO, RadialDamageEvent->Origin, Hit, EventInstigator, DamageCauser);

			// add any desired physics impulses to our components
			for (int HitIdx = 0; HitIdx < RadialDamageEvent->ComponentHits.Num(); ++HitIdx)
			{
				FHitResult const& CompHit = RadialDamageEvent->ComponentHits[HitIdx];
				UPrimitiveComponent* const PrimComp = CompHit.Component.Get();
				if (PrimComp && PrimComp->GetOwner() == this)
				{
					PrimComp->ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
				}
			}
		}
	}

	// generic damage notifications sent for any damage
	// note we will broadcast these for negative damage as well
	if (ActualDamage != 0.f)
	{
		ReceiveAnyDamage(ActualDamage, DamageTypeCDO, EventInstigator, DamageCauser);
		OnTakeAnyDamage.Broadcast(ActualDamage, DamageTypeCDO, EventInstigator, DamageCauser);
		if (EventInstigator != NULL)
		{
			EventInstigator->InstigatedAnyDamage(ActualDamage, DamageTypeCDO, this, DamageCauser);
		}
	}

	return ActualDamage;
}

float AActor::InternalTakeRadialDamage(float Damage, FRadialDamageEvent const& RadialDamageEvent, class AController* EventInstigator, class AActor* DamageCauser)
{
	float ActualDamage = Damage;

	FVector ClosestHitLoc(0);

	// find closest component
	// @todo, something more accurate here to account for size of components, e.g. closest point on the component bbox?
	// @todo, sum up damage contribution to each component?
	float ClosestHitDistSq = MAX_FLT;
	for (int32 HitIdx=0; HitIdx<RadialDamageEvent.ComponentHits.Num(); ++HitIdx)
	{
		FHitResult const& Hit = RadialDamageEvent.ComponentHits[HitIdx];
		float const DistSq = (Hit.ImpactPoint - RadialDamageEvent.Origin).SizeSquared();
		if (DistSq < ClosestHitDistSq)
		{
			ClosestHitDistSq = DistSq;
			ClosestHitLoc = Hit.ImpactPoint;
		}
	}

	float const RadialDamageScale = RadialDamageEvent.Params.GetDamageScale( FMath::Sqrt(ClosestHitDistSq) );

	ActualDamage = FMath::Lerp(RadialDamageEvent.Params.MinimumDamage, ActualDamage, FMath::Max(0.f, RadialDamageScale));

	return ActualDamage;
}

float AActor::InternalTakePointDamage(float Damage, FPointDamageEvent const& PointDamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	return Damage;
}

/** Util to check if prim comp pointer is valid and still alive */
static bool IsPrimCompValidAndAlive(UPrimitiveComponent* PrimComp)
{
	return (PrimComp != NULL) && !PrimComp->IsPendingKill();
}

/** Used to determine if it is ok to call a notification on this object */
static bool IsActorValidToNotify(AActor* Actor)
{
	return (Actor != NULL) && !Actor->IsPendingKill() && !Actor->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists);
}

void AActor::DispatchBlockingHit(UPrimitiveComponent* MyComp, UPrimitiveComponent* OtherComp, bool bSelfMoved, FHitResult const& Hit)
{
	check(MyComp);

	AActor* OtherActor = (OtherComp != NULL) ? OtherComp->GetOwner() : NULL;

	// Call virtual
	if(IsActorValidToNotify(this))
	{
		ReceiveHit(MyComp, OtherActor, OtherComp, bSelfMoved, Hit.ImpactPoint, Hit.ImpactNormal, FVector(0,0,0), Hit);
	}

	// If we are still ok, call delegate on actor
	if(IsActorValidToNotify(this))
	{
		OnActorHit.Broadcast(this, OtherActor, FVector(0,0,0), Hit);
	}

	// If component is still alive, call delegate on component
	if(!MyComp->IsPendingKill())
	{
		MyComp->OnComponentHit.Broadcast(OtherActor, MyComp, OtherComp, FVector(0,0,0), Hit);
	}
}

FString AActor::GetHumanReadableName() const
{
	return GetName();
}

void AActor::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	Canvas->SetDrawColor(255,0,0);

	FString T = GetHumanReadableName();
	if( IsPendingKill() )
	{
		T = FString::Printf(TEXT("%s DELETED (IsPendingKill() == true)"), *T);
	}
	UFont* RenderFont = GEngine->GetSmallFont();
	if( T != "" )
	{
		Canvas->DrawText(RenderFont, T, 4.0f, YPos);
		YPos += YL;
	}

	Canvas->SetDrawColor(255,255,255);

	if( DebugDisplay.IsDisplayOn(TEXT("net")) )
	{
		if( GetNetMode() != NM_Standalone )
		{
			// networking attributes
			T = FString::Printf(TEXT("ROLE: %i RemoteRole: %i NetNode: %i"), (int32)Role, (int32)RemoteRole, (int32)GetNetMode());

			if( bTearOff )
			{
				T = T + FString(TEXT(" Tear Off"));
			}
			Canvas->DrawText(RenderFont, T, 4.0f, YPos);
			YPos += YL;
		}
	}

	Canvas->DrawText(RenderFont, FString::Printf(TEXT("Location: %s Rotation: %s"), *GetActorLocation().ToString(), *GetActorRotation().ToString()), 4.0f, YPos);
	YPos += YL;

	if( DebugDisplay.IsDisplayOn(TEXT("physics")) )
	{
		Canvas->DrawText(RenderFont,FString::Printf(TEXT("Velocity: %s Speed: %f Speed2D: %f"), *GetVelocity().ToString(), GetVelocity().Size(), GetVelocity().Size2D()), 4.0f, YPos);
		YPos += YL;
	}

	if( DebugDisplay.IsDisplayOn(TEXT("collision")) )
	{
		Canvas->DrawColor.B = 0;
		float MyRadius, MyHeight;
		GetComponentsBoundingCylinder(MyRadius, MyHeight);
		Canvas->DrawText(RenderFont, FString::Printf(TEXT("Collision Radius: %f Height: %f"), MyRadius, MyHeight), 4.0f, YPos);
		YPos += YL;

		if ( RootComponent == NULL )
		{
			Canvas->DrawText(RenderFont, FString(TEXT("No RootComponent")), 4.0f, YPos );
			YPos += YL;
		}

		T = FString(TEXT("Touching "));

		TArray<AActor*> TouchingActors;
		GetOverlappingActors(TouchingActors);
		for (int32 iTouching = 0; iTouching<TouchingActors.Num(); ++iTouching)
		{
			AActor* const TestActor = TouchingActors[iTouching];
			if (TestActor &&
				!TestActor->IsPendingKill() &&
				TestActor->IsA<AActor>())
			{
				AActor* A = TestActor;
				T = T + A->GetName() + " ";
			}
		}

		if ( FCString::Strcmp(*T, TEXT("Overlapping ")))
		{
			T = TEXT("Overlapping nothing");
		}
		Canvas->DrawText(RenderFont,T, 4,YPos);
		YPos += YL;
	}
	Canvas->DrawText( RenderFont,FString::Printf(TEXT(" Instigator: %s Owner: %s"), (Instigator ? *Instigator->GetName() : TEXT("None")),
		(Owner ? *Owner->GetName() : TEXT("None"))), 4,YPos);
	YPos += YL;

	static FName NAME_Animation(TEXT("Animation"));
	static FName NAME_Bones = FName(TEXT("Bones"));
	if (DebugDisplay.IsDisplayOn(NAME_Animation) || DebugDisplay.IsDisplayOn(NAME_Bones))
	{
		TArray<USkeletalMeshComponent*> Components;
		GetComponents(Components);

		if (DebugDisplay.IsDisplayOn(NAME_Animation))
		{
			for (USkeletalMeshComponent* Comp : Components)
			{
				UAnimInstance* AnimInstance = Comp->GetAnimInstance();
				if (AnimInstance)
				{
					AnimInstance->DisplayDebug(Canvas, DebugDisplay, YL, YPos);
				}
			}
		}

		static FName NAME_3DBones = FName(TEXT("3DBones"));
		if (DebugDisplay.IsDisplayOn(NAME_Bones))
		{
			bool bSimpleBones = !DebugDisplay.IsCategoryToggledOn(NAME_3DBones, false);
			for (USkeletalMeshComponent* Comp : Components)
			{
				Comp->DebugDrawBones(Canvas, bSimpleBones);
			}
		}
	}
}

void AActor::OutsideWorldBounds()
{
	Destroy();
}

bool AActor::CanBeBaseForCharacter(APawn* APawn) const
{
	return true;
}

void AActor::BecomeViewTarget( APlayerController* PC )
{
	K2_OnBecomeViewTarget(PC);
}

void AActor::EndViewTarget( APlayerController* PC )
{
	K2_OnEndViewTarget(PC);
}

APawn* AActor::GetInstigator() const
{
	return Instigator;
}

AController* AActor::GetInstigatorController() const
{
	return Instigator ? Instigator->Controller : NULL;
}

void AActor::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (bFindCameraComponentWhenViewTarget)
	{
		// Look for the first active camera component and use that for the view
		TArray<UCameraComponent*> Cameras;
		GetComponents<UCameraComponent>(/*out*/ Cameras);

		for (UCameraComponent* CameraComponent : Cameras)
		{
			if (CameraComponent->bIsActive)
			{
				CameraComponent->GetCameraView(DeltaTime, OutResult);
				return;
			}
		}
	}

	GetActorEyesViewPoint(OutResult.Location, OutResult.Rotation);
}

void AActor::ForceNetRelevant()
{
	if ( !NeedsLoadForClient() )
	{
		UE_LOG(LogSpawn, Warning, TEXT("ForceNetRelevant called for actor that doesn't load on client: %s" ), *GetFullName() );
		return;
	}

	if (RemoteRole == ROLE_None)
	{
		SetReplicates(true);
		bAlwaysRelevant = true;
		if (NetUpdateFrequency == 0.f)
		{
			NetUpdateFrequency = 0.1f;
		}
	}
	ForceNetUpdate();
}

void AActor::GetActorEyesViewPoint( FVector& OutLocation, FRotator& OutRotation ) const
{
	OutLocation = GetActorLocation();
	OutRotation = GetActorRotation();
}

enum ECollisionResponse AActor::GetComponentsCollisionResponseToChannel(enum ECollisionChannel Channel) const
{
	ECollisionResponse OutResponse = ECR_Ignore;

	TArray<UPrimitiveComponent*> Components;
	GetComponents(Components);

	for (int32 i = 0; i < Components.Num(); i++)
	{
		UPrimitiveComponent* Primitive = Components[i];
		if ( Primitive->IsCollisionEnabled() )
		{
			// find Max of the response, blocking > overlapping > ignore
			OutResponse = FMath::Max(Primitive->GetCollisionResponseToChannel(Channel), OutResponse);
		}
	}

	return OutResponse;
};

void AActor::AddOwnedComponent(UActorComponent* Component)
{
	check(Component->GetOwner() == this);
	OwnedComponents.AddUnique(Component);

	if (Component->GetIsReplicated())
	{
		ReplicatedComponents.AddUnique(Component);
	}
}

void AActor::RemoveOwnedComponent(UActorComponent* Component)
{
	if (OwnedComponents.RemoveSwap(Component) == 0)
	{
		// If we didn't remove something we expected to then it probably got NULL through the
		// property system so take the time to pull them out now
		OwnedComponents.RemoveSwap(NULL);
	}

	ReplicatedComponents.RemoveSwap(Component);
}

#if DO_CHECK
bool AActor::OwnsComponent(UActorComponent* Component) const
{
	return OwnedComponents.Contains(Component);
}
#endif

const TArray<UActorComponent*>& AActor::GetReplicatedComponents() const
{
	return ReplicatedComponents;
}

void AActor::UpdateReplicatedComponent(UActorComponent* Component)
{
	check(Component->GetOwner() == this);
	if (Component->GetIsReplicated())
	{
		ReplicatedComponents.AddUnique(Component);
	}
	else
	{
		ReplicatedComponents.RemoveSwap(Component);
	}
}

UActorComponent* AActor::FindComponentByClass(const TSubclassOf<UActorComponent> ComponentClass) const
{
	UActorComponent* FoundComponent = NULL;
	for (UActorComponent* Component : OwnedComponents)
	{
		if (Component && Component->IsA(ComponentClass))
		{
			FoundComponent = Component;
			break;
		}
	}

	return FoundComponent;
}

UActorComponent* AActor::GetComponentByClass(TSubclassOf<UActorComponent> ComponentClass)
{
	UActorComponent* FoundComponent = NULL;
	for (UActorComponent* Component : OwnedComponents)
	{
		if (Component && Component->IsA(ComponentClass))
		{
			FoundComponent = Component;
			break;
		}
	}

	return FoundComponent;
}

TArray<UActorComponent*> AActor::GetComponentsByClass(TSubclassOf<UActorComponent> ComponentClass) const
{
	if (ComponentClass == UActorComponent::StaticClass())
	{
		return OwnedComponents;
	}

	TArray<UActorComponent*> ValidComponents;
	if (*ComponentClass)
	{
		for (UActorComponent* Component : OwnedComponents)
		{
			if (Component && Component->IsA(ComponentClass))
			{
				ValidComponents.Add(Component);
			}
		}
	}
	
	return ValidComponents;
}

void AActor::DisableComponentsSimulatePhysics()
{
	TArray<UPrimitiveComponent*> Components;
	GetComponents(Components);

	for (UPrimitiveComponent* Component : Components)
	{
		Component->SetSimulatePhysics(false);
	}
}

void AActor::PostRegisterAllComponents() 
{
}

/** Util to call OnComponentCreated on components */
static void DispatchOnComponentsCreated(AActor* NewActor)
{
	TArray<UActorComponent*> Components;
	NewActor->GetComponents(Components);

	for (int32 Idx = 0; Idx < Components.Num(); Idx++)
	{
		UActorComponent* ActorComp = Components[Idx];
		if (ActorComp != NULL)
		{
			ActorComp->OnComponentCreated();
		}
	}
}

void AActor::PostSpawnInitialize(FVector const& SpawnLocation, FRotator const& SpawnRotation, AActor* InOwner, APawn* InInstigator, bool bRemoteOwned, bool bNoFail, bool bDeferConstruction)
{
	// General flow here is like so
	// - Actor sets up the basics.
	// - Actor gets PreInitializeComponents()
	// - Actor constructs itself, after which its components should be fully assembled
	// - Actor components get OnComponentCreated
	// - Actor components get InitializeComponent
	// - Actor gets PostInitializeComponents() once everything is set up
	//
	// This should be the same sequence for deferred or nondeferred spawning.

	// It's not safe to call UWorld accessor functions till the world info has been spawned.
	bool const bActorsInitialized = GetWorld() && GetWorld()->AreActorsInitialized();

	CreationTime = GetWorld()->GetTimeSeconds();

	// Set network role.
	check(Role == ROLE_Authority);
	ExchangeNetRoles(bRemoteOwned);

	// Set the actor's location and rotation.
	if (GetRootComponent() != NULL)
	{
		GetRootComponent()->SetWorldLocationAndRotation(SpawnLocation, SpawnRotation);
	}

	// Call OnComponentCreated on all default (native) components
	DispatchOnComponentsCreated(this);

	// Initialize the actor's components.
	RegisterAllComponents();

	// Set owner.
	SetOwner(InOwner);

	// Set instigator
	Instigator = InInstigator;

#if WITH_EDITOR
	// When placing actors in the editor, init any random streams 
	if (!bActorsInitialized)
	{
		SeedAllRandomStreams();
	}
#endif

	// See if anything has deleted us
	if( IsPendingKill() && !bNoFail )
	{
		return;
	}

	// Send messages. We've fully spawned
	PostActorCreated();

	// Executes native and BP construction scripts.
	// After this, we can assume all components are created and assembled.
	if (!bDeferConstruction)
	{
		// Preserve original root component scale
		const FVector SpawnScale = GetRootComponent() ? GetRootComponent()->RelativeScale3D : FVector(1.0f, 1.0f, 1.0f);
		FinishSpawning(FTransform(SpawnRotation, SpawnLocation, SpawnScale), true);
	}
}

void AActor::FinishSpawning(const FTransform& Transform, bool bIsDefaultTransform)
{
	if (ensure(!bHasFinishedSpawning))
	{
		bHasFinishedSpawning = true;

		ExecuteConstruction(Transform, nullptr, bIsDefaultTransform);
		PostActorConstruction();
	}
}

void AActor::PostActorConstruction()
{
	bool const bActorsInitialized = GetWorld() && GetWorld()->AreActorsInitialized();

	if (bActorsInitialized)
	{
		PreInitializeComponents();
	}

	// components are all there, init overlapping state
	UpdateOverlaps();

	// If this is dynamically spawned replicted actor, defer calls to BeginPlay and UpdateOverlaps until replicated properties are deserialized
	bool deferBeginPlayAndUpdateOverlaps = (bExchangedRoles && RemoteRole == ROLE_Authority);

	if (bActorsInitialized)
	{
		// Call InitializeComponent on components
		InitializeComponents();

		PostInitializeComponents();
		if (!bActorInitialized && !IsPendingKill())
		{
			UE_LOG(LogActor, Fatal, TEXT("%s failed to route PostInitializeComponents.  Please call Super::PostInitializeComponents() in your <className>::PostInitializeComponents() function. "), *GetFullName() );
		}

		if (GetWorld()->HasBegunPlay() && !deferBeginPlayAndUpdateOverlaps)
		{
			BeginPlay();
		}
	}
	else
	{
		// Set IsPendingKill() to true so that when the initial undo record is made,
		// the actor will be treated as destroyed, in that undo an add will
		// actually work
		SetFlags(RF_PendingKill);
		Modify(false);
		ClearFlags(RF_PendingKill);
	}

	// Components are all there and we've begun play, init overlapping state
	if (!deferBeginPlayAndUpdateOverlaps)
	{
		UpdateOverlaps();
	}

	// Notify the texture streaming manager about the new actor.
	IStreamingManager::Get().NotifyActorSpawned(this);
}

void AActor::SetReplicates(bool bInReplicates)
{ 
	if (Role == ROLE_Authority || bInReplicates == false)
	{
		if (bReplicates == false && bInReplicates == true && GetWorld() != NULL)		// GetWorld will return NULL on CDO, FYI
		{
			GetWorld()->AddNetworkActor(this);
		}

		RemoteRole = (bInReplicates ? ROLE_SimulatedProxy : ROLE_None);
		bReplicates = bInReplicates;
	}
	else
	{
		UE_LOG(LogActor, Warning, TEXT("SetReplicates called on actor '%s' that is not valid for having its role modified."), *GetName());
	}
}

void AActor::SetAutonomousProxy(bool bInAutonomousProxy)
{
	if (bReplicates)
	{
		RemoteRole = (bInAutonomousProxy ? ROLE_AutonomousProxy : ROLE_SimulatedProxy);
	}
	else
	{
		UE_LOG(LogActor, Warning, TEXT("SetAutonomousProxy called on a unreplicated actor '%s"), *GetName());
	}
}

void AActor::CopyRemoteRoleFrom(const AActor* CopyFromActor)
{
	RemoteRole = CopyFromActor->GetRemoteRole();
	if (RemoteRole != ROLE_None)
	{
		GetWorld()->AddNetworkActor(this);
	}
}

ENetRole AActor::GetRemoteRole() const
{
	return RemoteRole;
}

void AActor::PostNetInit()
{
	if(RemoteRole != ROLE_Authority)
	{
		UE_LOG(LogActor, Warning, TEXT("AActor::PostNetInit %s Remoterole: %d"), *GetName(), (int)RemoteRole);
	}
	check(RemoteRole == ROLE_Authority);

	if (GetWorld() && GetWorld()->HasBegunPlay())
	{
		BeginPlay();
	}

	UpdateOverlaps();
}

void AActor::ExchangeNetRoles(bool bRemoteOwned)
{
	if (!bExchangedRoles)
	{
		if (bRemoteOwned)
		{
			Exchange( Role, RemoteRole );
		}
		bExchangedRoles = true;
	}
}

void AActor::BeginPlay()
{
	SetLifeSpan( InitialLifeSpan );

	ReceiveBeginPlay();
}

void AActor::EnableInput(APlayerController* PlayerController)
{
	if (PlayerController)
	{
		// If it doesn't exist create it and bind delegates
		if (!InputComponent)
		{
			InputComponent = ConstructObject<UInputComponent>(UInputComponent::StaticClass(), this);
			InputComponent->RegisterComponent();
			InputComponent->bBlockInput = bBlockInput;
			InputComponent->Priority = InputPriority;

			// Only do this if this actor is of a blueprint class
			UBlueprintGeneratedClass* BGClass = Cast<UBlueprintGeneratedClass>(GetClass());
			if(BGClass != NULL)
			{
				UInputDelegateBinding::BindInputDelegates(BGClass, InputComponent);
			}
		}
		else
		{
			// Make sure we only have one instance of the InputComponent on the stack
			PlayerController->PopInputComponent(InputComponent);
		}

		PlayerController->PushInputComponent(InputComponent);
	}
}

void AActor::DisableInput(APlayerController* PlayerController)
{
	if (InputComponent)
	{
		if (PlayerController)
		{
			PlayerController->PopInputComponent(InputComponent);
		}
		else
		{
			for (FConstPlayerControllerIterator PCIt = GetWorld()->GetPlayerControllerIterator(); PCIt; ++PCIt)
			{
				(*PCIt)->PopInputComponent(InputComponent);
			}
		}
	}
}

float AActor::GetInputAxisValue(const FName InputAxisName) const
{
	float Value = 0.f;

	if (InputComponent)
	{
		Value = InputComponent->GetAxisValue(InputAxisName);
	}

	return Value;
}

float AActor::GetInputAxisKeyValue(const FKey InputAxisKey) const
{
	float Value = 0.f;

	if (InputComponent)
	{
		Value = InputComponent->GetAxisKeyValue(InputAxisKey);
	}

	return Value;
}

FVector AActor::GetInputVectorAxisValue(const FKey InputAxisKey) const
{
	FVector Value;

	if (InputComponent)
	{
		Value = InputComponent->GetVectorAxisValue(InputAxisKey);
	}

	return Value;
}

bool AActor::SetActorLocation(const FVector& NewLocation, bool bSweep, FHitResult* OutSweepHitResult)
{
	if (RootComponent)
	{
		const FVector Delta = NewLocation - GetActorLocation();
		return RootComponent->MoveComponent( Delta, GetActorRotation(), bSweep, OutSweepHitResult );
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}

	return false;
}

bool AActor::SetActorRotation(FRotator NewRotation)
{
	if (RootComponent)
	{
		return RootComponent->MoveComponent( FVector::ZeroVector, NewRotation, true );
	}

	return false;
}

bool AActor::SetActorLocationAndRotation(FVector NewLocation, FRotator NewRotation, bool bSweep, FHitResult* OutSweepHitResult)
{
	if (RootComponent)
	{
		const FVector Delta = NewLocation - GetActorLocation();
		return RootComponent->MoveComponent( Delta, NewRotation, bSweep, OutSweepHitResult );
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}

	return false;
}

void AActor::SetActorScale3D(const FVector& NewScale3D)
{
	if (RootComponent)
	{
		RootComponent->SetWorldScale3D(NewScale3D);
	}
}


FVector AActor::GetActorScale3D() const
{
	if (RootComponent)
	{
		return RootComponent->GetComponentScale();
	}
	return FVector(1,1,1);
}

void AActor::AddActorWorldOffset(FVector DeltaLocation, bool bSweep, FHitResult* OutSweepHitResult)
{
	if (RootComponent)
	{
		RootComponent->AddWorldOffset(DeltaLocation, bSweep, OutSweepHitResult);
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}
}

void AActor::AddActorWorldRotation(FRotator DeltaRotation, bool bSweep, FHitResult* OutSweepHitResult)
{
	if (RootComponent)
	{
		RootComponent->AddWorldRotation(DeltaRotation, bSweep, OutSweepHitResult);
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}
}

void AActor::AddActorWorldTransform(const FTransform& DeltaTransform, bool bSweep, FHitResult* OutSweepHitResult)
{
	if (RootComponent)
	{
		RootComponent->AddWorldTransform(DeltaTransform, bSweep, OutSweepHitResult);
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}
}


bool AActor::SetActorTransform(const FTransform& NewTransform, bool bSweep, FHitResult* OutSweepHitResult)
{
	// we have seen this gets NAN from kismet, and would like to see if this
	// happens, and if so, something else is giving NAN as output
	if (RootComponent)
	{
		if (ensure(!NewTransform.ContainsNaN()))
		{
			RootComponent->SetWorldTransform(NewTransform, bSweep, OutSweepHitResult);
		}
		else
		{
			UE_LOG(LogScript, Warning, TEXT("SetActorTransform: Get NAN Transform data for %s: %s"), *GetNameSafe(this), *NewTransform.ToString());
			if (OutSweepHitResult)
			{
				*OutSweepHitResult = FHitResult();
			}
		}
		return true;
	}

	if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}
	return false;
}

void AActor::AddActorLocalOffset(FVector DeltaLocation, bool bSweep, FHitResult* OutSweepHitResult)
{
	if(RootComponent)
	{
		RootComponent->AddLocalOffset(DeltaLocation, bSweep, OutSweepHitResult);
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}
}

void AActor::AddActorLocalRotation(FRotator DeltaRotation, bool bSweep, FHitResult* OutSweepHitResult)
{
	if(RootComponent)
	{
		RootComponent->AddLocalRotation(DeltaRotation, bSweep, OutSweepHitResult);
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}
}

void AActor::AddActorLocalTransform(const FTransform& NewTransform, bool bSweep, FHitResult* OutSweepHitResult)
{
	if(RootComponent)
	{
		RootComponent->AddLocalTransform(NewTransform, bSweep, OutSweepHitResult);
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}
}

void AActor::SetActorRelativeLocation(FVector NewRelativeLocation, bool bSweep, FHitResult* OutSweepHitResult)
{
	if (RootComponent)
	{
		RootComponent->SetRelativeLocation(NewRelativeLocation, bSweep, OutSweepHitResult);
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}
}

void AActor::SetActorRelativeRotation(FRotator NewRelativeRotation, bool bSweep, FHitResult* OutSweepHitResult)
{
	if (RootComponent)
	{
		RootComponent->SetRelativeRotation(NewRelativeRotation, bSweep, OutSweepHitResult);
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}
}

void AActor::SetActorRelativeTransform(const FTransform& NewRelativeTransform, bool bSweep, FHitResult* OutSweepHitResult)
{
	if (RootComponent)
	{
		RootComponent->SetRelativeTransform(NewRelativeTransform, bSweep, OutSweepHitResult);
	}
	else if (OutSweepHitResult)
	{
		*OutSweepHitResult = FHitResult();
	}
}

void AActor::SetActorRelativeScale3D(FVector NewRelativeScale)
{
	if (RootComponent)
	{
		if (NewRelativeScale.ContainsNaN())
		{
			FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("InvalidScale", "Scale '{0}' is not valid."), FText::FromString(NewRelativeScale.ToString())));
			return;
		}

		RootComponent->SetRelativeScale3D(NewRelativeScale);
	}
}

FVector AActor::GetActorRelativeScale3D() const
{
	if (RootComponent)
	{
		return RootComponent->RelativeScale3D;
	}
	return FVector(1,1,1);
}

void AActor::SetActorHiddenInGame( bool bNewHidden )
{
	if (bHidden != bNewHidden)
	{
		bHidden = bNewHidden;
		MarkComponentsRenderStateDirty();
	}
}

void AActor::SetActorEnableCollision(bool bNewActorEnableCollision)
{
	if(bActorEnableCollision != bNewActorEnableCollision)
	{
		bActorEnableCollision = bNewActorEnableCollision;

		// Notify components about the change
		TArray<UActorComponent*> Components;
		GetComponents(Components);

		for(int32 CompIdx=0; CompIdx<Components.Num(); CompIdx++)
		{
			Components[CompIdx]->OnActorEnableCollisionChanged();
		}
	}
}


bool AActor::GetActorEnableCollision()
{
	return bActorEnableCollision;
}

bool AActor::Destroy( bool bNetForce, bool bShouldModifyLevel )
{
	// It's already pending kill, no need to beat the corpse
	if (!IsPendingKill())
	{
		UWorld* World = GetWorld();
		if (World)
		{
			World->DestroyActor( this, bNetForce, bShouldModifyLevel );
		}
		else
		{
			UE_LOG(LogSpawn, Warning, TEXT("Destroying %s, which doesn't have a valid world pointer"), *GetPathName());
		}
	}
	return IsPendingKill();
}

void AActor::K2_DestroyActor()
{
	Destroy();
}

bool AActor::HasAuthority() const
{
	return (Role == ROLE_Authority);
}

void AActor::K2_DestroyComponent(UActorComponent* Component)
{
	// If its a valid component, and we own it, destroy it
	if(Component && Component->GetOwner() == this)
	{
		Component->DestroyComponent();
	}
}

UPrimitiveComponent* AActor::GetRootPrimitiveComponent() const
{ 
	return Cast<class UPrimitiveComponent>(RootComponent); 
}

bool AActor::SetRootComponent(class USceneComponent* NewRootComponent)
{
	/** Only components owned by this actor can be used as a its root component. */
	if (ensure(NewRootComponent == NULL || NewRootComponent->GetOwner() == this))
	{
		RootComponent = NewRootComponent;
		return true;
	}

	return false;
}

/** K2 exposed 'get location' */
FVector AActor::K2_GetActorLocation() const
{
	return GetActorLocation();
}

/** K2 exposed 'get rotation' */
FRotator AActor::K2_GetActorRotation() const
{
	return GetActorRotation();
}

FVector AActor::GetActorForwardVector() const
{
	return GetTransform().GetUnitAxis(EAxis::X);
}

FVector AActor::GetActorUpVector() const
{
	return GetTransform().GetUnitAxis(EAxis::Z);
}

FVector AActor::GetActorRightVector() const
{
	return GetTransform().GetUnitAxis(EAxis::Y);
}

USceneComponent* AActor::K2_GetRootComponent() const
{
	return GetRootComponent();
}

void AActor::GetActorBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent) const
{
	const FBox Bounds = GetComponentsBoundingBox(!bOnlyCollidingComponents);

	// To keep consistency with the other GetBounds functions, transform our result into an origin / extent formatting
	Bounds.GetCenterAndExtents(Origin, BoxExtent);
}


AWorldSettings * AActor::GetWorldSettings() const
{
	UWorld* World = GetWorld();
	return (World ? World->GetWorldSettings() : nullptr);
}

void AActor::PlaySoundOnActor(USoundCue* InSoundCue, float VolumeMultiplier/*=1.f*/, float PitchMultiplier/*=1.f*/)
{
	UGameplayStatics::PlaySoundAtLocation( this, InSoundCue, GetActorLocation(), VolumeMultiplier, PitchMultiplier );
}

void AActor::PlaySoundAtLocation(USoundCue* InSoundCue, FVector SoundLocation, float VolumeMultiplier/*=1.f*/, float PitchMultiplier/*=1.f*/)
{
	UGameplayStatics::PlaySoundAtLocation( this, InSoundCue, (SoundLocation.IsZero() ? GetActorLocation() : SoundLocation), VolumeMultiplier, PitchMultiplier );
}

ENetMode AActor::GetNetMode() const
{
	UNetDriver *NetDriver = GetNetDriver();
	if (NetDriver != NULL)
	{
		return NetDriver->GetNetMode();
	}

	UWorld* World = GetWorld();
	if (World != NULL && World->DemoNetDriver != NULL)
	{
		return World->DemoNetDriver->GetNetMode();
	}

	return NM_Standalone;
}

UNetDriver* AActor::GetNetDriver() const
{
	UWorld *World = GetWorld();
	if (NetDriverName == NAME_GameNetDriver)
	{
		return World->GetNetDriver();
	}

	return GEngine->FindNamedNetDriver(World, NetDriverName);
}

//
// Return whether a function should be executed remotely.
//
int32 AActor::GetFunctionCallspace( UFunction* Function, void* Parameters, FFrame* Stack )
{
	// Quick reject 1.
	if ((Function->FunctionFlags & FUNC_Static))
	{
		// Call local
		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Local1: %s"), *Function->GetName());
		return FunctionCallspace::Local;
	}

	if (GAllowActorScriptExecutionInEditor)
	{
		// Call local
		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Local2: %s"), *Function->GetName());
		return FunctionCallspace::Local;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		// Call local
		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Local3: %s"), *Function->GetName());
		return FunctionCallspace::Local;
	}

	// If we are on a client and function is 'skip on client', absorb it
	FunctionCallspace::Type Callspace = (Role < ROLE_Authority) && Function->HasAllFunctionFlags(FUNC_BlueprintAuthorityOnly) ? FunctionCallspace::Absorbed : FunctionCallspace::Local;
	
	if (IsPendingKill())
	{
		// Never call remote on a pending kill actor. 
		// We can call it local or absorb it depending on authority/role check above.
		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace: IsPendingKill %s %s"), *Function->GetName(), FunctionCallspace::ToString(Callspace));
		return Callspace;
	}

	if (Function->FunctionFlags & FUNC_NetRequest)
	{
		// Call remote
		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace NetRequest: %s"), *Function->GetName());
		return FunctionCallspace::Remote;
	}	
	
	if (Function->FunctionFlags & FUNC_NetResponse)
	{
		if (Function->RPCId > 0)
		{
			// Call local
			DEBUG_CALLSPACE(TEXT("GetFunctionCallspace NetResponse Local: %s"), *Function->GetName());
			return FunctionCallspace::Local;
		}

		// Shouldn't happen, so skip call
		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace NetResponse Absorbed: %s"), *Function->GetName());
		return FunctionCallspace::Absorbed;
	}

	const ENetMode NetMode = GetNetMode();
	// Quick reject 2. Has to be a network game to continue
	if (NetMode == NM_Standalone)
	{
		if (Role < ROLE_Authority && (Function->FunctionFlags & FUNC_NetServer))
		{
			// Don't let clients call server functions (in edge cases where NetMode is standalone (net driver is null))
			DEBUG_CALLSPACE(TEXT("GetFunctionCallspace No Authority Server Call Absorbed: %s"), *Function->GetName());
			return FunctionCallspace::Absorbed;
		}

		// Call local
		return FunctionCallspace::Local;
	}
	
	// Dedicated servers don't care about "cosmetic" functions.
	if (NetMode == NM_DedicatedServer && Function->HasAllFunctionFlags(FUNC_BlueprintCosmetic))
	{
		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Blueprint Cosmetic Absorbed: %s"), *Function->GetName());
		return FunctionCallspace::Absorbed;
	}

	if (!(Function->FunctionFlags & FUNC_Net))
	{
		// Not a network function
		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Not Net: %s %s"), *Function->GetName(), FunctionCallspace::ToString(Callspace));
		return Callspace;
	}
	
	bool bIsServer = NetMode == NM_ListenServer || NetMode == NM_DedicatedServer;

	// get the top most function
	while (Function->GetSuperFunction() != NULL)
	{
		Function = Function->GetSuperFunction();
	}

	if ((Function->FunctionFlags & FUNC_NetMulticast))
	{
		if(bIsServer)
		{
			// Server should execute locally and call remotely
			if (RemoteRole != ROLE_None)
			{
				DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Multicast: %s"), *Function->GetName());
				return (FunctionCallspace::Local | FunctionCallspace::Remote);
			}

			DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Multicast NoRemoteRole: %s"), *Function->GetName());
			return FunctionCallspace::Local;
		}
		else
		{
			// Client should only execute locally iff it is allowed to (function is not KismetAuthorityOnly)
			DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Multicast Client: %s %s"), *Function->GetName(), FunctionCallspace::ToString(Callspace));
			return Callspace;
		}
	}

	// if we are the server, and it's not a send-to-client function,
	if (bIsServer && !(Function->FunctionFlags & FUNC_NetClient))
	{
		// don't replicate
		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Server calling Server function: %s %s"), *Function->GetName(), FunctionCallspace::ToString(Callspace));
		return Callspace;
	}
	// if we aren't the server, and it's not a send-to-server function,
	if (!bIsServer && !(Function->FunctionFlags & FUNC_NetServer))
	{
		// don't replicate
		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Client calling Client function: %s %s"), *Function->GetName(), FunctionCallspace::ToString(Callspace));
		return Callspace;
	}

	// Check if the actor can potentially call remote functions	
	if (Role == ROLE_Authority)
	{
		UNetConnection* NetConnection = GetNetConnection();
		if (NetConnection == NULL)
		{
			UPlayer *ClientPlayer = GetNetOwningPlayer();
			if (ClientPlayer == NULL)
			{
				// Check if a player ever owned this (topmost owner is playercontroller or beacon)
				if (HasNetOwner())
				{
					// Network object with no owning player, we must absorb
					DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Client without owner absorbed %s"), *Function->GetName());
					return FunctionCallspace::Absorbed;
				}
				
				// Role authority object calling a client RPC locally (ie AI owned objects)
				DEBUG_CALLSPACE(TEXT("GetFunctionCallspace authority non client owner %s %s"), *Function->GetName(), FunctionCallspace::ToString(Callspace));
				return Callspace;
			}
			else if (Cast<ULocalPlayer>(ClientPlayer) != NULL)
			{
				// This is a local player, call locally
				DEBUG_CALLSPACE(TEXT("GetFunctionCallspace Client local function: %s %s"), *Function->GetName(), FunctionCallspace::ToString(Callspace));
				return Callspace;
			}
		}
		else if (!NetConnection->Driver || !NetConnection->Driver->World)
		{
			// NetDriver does not have a world, most likely shutting down
			DEBUG_CALLSPACE(TEXT("GetFunctionCallspace NetConnection with no driver or world absorbed: %s %s %s"),
				*Function->GetName(), 
				NetConnection->Driver ? *NetConnection->Driver->GetName() : TEXT("NoNetDriver"),
				NetConnection->Driver && NetConnection->Driver->World ? *NetConnection->Driver->World->GetName() : TEXT("NoWorld"));
			return FunctionCallspace::Absorbed;
		}

		// There is a valid net connection, so continue and call remotely
	}

	// about to call remotely - unless the actor is not actually replicating
	if (RemoteRole == ROLE_None)
	{
		if (!bIsServer)
		{
			UE_LOG(LogNet, Warning, TEXT("Client is absorbing remote function %s on actor %s because RemoteRole is ROLE_None"), *Function->GetName(), *GetName() );
		}

		DEBUG_CALLSPACE(TEXT("GetFunctionCallspace RemoteRole None absorbed %s"), *Function->GetName());
		return FunctionCallspace::Absorbed;
	}

	// Call remotely
	DEBUG_CALLSPACE(TEXT("GetFunctionCallspace RemoteRole Remote %s"), *Function->GetName());
	return FunctionCallspace::Remote;
}

bool AActor::CallRemoteFunction( UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack )
{
	UNetDriver* NetDriver = GetNetDriver();
	if (NetDriver)
	{
		NetDriver->ProcessRemoteFunction(this, Function, Parameters, OutParms, Stack, NULL);
		return true;
	}

	return false;
}

void AActor::DispatchPhysicsCollisionHit(const FRigidBodyCollisionInfo& MyInfo, const FRigidBodyCollisionInfo& OtherInfo, const FCollisionImpactData& RigidCollisionData)
{
#if 0
	if(true)
	{
		FString MyName = MyInfo.Component ? FString(*MyInfo.Component->GetPathName()) : FString(TEXT(""));
		FString OtherName = OtherInfo.Component ? FString(*OtherInfo.Component->GetPathName()) : FString(TEXT(""));
		UE_LOG(LogPhysics, Log,  TEXT("COLLIDE! %s - %s"), *MyName, *OtherName );
	}
#endif

	checkSlow(RigidCollisionData.ContactInfos.Num() > 0);

	// @todo At the moment we only pass the first contact in the ContactInfos array. Maybe improve this?
	const FRigidBodyContactInfo& ContactInfo = RigidCollisionData.ContactInfos[0];

	FHitResult Result;
	Result.Location = Result.ImpactPoint = ContactInfo.ContactPosition;
	Result.Normal = Result.ImpactNormal = ContactInfo.ContactNormal;
	Result.PhysMaterial = ContactInfo.PhysMaterial[1];
	Result.Actor = OtherInfo.Actor;
	Result.Component = OtherInfo.Component;
	Result.Item = MyInfo.BodyIndex;
	Result.BoneName = MyInfo.BoneName;
	Result.bBlockingHit = true;

	ReceiveHit(MyInfo.Component.Get(), OtherInfo.Actor.Get(), OtherInfo.Component.Get(), true, Result.Location, Result.Normal, RigidCollisionData.TotalNormalImpulse, Result);

	// Execute delegates if bound

	if(OnActorHit.IsBound())
	{
		OnActorHit.Broadcast(this, OtherInfo.Actor.Get(), RigidCollisionData.TotalNormalImpulse, Result);
	}

	if(MyInfo.Component.IsValid() && MyInfo.Component.Get()->OnComponentHit.IsBound())
	{
		MyInfo.Component.Get()->OnComponentHit.Broadcast(OtherInfo.Actor.Get(), MyInfo.Component.Get(), OtherInfo.Component.Get(), RigidCollisionData.TotalNormalImpulse, Result);
	}
}

// COMPONENTS


void AActor::UnregisterAllComponents()
{
	TArray<UActorComponent*> Components;
	GetComponents(Components);

	for(int32 CompIdx = 0; CompIdx < Components.Num(); CompIdx++)
	{
		UActorComponent* Component = Components[CompIdx]; 
		if( Component->IsRegistered()) // In some cases unregistering one component can unregister another, so we do a check here to avoid trying twice
		{
			Component->UnregisterComponent();
		}
	}

	PostUnregisterAllComponents();
}

void AActor::RegisterAllComponents()
{
	// 0 - means register all components
	verify(IncrementalRegisterComponents(0));
}

// Walks through components hierarchy and returns closest to root parent component that is unregistered
// Only for components that belong to the same owner
static USceneComponent* GetUnregisteredParent(UActorComponent* Component)
{
	USceneComponent* ParentComponent = nullptr;
	USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
	
	while (	SceneComponent && 
			SceneComponent->AttachParent && 
			SceneComponent->AttachParent->GetOwner() == Component->GetOwner() && 
			!SceneComponent->AttachParent->IsRegistered())
	{
		SceneComponent = SceneComponent->AttachParent;
		if (SceneComponent->bAutoRegister && !SceneComponent->IsPendingKill())
		{
			// We found unregistered parent that should be registered
			// But keep looking up the tree
			ParentComponent = SceneComponent;
		}
	}

	return ParentComponent;
}

bool AActor::IncrementalRegisterComponents(int32 NumComponentsToRegister)
{
	if (NumComponentsToRegister == 0)
	{
		// 0 - means register all components
		NumComponentsToRegister = MAX_int32;
	}
	
	// Register RootComponent first so all other components can reliable use it (ie call GetLocation) when they register
	if( RootComponent != NULL && !RootComponent->IsRegistered() )
	{
		// An unregistered root component is bad news
		check(RootComponent->bAutoRegister);

		//Before we register our component, save it to our transaction buffer so if "undone" it will return to an unregistered state.
		//This should prevent unwanted components hanging around when undoing a copy/paste or duplication action.
		RootComponent->Modify(false);

		check(GetWorld());
		RootComponent->RegisterComponentWithWorld(GetWorld());
	}

	int32 NumTotalRegisteredComponents = 0;
	int32 NumRegisteredComponentsThisRun = 0;
	TArray<UActorComponent*> Components;
	GetComponents(Components);
	
	for (int32 CompIdx = 0; CompIdx < Components.Num() && NumRegisteredComponentsThisRun < NumComponentsToRegister; CompIdx++)
	{
		UActorComponent* Component = Components[CompIdx];
		if(!Component->IsRegistered() && Component->bAutoRegister && !Component->IsPendingKill())
		{
			// Ensure that all parent are registered first
			USceneComponent* ParentComponent = GetUnregisteredParent(Component);
			if (ParentComponent)
			{
				// Register parent first, then return to this component on a next iteration
				Component = ParentComponent;
				CompIdx--;
				NumTotalRegisteredComponents--; // because we will try to register the parent again later...
			}
				
			//Before we register our component, save it to our transaction buffer so if "undone" it will return to an unregistered state.
			//This should prevent unwanted components hanging around when undoing a copy/paste or duplication action.
			Component->Modify(false);

			check(GetWorld());
			Component->RegisterComponentWithWorld(GetWorld());
			NumRegisteredComponentsThisRun++;
		}

		NumTotalRegisteredComponents++;
	}

	// See whether we are done
	if (Components.Num() == NumTotalRegisteredComponents)
	{
		// Finally, call PostRegisterAllComponents
		PostRegisterAllComponents();
		return true;
	}
	
	// Still have components to register
	return false;
}

bool AActor::HasValidRootComponent()
{ 
	return (RootComponent != NULL && RootComponent->IsRegistered()); 
}

void AActor::MarkComponentsAsPendingKill()
{
	// Iterate components and mark them all as pending kill.
	TArray<UActorComponent*> Components;
	GetComponents(Components);

	for (int32 Index = 0; Index < Components.Num(); Index++)
	{
		UActorComponent* Component = Components[Index];

		// Modify component so undo/ redo works in the editor.
		if( GIsEditor )
		{
			Component->Modify();
		}
		Component->OnComponentDestroyed();
		Component->MarkPendingKill();
	}
}

void AActor::ReregisterAllComponents()
{
	UnregisterAllComponents();
	RegisterAllComponents();
}

void AActor::UpdateComponentTransforms()
{
	TArray<UActorComponent*> Components;
	GetComponents(Components);

	for (int32 Idx = 0; Idx < Components.Num(); Idx++)
	{
		UActorComponent* ActorComp = Components[Idx];
		if (ActorComp->IsRegistered())
		{
			ActorComp->UpdateComponentToWorld();
		}
	}
}

void AActor::MarkComponentsRenderStateDirty()
{
	TArray<UActorComponent*> Components;
	GetComponents(Components);

	for (int32 Idx = 0; Idx < Components.Num(); Idx++)
	{
		UActorComponent* ActorComp = Components[Idx];
		if (ActorComp->IsRegistered())
		{
			ActorComp->MarkRenderStateDirty();
		}
	}
}

void AActor::InitializeComponents()
{
	TArray<UActorComponent*> Components;
	GetComponents(Components);

	for (UActorComponent* ActorComp : Components)
	{
		if (ActorComp->bWantsInitializeComponent && ActorComp->IsRegistered())
		{
			ActorComp->InitializeComponent();
		}
	}
}

void AActor::UninitializeComponents()
{
	TArray<UActorComponent*> Components;
	GetComponents(Components);

	for (UActorComponent* ActorComp : Components)
	{
		if (ActorComp->bHasBeenInitialized)
		{
			ActorComp->UninitializeComponent();
		}
	}
}

void AActor::DrawDebugComponents(FColor const& BaseColor) const
{
	TArray<USceneComponent*> Components;
	GetComponents(Components);

	for(int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
	{
		USceneComponent const* const Component = Components[ComponentIndex]; 

			FVector const Loc = Component->GetComponentLocation();
			FRotator const Rot = Component->GetComponentRotation();

			// draw coord system at component loc
			DrawDebugCoordinateSystem(GetWorld(), Loc, Rot, 10.f);

			// draw line from me to my parent
			USceneComponent const* const ParentComponent = Cast<USceneComponent>(Component->AttachParent);
			if (ParentComponent)
			{
				DrawDebugLine(GetWorld(), ParentComponent->GetComponentLocation(), Loc, BaseColor);
			}

			// draw component name
			DrawDebugString(GetWorld(), Loc+FVector(0,0,32), *Component->GetName());
		}

}


void AActor::InvalidateLightingCacheDetailed(bool bTranslationOnly)
{
	TArray<UActorComponent*> Components;
	GetComponents(Components);

	for(int32 ComponentIndex = 0;ComponentIndex < Components.Num();ComponentIndex++)
	{
		UActorComponent* Component = Components[ComponentIndex]; 

		if(Component && Component->IsRegistered())
		{
			Component->InvalidateLightingCacheDetailed(true, bTranslationOnly);
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// Validate that we didn't change it during this action
	TArray<UActorComponent*> NewComponents;
	GetComponents(NewComponents);
	check(Components == NewComponents);
#endif
}

 // COLLISION

bool AActor::ActorLineTraceSingle(struct FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel, const struct FCollisionQueryParams& Params)
{
	OutHit = FHitResult(1.f);
	OutHit.TraceStart = Start;
	OutHit.TraceEnd = End;
	bool bHasHit = false;
	
	TArray<UPrimitiveComponent*> Components;
	GetComponents(Components);

	for (int32 ComponentIndex=0; ComponentIndex<Components.Num(); ComponentIndex++)
	{
		FHitResult HitResult;
		UPrimitiveComponent* Primitive = Components[ComponentIndex];
		if( Primitive->IsRegistered() && Primitive->IsCollisionEnabled() 
			&& (Primitive->GetCollisionResponseToChannel(TraceChannel) == ECollisionResponse::ECR_Block) 
			&& Primitive->LineTraceComponent(HitResult, Start, End, Params) )
		{
			// return closest hit
			if( HitResult.Time < OutHit.Time )
			{
				OutHit = HitResult;
				bHasHit = true;
			}
		}
	}

	return bHasHit;
}

float AActor::ActorGetDistanceToCollision(const FVector& Point, ECollisionChannel TraceChannel, FVector& ClosestPointOnCollision, UPrimitiveComponent** OutPrimitiveComponent) const
{
	ClosestPointOnCollision = Point;
	float ClosestPointDistance = -1.f;

	TArray<UPrimitiveComponent*> Components;
	GetComponents(Components);

	for (int32 ComponentIndex=0; ComponentIndex<Components.Num(); ComponentIndex++)
	{
		UPrimitiveComponent* Primitive = Components[ComponentIndex];
		if( Primitive->IsRegistered() && Primitive->IsCollisionEnabled() 
			&& (Primitive->GetCollisionResponseToChannel(TraceChannel) == ECollisionResponse::ECR_Block) )
		{
			FVector ClosestPoint;
			const float Distance = Primitive->GetDistanceToCollision(Point, ClosestPoint);

			if (Distance < 0.f)
			{
				// Invalid result, impossible to be better than ClosestPointDistance
				continue;
			}

			if( (ClosestPointDistance < 0.f) || (Distance < ClosestPointDistance) )
			{
				ClosestPointDistance = Distance;
				ClosestPointOnCollision = ClosestPoint;
				if( OutPrimitiveComponent )
				{
					*OutPrimitiveComponent = Primitive;
				}

				// If we're inside collision, we're not going to find anything better, so abort search we've got our best find.
				if( Distance <= KINDA_SMALL_NUMBER )
				{
					break;
				}
			}
		}
	}

	return ClosestPointDistance;
}


void AActor::LifeSpanExpired()
{
	Destroy();
}

void AActor::SetLifeSpan( float InLifespan )
{
	// Store the new value
	InitialLifeSpan = InLifespan;
	// Initialize a timer for the actors lifespan if there is one. Otherwise clear any existing timer
	if ((Role == ROLE_Authority || bTearOff) && !IsPendingKill())
	{
		if( InLifespan > 0.0f)
		{
			GetWorldTimerManager().SetTimer( this, &AActor::LifeSpanExpired, InLifespan );
		}
		else
		{
			GetWorldTimerManager().ClearTimer( this, &AActor::LifeSpanExpired );		
		}
	}
}

float AActor::GetLifeSpan() const
{
	// Timer remaining returns -1.0f if there is no such timer - return this as ZERO
	const float CurrentLifespan = GetWorldTimerManager().GetTimerRemaining( this, &AActor::LifeSpanExpired );
	return ( CurrentLifespan != -1.0f ) ? CurrentLifespan : 0.0f;
}

void AActor::PostInitializeComponents()
{
	if( !IsPendingKill() )
	{
		bActorInitialized = true;

		UNavigationSystem::OnActorRegistered(this);
	}
}

void AActor::PreInitializeComponents()
{
	if (AutoReceiveInput != EAutoReceiveInput::Disabled)
	{
		const int32 PlayerIndex = int32(AutoReceiveInput.GetValue()) - 1;

		APlayerController* PC = UGameplayStatics::GetPlayerController(this, PlayerIndex);
		if (PC)
		{
			EnableInput(PC);
		}
		else
		{
			GetWorld()->PersistentLevel->RegisterActorForAutoReceiveInput(this, PlayerIndex);
		}
	}
}

UWorld* AActor::K2_GetWorld() const
{
	// If an actor is pending kill we don't consider it to be in a world
	return (!IsPendingKill() ? GetLevel()->OwningWorld : NULL);
}

float AActor::GetActorTimeDilation() const
{
	// get actor custom time dilation
	// if you do slomo, that changes WorldSettings->TimeDilation
	// So multiply to get final TimeDilation
	return CustomTimeDilation*GetWorldSettings()->GetEffectiveTimeDilation();
}

UMaterialInstanceDynamic* AActor::MakeMIDForMaterial(class UMaterialInterface* Parent)
{
	// Deprecating this function. 
	// Please use PrimitiveComponent->CreateAndSetMaterialInstanceDynamic
	// OR PrimitiveComponent->CreateAndSetMaterialInstanceDynamicFromMaterial
	// OR UMaterialInstanceDynamic::Create

	return NULL;
}

float AActor::GetDistanceTo(AActor* OtherActor)
{
	return OtherActor ? (GetActorLocation() - OtherActor->GetActorLocation()).Size() : 0.f;
}

float AActor::GetHorizontalDistanceTo(AActor* OtherActor)
{
	return OtherActor ? (GetActorLocation() - OtherActor->GetActorLocation()).Size2D() : 0.f;
}

float AActor::GetVerticalDistanceTo(AActor* OtherActor)
{
	return OtherActor ? FMath::Abs((GetActorLocation().Z - OtherActor->GetActorLocation().Z)) : 0.f;
}

float AActor::GetDotProductTo(AActor* OtherActor)
{
	if (OtherActor)
	{
		FVector Dir = GetActorForwardVector();
		FVector Offset = OtherActor->GetActorLocation() - GetActorLocation();
		Offset = Offset.SafeNormal();
		return FVector::DotProduct(Dir, Offset);
	}
	return -2.0;
}

float AActor::GetHorizontalDotProductTo(AActor* OtherActor)
{
	if (OtherActor)
	{
		FVector Dir = GetActorForwardVector();
		FVector Offset = OtherActor->GetActorLocation() - GetActorLocation();
		Offset = Offset.SafeNormal2D();
		return FVector::DotProduct(Dir, Offset);
	}
	return -2.0;
}

#if WITH_EDITOR
const int32 AActor::GetNumUncachedStaticLightingInteractions() const
{
	if (GetRootComponent())
	{
		return GetRootComponent()->GetNumUncachedStaticLightingInteractions();
	}
	return 0;
}
#endif // WITH_EDITOR


// K2 versions of various transform changing operations.
// Note: we pass null for the hit result if not sweeping, for better perf.
// This assumes this K2 function is only used by blueprints, which initializes the param for each function call.

bool AActor::K2_SetActorLocation(FVector NewLocation, bool bSweep, FHitResult& SweepHitResult)
{
	return SetActorLocation(NewLocation, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

bool AActor::K2_SetActorLocationAndRotation(FVector NewLocation, FRotator NewRotation, bool bSweep, FHitResult& SweepHitResult)
{
	return SetActorLocationAndRotation(NewLocation, NewRotation, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

void AActor::K2_AddActorWorldOffset(FVector DeltaLocation, bool bSweep, FHitResult& SweepHitResult)
{
	AddActorWorldOffset(DeltaLocation, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

void AActor::K2_AddActorWorldRotation(FRotator DeltaRotation, bool bSweep, FHitResult& SweepHitResult)
{
	AddActorWorldRotation(DeltaRotation, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

void AActor::K2_AddActorWorldTransform(const FTransform& DeltaTransform, bool bSweep, FHitResult& SweepHitResult)
{
	AddActorWorldTransform(DeltaTransform, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

bool AActor::K2_SetActorTransform(const FTransform& NewTransform, bool bSweep, FHitResult& SweepHitResult)
{
	return SetActorTransform(NewTransform, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

void AActor::K2_AddActorLocalOffset(FVector DeltaLocation, bool bSweep, FHitResult& SweepHitResult)
{
	AddActorLocalOffset(DeltaLocation, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

void AActor::K2_AddActorLocalRotation(FRotator DeltaRotation, bool bSweep, FHitResult& SweepHitResult)
{
	AddActorLocalRotation(DeltaRotation, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

void AActor::K2_AddActorLocalTransform(const FTransform& NewTransform, bool bSweep, FHitResult& SweepHitResult)
{
	AddActorLocalTransform(NewTransform, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

void AActor::K2_SetActorRelativeLocation(FVector NewRelativeLocation, bool bSweep, FHitResult& SweepHitResult)
{
	SetActorRelativeLocation(NewRelativeLocation, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

void AActor::K2_SetActorRelativeRotation(FRotator NewRelativeRotation, bool bSweep, FHitResult& SweepHitResult)
{
	SetActorRelativeRotation(NewRelativeRotation, bSweep, (bSweep ? &SweepHitResult : nullptr));
}

void AActor::K2_SetActorRelativeTransform(const FTransform& NewRelativeTransform, bool bSweep, FHitResult& SweepHitResult)
{
	SetActorRelativeTransform(NewRelativeTransform, bSweep, (bSweep ? &SweepHitResult : nullptr));
}



#undef LOCTEXT_NAMESPACE
