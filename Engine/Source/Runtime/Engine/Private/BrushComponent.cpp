// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BrushComponent.cpp: Unreal brush component implementation
=============================================================================*/

#include "EnginePrivate.h"
#include "LevelUtils.h"

#include "DebuggingDefines.h"
#include "ActorEditorUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogBrushComponent, Log, All);

#if WITH_EDITOR
struct FModelWireVertex
{
	FVector Position;
	FPackedNormal TangentX;
	FPackedNormal TangentZ;
	FVector2D UV;
};

class FModelWireVertexBuffer : public FVertexBuffer
{
public:

	/** Initialization constructor. */
	FModelWireVertexBuffer(UModel* InModel):
		NumVertices(0)
	{
		Polys.Append(InModel->Polys->Element);
		for(int32 PolyIndex = 0;PolyIndex < InModel->Polys->Element.Num();PolyIndex++)
		{
			NumVertices += InModel->Polys->Element[PolyIndex].Vertices.Num();
		}
	}

	// FRenderResource interface.
	virtual void InitRHI()
	{
		if(NumVertices)
		{
			FRHIResourceCreateInfo CreateInfo;
			VertexBufferRHI = RHICreateVertexBuffer(NumVertices * sizeof(FModelWireVertex),BUF_Static, CreateInfo);

			FModelWireVertex* DestVertex = (FModelWireVertex*)RHILockVertexBuffer(VertexBufferRHI,0,NumVertices * sizeof(FModelWireVertex),RLM_WriteOnly);
			for(int32 PolyIndex = 0;PolyIndex < Polys.Num();PolyIndex++)
			{
				FPoly& Poly = Polys[PolyIndex];
				for(int32 VertexIndex = 0;VertexIndex < Poly.Vertices.Num();VertexIndex++)
				{
					DestVertex->Position = Poly.Vertices[VertexIndex];
					DestVertex->TangentX = FVector(1,0,0);
					DestVertex->TangentZ = FVector(0,0,1);
					// TangentZ.w contains the sign of the tangent basis determinant. Assume +1
					DestVertex->TangentZ.Vector.W = 255;
					DestVertex->UV.X	 = 0.0f;
					DestVertex->UV.Y	 = 0.0f;
					DestVertex++;
				}
			}
			RHIUnlockVertexBuffer(VertexBufferRHI);
		}
	}

	// Accessors.
	uint32 GetNumVertices() const { return NumVertices; }

private:
	TArray<FPoly> Polys;
	uint32 NumVertices;
};

class FModelWireIndexBuffer : public FIndexBuffer
{
public:

	/** Initialization constructor. */
	FModelWireIndexBuffer(UModel* InModel):
		NumEdges(0)
	{
		Polys.Append(InModel->Polys->Element);
		for(int32 PolyIndex = 0;PolyIndex < InModel->Polys->Element.Num();PolyIndex++)
		{
			NumEdges += InModel->Polys->Element[PolyIndex].Vertices.Num();
		}
	}

	// FRenderResource interface.
	virtual void InitRHI()
	{
		if(NumEdges)
		{
			FRHIResourceCreateInfo CreateInfo;
			IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16),NumEdges * 2 * sizeof(uint16),BUF_Static, CreateInfo);

			uint16* DestIndex = (uint16*)RHILockIndexBuffer(IndexBufferRHI,0,NumEdges * 2 * sizeof(uint16),RLM_WriteOnly);
			uint16 BaseIndex = 0;
			for(int32 PolyIndex = 0;PolyIndex < Polys.Num();PolyIndex++)
			{
				FPoly&	Poly = Polys[PolyIndex];
				for(int32 VertexIndex = 0;VertexIndex < Poly.Vertices.Num();VertexIndex++)
				{
					*DestIndex++ = BaseIndex + VertexIndex;
					*DestIndex++ = BaseIndex + ((VertexIndex + 1) % Poly.Vertices.Num());
				}
				BaseIndex += Poly.Vertices.Num();
			}
			RHIUnlockIndexBuffer(IndexBufferRHI);
		}
	}

	// Accessors.
	uint32 GetNumEdges() const { return NumEdges; }

private:
	TArray<FPoly> Polys;
	uint32 NumEdges;
};
#endif // WITH_EDITOR

class FBrushSceneProxy : public FPrimitiveSceneProxy
{
public:
	FBrushSceneProxy(UBrushComponent* Component, ABrush* Owner):
		FPrimitiveSceneProxy(Component),
#if WITH_EDITORONLY_DATA
		WireIndexBuffer(Component->Brush),
		WireVertexBuffer(Component->Brush),
#endif
		bVolume(false),
		bBuilder(false),
		bSolidWhenSelected(false),
		BrushColor(GEngine->C_BrushWire),
		LevelColor(255,255,255),
		PropertyColor(255,255,255),
		BodySetup(Component->BrushBodySetup),
		CollisionResponse(Component->GetCollisionResponseToChannels())
	{
		bWillEverBeLit = false;

		if(Owner)
		{
			// If the editor is in a state where drawing the brush wireframe isn't desired, bail out.
			if( !GEngine->ShouldDrawBrushWireframe( Owner ) )
			{
				return;
			}

			// Determine the type of brush this is.
			bVolume = Owner->IsVolumeBrush();
			bBuilder = FActorEditorUtils::IsABuilderBrush( Owner );
			BrushColor = Owner->GetWireColor();
			bSolidWhenSelected = Owner->bSolidWhenSelected;

			// Builder brushes should be unaffected by level coloration, so if this is a builder brush, use
			// the brush color as the level color.
			if ( bBuilder )
			{
				LevelColor = BrushColor;
			}
			else
			{
				// Try to find a color for level coloration.
				ULevel* Level = Owner->GetLevel();
				ULevelStreaming* LevelStreaming = FLevelUtils::FindStreamingLevel( Level );
				if ( LevelStreaming )
				{
					LevelColor = LevelStreaming->DrawColor;
				}
			}
		}

		// Get a color for property coloration.
		GEngine->GetPropertyColorationColor( (UObject*)Component, PropertyColor );

#if WITH_EDITORONLY_DATA
		ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
			InitBrushVertexFactory,
			FLocalVertexFactory*,VertexFactory,&VertexFactory,
			FVertexBuffer*,WireVertexBuffer,&WireVertexBuffer,
			{
				FLocalVertexFactory::DataType Data;
				Data.PositionComponent = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(WireVertexBuffer,FModelWireVertex,Position,VET_Float3);
				Data.TangentBasisComponents[0] = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(WireVertexBuffer,FModelWireVertex,TangentX,VET_PackedNormal);
				Data.TangentBasisComponents[1] = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(WireVertexBuffer,FModelWireVertex,TangentZ,VET_PackedNormal);
				Data.TextureCoordinates.Add( STRUCTMEMBER_VERTEXSTREAMCOMPONENT(WireVertexBuffer,FModelWireVertex,UV,VET_Float2) );
				VertexFactory->SetData(Data);
			});
#endif
	}

	virtual ~FBrushSceneProxy()
	{
#if WITH_EDITORONLY_DATA
		VertexFactory.ReleaseResource();
		WireIndexBuffer.ReleaseResource();
		WireVertexBuffer.ReleaseResource();
#endif

	}

	bool IsCollisionView(const FSceneView* View, bool & bDrawCollision) const
	{
		const bool bInCollisionView = View->Family->EngineShowFlags.CollisionVisibility || View->Family->EngineShowFlags.CollisionPawn;
		if (bInCollisionView && IsCollisionEnabled())
		{
			bDrawCollision = View->Family->EngineShowFlags.CollisionPawn && (CollisionResponse.GetResponse(ECC_Pawn) != ECR_Ignore);
			bDrawCollision |= View->Family->EngineShowFlags.CollisionVisibility && (CollisionResponse.GetResponse(ECC_Visibility) != ECR_Ignore);
		}
		else
		{
			bDrawCollision = false;
		}

		return bInCollisionView;
	}

	/** 
	* Draw the scene proxy as a dynamic element
	*
	* @param	PDI - draw interface to render to
	* @param	View - current view
	*/
	virtual void DrawDynamicElements(FPrimitiveDrawInterface* PDI,const FSceneView* View)
	{
		QUICK_SCOPE_CYCLE_COUNTER( STAT_BrushSceneProxy_DrawDynamicElements );


		if( AllowDebugViewmodes() )
		{
			bool bDrawCollision = false;
			const bool bInCollisionView = IsCollisionView(View, bDrawCollision);

			// Draw solid if 'solid when selected' and selected, or we are in a 'collision view'
			const bool bDrawSolid = ((bSolidWhenSelected && IsSelected()) || (bInCollisionView && bDrawCollision));
			// Don't draw wireframe if in a collision view mode and not drawing solid
			const bool bDrawWireframe = !bInCollisionView;

			// Choose color to draw it
			FLinearColor DrawColor = BrushColor;
			// In a collision view mode
			if(bInCollisionView)
			{
				DrawColor = BrushColor;
			}
			else if(View->Family->EngineShowFlags.PropertyColoration)
			{
				DrawColor = PropertyColor;
			}
			else if(View->Family->EngineShowFlags.LevelColoration)
			{
				DrawColor = LevelColor;
			}


			// SOLID
			if(bDrawSolid)
			{
				if(BodySetup != NULL)
				{
					const FColoredMaterialRenderProxy SolidMaterialInstance(
						GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy(IsSelected(), IsHovered()),
						DrawColor
						);

					FTransform GeomTransform(GetLocalToWorld());
					BodySetup->AggGeom.DrawAggGeom(PDI, GeomTransform, DrawColor, /*Material=*/&SolidMaterialInstance, false, /*bSolid=*/ true );
				}
			}
			// WIREFRAME
			else if(bDrawWireframe)
			{
				// If we have the editor data (Wire Buffers) use those for wireframe
#if WITH_EDITOR
				if(WireIndexBuffer.GetNumEdges() && WireVertexBuffer.GetNumVertices())
				{
					FColoredMaterialRenderProxy WireframeMaterial(
						GEngine->LevelColorationUnlitMaterial->GetRenderProxy(IsSelected(), IsHovered()),
						GetSelectionColor(DrawColor,!(GIsEditor && (View->Family->EngineShowFlags.Selection)) || IsSelected(), IsHovered(), /*bUseOverlayIntensity=*/false)
						);

					FMeshBatch Mesh;
					FMeshBatchElement& BatchElement = Mesh.Elements[0];
					BatchElement.IndexBuffer = &WireIndexBuffer;
					Mesh.VertexFactory = &VertexFactory;
					Mesh.MaterialRenderProxy = &WireframeMaterial;
					BatchElement.PrimitiveUniformBufferResource = &GetUniformBuffer();
					BatchElement.FirstIndex = 0;
					BatchElement.NumPrimitives = WireIndexBuffer.GetNumEdges();
					BatchElement.MinVertexIndex = 0;
					BatchElement.MaxVertexIndex = WireVertexBuffer.GetNumVertices() - 1;
					Mesh.CastShadow = false;
					Mesh.Type = PT_LineList;
					Mesh.DepthPriorityGroup = IsSelected() ? SDPG_Foreground : SDPG_World;
					PDI->DrawMesh(Mesh);
				}
				else 
#endif
				if(BodySetup != NULL)
				// If not, use the body setup for wireframe
				{
					FTransform GeomTransform(GetLocalToWorld());
					BodySetup->AggGeom.DrawAggGeom(PDI, GeomTransform, GetSelectionColor(DrawColor, IsSelected(), IsHovered()), /* Material=*/ NULL, false, /* bSolid=*/ false );
				}

			}
		}

	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View)
	{
		bool bVisible = false;


		// We render volumes in collision view. In game, always, in editor, if the EngineShowFlags.Volumes option is on.
		if(bSolidWhenSelected && IsSelected())
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = true;
			Result.bDynamicRelevance = true;
			return Result;
		}

		const bool bInCollisionView = (View->Family->EngineShowFlags.CollisionVisibility || View->Family->EngineShowFlags.CollisionPawn);

		if(IsShown(View))
		{
			bool bNeverShow = false;

			if( GIsEditor )
			{
				const bool bShowBuilderBrush = View->Family->EngineShowFlags.BuilderBrush != 0;

				// Only render builder brush and only if the show flags indicate that we should render builder brushes.
				if( bBuilder && (!bShowBuilderBrush) )
				{
					bNeverShow = true;
				}
			}

			if(bNeverShow == false)
			{
				const bool bBSPVisible = View->Family->EngineShowFlags.BSP;
				const bool bBrushesVisible = View->Family->EngineShowFlags.Brushes;

				if ( !bVolume ) // EngineShowFlags.Collision does not apply to volumes
				{
					if( (bBSPVisible && bBrushesVisible) )
					{
						bVisible = true;
					}
				}

				// See if we should be visible because we are in a 'collision view' and have collision enabled
				if (bInCollisionView && IsCollisionEnabled())
				{
					bVisible = true;
				}

				// Always show the build brush and any brushes that are selected in the editor.
				if( GIsEditor )
				{
					if( bBuilder || IsSelected() )
					{
						bVisible = true;
					}
				}

				if ( bVolume )
				{
					const bool bVolumesVisible = View->Family->EngineShowFlags.Volumes;
					if(!GIsEditor || View->bIsGameView || bVolumesVisible)
					{
						bVisible = true;
					}
				}		
			}
		}
		
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = bVisible;
		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = IsShadowCast(View);

		// Don't render on top in 'collision view' modes
		if(!bInCollisionView)
		{
			Result.bEditorPrimitiveRelevance = true;
		}

		return Result;
	}

	virtual void CreateRenderThreadResources()
	{
#if WITH_EDITORONLY_DATA
		VertexFactory.InitResource();
		WireIndexBuffer.InitResource();
		WireVertexBuffer.InitResource();
#endif

	}

	virtual uint32 GetMemoryFootprint( void ) const { return( sizeof( *this ) + GetAllocatedSize() ); }
	uint32 GetAllocatedSize( void ) const { return( FPrimitiveSceneProxy::GetAllocatedSize() ); }

private:
#if WITH_EDITORONLY_DATA
	FLocalVertexFactory VertexFactory;
	FModelWireIndexBuffer WireIndexBuffer;
	FModelWireVertexBuffer WireVertexBuffer;
#endif

	uint32 bVolume : 1;
	uint32 bBuilder : 1;	
	uint32 bSolidWhenSelected : 1;

	FColor BrushColor;
	FColor LevelColor;
	FColor PropertyColor;

	/** Collision Response of this component**/
	UBodySetup* BodySetup;
	FCollisionResponseContainer CollisionResponse;
};

UBrushComponent::UBrushComponent(const class FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
	bHiddenInGame = true;
	AlwaysLoadOnClient = false;
	AlwaysLoadOnServer = false;
	bUseAsOccluder = true;
	bRequiresCustomLocation = true;
	bUseEditorCompositing = true;
	bCanEverAffectNavigation = true;
}

FPrimitiveSceneProxy* UBrushComponent::CreateSceneProxy()
{
	FPrimitiveSceneProxy* Proxy = NULL;
	
	if (Brush != NULL)	
	{
		// Check to make sure that we want to draw this brushed based on editor settings.
		ABrush*	Owner = Cast<ABrush>(GetOwner());
		if(Owner)
		{
			// If the editor is in a state where drawing the brush wireframe isn't desired, bail out.
			if( GEngine->ShouldDrawBrushWireframe( Owner ) )
			{
				Proxy = new FBrushSceneProxy(this, Owner);
			}
		}
		else
		{
			Proxy = new FBrushSceneProxy(this, Owner);
		}
	}

	return Proxy;
}


FBoxSphereBounds UBrushComponent::CalcBounds(const FTransform & LocalToWorld) const
{
#if WITH_EDITOR
	if(Brush && Brush->Polys && Brush->Polys->Element.Num())
	{
		TArray<FVector> Points;
		for( int32 i=0; i<Brush->Polys->Element.Num(); i++ )
		{
			for( int32 j=0; j<Brush->Polys->Element[i].Vertices.Num(); j++ )
			{
				Points.Add(Brush->Polys->Element[i].Vertices[j]);
			}
		}
		return FBoxSphereBounds( Points.GetTypedData(), Points.Num() ).TransformBy(LocalToWorld);
	}
	else 
#endif // WITH_EDITOR
	if ((BrushBodySetup != NULL) && (BrushBodySetup->AggGeom.GetElementCount() > 0))
	{
		FBoxSphereBounds NewBounds;
		BrushBodySetup->AggGeom.CalcBoxSphereBounds(NewBounds, LocalToWorld);
		return NewBounds;
	}
	else
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.f);
	}
}

FVector UBrushComponent::GetCustomLocation() const
{
	const FVector LocationWithPivot = ComponentToWorld.GetLocation();
	const FVector LocationNoPivot = LocationWithPivot + ComponentToWorld.TransformVector(PrePivot);

	return LocationNoPivot;
}

FTransform UBrushComponent::CalcNewComponentToWorld(const FTransform& NewRelativeTransform) const
{
	FTransform CompToWorld = Super::CalcNewComponentToWorld(NewRelativeTransform);

	const FVector LocationNoPivot = CompToWorld.GetLocation();
	const FVector LocationWithPivot = LocationNoPivot + CompToWorld.TransformVector(-PrePivot);

	CompToWorld.SetLocation(LocationWithPivot);

	return CompToWorld;
}


void UBrushComponent::GetUsedMaterials( TArray<UMaterialInterface*>& OutMaterials ) const
{
#if WITH_EDITOR
	// Get the material from each polygon making up the brush.
	if( Brush && Brush->Polys )
	{
		UPolys* Polys = Brush->Polys;
		if( Polys )
		{
			for( int32 ElementIdx = 0; ElementIdx < Polys->Element.Num(); ++ElementIdx )
			{
				OutMaterials.Add( Polys->Element[ ElementIdx ].Material );
			}
		}
	}
#endif // WITH_EDITOR
}

void UBrushComponent::PostLoad()
{
	Super::PostLoad();

	// Stop existing BrushComponents from generating mirrored collision mesh
	if ((GetLinkerUE4Version() < VER_UE4_NO_MIRROR_BRUSH_MODEL_COLLISION) && (BrushBodySetup != NULL))
	{
		BrushBodySetup->bGenerateMirroredCollision = false;
	}
}


SIZE_T UBrushComponent::GetResourceSize(EResourceSizeMode::Type Mode)
{
	SIZE_T ResSize = Super::GetResourceSize(Mode);

	// Count the bodysetup we own as well for 'inclusive' stats
	if((Mode == EResourceSizeMode::Inclusive) && (BrushBodySetup != NULL))
	{
		ResSize += BrushBodySetup->GetResourceSize(Mode);
	}

	return ResSize;
}

uint8 UBrushComponent::GetStaticDepthPriorityGroup() const
{
	ABrush* BrushOwner = Cast<ABrush>(GetOwner());

	// Draw selected and builder brushes in the foreground DPG.
	if(BrushOwner && (IsOwnerSelected() || FActorEditorUtils::IsABuilderBrush(BrushOwner)))
	{
		return SDPG_Foreground;
	}
	else
	{
		return DepthPriorityGroup;
	}
}

void UBrushComponent::BuildSimpleBrushCollision()
{
	AActor* Owner = GetOwner();
	if(!Owner)
	{
		UE_LOG(LogBrushComponent, Warning, TEXT("BuildSimpleBrushCollision: BrushComponent with no Owner!") );
		return;
	}

	if(BrushBodySetup == NULL)
	{
		BrushBodySetup = ConstructObject<UBodySetup>(UBodySetup::StaticClass(), this);
		check(BrushBodySetup);
	}

	// No complex collision, so use the simple for that
	BrushBodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	// Don't need mirrored version of collision data
	BrushBodySetup->bGenerateMirroredCollision = false;

#if WITH_EDITOR
	// Convert collision model into convex hulls.
	BrushBodySetup->CreateFromModel( Brush, true );
#endif // WITH_EDITOR

	MarkPackageDirty();
}



