// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_BlackboardBase.h"
#include "BTDecorator_Blackboard.generated.h"

/**
 *  Decorator for accessing blackboard values
 */

UENUM()
namespace EBTBlackboardRestart
{
	enum Type
	{
		ValueChange		UMETA(DisplayName="On Value Change", ToolTip="Restart on every change of observed blackboard value"),
		ResultChange	UMETA(DisplayName="On Result Change", ToolTip="Restart only when result of evaluated condition is changed"),
	};
}

/**
 * Blackboard decorator node.
 * A decorator node that bases its condition on a Blackboard key.
 */
UCLASS(HideCategories=(Condition))
class AIMODULE_API UBTDecorator_Blackboard : public UBTDecorator_BlackboardBase
{
	GENERATED_UCLASS_BODY()

	virtual bool CalculateRawConditionValue(class UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory) const override;
	virtual void OnBlackboardChange(const class UBlackboardComponent* Blackboard, FBlackboard::FKey ChangedKeyID) override;
	virtual void DescribeRuntimeValues(const class UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const override;
	virtual FString GetStaticDescription() const override;

protected:

	/** value for arithmetic operations */
	UPROPERTY(Category=Blackboard, EditAnywhere, meta=(DisplayName="Key Value"))
	int32 IntValue;

	/** value for arithmetic operations */
	UPROPERTY(Category=Blackboard, EditAnywhere, meta=(DisplayName="Key Value"))
	float FloatValue;

	/** value for string operations */
	UPROPERTY(Category=Blackboard, EditAnywhere, meta=(DisplayName="Key Value"))
	FString StringValue;

	/** cached description */
	UPROPERTY()
	FString CachedDescription;

	/** operation type */
	UPROPERTY()
	uint8 OperationType;

	/** when observer can try to request abort? */
	UPROPERTY(Category=FlowControl, EditAnywhere)
	TEnumAsByte<EBTBlackboardRestart::Type> NotifyObserver;

#if WITH_EDITORONLY_DATA

	UPROPERTY(Category=Blackboard, EditAnywhere, meta=(DisplayName="Key Query"))
	TEnumAsByte<EBasicKeyOperation::Type> BasicOperation;

	UPROPERTY(Category=Blackboard, EditAnywhere, meta=(DisplayName="Key Query"))
	TEnumAsByte<EArithmeticKeyOperation::Type> ArithmeticOperation;

	UPROPERTY(Category=Blackboard, EditAnywhere, meta=(DisplayName="Key Query"))
	TEnumAsByte<ETextKeyOperation::Type> TextOperation;

#endif

#if WITH_EDITOR

	/** describe decorator and cache it */
	virtual void BuildDescription();

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void InitializeFromAsset(class UBehaviorTree* Asset) override;

#endif

	/** take blackboard value and evaluate decorator's condition */
	bool EvaluateOnBlackboard(const class UBlackboardComponent* BlackboardComp) const;

#if CPP
	friend class FBlackboardDecoratorDetails;
#endif
};
