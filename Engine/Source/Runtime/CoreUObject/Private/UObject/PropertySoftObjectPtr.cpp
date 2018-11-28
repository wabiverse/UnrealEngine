// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"

/*-----------------------------------------------------------------------------
	USoftObjectProperty.
-----------------------------------------------------------------------------*/

FString USoftObjectProperty::GetCPPTypeCustom(FString* ExtendedTypeText, uint32 CPPExportFlags, const FString& InnerNativeTypeName) const
{
	ensure(!InnerNativeTypeName.IsEmpty());
	return FString::Printf(TEXT("TSoftObjectPtr<%s>"), *InnerNativeTypeName);
}
FString USoftObjectProperty::GetCPPMacroType( FString& ExtendedTypeText ) const
{
	ExtendedTypeText = FString::Printf(TEXT("TSoftObjectPtr<%s%s>"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName());
	return TEXT("SOFTOBJECT");
}

FString USoftObjectProperty::GetCPPTypeForwardDeclaration() const
{
	return FString::Printf(TEXT("class %s%s;"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName());
}

FName USoftObjectProperty::GetID() const
{
	// SoftClass shares the same tag, they are binary compatible
	return NAME_SoftObjectProperty;
}

// this is always shallow, can't see that we would want it any other way
bool USoftObjectProperty::Identical( const void* A, const void* B, uint32 PortFlags ) const
{
	FSoftObjectPtr ObjectA = A ? *((FSoftObjectPtr*)A) : FSoftObjectPtr();
	FSoftObjectPtr ObjectB = B ? *((FSoftObjectPtr*)B) : FSoftObjectPtr();

	return ObjectA.GetUniqueID() == ObjectB.GetUniqueID();
}

void USoftObjectProperty::SerializeItem( FStructuredArchive::FSlot Slot, void* Value, void const* Defaults ) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	// We never serialize our reference while the garbage collector is harvesting references
	// to objects, because we don't want soft object pointers to keep objects from being garbage collected
	// Allow persistent archives so they can keep track of string references. (e.g. FArchiveSaveTagImports)
	if( !UnderlyingArchive.IsObjectReferenceCollector() || UnderlyingArchive.IsModifyingWeakAndStrongReferences() || UnderlyingArchive.IsPersistent() )
	{
		FSoftObjectPtr OldValue = *(FSoftObjectPtr*)Value;
		Slot << *(FSoftObjectPtr*)Value;

		if (UnderlyingArchive.IsLoading() || UnderlyingArchive.IsModifyingWeakAndStrongReferences()) 
		{
			if (OldValue.GetUniqueID() != ((FSoftObjectPtr*)Value)->GetUniqueID())
			{
				CheckValidObject(Value);
			}
		}
	}
	else
	{
		// TODO: This isn't correct, but it keeps binary serialization happy. We should ALWAYS be serializing the pointer
		// to the archive in this function, and allowing the underlying archive to ignore it if necessary
		Slot.EnterStream();
	}
}

bool USoftObjectProperty::NetSerializeItem(FArchive& Ar, UPackageMap* Map, void* Data, TArray<uint8>* MetaData) const
{
	// Serialize directly, will use FBitWriter/Reader
	Ar << *(FSoftObjectPtr*)Data;

	return true;
}

void USoftObjectProperty::ExportTextItem( FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope ) const
{
	FSoftObjectPtr& SoftObjectPtr = *(FSoftObjectPtr*)PropertyValue;

	FSoftObjectPath SoftObjectPath;
	UObject *Object = SoftObjectPtr.Get();

	if (Object)
	{
		// Use object in case name has changed.
		SoftObjectPath = FSoftObjectPath(Object);
	}
	else
	{
		SoftObjectPath = SoftObjectPtr.GetUniqueID();
	}

	if (0 != (PortFlags & PPF_ExportCpp))
	{
		ValueStr += FString::Printf(TEXT("FSoftObjectPath(TEXT(\"%s\"))"), *SoftObjectPath.ToString().ReplaceCharWithEscapedChar());
		return;
	}

	SoftObjectPath.ExportTextItem(ValueStr, SoftObjectPath, Parent, PortFlags, ExportRootScope);
}

const TCHAR* USoftObjectProperty::ImportText_Internal( const TCHAR* InBuffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText ) const
{
	FSoftObjectPtr& SoftObjectPtr = *(FSoftObjectPtr*)Data;

	FSoftObjectPath SoftObjectPath;

	if (SoftObjectPath.ImportTextItem(InBuffer, PortFlags, Parent, ErrorText))
	{
		SoftObjectPtr = SoftObjectPath;
		return InBuffer;
	}

	else
	{
		SoftObjectPtr = nullptr;
		return nullptr;
	}
}

EConvertFromTypeResult USoftObjectProperty::ConvertFromType(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct)
{
	static FName NAME_AssetObjectProperty = "AssetObjectProperty";
	static FName NAME_SoftObjectPath = "SoftObjectPath";
	static FName NAME_SoftClassPath = "SoftClassPath";
	static FName NAME_StringAssetReference = "StringAssetReference";
	static FName NAME_StringClassReference = "StringClassReference";

	FArchive& Archive = Slot.GetUnderlyingArchive();

	if (Tag.Type == NAME_AssetObjectProperty)
	{
		// Old name of soft object property, serialize normally
		uint8* DestAddress = ContainerPtrToValuePtr<uint8>(Data, Tag.ArrayIndex);

		Tag.SerializeTaggedProperty(Slot, this, DestAddress, nullptr);

		if (Archive.IsCriticalError())
		{
			return EConvertFromTypeResult::CannotConvert;
		}

		return EConvertFromTypeResult::Converted;
	}
	else if (Tag.Type == NAME_ObjectProperty)
	{
		// This property used to be a raw UObjectProperty Foo* but is now a TSoftObjectPtr<Foo>;
		// Serialize from mismatched tag directly into the FSoftObjectPtr's soft object path to ensure that the delegates needed for cooking
		// are fired
		FSoftObjectPtr* PropertyValue = GetPropertyValuePtr_InContainer(Data, Tag.ArrayIndex);
		check(PropertyValue);

		return PropertyValue->GetUniqueID().SerializeFromMismatchedTag(Tag, Slot) ? EConvertFromTypeResult::Converted : EConvertFromTypeResult::UseSerializeItem;
	}
	else if (Tag.Type == NAME_StructProperty && (Tag.StructName == NAME_SoftObjectPath || Tag.StructName == NAME_SoftClassPath || Tag.StructName == NAME_StringAssetReference || Tag.StructName == NAME_StringClassReference))
	{
		// This property used to be a FSoftObjectPath but is now a TSoftObjectPtr<Foo>
		FSoftObjectPath PreviousValue;
		// explicitly call Serialize to ensure that the various delegates needed for cooking are fired
		PreviousValue.Serialize(Slot);

		// now copy the value into the object's address space
		FSoftObjectPtr PreviousValueSoftObjectPtr;
		PreviousValueSoftObjectPtr = PreviousValue;
		SetPropertyValue_InContainer(Data, PreviousValueSoftObjectPtr, Tag.ArrayIndex);

		return EConvertFromTypeResult::Converted;
	}

	return EConvertFromTypeResult::UseSerializeItem;
}

UObject* USoftObjectProperty::GetObjectPropertyValue(const void* PropertyValueAddress) const
{
	return GetPropertyValue(PropertyValueAddress).Get();
}

void USoftObjectProperty::SetObjectPropertyValue(void* PropertyValueAddress, UObject* Value) const
{
	SetPropertyValue(PropertyValueAddress, TCppType(Value));
}

bool USoftObjectProperty::AllowCrossLevel() const
{
	return true;
}

uint32 USoftObjectProperty::GetValueTypeHashInternal(const void* Src) const
{
	return GetTypeHash(GetPropertyValue(Src));
}

void USoftObjectProperty::CopySingleValueToScriptVM(void* Dest, void const* Src) const
{
	CopySingleValue(Dest, Src);
}

void USoftObjectProperty::CopyCompleteValueToScriptVM(void* Dest, void const* Src) const
{
	CopyCompleteValue(Dest, Src);
}

void USoftObjectProperty::CopySingleValueFromScriptVM(void* Dest, void const* Src) const
{
	CopySingleValue(Dest, Src);
}

void USoftObjectProperty::CopyCompleteValueFromScriptVM(void* Dest, void const* Src) const
{
	CopyCompleteValue(Dest, Src);
}

IMPLEMENT_CORE_INTRINSIC_CLASS(USoftObjectProperty, UObjectPropertyBase,
	{
	}
);

