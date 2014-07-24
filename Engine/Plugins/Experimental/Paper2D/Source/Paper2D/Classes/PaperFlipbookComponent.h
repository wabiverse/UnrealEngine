// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.generated.h"

// Event for a non-looping flipbook finishing play
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFlipbookFinishedPlaySignature);

UCLASS(ShowCategories=(Mobility, ComponentReplication), EarlyAccessPreview, meta=(BlueprintSpawnableComponent))
class PAPER2D_API UPaperFlipbookComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

protected:
	/** Flipbook currently being played */
	UPROPERTY(Category=Sprite, EditAnywhere, meta=(DisplayThumbnail = "true"), ReplicatedUsing=OnRep_SourceFlipbook)
	UPaperFlipbook* SourceFlipbook;

	/** Material used to display the flipbook */
	UPROPERTY(Category=Sprite, EditAnywhere)
	UMaterialInterface* Material;

	/** Current play rate of the flipbook */
	UPROPERTY(Category=Sprite, EditAnywhere, Replicated)
	float PlayRate;

	/** Whether the flipbook should loop when it reaches the end, or stop */
	UPROPERTY(Replicated)
	uint32 bLooping:1;

	/** If playback should move the current position backwards instead of forwards */
	UPROPERTY(Replicated)
	uint32 bReversePlayback:1;

	/** Are we currently playing (moving Position) */
	UPROPERTY(Replicated)
	uint32 bPlaying:1;

	/** Current position in the timeline */
	UPROPERTY(Replicated)
	float AccumulatedTime;

	/** Last frame index calculated */
	UPROPERTY()
	int32 CachedFrameIndex;

	/** Vertex color to apply to the frames */
	UPROPERTY(BlueprintReadOnly, Interp, Category=Sprite)
	FLinearColor SpriteColor;

public:
	/** Event called whenever a non-looping flipbook finishes playing (either reaching the beginning or the end, depending on the play direction) */
	UPROPERTY(BlueprintAssignable)
	FFlipbookFinishedPlaySignature OnFinishedPlaying;

public:
	/** Change the flipbook used by this instance (will reset the play time to 0 if it is a new flipbook). */
	UFUNCTION(BlueprintCallable, Category="Sprite")
	virtual bool SetFlipbook(class UPaperFlipbook* NewFlipbook);

	/** Gets the flipbook used by this instance. */
	UFUNCTION(BlueprintPure, Category="Sprite")
	virtual UPaperFlipbook* GetFlipbook();

	/** Gets the material used by this instance */
	UFUNCTION(BlueprintCallable, Category="Sprite")
	UMaterialInterface* GetSpriteMaterial() const;

	/** Set color of the sprite */
	UFUNCTION(BlueprintCallable, Category="Sprite")
	void SetSpriteColor(FLinearColor NewColor);

	/** Start playback of flipbook */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	void Play();

	/** Start playback of flipbook from the start */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	void PlayFromStart();

	/** Start playback of flipbook in reverse */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	void Reverse();

	/** Start playback of flipbook in reverse from the end */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	void ReverseFromEnd();

	/** Stop playback of flipbook */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	void Stop();

	/** Get whether this flipbook is playing or not. */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	bool IsPlaying() const;

	/** Get whether we are reversing or not */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	bool IsReversing() const;

	/** Jump to a position in the flipbook. If bFireEvents is true, event functions will fire, otherwise will not. */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	void SetPlaybackPosition(float NewPosition, bool bFireEvents);

	/** Get the current playback position of the flipbook */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	float GetPlaybackPosition() const;

	/** true means we should loop, false means we should not. */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	void SetLooping(bool bNewLooping);

	/** Get whether we are looping or not */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	bool IsLooping() const;

	/** Sets the new play rate for this flipbook */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	void SetPlayRate(float NewRate);

	/** Get the current play rate for this flipbook */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	float GetPlayRate() const;

	/** Set the new playback position time to use */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	void SetNewTime(float NewTime);

	/** Get length of the flipbook (in seconds) */
	UFUNCTION(BlueprintCallable, Category="Components|Flipbook")
	float GetFlipbookLength() const;

	/** Get length of the flipbook (in frames) */
	UFUNCTION(BlueprintCallable, Category = "Components|Flipbook")
	int32 GetFlipbookLengthInFrames() const;

	/** Get the nominal framerate that the flipbook will be played back at (ignoring PlayRate), in frames per second */
	UFUNCTION(BlueprintCallable, Category = "Components|Flipbook")
	float GetFlipbookFramerate() const;

protected:
	UFUNCTION()
	void OnRep_SourceFlipbook(class UPaperFlipbook* OldFlipbook);

	void CalculateCurrentFrame();
	UPaperSprite* GetSpriteAtCachedIndex() const;

	void TickFlipbook(float DeltaTime);

public:
	// UActorComponent interface
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void SendRenderDynamicData_Concurrent() override;
	virtual const UObject* AdditionalStatObject() const override;
	// End of UActorComponent interface

	// UPrimitiveComponent interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform & LocalToWorld) const override;
	// End of UPrimitiveComponent interface
};

// Allow the old name to continue to work for one release
DEPRECATED(4.3, "UPaperAnimatedRenderComponent has been renamed to UPaperFlipbookComponent")
typedef UPaperFlipbookComponent UPaperAnimatedRenderComponent;
