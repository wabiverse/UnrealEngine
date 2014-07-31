// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.
#include "WorldBrowserPrivatePCH.h"

#include "ObjectTools.h"
#include "EdGraphUtilities.h"
#include "WorldTileCollectionModel.h"
#include "SWorldTileItem.h"
#include "WorldTileDetails.h"

#define LOCTEXT_NAMESPACE "WorldBrowser"

FTileItemThumbnail::FTileItemThumbnail(FSlateTextureRenderTarget2DResource* InThumbnailRenderTarget, 
									   TSharedPtr<FLevelModel> InItemModel)
	: LevelModel(InItemModel)
	, ThumbnailTexture(NULL)
	, ThumbnailRenderTarget(InThumbnailRenderTarget)
{
	FIntPoint RTSize = ThumbnailRenderTarget->GetSizeXY();
	ThumbnailTexture = new FSlateTexture2DRHIRef(RTSize.X, RTSize.Y, PF_B8G8R8A8, NULL, TexCreate_Dynamic);
	BeginInitResource(ThumbnailTexture);
}

FTileItemThumbnail::~FTileItemThumbnail()
{
	BeginReleaseResource(ThumbnailTexture);
	
	// Wait for all resources to be released
	FlushRenderingCommands();
	delete ThumbnailTexture;
	ThumbnailTexture = NULL;
}

FIntPoint FTileItemThumbnail::GetSize() const
{
	return ThumbnailRenderTarget->GetSizeXY();
}

FSlateShaderResource* FTileItemThumbnail::GetViewportRenderTargetTexture() const
{
	return ThumbnailTexture;
}

bool FTileItemThumbnail::RequiresVsync() const
{
	return false;
}

void FTileItemThumbnail::UpdateTextureData(FObjectThumbnail* ObjectThumbnail)
{
	check(ThumbnailTexture)
	
	if (ObjectThumbnail &&
		ObjectThumbnail->GetImageWidth() > 0 && 
		ObjectThumbnail->GetImageHeight() > 0 && 
		ObjectThumbnail->GetUncompressedImageData().Num() > 0)
	{
		// Make bulk data for updating the texture memory later
		FSlateTextureDataPtr ThumbnailBulkData = MakeShareable(new FSlateTextureData(
			ObjectThumbnail->GetImageWidth(), 
			ObjectThumbnail->GetImageHeight(), 
			GPixelFormats[PF_B8G8R8A8].BlockBytes, 
			ObjectThumbnail->AccessImageData())
			);
					
		// Update the texture RHI
		ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
			UpdateGridItemThumbnailResourse,
			FSlateTexture2DRHIRef*, Texture, ThumbnailTexture,
			FSlateTextureDataPtr, BulkData, ThumbnailBulkData,
		{
			Texture->SetTextureData(BulkData);
			Texture->UpdateRHI();
		});
	}
}

void FTileItemThumbnail::UpdateThumbnail()
{
	// No need images for persistent and always loaded levels
	if (LevelModel->IsPersistent())
	{
		return;
	}
	
	// Load image from a package header
	if (!LevelModel->IsVisible() || LevelModel->IsSimulating())
	{
		TSet<FName> ObjectFullNames;
		ObjectFullNames.Add(LevelModel->GetAssetName());
		FThumbnailMap ThumbnailMap;
			
		if (ThumbnailTools::ConditionallyLoadThumbnailsFromPackage(LevelModel->GetPackageFileName(), ObjectFullNames, ThumbnailMap))
		{
			UpdateTextureData(ThumbnailMap.Find(LevelModel->GetAssetName()));
		}
	}
	// Render image from a loaded visble level
	else
	{
		ULevel* TargetLevel = LevelModel->GetLevelObject();
		if (TargetLevel)
		{
			FIntPoint RTSize = ThumbnailRenderTarget->GetSizeXY();
			
			// Set persistent world package as transient to avoid package dirtying during thumbnail rendering
			FUnmodifiableObject ImmuneWorld(TargetLevel->OwningWorld);
			
			FObjectThumbnail NewThumbnail;
			// Generate the thumbnail
			ThumbnailTools::RenderThumbnail(
				TargetLevel,
				RTSize.X,
				RTSize.Y,
				ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush,
				ThumbnailRenderTarget,
				&NewThumbnail
				);

			UPackage* MyOutermostPackage = CastChecked<UPackage>(TargetLevel->GetOutermost());
			ThumbnailTools::CacheThumbnail(LevelModel->GetAssetName().ToString(), &NewThumbnail, MyOutermostPackage);
			UpdateTextureData(&NewThumbnail);
		}
	}
}

//----------------------------------------------------------------
//
//
//----------------------------------------------------------------
SWorldTileItem::~SWorldTileItem()
{
	TileModel->ChangedEvent.RemoveAll(this);
}

void SWorldTileItem::Construct(const FArguments& InArgs)
{
	WorldModel = InArgs._InWorldModel;
	TileModel = InArgs._InItemModel;

	ProgressBarImage = FEditorStyle::GetBrush(TEXT("ProgressBar.Marquee"));
	
	bNeedRefresh = true;
	bIsDragging = false;
	bAffectedByMarquee = false;

	Thumbnail = MakeShareable(new FTileItemThumbnail(InArgs._ThumbnailRenderTarget, TileModel));
	ThumbnailViewport = SNew(SViewport)
							.EnableGammaCorrection(false);
							
	ThumbnailViewport->SetViewportInterface(Thumbnail.ToSharedRef());
	// This will grey out tile in case level is not loaded or locked
	ThumbnailViewport->SetEnabled(TAttribute<bool>(this, &SWorldTileItem::IsItemEnabled));

	TileModel->ChangedEvent.AddSP(this, &SWorldTileItem::RequestRefresh);
			
	ChildSlot
	[
		ThumbnailViewport.ToSharedRef()
	];

	SetToolTip(CreateToolTipWidget());

	CurveSequence = FCurveSequence(0.0f, 0.5f);
	CurveSequence.Play();
}

void SWorldTileItem::RequestRefresh()
{
	bNeedRefresh = true;
}

UObject* SWorldTileItem::GetObjectBeingDisplayed() const
{
	return TileModel->GetNodeObject();
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SToolTip> SWorldTileItem::CreateToolTipWidget()
{
	TSharedPtr<SToolTip>		TooltipWidget;
	
	SAssignNew(TooltipWidget, SToolTip)
	.TextMargin(1)
	.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ToolTipBorder"))
	[
		SNew(SBorder)
		.Padding(6)
		.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.NonContentBorder"))
		[
			SNew(SVerticalBox)

			// Level name section
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0,0,0,4)
			[
				SNew(SBorder)
				.Padding(6)
				.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
				[
					SNew(SVerticalBox)
					
					+SVerticalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						// Level name
						+SHorizontalBox::Slot()
						.Padding(6,0,0,0)
						.HAlign(HAlign_Left)
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(this, &SWorldTileItem::GetLevelNameText) 
							.Font(FEditorStyle::GetFontStyle("ContentBrowser.TileViewTooltip.NameFont"))
						]
					]
				]
			]
			
			// Tile info section
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0,0,0,4)
			[
				SNew(SBorder)
				.Padding(6)
				.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
				[
					SNew(SUniformGridPanel)

					// Tile position
					+SUniformGridPanel::Slot(0, 0)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Item_OriginOffset", "Position:")) 
					]
					
					+SUniformGridPanel::Slot(1, 0)
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Text(this, &SWorldTileItem::GetPositionText) 
					]

					// Tile bounds
					+SUniformGridPanel::Slot(0, 1)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Item_BoundsExtent", "Extent:")) 
					]
					
					+SUniformGridPanel::Slot(1, 1)
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Text(this, &SWorldTileItem::GetBoundsExtentText) 
					]

					// Layer name
					+SUniformGridPanel::Slot(0, 2)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Item_Name", "Layer Name:")) 
					]
					
					+SUniformGridPanel::Slot(1, 2)
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Text(this, &SWorldTileItem::GetLevelLayerNameText) 
					]

					// Streaming distance
					+SUniformGridPanel::Slot(0, 3)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Item_Distance", "Streaming Distance:")) 
					]
					
					+SUniformGridPanel::Slot(1, 3)
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Text(this, &SWorldTileItem::GetLevelLayerDistanceText) 
					]
				]
			]
		]
	];

	return TooltipWidget.ToSharedRef();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FVector2D SWorldTileItem::GetPosition() const
{
	return TileModel->GetLevelPosition2D();
}

TSharedPtr<FLevelModel> SWorldTileItem::GetLevelModel() const
{
	return StaticCastSharedPtr<FLevelModel>(TileModel);
}

const FSlateBrush* SWorldTileItem::GetShadowBrush(bool bSelected) const
{
	return bSelected ? FEditorStyle::GetBrush(TEXT("Graph.CompactNode.ShadowSelected")) : FEditorStyle::GetBrush(TEXT("Graph.Node.Shadow"));
}

FOptionalSize SWorldTileItem::GetItemWidth() const
{
	return FOptionalSize(TileModel->GetLevelSize2D().X);
}

FOptionalSize SWorldTileItem::GetItemHeight() const
{
	return FOptionalSize(TileModel->GetLevelSize2D().Y);
}

FSlateRect SWorldTileItem::GetItemRect() const
{
	FVector2D LevelSize = TileModel->GetLevelSize2D();
	FVector2D LevelPos = GetPosition();
	return FSlateRect(LevelPos, LevelPos + LevelSize);
}

TSharedPtr<IToolTip> SWorldTileItem::GetToolTip()
{
	// Hide tooltip in case item is being dragged now
	if (TileModel->GetLevelTranslationDelta().Size() > KINDA_SMALL_NUMBER)
	{
		return NULL;
	}
	
	return SNodePanel::SNode::GetToolTip();
}

FVector2D SWorldTileItem::GetDesiredSizeForMarquee() const
{
	// we don't want to select items in non visible layers
	if (!WorldModel->PassesAllFilters(TileModel))
	{
		return FVector2D::ZeroVector;
	}

	return SNodePanel::SNode::GetDesiredSizeForMarquee();
}

FVector2D SWorldTileItem::ComputeDesiredSize() const
{
	return TileModel->GetLevelSize2D();
}

int32 SWorldTileItem::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& ClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bIsVisible = FSlateRect::DoRectanglesIntersect(AllottedGeometry.GetClippingRect(), ClippingRect);
	
	if (bIsVisible)
	{
		// Redraw thumbnail image if requested
		if (bNeedRefresh)
		{
			Thumbnail->UpdateThumbnail();
			bNeedRefresh = false;
		}
		
		LayerId = SNodePanel::SNode::OnPaint(Args, AllottedGeometry, ClippingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
		
		const bool bSelected = (IsItemSelected() || bAffectedByMarquee);

		// Draw the node's selection.
		if (bSelected)
		{
			FPaintGeometry SelectionGeometry = AllottedGeometry.ToInflatedPaintGeometry(FVector2D(4,4)/AllottedGeometry.Scale);
			SelectionGeometry.DrawScale = 0.5;
				
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				SelectionGeometry,
				GetShadowBrush(bSelected),
				ClippingRect
				);
		}

		// Highlight potentially visible streaming levels from current preview point
		//const auto& PreviewStreamingLevels = WorldModel->GetPreviewStreamingLevels();
		//if (PreviewStreamingLevels.Find(LevelModel->TileDetails->PackageName) != INDEX_NONE)
		//{
		//	FPaintGeometry SelectionGeometry = AllottedGeometry.ToInflatedPaintGeometry(FVector2D(4,4)/AllottedGeometry.Scale);
		//	SelectionGeometry.DrawScale = 0.5;

		//	FSlateDrawElement::MakeBox(
		//		OutDrawElements,
		//		LayerId + 1,
		//		SelectionGeometry,
		//		GetShadowBrush(true),
		//		ClippingRect,
		//		ESlateDrawEffect::None,
		//		FLinearColor::Green
		//		);
		//}

		// Draw progress bar if level is currently loading
		if (TileModel->IsLoading())
		{
			const float ProgressBarAnimOffset = ProgressBarImage->ImageSize.X * CurveSequence.GetLerpLooping() / AllottedGeometry.Scale;
			const float ProgressBarImageSize = ProgressBarImage->ImageSize.X / AllottedGeometry.Scale;
			const float ProgressBarImageHeight = ProgressBarImage->ImageSize.Y / AllottedGeometry.Scale;
			const FSlateRect ProgressBarClippingRect = AllottedGeometry.ToPaintGeometry().ToSlateRect().IntersectionWith(ClippingRect);
		
			FPaintGeometry LoadingBarPaintGeometry = AllottedGeometry.ToPaintGeometry(
					FVector2D(ProgressBarAnimOffset - ProgressBarImageSize, 0 ),
					FVector2D(AllottedGeometry.Size.X + ProgressBarImageSize, FMath::Min(AllottedGeometry.Size.Y, ProgressBarImageHeight))
			);
			LoadingBarPaintGeometry.DrawScale = 1.f;
								
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				LoadingBarPaintGeometry,
				ProgressBarImage,
				ProgressBarClippingRect,
				ESlateDrawEffect::None,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.5f)
			);
		}
	}
	
	return LayerId;
}

FReply SWorldTileItem::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	TileModel->MakeLevelCurrent();
	return FReply::Handled();
}

FString SWorldTileItem::GetLevelNameText() const
{
	return TileModel->GetDisplayName();
}

FText SWorldTileItem::GetPositionText() const
{
	FIntPoint Position = TileModel->GetRelativeLevelPosition();
	return FText::FromString(FString::Printf(TEXT("%d, %d"), Position.X, Position.Y));
}

FText SWorldTileItem::GetBoundsExtentText() const
{
	FVector2D Size = TileModel->GetLevelSize2D();
	return FText::FromString(FString::Printf(TEXT("%d, %d"), FMath::RoundToInt(Size.X*0.5f), FMath::RoundToInt(Size.Y*0.5f)));
}

FText SWorldTileItem::GetLevelLayerNameText() const
{
	return FText::FromString(TileModel->TileDetails->Layer.Name);
}

FText SWorldTileItem::GetLevelLayerDistanceText() const
{
	return FText::AsNumber(TileModel->TileDetails->Layer.StreamingDistance);
}

bool SWorldTileItem::IsItemEditable() const
{
	return TileModel->IsEditable();
}

bool SWorldTileItem::IsItemSelected() const
{
	return TileModel->GetLevelSelectionFlag();
}

bool SWorldTileItem::IsItemEnabled() const
{
	if (WorldModel->IsSimulating())
	{
		return TileModel->IsVisible();
	}
	else
	{
		return TileModel->IsEditable();
	}
}

#undef LOCTEXT_NAMESPACE
