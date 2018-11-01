// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "AssetData.h"
#include "IAssetTypeActions.h"
#include "ContentBrowserDelegates.h"
#include "SNiagaraNewAssetDialog.h"

class SNiagaraTemplateAssetPicker;
class SWrapBox;

/** A modal dialog to collect information needed to create a new niagara system. */
class SNewSystemDialog : public SNiagaraNewAssetDialog
{
public:
	SLATE_BEGIN_ARGS(SNewSystemDialog)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	TOptional<FAssetData> GetSelectedSystemAsset() const;

	TArray<FAssetData> GetSelectedEmitterAssets() const;

private:
	void GetSelectedSystemTemplateAssets(TArray<FAssetData>& OutSelectedAssets);

	void GetSelectedProjectSystemAssets(TArray<FAssetData>& OutSelectedAssets);

	void GetSelectedProjectEmiterAssets(TArray<FAssetData>& OutSelectedAssets);

	void OnTemplateAssetActivated(const FAssetData& ActivatedTemplateAsset);

	void OnSystemAssetsActivated(const TArray<FAssetData>& ActivatedAssets, EAssetTypeActivationMethod::Type ActivationMethod);

	void OnEmitterAssetsActivated(const TArray<FAssetData>& ActivatedAssets, EAssetTypeActivationMethod::Type ActivationMethod);

	bool IsAddEmittersToSelectionButtonEnabled() const;
	
	FReply AddEmittersToSelectionButtonClicked();

	void AddEmitterAssetsToSelection(const TArray<FAssetData>& EmitterAssets);

	FReply RemoveEmitterFromSelectionButtonClicked(FAssetData EmitterAsset);

private:
	FGetCurrentSelectionDelegate GetSelectedSystemAssetsFromPicker;

	FGetCurrentSelectionDelegate GetSelectedEmitterAssetsFromPicker;

	FAssetData ActivatedTemplateSystemAsset;

	FAssetData ActivatedProjectSystemAsset;

	TArray<FAssetData> SelectedEmitterAssets;

	TArray<TSharedPtr<SWidget>> SelectedEmitterAssetWidgets;

	TSharedPtr<SWrapBox> SelectedEmitterBox;

	TSharedPtr<SNiagaraTemplateAssetPicker> TemplateAssetPicker;
};