// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "PointCloudComponent.h"
#include "PointCloud.h"
#include "PointCloudSceneProxy.h"

DECLARE_CYCLE_STAT(TEXT("Create Point Cloud Proxy"), STAT_PointCloud_CreateSceneProxy, STATGROUP_POINTCLOUD);
DECLARE_CYCLE_STAT(TEXT("Point Cloud Comp Update"), STAT_PointCloud_ComponentUpdateCost, STATGROUP_POINTCLOUD);

UPointCloudComponent::UPointCloudComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PointColor(1.f, 1.f, 1.f, 1.f)
	, PointSize(1.f)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	// Load our material we use f
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterial(TEXT("/PointCloud/DefaultPointCloudMaterial"));
	PointCloudMaterial = DefaultMaterial.Object;
	if (PointCloudMaterial == nullptr)
	{
		PointCloudMaterial = GEngine->WireframeMaterial;
	}
}

void UPointCloudComponent::ClearPointCloud()
{
	PointCloud.Empty();
	PointColors.Empty();
	WorldBounds = FBoxSphereBounds(FVector(0, 0, 0), FVector(0, 0, 0), 0);

	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UPointCloudComponent::CreateSceneProxy()
{
	SCOPE_CYCLE_COUNTER(STAT_PointCloud_CreateSceneProxy);

	// Use the fast version if supported
	if (PointCloud.Num() > 0 && RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
	{
		return new FPointCloudSceneProxy(this);
	}
	return new FNoFetchPointCloudSceneProxy(this);
}

FBoxSphereBounds UPointCloudComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return WorldBounds;
}

FMatrix UPointCloudComponent::GetRenderMatrix() const
{
	return FMatrix::Identity;
}

void UPointCloudComponent::SetPointCloud(const TArray<FVector>& Points)
{
	SCOPE_CYCLE_COUNTER(STAT_PointCloud_ComponentUpdateCost);

	// Zero and rebuild our bounds from the points
	FBox PointBounds(Points);
	WorldBounds = FBoxSphereBounds(PointBounds);
	PointCloud = Points;

	// An update won't be faster than a rebuild, so force a rebuild
	MarkRenderStateDirty();
}

void UPointCloudComponent::SetPointCloudWithColors(const TArray<FVector>& Points, const TArray<FColor>& Colors)
{
	{
		SCOPE_CYCLE_COUNTER(STAT_PointCloud_ComponentUpdateCost);
		PointColors = Colors;
	}
	SetPointCloud(Points);
}

void UPointCloudComponent::SetPointColor(const FLinearColor& Color)
{
	PointColor = Color;
//@todo joeg - Change to a render thread call on the proxy
	MarkRenderStateDirty();
}

void UPointCloudComponent::SetPointSize(float Size)
{
	PointSize = Size;
	if (PointSize <= 0.f)
	{
		PointSize = 1.f;
	}
//@todo joeg - Change to a render thread call on the proxy
	MarkRenderStateDirty();
}

TArray<FVector> UPointCloudComponent::GetPointsInBox(const FBox& WorldSpaceBox) const
{
	TArray<FVector> OutPoints;
	// Miminize the number of allocations by presizing the max
	OutPoints.Empty(PointCloud.Num());

	for (const FVector& Point : PointCloud)
	{
		if (WorldSpaceBox.IsInsideOrOn(Point))
		{
			OutPoints.Add(Point);
		}
	}

	return OutPoints;
}

TArray<FVector> UPointCloudComponent::GetPointsOutsideBox(const FBox& WorldSpaceBox) const
{
	TArray<FVector> OutPoints;
	// Miminize the number of allocations by presizing the max
	OutPoints.Empty(PointCloud.Num());
	
	for (const FVector& Point : PointCloud)
	{
		if (!WorldSpaceBox.IsInsideOrOn(Point))
		{
			OutPoints.Add(Point);
		}
	}
	
	return OutPoints;
}

void UPointCloudComponent::SetIsVisible(bool bNewVisibity)
{
	if (bNewVisibity != bIsVisible)
	{
		bIsVisible = bNewVisibity;
		
		MarkRenderStateDirty();
	}
}

void UPointCloudComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool) const
{
	if (PointCloudMaterial != nullptr)
	{
		OutMaterials.Add(PointCloudMaterial);
	}
}
