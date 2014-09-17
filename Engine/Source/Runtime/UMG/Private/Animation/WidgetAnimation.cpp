// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"
#include "MovieScene.h"
#include "WidgetAnimation.h"

UObject* FWidgetAnimationBinding::FindRuntimeObject( UWidgetTree& WidgetTree ) const
{
	UObject* FoundObject = FindObject<UObject>(&WidgetTree, *WidgetName.ToString());
	if(FoundObject)
	{
		if(SlotWidgetName != NAME_None)
		{
			// if we were animating the slot, look up the slot that contains the widget 
			UWidget* WidgetObject = Cast<UWidget>(FoundObject);
			if(WidgetObject->Slot)
			{
				FoundObject = WidgetObject->Slot;
			}
		}
	}

	return FoundObject;
}

UWidgetAnimation::UWidgetAnimation(const FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{

}

#if WITH_EDITOR

UWidgetAnimation* UWidgetAnimation::GetNullAnimation()
{
	static UWidgetAnimation* NullAnimation = nullptr;
	if( !NullAnimation )
	{
		NullAnimation = ConstructObject<UWidgetAnimation>( UWidgetAnimation::StaticClass(), GetTransientPackage(), NAME_None, RF_RootSet );
		NullAnimation->MovieScene = ConstructObject<UMovieScene>( UMovieScene::StaticClass(), NullAnimation, FName("No Animation"), RF_RootSet );
	}

	return NullAnimation;
}

#endif