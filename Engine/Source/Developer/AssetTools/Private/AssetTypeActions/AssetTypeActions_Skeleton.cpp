// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "AssetToolsPrivatePCH.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/AssetEditorManager.h"
#include "Editor/UnrealEd/Public/PackageTools.h"
#include "AssetRegistryModule.h"
#include "Editor/Persona/Public/PersonaModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "SSkeletonWidget.h"
#include "AnimationEditorUtils.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Animation/Rig.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

/** 
* FBoneCheckbox
* 
* Context data for the SCreateRigDlg panel check boxes
*/
struct FBoneCheckbox
{
	FName BoneName;
	int32 BoneID;
	bool  bUsed;
};

/** 
* FCreateRigDlg
* 
* Wrapper class for SCreateRigDlg. This class creates and launches a dialog then awaits the
* result to return to the user. 
*/
class FCreateRigDlg
{
public:
	enum EResult
	{
		Cancel = 0,			// No/Cancel, normal usage would stop the current action
		Confirm = 1,		// Yes/Ok/Etc, normal usage would continue with action
	};

	FCreateRigDlg( USkeleton* InSkeleton );

	/**  Shows the dialog box and waits for the user to respond. */
	EResult ShowModal();

	// Map of RequiredBones of (BoneIndex, ParentIndex)
	TMap<int32, int32> RequiredBones;
private:

	/** Cached pointer to the modal window */
	TSharedPtr<SWindow> DialogWindow;

	/** Cached pointer to the merge skeleton widget */
	TSharedPtr<class SCreateRigDlg> DialogWidget;

	/** The Skeleton to merge bones to */
	USkeleton* Skeleton;
};

/** 
 * Slate panel for choosing which bones to merge into the skeleton
 */
class SCreateRigDlg : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SCreateRigDlg)
		{}
		/** Window in which this widget resides */
		SLATE_ATTRIBUTE(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()	

	/**
	 * Constructs this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
	void Construct( const FArguments& InArgs )
	{
		UserResponse = FCreateRigDlg::Cancel;
		ParentWindow = InArgs._ParentWindow.Get();

		this->ChildSlot[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 4.0f, 8.0f, 4.0f)
			[
				SNew( STextBlock )
				.Text( LOCTEXT("MergeSkeletonDlgDescription", "Would you like to add following bones to the skeleton?"))
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 4.0f, 8.0f, 4.0f)
			[
				SNew(SSeparator)
			]
			+SVerticalBox::Slot()
			.Padding(8.0f, 4.0f, 8.0f, 4.0f)
			[
				SNew(SBorder)
				[
					SNew(SScrollBox)
					+SScrollBox::Slot()
					[
						//Save this widget so we can populate it later with check boxes
						SAssignNew(CheckBoxContainer, SVerticalBox)
					]
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			.Padding(8.0f, 4.0f, 8.0f, 4.0f)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
				.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
				.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
				+SUniformGridPanel::Slot(0,0)
				[
					SNew(SButton) 
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.OnClicked(this, &SCreateRigDlg::ChangeAllOptions, true)
					.Text(LOCTEXT("SkeletonMergeSelectAll", "Select All"))
				]
				+SUniformGridPanel::Slot(1,0)
				[
					SNew(SButton) 
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.OnClicked(this, &SCreateRigDlg::ChangeAllOptions, false)
					.Text(LOCTEXT("SkeletonMergeDeselectAll", "Deselect All"))
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 4.0f, 8.0f, 4.0f)
			[
				SNew(SSeparator)
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(8.0f, 4.0f, 8.0f, 4.0f)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
				.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
				.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
				+SUniformGridPanel::Slot(0,0)
				[
					SNew(SButton) 
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.OnClicked( this, &SCreateRigDlg::OnButtonClick, FCreateRigDlg::Confirm )
					.Text(LOCTEXT("SkeletonMergeOk", "OK"))
				]
				+SUniformGridPanel::Slot(1,0)
				[
					SNew(SButton) 
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.OnClicked( this, &SCreateRigDlg::OnButtonClick, FCreateRigDlg::Cancel )
					.Text(LOCTEXT("SkeletonMergeCancel", "Cancel"))
				]
			]
		];
	}
	END_SLATE_FUNCTION_BUILD_OPTIMIZATION

	/**
	 * Creates a Slate check box 
	 *
	 * @param	Label		Text label for the check box
	 * @param	ButtonId	The ID for the check box
	 * @return				The created check box widget
	 */
	TSharedRef<SWidget> CreateCheckBox( const FString& Label, int32 ButtonId )
	{
		return
			SNew(SCheckBox)
			.IsChecked( this, &SCreateRigDlg::IsCheckboxChecked, ButtonId )
			.OnCheckStateChanged( this, &SCreateRigDlg::OnCheckboxChanged, ButtonId )
			[
				SNew(STextBlock).Text(Label)
			];
	}

	/**
	 * Returns the state of the check box
	 *
	 * @param	ButtonId	The ID for the check box
	 * @return				The status of the check box
	 */
	ESlateCheckBoxState::Type IsCheckboxChecked( int32 ButtonId ) const
	{
		return CheckBoxInfoMap.FindChecked(ButtonId).bUsed ? ESlateCheckBoxState::Checked : ESlateCheckBoxState::Unchecked;
	}

	/**
	 * Handler for all check box clicks
	 *
	 * @param	NewCheckboxState	The new state of the check box
	 * @param	CheckboxThatChanged	The ID of the radio button that has changed. 
	 */
	void OnCheckboxChanged( ESlateCheckBoxState::Type NewCheckboxState, int32 CheckboxThatChanged )
	{
		FBoneCheckbox& Info = CheckBoxInfoMap.FindChecked(CheckboxThatChanged);
		Info.bUsed = !Info.bUsed;
	}

	/**
	 * Handler for the Select All and Deselect All buttons
	 *
	 * @param	bNewCheckedState	The new state of the check boxes
	 */
	FReply ChangeAllOptions(bool bNewCheckedState)
	{
		for(auto Iter = CheckBoxInfoMap.CreateIterator(); Iter; ++Iter)
		{
			FBoneCheckbox& Info = Iter.Value();
			Info.bUsed = bNewCheckedState;
		}
		return FReply::Handled();
	}

	/**
	 * Populated the dialog with multiple check boxes, each corresponding to a bone
	 *
	 * @param	BoneInfos	The list of Bones to populate the dialog with
	 */
	void PopulateOptions(TArray<FBoneCheckbox>& BoneInfos)
	{
		for(auto Iter = BoneInfos.CreateIterator(); Iter; ++Iter)
		{
			FBoneCheckbox& Info = (*Iter);
			Info.bUsed = true;

			CheckBoxInfoMap.Add(Info.BoneID, Info);

			CheckBoxContainer->AddSlot()
			.AutoHeight()
			[
				CreateCheckBox(Info.BoneName.GetPlainNameString(), Info.BoneID)
			];
		}
	}

	/** 
	 * Returns the EResult of the button which the user pressed. Closing of the dialog
	 * in any other way than clicking "Ok" results in this returning a "Cancel" value
	 */
	FCreateRigDlg::EResult GetUserResponse() const 
	{
		return UserResponse; 
	}

	/** 
	 * Returns whether the user selected that bone to be used (checked its respective check box)
	 */
	bool IsBoneIncluded(int32 BoneID)
	{
		auto* Item = CheckBoxInfoMap.Find(BoneID);
		return Item ? Item->bUsed : false;
	}

private:
	
	/** 
	 * Handles when a button is pressed, should be bound with appropriate EResult Key
	 * 
	 * @param ButtonID - The return type of the button which has been pressed.
	 */
	FReply OnButtonClick(FCreateRigDlg::EResult ButtonID)
	{
		ParentWindow->RequestDestroyWindow();
		UserResponse = ButtonID;

		return FReply::Handled();
	}

	/** Stores the users response to this dialog */
	FCreateRigDlg::EResult	 UserResponse;

	/** The slate container that the bone check boxes get added to */
	TSharedPtr<SVerticalBox>	 CheckBoxContainer;
	/** Store the check box state for each bone */
	TMap<int32,FBoneCheckbox> CheckBoxInfoMap;

	/** Pointer to the window which holds this Widget, required for modal control */
	TSharedPtr<SWindow>			 ParentWindow;
};

FCreateRigDlg::FCreateRigDlg( USkeleton* InSkeleton )
{
	Skeleton = InSkeleton;

	if (FSlateApplication::IsInitialized())
	{
		DialogWindow = SNew(SWindow)
			.Title( LOCTEXT("MergeSkeletonDlgTitle", "Merge Bones") )
			.SupportsMinimize(false) .SupportsMaximize(false)
			.ClientSize(FVector2D(350, 500));

		TSharedPtr<SBorder> DialogWrapper = 
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(4.0f)
			[
				SAssignNew(DialogWidget, SCreateRigDlg)
				.ParentWindow(DialogWindow)
			];

		DialogWindow->SetContent(DialogWrapper.ToSharedRef());
	}
}

FCreateRigDlg::EResult FCreateRigDlg::ShowModal()
{
	RequiredBones.Empty();

	TArray<FBoneCheckbox> BoneInfos;

	// Make a list of all skeleton bone list
	const FReferenceSkeleton & RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FBoneNode> & BoneTree = Skeleton->GetBoneTree();
	for ( int32 BoneTreeId=0; BoneTreeId<RefSkeleton.GetNum(); ++BoneTreeId )
	{
		const FName & BoneName = RefSkeleton.GetBoneName(BoneTreeId);

		FBoneCheckbox Info;
		Info.BoneID = BoneTreeId;
		Info.BoneName = BoneName;
		BoneInfos.Add(Info);
	}

	if (BoneInfos.Num() == 0)
	{
		// something wrong
		return EResult::Cancel;
	}

	DialogWidget->PopulateOptions(BoneInfos);

	//Show Dialog
	GEditor->EditorAddModalWindow(DialogWindow.ToSharedRef());
	EResult UserResponse = (EResult)DialogWidget->GetUserResponse();

	if(UserResponse == EResult::Confirm)
	{
		for ( int32 RefBoneId= 0 ; RefBoneId< RefSkeleton.GetNum() ; ++RefBoneId )
		{
			if ( DialogWidget->IsBoneIncluded(RefBoneId) )
			{
				// I need to find parent that exists in the RefSkeleton
				int32 ParentIndex = RefSkeleton.GetParentIndex(RefBoneId);
				bool bFoundParent = false;

				// make sure RequiredBones already have ParentIndex
				while (ParentIndex >= 0 )
				{
					// if I don't have it yet
					if ( RequiredBones.Contains(ParentIndex) )
					{
						bFoundParent = true;
						// find the Parent that is related
						break;
					}
					else
					{
						ParentIndex = RefSkeleton.GetParentIndex(ParentIndex);					
					}
				}

				if (bFoundParent)
				{
					RequiredBones.Add(RefBoneId, ParentIndex);
				}
				else
				{
					RequiredBones.Add(RefBoneId, INDEX_NONE);
				}
			}
		}
	}

	return (RequiredBones.Num() > 0)? EResult::Confirm : EResult::Cancel;
}

///////////////////////////////
void FAssetTypeActions_Skeleton::GetActions( const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder )
{
	auto Skeletons = GetTypedWeakObjectPtrs<USkeleton>(InObjects);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Skeleton_Edit", "Edit"),
		LOCTEXT("Skeleton_EditTooltip", "Opens the selected skeleton in the persona."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP( this, &FAssetTypeActions_Skeleton::ExecuteEdit, Skeletons ),
			FCanExecuteAction()
			)
		);

	// create menu
	MenuBuilder.AddSubMenu(
			LOCTEXT("CreateSkeletonSubmenu", "Create"),
			LOCTEXT("CreateSkeletonSubmenu_ToolTip", "Create assets for this skeleton"),
			FNewMenuDelegate::CreateSP(this, &FAssetTypeActions_Skeleton::FillCreateMenu, Skeletons));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Skeleton_Retarget", "Retarget to Another Skeleton"),
		LOCTEXT("Skeleton_RetargetTooltip", "Allow all animation assets for this skeleton retarget to another skeleton."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP( this, &FAssetTypeActions_Skeleton::ExecuteRetargetSkeleton, Skeletons ),
			FCanExecuteAction()
			)
		);

	if (InObjects.Num() == 1)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Skeleton_CreateRig", "Create Rig"),
			LOCTEXT("Skeleton_CreateRigTooltip", "Create Rig from this skeleton."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAssetTypeActions_Skeleton::ExecuteCreateRig, Skeletons),
				FCanExecuteAction()
				)
			);
	}
	// @todo ImportAnimation
	/*
	MenuBuilder.AddMenuEntry(
		LOCTEXT("Skeleton_ImportAnimation", "Import Animation"),
		LOCTEXT("Skeleton_ImportAnimationTooltip", "Imports an animation for the selected skeleton."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP( this, &FAssetTypeActions_Skeleton::ExecuteImportAnimation, Skeletons ),
			FCanExecuteAction()
			)
		);
	*/
}

void FAssetTypeActions_Skeleton::FillCreateMenu(FMenuBuilder& MenuBuilder, TArray<TWeakObjectPtr<USkeleton>> Skeletons) const
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("Skeleton_NewAnimBlueprint", "Create Anim Blueprint"),
		LOCTEXT("Skeleton_NewAnimBlueprintTooltip", "Creates an Anim Blueprint using the selected skeleton."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_Skeleton::ExecuteNewAnimBlueprint, Skeletons),
			FCanExecuteAction()
			)
		);

	FAnimAssetCreated OnAssetCreated;
	OnAssetCreated.BindSP(this, &FAssetTypeActions_Skeleton::OnAssetCreated);
	AnimationEditorUtils::FillCreateAssetMenu(MenuBuilder, Skeletons, /*OnAssetCreated*/FAnimAssetCreated::CreateSP(this, &FAssetTypeActions_Skeleton::OnAssetCreated));
}

void FAssetTypeActions_Skeleton::OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor )
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		auto Skeleton = Cast<USkeleton>(*ObjIt);
		if (Skeleton != NULL)
		{
			FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>( "Persona" );
			PersonaModule.CreatePersona( Mode, EditWithinLevelEditor, Skeleton, NULL, NULL, NULL );
		}
	}
}

void FAssetTypeActions_Skeleton::ExecuteEdit(TArray<TWeakObjectPtr<USkeleton>> Objects)
{
	for (auto ObjIt = Objects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		auto Object = (*ObjIt).Get();
		if ( Object )
		{
			FAssetEditorManager::Get().OpenEditorForAsset(Object);
		}
	}
}

void FAssetTypeActions_Skeleton::ExecuteNewAnimBlueprint(TArray<TWeakObjectPtr<USkeleton>> Objects)
{
	const FString DefaultSuffix = TEXT("_AnimBlueprint");

	if ( Objects.Num() == 1 )
	{
		auto Object = Objects[0].Get();

		if ( Object )
		{
			// Determine an appropriate name for inline-rename
			FString Name;
			FString PackageName;
			CreateUniqueAssetName(Object->GetOutermost()->GetName(), DefaultSuffix, PackageName, Name);

			UAnimBlueprintFactory* Factory = ConstructObject<UAnimBlueprintFactory>(UAnimBlueprintFactory::StaticClass());
			Factory->TargetSkeleton = Object;

			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			ContentBrowserModule.Get().CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackageName), UAnimBlueprint::StaticClass(), Factory);
		}
	}
	else
	{
		TArray<UObject*> ObjectsToSync;
		for (auto ObjIt = Objects.CreateConstIterator(); ObjIt; ++ObjIt)
		{
			auto Object = (*ObjIt).Get();
			if ( Object )
			{
				// Determine an appropriate name
				FString Name;
				FString PackageName;
				CreateUniqueAssetName(Object->GetOutermost()->GetName(), DefaultSuffix, PackageName, Name);

				// Create the anim blueprint factory used to generate the asset
				UAnimBlueprintFactory* Factory = ConstructObject<UAnimBlueprintFactory>(UAnimBlueprintFactory::StaticClass());
				Factory->TargetSkeleton = Object;

				FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
				UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackageName), UAnimBlueprint::StaticClass(), Factory);

				if ( NewAsset )
				{
					ObjectsToSync.Add(NewAsset);
				}
			}
		}

		if ( ObjectsToSync.Num() > 0 )
		{
			FAssetTools::Get().SyncBrowserToAssets(ObjectsToSync);
		}
	}
}

/** Handler for when Create Rig is selected */
void FAssetTypeActions_Skeleton::ExecuteCreateRig(TArray<TWeakObjectPtr<USkeleton>> Skeletons)
{
	if (Skeletons.Num() == 1)
	{
		CreateRig(Skeletons[0]);
	}
}

void FAssetTypeActions_Skeleton::CreateRig(const TWeakObjectPtr<USkeleton> Skeleton)
{
	if(Skeleton.IsValid())
	{
		FCreateRigDlg CreateRigDlg(Skeleton.Get());
		if(CreateRigDlg.ShowModal() == FCreateRigDlg::Confirm)
		{
			check(CreateRigDlg.RequiredBones.Num() > 0);

			// Determine an appropriate name
			FString Name;
			FString PackageName;
			CreateUniqueAssetName(Skeleton->GetOutermost()->GetName(), TEXT("Rig"), PackageName, Name);

			// Create the asset, and assign its skeleton
			FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
			URig* NewAsset = Cast<URig>(AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackageName), URig::StaticClass(), NULL));

			if(NewAsset)
			{
				NewAsset->CreateFromSkeleton(Skeleton.Get(), CreateRigDlg.RequiredBones);
				NewAsset->MarkPackageDirty();


				TArray<UObject*> ObjectsToSync;
				ObjectsToSync.Add(NewAsset);
				FAssetTools::Get().SyncBrowserToAssets(ObjectsToSync);
			}
		}
	}
}

void FAssetTypeActions_Skeleton::CreateAnimationAssets(const TArray<TWeakObjectPtr<USkeleton>>& Skeletons, TSubclassOf<UAnimationAsset> AssetClass, const FString& InPrefix)
{
	FAnimAssetCreated OnAssetCreated;
	OnAssetCreated.BindSP(this, &FAssetTypeActions_Skeleton::OnAssetCreated);
	AnimationEditorUtils::CreateAnimationAssets(Skeletons, AssetClass, InPrefix, /*OnAssetCreated*/FAnimAssetCreated::CreateSP(this, &FAssetTypeActions_Skeleton::OnAssetCreated));
}

void FAssetTypeActions_Skeleton::ExecuteRetargetSkeleton(TArray<TWeakObjectPtr<USkeleton>> Skeletons)
{
	// warn the user to shut down any persona that is opened
	if ( FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("CloseReferencingEditors", "You need to close Persona or anything that references animation, mesh or animation blueprint before this step. Continue?")) == EAppReturnType::Yes )
	{
		for (auto SkelIt = Skeletons.CreateConstIterator(); SkelIt; ++SkelIt)
		{	
			USkeleton * OldSkeleton = (*SkelIt).Get();
			USkeleton * NewSkeleton = NULL;

			const FText Message = LOCTEXT("RetargetSkeleton_Warning", "This only converts animation data -i.e. animation assets and Anim Blueprints. \nIf you'd like to convert SkeletalMesh, use the context menu (Assign Skeleton) for each mesh. \n\nIf you'd like to convert mesh as well, please do so before converting animation data. \nOtherwise you will lose any extra track that is in the new mesh.");

			bool bConvertSpaces = OldSkeleton != NULL;
			bool bShowOnlyCompatibleSkeletons = OldSkeleton != NULL;

			// ask user what they'd like to change to 
			if (SAnimationRemapSkeleton::ShowModal(OldSkeleton, NewSkeleton, Message, OldSkeleton? &bShowOnlyCompatibleSkeletons : NULL, OldSkeleton ? &bConvertSpaces : NULL))
			{
				// find all assets who references old skeleton
				TArray<FName> Packages;

				// If the asset registry is still loading assets, we cant check for referencers, so we must open the rename dialog
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				AssetRegistryModule.Get().GetReferencers(OldSkeleton->GetOutermost()->GetFName(), Packages);

				if ( AssetRegistryModule.Get().IsLoadingAssets() )
				{
					// Open a dialog asking the user to wait while assets are being discovered
					SDiscoveringAssetsDialog::OpenDiscoveringAssetsDialog(
						SDiscoveringAssetsDialog::FOnAssetsDiscovered::CreateSP(this, &FAssetTypeActions_Skeleton::PerformRetarget, OldSkeleton, NewSkeleton, Packages, bConvertSpaces)
						);
				}
				else
				{
					PerformRetarget(OldSkeleton, NewSkeleton, Packages, bConvertSpaces);
				}
				
			}
		}
	}
}

void FAssetTypeActions_Skeleton::PerformRetarget(USkeleton *OldSkeleton, USkeleton *NewSkeleton, TArray<FName> Packages, bool bConvertSpaces) const
{
	TArray<FAssetToRemapSkeleton> AssetsToRemap;

	// populate AssetsToRemap
	AssetsToRemap.Empty(Packages.Num());

	for ( int32 AssetIndex=0; AssetIndex<Packages.Num(); ++AssetIndex)
	{
		AssetsToRemap.Add( FAssetToRemapSkeleton(Packages[AssetIndex]) );
	}

	// Load all packages 
	TArray<UPackage*> PackagesToSave;
	LoadPackages(AssetsToRemap, PackagesToSave);

	// Update the source control state for the packages containing the assets we are remapping
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if ( ISourceControlModule::Get().IsEnabled() )
	{
		SourceControlProvider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), PackagesToSave);
	}

	// Prompt to check out all referencing packages, leave redirectors for assets referenced by packages that are not checked out and remove those packages from the save list.
	const bool bUserAcceptedCheckout = CheckOutPackages(AssetsToRemap, PackagesToSave);

	if ( bUserAcceptedCheckout )
	{
		// If any referencing packages are left read-only, the checkout failed or SCC was not enabled. Trim them from the save list and leave redirectors.
		DetectReadOnlyPackages(AssetsToRemap, PackagesToSave);

		// retarget skeleton
		RetargetSkeleton(AssetsToRemap, OldSkeleton, NewSkeleton, bConvertSpaces);

		// Save all packages that were referencing any of the assets that were moved without redirectors
		SavePackages(PackagesToSave);

		// Finally, report any failures that happened during the rename
		ReportFailures(AssetsToRemap);
	}
}

void FAssetTypeActions_Skeleton::LoadPackages(TArray<FAssetToRemapSkeleton>& AssetsToRemap, TArray<UPackage*>& OutPackagesToSave) const
{
	const FText StatusUpdate = LOCTEXT("RemapSkeleton_LoadPackage", "Loading Packages");
	GWarn->BeginSlowTask( StatusUpdate, true );

	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	// go through all assets try load
	for ( int32 AssetIdx = 0; AssetIdx < AssetsToRemap.Num(); ++AssetIdx )
	{
		GWarn->StatusUpdate( AssetIdx, AssetsToRemap.Num(), StatusUpdate );

		FAssetToRemapSkeleton& RemapData = AssetsToRemap[AssetIdx];

		const FString PackageName = RemapData.PackageName.ToString();

		// load package
		UPackage* Package = LoadPackage(NULL, *PackageName, LOAD_None);
		if ( !Package )
		{
			RemapData.ReportFailed( LOCTEXT("RemapSkeletonFailed_LoadPackage", "Could not load the package."));
			continue;
		}

		// get all the objects
		TArray<UObject*> Objects;
		GetObjectsWithOuter(Package, Objects);

		// see if we have skeletalmesh
		bool bSkeletalMeshPackage = false;
		for (auto Iter=Objects.CreateIterator(); Iter; ++Iter)
		{
			// we only care animation asset or animation blueprint
			if ((*Iter)->GetClass()->IsChildOf(UAnimationAsset::StaticClass()) ||
				(*Iter)->GetClass()->IsChildOf(UAnimBlueprint::StaticClass()))
			{
				// add to asset 
				RemapData.Asset = (*Iter);
				break;
			}
			else if ((*Iter)->GetClass()->IsChildOf(USkeletalMesh::StaticClass()) )
			{
				bSkeletalMeshPackage = true;
				break;
			}
		}

		// if we have skeletalmesh, we ignore this package, do not report as error
		if ( bSkeletalMeshPackage )
		{
			continue;
		}

		// if none was relevant - skeletal mesh is going to get here
		if ( !RemapData.Asset.IsValid() )
		{
			RemapData.ReportFailed( LOCTEXT("RemapSkeletonFailed_LoadObject", "Could not load any related object."));
			continue;
		}

		OutPackagesToSave.Add(Package);
	}

	GWarn->EndSlowTask();
}

bool FAssetTypeActions_Skeleton::CheckOutPackages(TArray<FAssetToRemapSkeleton>& AssetsToRemap, TArray<UPackage*>& InOutPackagesToSave) const
{
	bool bUserAcceptedCheckout = true;
	
	if ( InOutPackagesToSave.Num() > 0 )
	{
		if ( ISourceControlModule::Get().IsEnabled() )
		{
			TArray<UPackage*> PackagesCheckedOutOrMadeWritable;
			TArray<UPackage*> PackagesNotNeedingCheckout;
			bUserAcceptedCheckout = FEditorFileUtils::PromptToCheckoutPackages( false, InOutPackagesToSave, &PackagesCheckedOutOrMadeWritable, &PackagesNotNeedingCheckout );
			if ( bUserAcceptedCheckout )
			{
				TArray<UPackage*> PackagesThatCouldNotBeCheckedOut = InOutPackagesToSave;

				for ( auto PackageIt = PackagesCheckedOutOrMadeWritable.CreateConstIterator(); PackageIt; ++PackageIt )
				{
					PackagesThatCouldNotBeCheckedOut.Remove(*PackageIt);
				}

				for ( auto PackageIt = PackagesNotNeedingCheckout.CreateConstIterator(); PackageIt; ++PackageIt )
				{
					PackagesThatCouldNotBeCheckedOut.Remove(*PackageIt);
				}

				for ( auto PackageIt = PackagesThatCouldNotBeCheckedOut.CreateConstIterator(); PackageIt; ++PackageIt )
				{
					const FName NonCheckedOutPackageName = (*PackageIt)->GetFName();

					for ( auto RenameDataIt = AssetsToRemap.CreateIterator(); RenameDataIt; ++RenameDataIt )
					{
						FAssetToRemapSkeleton& RemapData = *RenameDataIt;

						if (RemapData.Asset.IsValid() && RemapData.Asset.Get()->GetOutermost() == *PackageIt)
						{
							RemapData.ReportFailed( LOCTEXT("RemapSkeletonFailed_CheckOutFailed", "Check out failed"));
						}
					}

					InOutPackagesToSave.Remove(*PackageIt);
				}
			}
		}
	}

	return bUserAcceptedCheckout;
}

void FAssetTypeActions_Skeleton::DetectReadOnlyPackages(TArray<FAssetToRemapSkeleton>& AssetsToRemap, TArray<UPackage*>& InOutPackagesToSave) const
{
	// For each valid package...
	for ( int32 PackageIdx = InOutPackagesToSave.Num() - 1; PackageIdx >= 0; --PackageIdx )
	{
		UPackage* Package = InOutPackagesToSave[PackageIdx];

		if ( Package )
		{
			// Find the package filename
			FString Filename;
			if ( FPackageName::DoesPackageExist(Package->GetName(), NULL, &Filename) )
			{
				// If the file is read only
				if ( IFileManager::Get().IsReadOnly(*Filename) )
				{
					FName PackageName = Package->GetFName();

					for ( auto RenameDataIt = AssetsToRemap.CreateIterator(); RenameDataIt; ++RenameDataIt )
					{
						FAssetToRemapSkeleton& RenameData = *RenameDataIt;

						if (RenameData.Asset.IsValid() && RenameData.Asset.Get()->GetOutermost() == Package)
						{
							RenameData.ReportFailed( LOCTEXT("RemapSkeletonFailed_FileReadOnly", "File still read only"));
						}
					}				

					// Remove the package from the save list
					InOutPackagesToSave.RemoveAt(PackageIdx);
				}
			}
		}
	}
}

void FAssetTypeActions_Skeleton::SavePackages(const TArray<UPackage*> PackagesToSave) const
{
	if ( PackagesToSave.Num() > 0 )
	{
		const bool bCheckDirty = false;
		const bool bPromptToSave = false;
		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);

		ISourceControlModule::Get().QueueStatusUpdate(PackagesToSave);
	}
}

void FAssetTypeActions_Skeleton::ReportFailures(const TArray<FAssetToRemapSkeleton>& AssetsToRemap) const
{
	TArray<FText> FailedToRemap;
	for ( auto RenameIt = AssetsToRemap.CreateConstIterator(); RenameIt; ++RenameIt )
	{
		const FAssetToRemapSkeleton& RemapData = *RenameIt;
		if ( RemapData.bRemapFailed )
		{
			UObject* Asset = RemapData.Asset.Get();
			if ( Asset )
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("FailureReason"), RemapData.FailureReason);
				Args.Add(TEXT("AssetName"), FText::FromString(Asset->GetOutermost()->GetName()));

				FailedToRemap.Add(FText::Format(LOCTEXT("AssetRemapFailure", "{AssetName} - {FailureReason}"), Args));
			}
			else
			{
				FailedToRemap.Add(LOCTEXT("RemapSkeletonFailed_InvalidAssetText", "Invalid Asset"));
			}
		}
	}

	if ( FailedToRemap.Num() > 0 )
	{
		SRemapFailures::OpenRemapFailuresDialog(FailedToRemap);
	}
}

void FAssetTypeActions_Skeleton::RetargetSkeleton(TArray<FAssetToRemapSkeleton>& AssetsToRemap, USkeleton* OldSkeleton, USkeleton* NewSkeleton, bool bConvertSpaces) const
{
	TArray<UAnimBlueprint*>	AnimBlueprints;

	// first we convert all individual assets
	for ( auto RenameIt = AssetsToRemap.CreateIterator(); RenameIt; ++RenameIt )
	{
		FAssetToRemapSkeleton& RenameData = *RenameIt;
		if ( RenameData.bRemapFailed == false  )
		{
			UObject* Asset = RenameData.Asset.Get();
			if ( Asset )
			{
				if ( UAnimationAsset * AnimAsset = Cast<UAnimationAsset>(Asset) )
				{
					UAnimSequenceBase * SequenceBase = Cast<UAnimSequenceBase>(AnimAsset);
					if (SequenceBase)
					{
						// Copy curve data from source asset, preserving data in the target if present.
						FSmartNameMapping* OldNameMapping = OldSkeleton->SmartNames.GetContainer(USkeleton::AnimCurveMappingName);
						FSmartNameMapping* NewNameMapping = NewSkeleton->SmartNames.GetContainer(USkeleton::AnimCurveMappingName);
						SequenceBase->RawCurveData.UpdateLastObservedNames(OldNameMapping);

						for(FFloatCurve& Curve : SequenceBase->RawCurveData.FloatCurves)
						{
							NewNameMapping->AddName(Curve.LastObservedName, Curve.CurveUid);
						}
					}
					
					AnimAsset->ReplaceSkeleton(NewSkeleton, bConvertSpaces);
				}
				else if ( UAnimBlueprint * AnimBlueprint = Cast<UAnimBlueprint>(Asset) )
				{
					AnimBlueprints.Add(AnimBlueprint);
				}
			}
		}
	}

	// convert all Animation Blueprints and compile 
	for ( auto AnimBPIter = AnimBlueprints.CreateIterator(); AnimBPIter; ++AnimBPIter )
	{
		UAnimBlueprint * AnimBlueprint = (*AnimBPIter);
		AnimBlueprint->TargetSkeleton = NewSkeleton;
		
		bool bIsRegeneratingOnLoad = false;
		bool bSkipGarbageCollection = true;
		FBlueprintEditorUtils::RefreshAllNodes(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint, bIsRegeneratingOnLoad, bSkipGarbageCollection);
	}

	// now update any running instance
	for (FObjectIterator Iter(USkeletalMeshComponent::StaticClass()); Iter; ++Iter)
	{
		USkeletalMeshComponent * MeshComponent = Cast<USkeletalMeshComponent>(*Iter);
		if (MeshComponent->SkeletalMesh && MeshComponent->SkeletalMesh->Skeleton == OldSkeleton)
		{
			MeshComponent->InitAnim(true);
		}
	}
}

void FAssetTypeActions_Skeleton::OnAssetCreated(TArray<UObject*> NewAssets) const
{
	if(NewAssets.Num() > 1)
	{
		FAssetTools::Get().SyncBrowserToAssets(NewAssets);
	}

}
#undef LOCTEXT_NAMESPACE
