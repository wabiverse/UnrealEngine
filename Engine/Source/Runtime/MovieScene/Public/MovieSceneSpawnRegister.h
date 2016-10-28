// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneFwd.h"
#include "MovieSceneSpawnable.h"
#include "ValueOrError.h"

class UMovieSceneSequence;

/** Struct used for defining a new spawnable type */
struct FNewSpawnable
{
	FNewSpawnable() : ObjectTemplate(nullptr) {}
	FNewSpawnable(UObject* InObjectTemplate, FString InName) : ObjectTemplate(InObjectTemplate), Name(MoveTemp(InName)) {}

	/** The archetype object template that defines the spawnable */
	UObject* ObjectTemplate;

	/** The desired name of the new spawnable */
	FString Name;
};

/**
 * Class responsible for managing spawnables in a movie scene
 */
class FMovieSceneSpawnRegister : public TSharedFromThis<FMovieSceneSpawnRegister>
{
public:

	/**
	 * Virtual destructor
	 */
	virtual ~FMovieSceneSpawnRegister()
	{
	}

	/**
	 * Attempt to find a previously spawned object represented by the specified object and template IDs
	 * @param BindingId		ID of the object to find
	 * @param TemplateID	Unique ID of the template to look within
	 * @return The spawned object if found; nullptr otherwise.
	 */
	MOVIESCENE_API UObject* FindSpawnedObject(const FGuid& BindingId, FMovieSceneSequenceIDRef TemplateID) const;

	/**
	 * Spawn an object for the specified GUID, from the specified sequence instance.
	 *
	 * @param BindingId 	ID of the object to spawn
	 * @param TemplateID 	Identifier for the current template we're evaluating
	 * @param Player 		Movie scene player that is ultimately responsible for spawning the object
	 * @return the spawned object, or nullptr on failure
	 */
	MOVIESCENE_API UObject* SpawnObject(const FGuid& BindingId, UMovieScene& MovieScene, FMovieSceneSequenceIDRef Template, IMovieScenePlayer& Player);

	/**
	 * Destroy a specific previously spawned object
	 *
	 * @param BindingId		ID of the object to destroy
	 * @param TemplateID 	Identifier for the current template we're evaluating
	 * @param Player 		Movie scene player that is ultimately responsible for destroying the object
	 */
	MOVIESCENE_API void DestroySpawnedObject(const FGuid& BindingId, FMovieSceneSequenceIDRef TemplateID, IMovieScenePlayer& Player);

	/**
	 * Destroy a specific previously spawned object, where its binding ID and sequence ID is not known.
	 * @note Should only be used for restoring pre-animated state, or where it is otherwise impossible to call DestroySpawnedObject
	 *
	 * @param InObject		the object to destroy
	 */
	void DestroyObjectDirectly(UObject& InObject) { DestroySpawnedObject(InObject); }

	/**
	 * Destroy spawned objects using a custom predicate
	 *
	 * @param Player 		Movie scene player that is ultimately responsible for destroying the objects
	 * @param Predicate		Predicate used for testing whether an object should be destroyed. Returns true for destruction, false to skip.
	 */
	MOVIESCENE_API void DestroyObjectsByPredicate(IMovieScenePlayer& Player, const TFunctionRef<bool(const FGuid&, ESpawnOwnership, FMovieSceneSequenceIDRef)>& Predicate);

	/**
	 * Purge any memory of any objects that are considered externally owned

	 * @param Player 		Movie scene player that is ultimately responsible for destroying the objects
	 */
	MOVIESCENE_API void ForgetExternallyOwnedSpawnedObjects(FMovieSceneEvaluationState& State, IMovieScenePlayer& Player);

public:

	/**
	 * Called to indiscriminately clean up any spawned objects
	 */
	MOVIESCENE_API void CleanUp(IMovieScenePlayer& Player);

	/**
	 * Called to clean up any non-externally owned spawnables that were spawned from the specified instance
	 */
	MOVIESCENE_API void CleanUpSequence(FMovieSceneSequenceIDRef TemplateID, IMovieScenePlayer& Player);

	/**
	 * Called when the current time has moved beyond the specified sequence's play range
	 */
	MOVIESCENE_API void OnSequenceExpired(FMovieSceneSequenceIDRef TemplateID, IMovieScenePlayer& Player);

#if WITH_EDITOR
	
public:

	/**
	 * Create a new spawnable type from the given source object
	 *
	 * @param SourceObject		The source object to create the spawnable from
	 * @param OwnerMovieScene	The owner movie scene that this spawnable type should reside in
	 * @return the new spawnable type, or error
	 */
	virtual TValueOrError<FNewSpawnable, FText> CreateNewSpawnableType(UObject& SourceObject, UMovieScene& OwnerMovieScene) { return MakeError(NSLOCTEXT("SpawnRegister", "NotSupported", "Not supported")); }

	/**
	 * Called to save the default state of the specified spawnable
	 */
	virtual void SaveDefaultSpawnableState(FMovieSceneSpawnable& Spawnable, FMovieSceneSequenceIDRef TemplateID, IMovieScenePlayer& Player) {}

#endif

protected:

	/**
	 * Spawn an object for the specified GUID, from the specified sequence instance.
	 *
	 * @param Object 		ID of the object to spawn
	 * @param TemplateID 	Identifier for the current template we're evaluating
	 * @param Player 		Movie scene player that is ultimately responsible for spawning the object
	 * @return the spawned object, or nullptr on failure
	 */
	virtual UObject* SpawnObject(FMovieSceneSpawnable& Spawnable, FMovieSceneSequenceIDRef TemplateID, IMovieScenePlayer& Player) = 0;

	/**
	 * Called right before a spawned object with the specified ID and template ID is destroyed
	 */
	virtual void PreDestroyObject(UObject& Object, const FGuid& BindingId, FMovieSceneSequenceIDRef TemplateID) {}

	/**
	 * Destroy a specific previously spawned object
	 *
	 * @param Object 		The object to destroy
	 */
	virtual void DestroySpawnedObject(UObject& Object) = 0;

protected:

	/** Structure holding information pertaining to a spawned object */
	struct FSpawnedObject
	{
		FSpawnedObject(const FGuid& InGuid, UObject& InObject, ESpawnOwnership InOwnership)
			: Guid(InGuid)
			, Object(&InObject)
			, Ownership(InOwnership)
		{}

		/** The ID of the sequencer object binding that this object relates to */
		FGuid Guid;

		/** The object that has been spawned */
		TWeakObjectPtr<UObject> Object;

		/** What level of ownership this object was spawned with */
		ESpawnOwnership Ownership;
	};

	/**
	 *	Helper key type for mapping a guid and sequence instance to a specific value
	 */
	struct FMovieSceneSpawnRegisterKey
	{
		FMovieSceneSpawnRegisterKey(FMovieSceneSequenceIDRef InTemplateID, const FGuid& InBindingId)
			: BindingId(InBindingId)
			, TemplateID(InTemplateID)
		{
		}

		bool operator==(const FMovieSceneSpawnRegisterKey& Other) const
		{
			return BindingId == Other.BindingId && TemplateID == Other.TemplateID;
		}
		
		friend uint32 GetTypeHash(const FMovieSceneSpawnRegisterKey& Key)
		{
			return HashCombine(GetTypeHash(Key.BindingId), GetTypeHash(Key.TemplateID));
		}

		/** BindingId of the object binding */
		FGuid BindingId;

		/** Movie Scene template identifier that spawned the object */
		FMovieSceneSequenceID TemplateID;
	};

protected:

	/** Register of spawned objects */
	TMap<FMovieSceneSpawnRegisterKey, FSpawnedObject> Register;
};

class FNullMovieSceneSpawnRegister : public FMovieSceneSpawnRegister
{
public:
	virtual UObject* SpawnObject(FMovieSceneSpawnable&, FMovieSceneSequenceIDRef, IMovieScenePlayer&) override { check(false); return nullptr; }
	virtual void DestroySpawnedObject(UObject&) override { }
};