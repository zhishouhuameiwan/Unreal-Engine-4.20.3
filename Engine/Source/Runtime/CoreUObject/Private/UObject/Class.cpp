// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnClass.cpp: Object class implementation.
=============================================================================*/

#include "CoreUObjectPrivate.h"

#include "PropertyTag.h"

DECLARE_LOG_CATEGORY_EXTERN(LogScriptSerialization, Log, All);
DEFINE_LOG_CATEGORY(LogScriptSerialization);
DEFINE_LOG_CATEGORY(LogClass);


/*----------------------------------------------------------------------------
	FArchiveScriptReferenceCollector.
----------------------------------------------------------------------------*/

class FArchiveScriptReferenceCollector : public FArchiveUObject
{
public:
	/**
	 * Constructor
	 *
	 * @param	InObjectArray			Array to add object references to
	 */
	FArchiveScriptReferenceCollector( TArray<UObject*>& InObjectArray )
	:	ObjectArray( InObjectArray )
	{
		ArIsObjectReferenceCollector = true;
		ArIsPersistent = false;
		ArIgnoreArchetypeRef = false;
	}
protected:
	/** 
	 * UObject serialize operator implementation
	 *
	 * @param Object	reference to Object reference
	 * @return reference to instance of this class
	 */
	FArchive& operator<<( UObject*& Object )
	{
		// Avoid duplicate entries.
		if ( Object != NULL && !ObjectArray.Contains(Object) )
		{
			check( Object->IsValidLowLevel() );
			ObjectArray.Add( Object );
		}
		return *this;
	}

	/** Stored reference to array of objects we add object references to */
	TArray<UObject*>&		ObjectArray;
};

//////////////////////////////////////////////////////////////////////////
// FPropertySpecifier

FString FPropertySpecifier::ConvertToString() const
{
	FString Result;

	// Emit the specifier key
	Result += Key;

	// Emit the values if there are any
	if (Values.Num())
	{
		Result += TEXT("=");

		if (Values.Num() == 1)
		{
			// One value goes on it's own
			Result += Values[0];
		}
		else
		{
			// More than one value goes in parens, separated by commas
			Result += TEXT("(");
			for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
			{
				if (ValueIndex > 0)
				{
					Result += TEXT(", ");
				}
				Result += Values[ValueIndex];
			}
			Result += TEXT(")");
		}
	}

	return Result;
}

//////////////////////////////////////////////////////////////////////////

/*
 * Shared function called from the various InitializePrivateStaticClass functions generated my the IMPLEMENT_CLASS macro.
 */
COREUOBJECT_API void InitializePrivateStaticClass(
	class UClass* TClass_Super_StaticClass,
	class UClass* TClass_PrivateStaticClass,
	class UClass* TClass_WithinClass_StaticClass,
	const TCHAR* PackageName,
	const TCHAR* Name
	)
{
	/* No recursive ::StaticClass calls allowed. Setup extras. */
	if (TClass_Super_StaticClass != TClass_PrivateStaticClass)
	{
		TClass_PrivateStaticClass->SetSuperStruct(TClass_Super_StaticClass);
	}
	else
	{
		TClass_PrivateStaticClass->SetSuperStruct(NULL);
	}
	TClass_PrivateStaticClass->ClassWithin = TClass_WithinClass_StaticClass;

	// Register the class's dependencies, then itself.
	TClass_PrivateStaticClass->RegisterDependencies();
	TClass_PrivateStaticClass->Register(PackageName,Name);
}

void FNativeFunctionRegistrar::RegisterFunction(class UClass* Class, const ANSICHAR* InName, Native InPointer)
{
	Class->AddNativeFunction(InName, InPointer);
}


/*-----------------------------------------------------------------------------
	UField implementation.
-----------------------------------------------------------------------------*/

UField::UField( EStaticConstructor, EObjectFlags InFlags )
: UObject( EC_StaticConstructor, InFlags )
, Next( NULL )
{}

UClass* UField::GetOwnerClass() const
{
	UClass* OwnerClass = NULL;
	UObject* TestObject = const_cast<UField*>(this);

	while ((TestObject != NULL) && (OwnerClass == NULL))
	{
		OwnerClass = Cast<UClass>(TestObject);
		TestObject = TestObject->GetOuter();
	}

	return OwnerClass;
}

UStruct* UField::GetOwnerStruct() const
{
	const UObject* Obj;
	for ( Obj=this; Obj && !Obj->IsA(UStruct::StaticClass()); Obj=Obj->GetOuter() );
	return (UStruct*)Obj;
}

void UField::Bind()
{
}

void UField::PostLoad()
{
	Super::PostLoad();
	Bind();
}

void UField::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );
	Ar << Next;
}

void UField::AddCppProperty( UProperty* Property )
{
	UE_LOG(LogClass, Fatal,TEXT("UField::AddCppProperty"));
}

#if WITH_EDITOR || HACK_HEADER_GENERATOR
/**
 * Finds the localized display name or native display name as a fallback.
 *
 * @return The display name for this object.
 */
FText UField::GetDisplayNameText() const
{
	FText LocalizedDisplayName;

	static const FString Namespace = TEXT("UObjectDisplayNames");
	const FString Key = GetFullGroupName(true) + TEXT(".") + GetName();

	FString NativeDisplayName;
	if( HasMetaData( TEXT("DisplayName") ) )
	{
		NativeDisplayName = GetMetaData( TEXT("DisplayName") );
	}
	else
	{
		NativeDisplayName = FName::NameToDisplayString(GetName(), IsA<UBoolProperty>());
	}

	if ( !( FText::FindText( Namespace, Key, /*OUT*/LocalizedDisplayName, &NativeDisplayName ) ) )
	{
		LocalizedDisplayName = FText::FromString(NativeDisplayName );
	}

	return LocalizedDisplayName;
}

/**
 * Finds the localized tooltip or native tooltip as a fallback.
 *
 * @return The tooltip for this object.
 */
FText UField::GetToolTipText() const
{
	FText LocalizedToolTip;
	const FString NativeToolTip = GetMetaData( TEXT("Tooltip") );

	static const FString Namespace = TEXT("UObjectToolTips");
	const FString Key = GetFullGroupName(true) + TEXT(".") + GetName();
	if ( !(FText::FindText( Namespace, Key, /*OUT*/LocalizedToolTip )) || *FTextInspector::GetSourceString(LocalizedToolTip) != NativeToolTip)
	{
		LocalizedToolTip = FText::FromString(NativeToolTip);
	}

	return LocalizedToolTip;
}

/**
 * Determines if the property has any metadata associated with the key
 * 
 * @param Key The key to lookup in the metadata
 * @return true if there is a (possibly blank) value associated with this key
 */
bool UField::HasMetaData(const TCHAR* Key) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	bool bHasMetaData = MetaData->HasValue(this, Key);
		
	return bHasMetaData;
}

bool UField::HasMetaData(const FName& Key) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	bool bHasMetaData = MetaData->HasValue(this, Key);

	return bHasMetaData;
}

/**
 * Find the metadata value associated with the key
 * 
 * @param Key The key to lookup in the metadata
 * @return The value associated with the key
*/
const FString& UField::GetMetaData(const TCHAR* Key) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	const FString& MetaDataString = MetaData->GetValue(this, Key);
		
	return MetaDataString;
}

const FString& UField::GetMetaData(const FName& Key) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	const FString& MetaDataString = MetaData->GetValue(this, Key);

	return MetaDataString;
}

/**
 * Sets the metadata value associated with the key
 * 
 * @param Key The key to lookup in the metadata
 * @return The value associated with the key
 */
void UField::SetMetaData(const TCHAR* Key, const TCHAR* InValue)
{
	UPackage* Package = GetOutermost();
	check(Package);

	return Package->GetMetaData()->SetValue(this, Key, InValue);
}

void UField::SetMetaData(const FName& Key, const TCHAR* InValue)
{
	UPackage* Package = GetOutermost();
	check(Package);

	return Package->GetMetaData()->SetValue(this, Key, InValue);
}

UClass* UField::GetClassMetaData(const TCHAR* Key) const
{
	const FString & ClassName = GetMetaData(Key);
	UClass* const FoundObject = FindObject<UClass>(ANY_PACKAGE, *ClassName);
	return FoundObject;
}

void UField::RemoveMetaData(const TCHAR* Key)
{
	UPackage* Package = GetOutermost();
	check(Package);
	return Package->GetMetaData()->RemoveValue(this, Key);
}

void UField::RemoveMetaData(const FName& Key)
{
	UPackage* Package = GetOutermost();
	check(Package);
	return Package->GetMetaData()->RemoveValue(this, Key);
}

#endif
IMPLEMENT_CORE_INTRINSIC_CLASS(UField, UObject,
	{
		Class->EmitObjectReference( STRUCT_OFFSET( UField, Next ) );
	}
);

/*-----------------------------------------------------------------------------
	UStruct implementation.
-----------------------------------------------------------------------------*/

//
// Constructors.
//
UStruct::UStruct( EStaticConstructor, int32 InSize, EObjectFlags InFlags )
:	UField			( EC_StaticConstructor, InFlags )
,	Children		( NULL )
,	PropertiesSize	( InSize )
,	MinAlignment	( 1 )
,	PropertyLink	( NULL )
,	RefLink			( NULL )
,	DestructorLink	( NULL )
, PostConstructLink( NULL )
{
}

UStruct::UStruct(const class FPostConstructInitializeProperties& PCIP, UStruct* InSuperStruct, SIZE_T ParamsSize, SIZE_T Alignment )
:	UField			(PCIP)
,   SuperStruct		( InSuperStruct )
,	Children		( NULL )
,	PropertiesSize	( ParamsSize ? ParamsSize : (InSuperStruct ? InSuperStruct->GetPropertiesSize() : 0) )
,	MinAlignment	( Alignment ? Alignment : (FMath::Max(InSuperStruct ? InSuperStruct->GetMinAlignment() : 1,1)) )
,	PropertyLink	( NULL )
,	RefLink			( NULL )
,	DestructorLink	( NULL )
, PostConstructLink( NULL )
{
}

/**
 * Force any base classes to be registered first, then call BaseRegister
 */
void UStruct::RegisterDependencies()
{
	Super::RegisterDependencies();
	if (SuperStruct != NULL)
	{
		SuperStruct->RegisterDependencies();
	}
}

void UStruct::AddCppProperty( UProperty* Property )
{
	Property->Next = Children;
	Children       = Property;
}

void UStruct::StaticLink(bool bRelinkExistingProperties)
{
	FArchive ArDummy;
	Link(ArDummy, bRelinkExistingProperties);
}

void UStruct::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	if (bRelinkExistingProperties)
	{
		// Preload everything before we calculate size, as the preload may end up recursively linking things
		UStruct* InheritanceSuper = GetInheritanceSuper();
		if (InheritanceSuper)
		{
			Ar.Preload(InheritanceSuper);			
		}

		for (UField* Field = Children; Field; Field = Field->Next)
		{
			// calling Preload here is required in order to load the value of Field->Next
			Ar.Preload(Field);
		}

		PropertiesSize = 0;
		MinAlignment = 1;

		if (InheritanceSuper)
		{
			PropertiesSize = InheritanceSuper->GetPropertiesSize();
			MinAlignment = InheritanceSuper->GetMinAlignment();
		}

		for (UField* Field = Children; Field; Field = Field->Next)
		{
			if (Field->GetOuter() != this)
			{
				break;
			}

			if (UProperty* Property = Cast<UProperty>(Field))
			{
#if !WITH_EDITORONLY_DATA
				// If we don't have the editor, make sure we aren't trying to link properties that are editor only.
				check( !Property->IsEditorOnlyProperty() );
#endif // WITH_EDITORONLY_DATA
				ensureMsgf(Property->GetOuter() == this, TEXT("Linking '%s'. Property '%s' has outer '%s'"),
					*GetFullName(), *Property->GetName(), *Property->GetOuter()->GetFullName());
				PropertiesSize = Property->Link(Ar);
				MinAlignment = FMath::Max( MinAlignment, Property->GetMinAlignment() );
			}
		}
		bool bHandledWithCppStructOps = false;
		if (GetClass()->IsChildOf(UScriptStruct::StaticClass()))
		{
			// check for internal struct recursion via arrays
			for (UField* Field = Children; Field; Field = Field->Next)
			{
				UArrayProperty* ArrayProp = Cast<UArrayProperty>(Field);
				if (ArrayProp != NULL)
				{
					UStructProperty* StructProp = Cast<UStructProperty>(ArrayProp->Inner);
					if (StructProp != NULL && StructProp->Struct == this)
					{
						//we won't support this, too complicated
						FError::Throwf(TEXT("'Struct recursion via arrays is unsupported for properties."));
					}
				}
			}
			UScriptStruct* ScriptStruct = CastChecked<UScriptStruct>(this);
			ScriptStruct->PrepareCppStructOps();
			UScriptStruct::ICppStructOps* CppStructOps = ScriptStruct->GetCppStructOps();
			if (CppStructOps)
			{
				if (!ScriptStruct->InheritedCppStructOps())
				{
					MinAlignment = CppStructOps->GetAlignment();
					PropertiesSize = CppStructOps->GetSize();
				}
				else
				{
					// derived class might have increased the alignment, we want the max
					MinAlignment = FMath::Max(MinAlignment, CppStructOps->GetAlignment());
				}
				bHandledWithCppStructOps = true;
			}
		}
	}
	else
	{
		for (UField* Field = Children; (Field != NULL) && (Field->GetOuter() == this); Field = Field->Next)
		{
			if (UProperty* Property = Cast<UProperty>(Field))
			{
				Property->LinkWithoutChangingOffset(Ar);
			}
		}
	}

	if (GetOutermost()->GetFName() == GLongCoreUObjectPackageName)
	{
		FName ToTest = GetFName();
		if ( ToTest == NAME_Matrix )
		{
			check(MinAlignment == ALIGNOF(FMatrix));
			check(PropertiesSize == sizeof(FMatrix));
		}
		else if ( ToTest == NAME_Plane )
		{
			check(MinAlignment == ALIGNOF(FPlane));
			check(PropertiesSize == sizeof(FPlane));
		}
		else if ( ToTest == NAME_Vector4 )
		{
			check(MinAlignment == ALIGNOF(FVector4));
			check(PropertiesSize == sizeof(FVector4));
		}
		else if ( ToTest == NAME_Quat )
		{
			check(MinAlignment == ALIGNOF(FQuat));
			check(PropertiesSize == sizeof(FQuat));
		}
		else if ( ToTest == NAME_Double )
		{
			check(MinAlignment == ALIGNOF(double));
			check(PropertiesSize == sizeof(double));
		}
		else if ( ToTest == NAME_Color )
		{
			check(MinAlignment == ALIGNOF(FColor));
			check(PropertiesSize == sizeof(FColor));
#if !PLATFORM_LITTLE_ENDIAN
			// Object.h declares FColor as BGRA which doesn't match up with what we'd like to use on
			// Xenon to match up directly with the D3D representation of D3DCOLOR. We manually fiddle 
			// with the property offsets to get everything to line up.
			// In any case, on big-endian systems we want to byte-swap this.
			//@todo cooking: this should be moved into the data cooking step.
			{
				UProperty*	ColorComponentEntries[4];
				uint32		ColorComponentIndex = 0;

				for( UField* Field=Children; Field && Field->GetOuter()==this; Field=Field->Next )
				{
					UProperty* Property = Cast<UProperty>( Field );
					check(Property);
					ColorComponentEntries[ColorComponentIndex++] = Property;
				}
				check( ColorComponentIndex == 4 );

				Exchange( ColorComponentEntries[0]->Offset, ColorComponentEntries[3]->Offset );
				Exchange( ColorComponentEntries[1]->Offset, ColorComponentEntries[2]->Offset );
			}
#endif

		}
	}


	// Link the references, structs, and arrays for optimized cleanup.
	// Note: Could optimize further by adding UProperty::NeedsDynamicRefCleanup, excluding things like arrays of ints.
	UProperty** PropertyLinkPtr = &PropertyLink;
	UProperty** DestructorLinkPtr = &DestructorLink;
	UProperty** RefLinkPtr = (UProperty**)&RefLink;
	UProperty** PostConstructLinkPtr = &PostConstructLink;

	for (TFieldIterator<UProperty> It(this); It; ++It)
	{
		UProperty* Property = *It;

		if (Property->ContainsObjectReference() || Property->ContainsWeakObjectReference())
		{
			*RefLinkPtr = Property;
			RefLinkPtr = &(*RefLinkPtr)->NextRef;
		}

		bool bOwnedByNativeClass = Property->GetOwnerClass() && Property->GetOwnerClass()->HasAnyClassFlags(CLASS_Native | CLASS_Intrinsic);

		if (!Property->HasAnyPropertyFlags(CPF_IsPlainOldData | CPF_NoDestructor) &&
			!bOwnedByNativeClass) // these would be covered by the native destructor
		{	
			// things in a struct that need a destructor will still be in here, even though in many cases they will also be destroyed by a native destructor on the whole struct
			*DestructorLinkPtr = Property;
			DestructorLinkPtr = &(*DestructorLinkPtr)->DestructorLinkNext;
		}

		// Link references to properties that require their values to be copied from CDO.
		if ((Property->HasAnyPropertyFlags(CPF_Config) && Property->GetOwnerClass() && !Property->GetOwnerClass()->HasAnyClassFlags(CLASS_PerObjectConfig)) ||
			 Property->HasAnyPropertyFlags(CPF_Localized))
		{
			*PostConstructLinkPtr = Property;
			PostConstructLinkPtr = &(*PostConstructLinkPtr)->PostConstructLinkNext;
		}

		*PropertyLinkPtr = Property;
		PropertyLinkPtr = &(*PropertyLinkPtr)->PropertyLinkNext;
	}

	*PropertyLinkPtr = NULL;
	*DestructorLinkPtr = NULL;
	*RefLinkPtr = NULL;
}

//
// Serialize all of the class's data that belongs in a particular
// bin and resides in Data.
//
void UStruct::SerializeBin( FArchive& Ar, void* Data, int32 MaxReadBytes ) const
{
	if( Ar.IsObjectReferenceCollector() )
	{
		for( UProperty* RefLinkProperty=RefLink; RefLinkProperty!=NULL; RefLinkProperty=RefLinkProperty->NextRef )
		{
			RefLinkProperty->SerializeBinProperty( Ar, Data );
		}
	}
	else
	{
		for (UProperty* Property = PropertyLink; Property != NULL; Property = Property->PropertyLinkNext)
		{
			Property->SerializeBinProperty(Ar, Data);
		}
	}
}

void UStruct::SerializeBinEx( FArchive& Ar, void* Data, void const* DefaultData, UStruct* DefaultStruct ) const
{
	if ( !DefaultData || !DefaultStruct )
	{
		SerializeBin(Ar, Data, 0);
		return;
	}

	for( TFieldIterator<UProperty> It(this); It; ++It )
	{
		It->SerializeNonMatchingBinProperty(Ar, Data, DefaultData, DefaultStruct);
	}
}

TMap<FName,TMap<FName,FName> > UStruct::TaggedPropertyRedirects;
void UStruct::InitTaggedPropertyRedirectsMap()
{
	if( GConfig )
	{
		FConfigSection* PackageRedirects = GConfig->GetSectionPrivate( TEXT("/Script/Engine.Engine"), false, true, GEngineIni );
		for( FConfigSection::TIterator It(*PackageRedirects); It; ++It )
		{
			if( It.Key() == TEXT("TaggedPropertyRedirects") )
			{
				FName ClassName = NAME_None;
				FName OldPropertyName = NAME_None;
				FName NewPropertyName = NAME_None;

				FParse::Value( *It.Value(), TEXT("ClassName="), ClassName );
				FParse::Value( *It.Value(), TEXT("OldPropertyName="), OldPropertyName );
				FParse::Value( *It.Value(), TEXT("NewPropertyName="), NewPropertyName );

				check(ClassName != NAME_None && OldPropertyName != NAME_None && NewPropertyName != NAME_None );
				TaggedPropertyRedirects.FindOrAdd(ClassName).Add(OldPropertyName, NewPropertyName);
			}			
		}
	}
	else
	{
		UE_LOG(LogClass, Warning, TEXT(" **** TAGGED PROPERTY REDIRECTS UNABLE TO INITIALIZE! **** "));
	}
}

void UStruct::SerializeTaggedProperties(FArchive& Ar, uint8* Data, UStruct* DefaultsStruct, uint8* Defaults) const
{
	FName PropertyName(NAME_None);

	check(Ar.IsLoading() || Ar.IsSaving());

	UClass* DefaultsClass = Cast<UClass>(DefaultsStruct);
	UScriptStruct* DefaultsScriptStruct = Cast<UScriptStruct>(DefaultsStruct);

	if( Ar.IsLoading() )
	{
		// Load tagged properties.

		// This code assumes that properties are loaded in the same order they are saved in. This removes a n^2 search 
		// and makes it an O(n) when properties are saved in the same order as they are loaded (default case). In the 
		// case that a property was reordered the code falls back to a slower search.
		UProperty*	Property			= PropertyLink;
		bool		AdvanceProperty		= 0;
		int32		RemainingArrayDim	= Property ? Property->ArrayDim : 0;

		// Load all stored properties, potentially skipping unknown ones.
		while( 1 )
		{
			FPropertyTag Tag;
			Ar << Tag;
			if( Tag.Name == NAME_None )
			{
				break;
			}
			PropertyName = Tag.Name;

			static FName DEBUG_Name(TEXT("NewVar"));
			if (Tag.Name == DEBUG_Name)
			{
				static volatile int32 xx = 0;
				xx++;
			}

			// Move to the next property to be serialized
			if( AdvanceProperty && --RemainingArrayDim <= 0 )
			{
				Property = Property->PropertyLinkNext;
				// Skip over properties that don't need to be serialized.
				while( Property && !Property->ShouldSerializeValue( Ar ) )
				{
					Property = Property->PropertyLinkNext;
				}
				AdvanceProperty		= 0;
				RemainingArrayDim	= Property ? Property->ArrayDim : 0;
			}
			
			// If this property is not the one we expect (e.g. skipped as it matches the default value), do the brute force search.
			if( Property == NULL || Property->GetFName() != Tag.Name )
			{
				// No need to check redirects on platforms where everything is cooked. Always check for save games
				if (!FPlatformProperties::RequiresCookedData() || Ar.IsSaveGame())
				{
					// Look in the redirect table to see if we're searching for a different name
					static bool bAlreadyInitialized_TaggedPropertyRedirectsMap = false;
					if( !bAlreadyInitialized_TaggedPropertyRedirectsMap )
					{
						InitTaggedPropertyRedirectsMap();
						bAlreadyInitialized_TaggedPropertyRedirectsMap = true;
					}
					
					FName EachName = GetFName();
					// Search the current class first, then work up the class hierarchy to see if theres a match for our fixup.
					UStruct* Owner = GetOwnerStruct();		
					if( Owner )
					{
						UStruct* SuperClass = Owner->GetSuperStruct();
						while( EachName != NAME_None)
						{							
							const TMap<FName, FName>* ClassTaggedPropertyRedirects = TaggedPropertyRedirects.Find( EachName );
							if (ClassTaggedPropertyRedirects)
							{
								const FName* NewPropertyName = ClassTaggedPropertyRedirects->Find(Tag.Name);
								if (NewPropertyName)
								{
									Tag.Name = *NewPropertyName;
									break;
								}
							}
							// If theres another class name to check get it, otherwise flag the end.
							if( SuperClass != NULL )
							{
								EachName = SuperClass->GetFName();
								SuperClass = SuperClass->GetSuperStruct();
							}
							else
							{
								EachName = NAME_None;
							}
						}
					}
				}

				UProperty* CurrentProperty = Property;
				// Search forward...
				for ( ; Property; Property=Property->PropertyLinkNext )
				{
					if( Property->GetFName() == Tag.Name )
					{
						break;
					}
				}
				// ... and then search from the beginning till we reach the current property if it's not found.
				if( Property == NULL )
				{
					for( Property = PropertyLink; Property && Property != CurrentProperty; Property = Property->PropertyLinkNext )
					{
						if( Property->GetFName() == Tag.Name )
						{
							break;
						}
					}

					if( Property == CurrentProperty )
					{
						// Property wasn't found.
						Property = NULL;
					}
				}

				RemainingArrayDim = Property ? Property->ArrayDim : 0;
			}

			// Check if this is a struct property and we have a redirector
			if (Tag.Type==NAME_StructProperty && Property != NULL && Tag.Type == Property->GetID())
			{
				FName* NewName = ULinkerLoad::StructNameRedirects.Find(Tag.StructName);
				FName StructName = CastChecked<UStructProperty>(Property)->Struct->GetFName();
				if (NewName != NULL && *NewName == StructName)
				{
					Tag.StructName = *NewName;
				}
			}

			bool bSkipSkipWarning = false;

			if( !Property )
			{
				//UE_LOG(LogClass, Warning, TEXT("Property %s of %s not found for package:  %s"), *Tag.Name.ToString(), *GetFullName(), *Ar.GetArchiveName() );
			}
			// editoronly properties should be skipped if we are NOT the editor, or we are 
			// the editor but are cooking for console (editoronly implies notforconsole)
			else if ((Property->PropertyFlags & CPF_EditorOnly) && !FPlatformProperties::HasEditorOnlyData() && !GForceLoadEditorOnly)
			{
				bSkipSkipWarning = true;
			}
			// check for valid array index
			else if( Tag.ArrayIndex >= Property->ArrayDim || Tag.ArrayIndex < 0 )
			{
				UE_LOG(LogClass, Warning, TEXT("Array bounds in %s of %s: %i/%i for package:  %s"), *Tag.Name.ToString(), *GetName(), Tag.ArrayIndex, Property->ArrayDim, *Ar.GetArchiveName() );
			}
			else if( Tag.Type==NAME_StrProperty && Cast<UNameProperty>(Property) )
			{ 
				FString str;  
				Ar << str;
				CastChecked<UNameProperty>(Property)->SetPropertyValue_InContainer(Data, FName(*str), Tag.ArrayIndex);
				AdvanceProperty = true;
				continue; 
			}
			else if( Tag.Type==NAME_StrProperty && Cast<UTextProperty>(Property) ) // Convert serialized string to text.
			{ 
				FString str;
				Ar << str;
				FText Text = FText::FromString(str);
				Text.Flags |= ETextFlag::ConvertedProperty;
				CastChecked<UTextProperty>(Property)->SetPropertyValue_InContainer(Data, Text, Tag.ArrayIndex);
				AdvanceProperty = true;
				continue; 
			}
			else if( Tag.Type==NAME_TextProperty && Cast<UStrProperty>(Property) ) // Convert serialized text to string.
			{ 
				FText Text;  
				Ar << Text;
				FString String = FTextInspector::GetSourceString(Text) ? *FTextInspector::GetSourceString(Text) : TEXT("");
				CastChecked<UStrProperty>(Property)->SetPropertyValue_InContainer(Data, String, Tag.ArrayIndex);
				AdvanceProperty = true;
				continue; 
			}
			else if( Ar.UE4Ver() < VER_UE4_BLUEPRINT_PROPERTYFLAGS_SIZE_CHANGE && Tag.Type == NAME_IntProperty && Property->GetID() == NAME_UInt64Property)
			{
				// PropertyFlag on Blueprints changes from int32 uint64
				int32 PreviousValue;

				// de-serialize the previous value
				Ar << PreviousValue;

				// now copy the value into the object's address space
				CastChecked<UUInt64Property>(Property)->SetPropertyValue_InContainer(Data, PreviousValue, Tag.ArrayIndex);
				AdvanceProperty = true;
				continue;

			}
			else if ( Tag.Type == NAME_ByteProperty && Property->GetID() == NAME_IntProperty )
			{
				// this property's data was saved as a uint8, but the property has been changed to an int32.  Since there is no loss of data
				// possible, we can auto-convert to the right type.
				uint8 PreviousValue;

				// de-serialize the previous value
				// if the byte property had an enum, it's serialized differently so we need to account for that
				if (Tag.EnumName != NAME_None)
				{
					//@warning: mirrors loading code in UByteProperty::SerializeItem()
					FName EnumValue;
					Ar << EnumValue;
					UEnum* Enum = FindField<UEnum>((DefaultsClass != NULL) ? DefaultsClass : DefaultsStruct->GetTypedOuter<UClass>(), Tag.EnumName);
					if (Enum == NULL)
					{
						Enum = FindObject<UEnum>(ANY_PACKAGE, *Tag.EnumName.ToString(), true);
					}
					if (Enum == NULL)
					{
						UE_LOG(LogClass, Warning, TEXT("Failed to find enum '%s' when converting property '%s' to int during property loading"), *Tag.EnumName.ToString(), *Tag.Name.ToString());
						PreviousValue = 0;
					}
					else
					{
						Ar.Preload(Enum);
						PreviousValue = Enum->FindEnumIndex(EnumValue);
						if (Enum->NumEnums() < PreviousValue)
						{
							PreviousValue = Enum->NumEnums() - 1;
						}
					}
				}
				else
				{
					Ar << PreviousValue;
				}

				// now copy the value into the object's address spaace
				CastChecked<UIntProperty>(Property)->SetPropertyValue_InContainer(Data, PreviousValue, Tag.ArrayIndex);
				AdvanceProperty = true;
				continue;
			}
			else if ((Tag.Type == NAME_AssetObjectProperty) && (Property->GetID() == NAME_ObjectProperty))
			{
				// This property used to be a TAssetPtr<Foo> but is now a raw UObjectProperty Foo*, we can convert without loss of data
				FAssetPtr PreviousValue;
				Ar << PreviousValue;

				// now copy the value into the object's address space
				UObject* PreviousValueObj = PreviousValue.Get();
				CastChecked<UObjectProperty>(Property)->SetPropertyValue_InContainer(Data, PreviousValueObj, Tag.ArrayIndex);

				AdvanceProperty = true;
				continue;
			}
			else if ((Tag.Type == NAME_ObjectProperty) && (Property->GetID() == NAME_AssetObjectProperty))
			{
				// This property used to be a raw UObjectProperty Foo* but is now a TAssetPtr<Foo>
				UObject* PreviousValue = NULL;
				Ar << PreviousValue;

				// now copy the value into the object's address space
				FAssetPtr PreviousValueAssetPtr(PreviousValue);
				CastChecked<UAssetObjectProperty>(Property)->SetPropertyValue_InContainer(Data, PreviousValueAssetPtr, Tag.ArrayIndex);

				AdvanceProperty = true;
				continue;
			}
			else if(Tag.Type == NAME_IntProperty && Property->GetID() == NAME_BoolProperty )
			{
				// Property was saved as an int32, but has been changed to a bool (bitfield)
				int32 IntValue;
				Ar << IntValue;

				if( IntValue != 0 )
				{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					if(IntValue != 1)
					{
						UE_LOG(LogClass, Log, TEXT("Loading int32 property (%s) that is now a uint32 - value '%d', expecting 0 or 1. Value set to true."), *Property->GetPathName(), IntValue);
					}
#endif
					CastChecked<UBoolProperty>(Property)->SetPropertyValue_InContainer(Data, true, Tag.ArrayIndex);
				}
				else
				{
					CastChecked<UBoolProperty>(Property)->SetPropertyValue_InContainer(Data, false, Tag.ArrayIndex);
				}

				AdvanceProperty = true;
				continue; 
			}
			else if( Tag.Type!=Property->GetID() && Cast<UStructProperty>(Property) && Cast<UStructProperty>(Property)->Struct && (Cast<UStructProperty>(Property)->Struct->StructFlags & STRUCT_SerializeFromMismatchedTag))
			{
				UScriptStruct::ICppStructOps* CppStructOps = Cast<UStructProperty>(Property)->Struct->GetCppStructOps();
				check(CppStructOps && CppStructOps->HasSerializeFromMismatchedTag()); // else should not have STRUCT_SerializeFromMismatchedTag
				void* DestAddress = Property->ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex);  
				if (CppStructOps->SerializeFromMismatchedTag(Tag, Ar, DestAddress))
				{
					AdvanceProperty = true;
					continue;
				}
				else
				{
					UE_LOG(LogClass, Warning, TEXT("SerializeFromMismatchedTag failed: Type mismatch in %s of %s - Previous (%s) Current(%s) for package:  %s"), *Tag.Name.ToString(), *GetName(), *Tag.Type.ToString(), *Property->GetID().ToString(), *Ar.GetArchiveName() );
				}
			}
			else if( Tag.Type!=Property->GetID() )
			{
				UE_LOG(LogClass, Warning, TEXT("Type mismatch in %s of %s - Previous (%s) Current(%s) for package:  %s"), *Tag.Name.ToString(), *GetName(), *Tag.Type.ToString(), *Property->GetID().ToString(), *Ar.GetArchiveName() );
			}
			else if( Tag.Type == NAME_ArrayProperty && Tag.InnerType != NAME_None && Tag.InnerType != CastChecked<UArrayProperty>(Property)->Inner->GetID() )
			{
				UE_LOG(LogClass, Warning, TEXT("Array Inner Type mismatch in %s of %s - Previous (%s) Current(%s) for package:  %s"), *Tag.Name.ToString(), *GetName(), *Tag.InnerType.ToString(), *CastChecked<UArrayProperty>(Property)->Inner->GetID().ToString(), *Ar.GetArchiveName() );
			}
			else if (Tag.Type == NAME_AttributeProperty && Tag.InnerType != NAME_None && Tag.InnerType != CastChecked<UAttributeProperty>(Property)->Inner->GetID())
			{
				UE_LOG(LogClass, Warning, TEXT("Attribute Inner Type mismatch in %s of %s - Previous (%s) Current(%s) for package:  %s"), *Tag.Name.ToString(), *GetName(), *Tag.InnerType.ToString(), *CastChecked<UAttributeProperty>(Property)->Inner->GetID().ToString(), *Ar.GetArchiveName());
			}
			else if( Tag.Type==NAME_StructProperty && Tag.StructName!=CastChecked<UStructProperty>(Property)->Struct->GetFName() 
				&& CastChecked<UStructProperty>(Property)->UseBinaryOrNativeSerialization(Ar) )
			{
				UE_LOG(LogClass, Warning, TEXT("Property %s of %s struct type mismatch %s/%s for package:  %s. If that property got renamed, add an ActiveStructRedirect."), *Tag.Name.ToString(), *GetName(), *Tag.StructName.ToString(), *CastChecked<UStructProperty>(Property)->Struct->GetName(), *Ar.GetArchiveName() );
			}
			else if( !Property->ShouldSerializeValue(Ar) )
			{
				UE_LOG(LogClass, Warning, TEXT("Property %s of %s is not serializable for package:  %s"), *Tag.Name.ToString(), *GetName(), *Ar.GetArchiveName() );
			}
			else if ( Tag.Type == NAME_ByteProperty && ( (Tag.EnumName == NAME_None && ExactCast<UByteProperty>(Property)->Enum != NULL) || 
														(Tag.EnumName != NAME_None && ExactCast<UByteProperty>(Property)->Enum == NULL) ))
			{
				// a byte property gained or lost an enum
				// attempt to convert it
				uint8 PreviousValue;
				if (Tag.EnumName == NAME_None)
				{
					// simply pretend the property still doesn't have an enum and serialize the single byte
					Ar << PreviousValue;
				}
				else
				{
					// attempt to find the old enum and get the byte value from the serialized enum name
					//@warning: mirrors loading code in UByteProperty::SerializeItem()
					FName EnumValue;
					Ar << EnumValue;
					UEnum* Enum = FindField<UEnum>((DefaultsClass != NULL) ? DefaultsClass : DefaultsStruct->GetTypedOuter<UClass>(), Tag.EnumName);
					if (Enum == NULL)
					{
						Enum = FindObject<UEnum>(ANY_PACKAGE, *Tag.EnumName.ToString(), true);
					}
					if (Enum == NULL)
					{
						UE_LOG(LogClass, Warning, TEXT("Failed to find enum '%s' when converting property '%s' to byte during property loading"), *Tag.EnumName.ToString(), *Tag.Name.ToString());
						PreviousValue = 0;
					}
					else
					{
						Ar.Preload(Enum);
						PreviousValue = Enum->FindEnumIndex(EnumValue);
						if (Enum->NumEnums() < PreviousValue)
						{
							PreviousValue = Enum->NumEnums() - 1;
						}
					}
				}
				
				// now copy the value into the object's address spaace
				CastChecked<UByteProperty>(Property)->SetPropertyValue_InContainer(Data, PreviousValue, Tag.ArrayIndex);
				AdvanceProperty = true;
				continue;
			}
			else
			{
					uint8* DestAddress = Property->ContainerPtrToValuePtr<uint8>(Data, Tag.ArrayIndex);  

					// This property is ok.			
					Tag.SerializeTaggedProperty( Ar, Property, DestAddress, Tag.Size, NULL );

					AdvanceProperty = true;
					continue;
				}

			AdvanceProperty = false;

			// Skip unknown or bad property.
			uint8 B;
			for( int32 i=0; i<Tag.Size; i++ )
			{
				Ar << B;
			}
		}
	}
	else
	{
		/** If true, it means that we want to serialize all properties of this struct if any properties differ from defaults */
		bool bUseAtomicSerialization = false;
		if (DefaultsScriptStruct)
		{
			bUseAtomicSerialization = DefaultsScriptStruct->ShouldSerializeAtomically(Ar);
		}

		// Save tagged properties.

		// Iterate over properties in the order they were linked and serialize them.
		for( UProperty* Property = PropertyLink; Property; Property = Property->PropertyLinkNext )
		{
			if( Property->ShouldSerializeValue(Ar) )
			{
				PropertyName = Property->GetFName();
				for( int32 Idx=0; Idx<Property->ArrayDim; Idx++ )
				{
					uint8* DataPtr = Property->ContainerPtrToValuePtr<uint8>(Data, Idx);
					uint8* DefaultValue = Property->ContainerPtrToValuePtrForDefaults<uint8>(DefaultsStruct, Defaults, Idx);
					if( (!IsA(UClass::StaticClass()) && !Defaults) || !Ar.DoDelta() || 
						!Property->Identical( DataPtr, DefaultValue, Ar.GetPortFlags()) || Ar.IsTransacting() )
					{
						if (bUseAtomicSerialization)
						{
							DefaultValue = NULL;
						}
						FPropertyTag Tag( Ar, Property, Idx, DataPtr, DefaultValue );
						Ar << Tag;

						// need to know how much data this call to SerializeTaggedProperty consumes, so mark where we are
						int32 DataOffset = Ar.Tell();

						Tag.SerializeTaggedProperty( Ar, Property, DataPtr, 0, DefaultValue );

						// set the tag's size
						Tag.Size = Ar.Tell() - DataOffset;

						if ( Tag.Size >  0 )
						{
							// mark our current location
							DataOffset = Ar.Tell();

							// go back and re-serialize the size now that we know it
							Ar.Seek(Tag.SizeOffset);
							Ar << Tag.Size;

							// return to the current location
							Ar.Seek(DataOffset);
						}
					}
				}
			}
		}
		static FName Temp(NAME_None);
		Ar << Temp;
	}
}
void UStruct::FinishDestroy()
{
	Script.Empty();
	Super::FinishDestroy();
}
void UStruct::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );

	Ar << SuperStruct;

	if (Ar.UE4Ver() < VER_UE4_CONSOLIDATE_HEADER_PARSER_ONLY_PROPERTIES)
	{
		UTextBuffer* ScriptText;
		Ar << ScriptText;
	}

	Ar << Children;

	if (Ar.UE4Ver() < VER_UE4_CONSOLIDATE_HEADER_PARSER_ONLY_PROPERTIES)
	{
		UTextBuffer* CppText = NULL;
		Ar << CppText;

		int32 Line = 0;
		Ar << Line;

		int32 TextPos = 0;
		Ar << TextPos;
	}

	// Script code.
	// Skip serialization if we're duplicating classes for reinstancing, since we only need the memory layout
	int32 ScriptBytecodeSize = !GIsDuplicatingClassForReinstancing ? Script.Num() : 0;
	int32 ScriptStorageSize = 0;
	int32 ScriptStorageSizeOffset = 0;
	if ( Ar.IsLoading() )
	{
		Ar << ScriptBytecodeSize;
		Ar << ScriptStorageSize;

		Script.Empty( ScriptBytecodeSize );
		Script.AddUninitialized( ScriptBytecodeSize );
	}

	// Ensure that last byte in script code is EX_EndOfScript to work around script debugger implementation.
	else if( Ar.IsSaving() )
	{
		Ar << ScriptBytecodeSize;

		// drop a zero here.  will seek back later and re-write it when we know it
		ScriptStorageSizeOffset = Ar.Tell();
		Ar << ScriptStorageSize;
	}

	// If we're duplicating for reinstancing, we only need memory layout, and cyclic dependencies within object literals can potentially cause problems, so do not serialize bytecode
	if( !GIsDuplicatingClassForReinstancing )
	{
		// no bytecode patch for this struct - serialize normally [i.e. from disk]
		int32 iCode = 0;
		int32 const BytecodeStartOffset = Ar.Tell();

		if (Ar.IsPersistent() && Ar.GetLinker())
		{
			if (Ar.IsLoading())
			{
				// make sure this is a ULinkerLoad
				ULinkerLoad* LinkerLoad = CastChecked<ULinkerLoad>(Ar.GetLinker());

				// preload the bytecode
				TArray<uint8> TempScript;
				TempScript.AddUninitialized(ScriptStorageSize);
				int32 ScriptStart = Ar.Tell();
				Ar.Serialize(TempScript.GetData(), ScriptStorageSize);

				if ((Ar.UE4Ver() < VER_MIN_SCRIPTVM_UE4) || (Ar.LicenseeUE4Ver() < VER_MIN_SCRIPTVM_LICENSEEUE4))
				{
					// Discard the bytecode as it's too old and might cause serialization errors
					ScriptStorageSize = 0;
					ScriptBytecodeSize = 0;
					TempScript.Empty();
					Script.Empty();
				}
				else
				{
					Ar.Seek(ScriptStart); // seek back and load it again
					// now, use the linker to load the byte code, but reading from memory
					while( iCode < ScriptBytecodeSize )
					{	
						SerializeExpr( iCode, Ar );
					}
				}
				// and update the SHA (does nothing if not currently calculating SHA)
				LinkerLoad->UpdateScriptSHAKey(TempScript);
			}
			else
			{
				// make sure this is a ULinkerSave
				ULinkerSave* LinkerSave = CastChecked<ULinkerSave>(Ar.GetLinker());
			
				// remember how we were saving
				FArchive* SavedSaver = LinkerSave->Saver;

				// force writing to a buffer
				TArray<uint8> TempScript;
				FMemoryWriter MemWriter(TempScript, Ar.IsPersistent());
				LinkerSave->Saver = &MemWriter;

				// now, use the linker to save the byte code, but writing to memory
				while( iCode < ScriptBytecodeSize )
				{	
					SerializeExpr( iCode, Ar );
				}

				// restore the saver
				LinkerSave->Saver = SavedSaver;

				// now write out the memory bytes
				Ar.Serialize(TempScript.GetData(), TempScript.Num());

				// and update the SHA (does nothing if not currently calculating SHA)
				LinkerSave->UpdateScriptSHAKey(TempScript);
			}
		}
		else
		{
			while( iCode < ScriptBytecodeSize )
			{	
				SerializeExpr( iCode, Ar );
			}
		}

		if( iCode != ScriptBytecodeSize )
		{	
			UE_LOG(LogClass, Fatal, TEXT("Script serialization mismatch: Got %i, expected %i"), iCode, ScriptBytecodeSize );
		}

		if (Ar.IsSaving())
		{
			int32 const BytecodeEndOffset = Ar.Tell();

			// go back and write on-disk size
			Ar.Seek(ScriptStorageSizeOffset);
			ScriptStorageSize = BytecodeEndOffset - BytecodeStartOffset;
			Ar << ScriptStorageSize;

			// back to where we were
			Ar.Seek(BytecodeEndOffset);
		}
		if( Ar.IsLoading() )
		{
			// Collect references to objects embedded in script and store them in easily accessible array. This is skipped if
			// the struct is disregarded for GC as the references won't be of any use.
			ScriptObjectReferences.Empty();
			if( !GUObjectArray.IsDisregardForGC(this) )
			{
				FArchiveScriptReferenceCollector ObjectReferenceCollector( ScriptObjectReferences );

				int32 iCode2 = 0;
				while( iCode2 < Script.Num() )
				{	
					SerializeExpr( iCode2, ObjectReferenceCollector );
				}
			}
		}	
	}

	if (Ar.IsLoading() && !Cast<UClass>(this)) // classes are linked in the UClass serializer, which just called me
	{
		// Link the properties.
		Link( Ar, true );
	}
}

void UStruct::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UStruct* This = CastChecked<UStruct>(InThis);
#if WITH_EDITOR
	if( GIsEditor )
	{
		// Required by the unified GC when running in the editor
		Collector.AddReferencedObject( This->SuperStruct, This );
		Collector.AddReferencedObject( This->Children, This );

		TArray<UObject*> ScriptObjectReferences;
		FArchiveScriptReferenceCollector ObjectReferenceCollector( ScriptObjectReferences );
		int32 iCode = 0;
		while( iCode < This->Script.Num() )
		{	
			const_cast<UStruct*>(This)->SerializeExpr( iCode, ObjectReferenceCollector );
		}
		for( int32 Index = 0; Index < ScriptObjectReferences.Num(); Index++ )
		{
			Collector.AddReferencedObject( ScriptObjectReferences[ Index ], This );
		}
	}

	//@todo NickW, temp hack to make stale property chains less crashy
	for (UProperty* Property = This->PropertyLink; Property != NULL; Property = Property->PropertyLinkNext)
	{
		Collector.AddReferencedObject(Property, This);
	}
	for (UProperty* Property = This->RefLink; Property != NULL; Property = Property->NextRef)
	{
		Collector.AddReferencedObject(Property, This);
	}
	for (UProperty* Property = This->DestructorLink; Property != NULL; Property = Property->DestructorLinkNext)
	{
		Collector.AddReferencedObject(Property, This);
	}
	//
#endif
	Super::AddReferencedObjects( This, Collector );
}

void UStruct::SetSuperStruct(UStruct* NewSuperStruct)
{
	SuperStruct = NewSuperStruct;
}

#if WITH_EDITOR
bool UStruct::GetBoolMetaDataHierarchical(const FName& Key) const
{
	bool bResult = false;
	const UStruct* TestStruct = this;
	while( TestStruct )
	{
		if( TestStruct->HasMetaData(Key) )
		{
			bResult = TestStruct->GetBoolMetaData(Key);
			break;
		}

		TestStruct = TestStruct->SuperStruct;
	}
	return bResult;
}
#endif
//
// Serialize an expression to an archive.
// Returns expression token.
//
EExprToken UStruct::SerializeExpr( int32& iCode, FArchive& Ar )
{
#define SERIALIZEEXPR_INC
#include "ScriptSerialization.h"
	return Expr;
#undef SERIALIZEEXPR_INC

#undef XFER
#undef XFERPTR
#undef XFERNAME
#undef XFER_FUNC_POINTER
#undef XFER_FUNC_NAME
#undef XFER_PROP_POINTER
}

void UStruct::InstanceSubobjectTemplates( void* Data, void const* DefaultData, UStruct* DefaultStruct, UObject* Owner, FObjectInstancingGraph* InstanceGraph )
{
	checkSlow(Data);
	checkSlow(Owner);

	for ( UProperty* Property = RefLink; Property != NULL; Property = Property->NextRef )
	{
		if (Property->ContainsInstancedObjectProperty())
		{
			Property->InstanceSubobjects( Property->ContainerPtrToValuePtr<uint8>(Data), (uint8*)Property->ContainerPtrToValuePtrForDefaults<uint8>(DefaultStruct, DefaultData), Owner, InstanceGraph );
		}
	}
}

IMPLEMENT_CORE_INTRINSIC_CLASS(UStruct, UField,
	{
		Class->ClassAddReferencedObjects = &UStruct::AddReferencedObjects;
		Class->EmitObjectReference( STRUCT_OFFSET( UStruct, SuperStruct ) );
		Class->EmitObjectReference( STRUCT_OFFSET( UStruct, Children ) );

		// Note: None of the *Link members need to be emitted, as they only contain properties
		// that are in the Children chain or SuperStruct->Children chains.

		Class->EmitObjectArrayReference( STRUCT_OFFSET( UStruct, ScriptObjectReferences ) );
	}
);

void UStruct::TagSubobjects(EObjectFlags NewFlags)
{
	Super::TagSubobjects(NewFlags);

	// Tag our properties
	for (TFieldIterator<UProperty> It(this, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		UProperty* Property = *It;
		if (Property && !Property->HasAnyFlags(GARBAGE_COLLECTION_KEEPFLAGS | RF_RootSet))
		{
			Property->SetFlags(NewFlags);
			Property->TagSubobjects(NewFlags);
		}
	}
}

/*-----------------------------------------------------------------------------
	UScriptStruct.
-----------------------------------------------------------------------------*/

// sample of how to customize structs
#if 0
USTRUCT()
struct ENGINE_API FTestStruct
{
	GENERATED_USTRUCT_BODY()

	TMap<int32, double> Doubles;
	FTestStruct()
	{
		Doubles.Add(1, 1.5);
		Doubles.Add(2, 2.5);
	}
	void AddStructReferencedObjects(class FReferenceCollector& Collector) const
	{
		Collector.AddReferencedObject(AActor::StaticClass());
	}
	bool Serialize(FArchive& Ar)
	{
		Ar << Doubles;
		return true;
	}
	bool operator==(FTestStruct const& Other) const
	{
		if (Doubles.Num() != Other.Doubles.Num())
		{
			return false;
		}
		for (TMap<int32, double>::TConstIterator It(Doubles); It; ++It)
		{
			double const* OtherVal = Other.Doubles.Find(It.Key());
			if (!OtherVal || *OtherVal != It.Value() )
			{
				return false;
			}
		}
		return true;
	}
	bool Identical(FTestStruct const& Other, uint32 PortFlags) const
	{
		return (*this) == Other;
	}
	void operator=(FTestStruct const& Other)
	{
		Doubles.Empty(Other.Doubles.Num());
		for (TMap<int32, double>::TConstIterator It(Other.Doubles); It; ++It)
		{
			Doubles.Add(It.Key(), It.Value());
		}
	}
	bool ExportTextItem(FString& ValueStr, FTestStruct const& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
	{
		ValueStr += TEXT("(");
		for (TMap<int32, double>::TConstIterator It(Doubles); It; ++It)
		{
			ValueStr += FString::Printf( TEXT("(%d,%f)"),It.Key(), It.Value());
		}
		ValueStr += TEXT(")");
		return true;
	}
	bool ImportTextItem( const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText )
	{
		check(*Buffer == TEXT('('));
		Buffer++;
		Doubles.Empty();
		while (1)
		{
			const TCHAR* Start = Buffer;
			while (*Buffer && *Buffer != TEXT(','))
			{
				if (*Buffer == TEXT(')'))
				{
					break;
				}
				Buffer++;
			}
			if (*Buffer == TEXT(')'))
			{
				break;
			}
			int32 Key = FCString::Atoi(Start);
			if (*Buffer)
			{
				Buffer++;
			}
			Start = Buffer;
			while (*Buffer && *Buffer != TEXT(')'))
			{
				Buffer++;
			}
			double Value = FCString::Atod(Start);

			if (*Buffer)
			{
				Buffer++;
			}
			Doubles.Add(Key, Value);
		}
		if (*Buffer)
		{
			Buffer++;
		}
		return true;
	}
	bool SerializeFromMismatchedTag(struct FPropertyTag const& Tag, FArchive& Ar)
	{
		// no example of this provided, doesn't make sense
		return false;
	}
};

template<>
struct TStructOpsTypeTraits<FTestStruct> : public TStructOpsTypeTraitsBase
{
	enum 
	{
		WithZeroConstructor = true,
		WithSerializer = true,
		WithPostSerialize = true,
		WithCopy = true,
		WithIdenticalViaEquality = true,
		//WithIdentical = true,
		WithExportTextItem = true,
		WithImportTextItem = true,
		WithAddStructReferencedObjects = true,
		WithSerializeFromMismatchedTag = true,
	};
};

#endif


/** Used to hold virtual methods to construct, destruct, etc native structs in a generic and dynamic fashion 
 * singleton-style to avoid issues with static constructor order
**/
static TMap<FName,UScriptStruct::ICppStructOps*>& GetDeferredCppStructOps()
{
	static TMap<FName,UScriptStruct::ICppStructOps*> DeferredCppStructOps;
	return DeferredCppStructOps;
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
bool FindConstructorUninitialized(UStruct* BaseClass,uint8* Data,uint8* Defaults)
{
	bool bAnyProblem = false;
	static TSet<FString> PrintedWarnings;
	for(UProperty* P=BaseClass->PropertyLink; P; P=P->PropertyLinkNext )
	{		
		int32 Size = P->GetSize();
		bool bProblem = false;
		check(Size);
		UBoolProperty* PB = Cast<UBoolProperty>(P);
		UStructProperty* PS = Cast<UStructProperty>(P);
		UStrProperty* PStr = Cast<UStrProperty>(P);
		UArrayProperty* PArray = Cast<UArrayProperty>(P);
		if(PStr)
		{
			// string that actually have data would be false positives, since they would point to the same string, but actually be different pointers
			// string is known to have a good default constructor
		}
		else if(PB)
		{
			check(Size == PB->ElementSize);
			if( PB->GetPropertyValue_InContainer(Data) && !PB->GetPropertyValue_InContainer(Defaults) )
			{
				bProblem = true;
			}
		}
		else if (PS)
		{
			// these are legitimate exceptions
			if (PS->Struct->GetName() != TEXT("BitArray")
				&& PS->Struct->GetName() != TEXT("SparseArray")
				&& PS->Struct->GetName() != TEXT("Set")
				&& PS->Struct->GetName() != TEXT("Map")
				&& PS->Struct->GetName() != TEXT("MultiMap")
				&& PS->Struct->GetName() != TEXT("ShowFlags_Mirror")
				&& PS->Struct->GetName() != TEXT("Pointer")
				)
			{
				bProblem = FindConstructorUninitialized(PS->Struct, P->ContainerPtrToValuePtr<uint8>(Data), P->ContainerPtrToValuePtr<uint8>(Defaults));
			}
		}
		else if (PArray)
		{
			bProblem = !PArray->Identical_InContainer(Data, Defaults);
		}
		else
		{
			if (FMemory::Memcmp(P->ContainerPtrToValuePtr<uint8>(Data), P->ContainerPtrToValuePtr<uint8>(Defaults), Size) != 0)
			{
//				UE_LOG(LogClass, Warning,TEXT("Mismatch %d %d"),(int32)*(Data + P->Offset), (int32)*(Defaults + P->Offset));
				bProblem = true;
			}	
		}
		if (bProblem)
		{
			FString Issue;
			if (PS)
			{
				Issue = TEXT("     From ");
				Issue += P->GetFullName();
			}
			else
			{
				Issue = BaseClass->GetPathName() + TEXT(",") + P->GetFullName();
			}
			if (!PrintedWarnings.Contains(Issue))
			{
				bAnyProblem = true;
				PrintedWarnings.Add(Issue);
				if (PS)
				{
					UE_LOG(LogClass, Warning,TEXT("%s"),*Issue);
//					OutputDebugStringW(*FString::Printf(TEXT("%s\n"),*Issue));
				}
				else
				{
					UE_LOG(LogClass, Warning,TEXT("Native constructor does not initialize all properties %s (may need to recompile excutable with new headers)"),*Issue);
//					OutputDebugStringW(*FString::Printf(TEXT("Native contructor does not initialize all properties %s\n"),*Issue));
				}
			}
		}
	}
	return bAnyProblem;
}
#endif


UScriptStruct::UScriptStruct( EStaticConstructor, int32 InSize, EObjectFlags InFlags )
	: UStruct( EC_StaticConstructor, InSize, InFlags )
	, StructFlags(STRUCT_NoFlags)
#if HACK_HEADER_GENERATOR
	, StructMacroDeclaredLineNumber(INDEX_NONE)
#endif
	, CppStructOps(NULL)
	, bCppStructOpsFromBaseClass(false)
	, bPrepareCppStructOpsCompleted(false)
{
}

UScriptStruct::UScriptStruct(const class FPostConstructInitializeProperties& PCIP, UScriptStruct* InSuperStruct, ICppStructOps* InCppStructOps, EStructFlags InStructFlags, SIZE_T ExplicitSize, SIZE_T ExplicitAlignment )
	: UStruct(PCIP, InSuperStruct, InCppStructOps ? InCppStructOps->GetSize() : ExplicitSize, InCppStructOps ? InCppStructOps->GetAlignment() : ExplicitAlignment )
	, StructFlags(EStructFlags(InStructFlags | (InCppStructOps ? STRUCT_Native : STRUCT_NoFlags)))
#if HACK_HEADER_GENERATOR
	, StructMacroDeclaredLineNumber(INDEX_NONE)
#endif
	, CppStructOps(InCppStructOps)
	, bCppStructOpsFromBaseClass(false)
	, bPrepareCppStructOpsCompleted(false)
{
	PrepareCppStructOps(); // propgate flags, etc
}

UScriptStruct::UScriptStruct(const class FPostConstructInitializeProperties& PCIP)
	: UStruct(PCIP)
	, StructFlags(STRUCT_NoFlags)
#if HACK_HEADER_GENERATOR
	, StructMacroDeclaredLineNumber(INDEX_NONE)
#endif
	, CppStructOps(NULL)
	, bCppStructOpsFromBaseClass(false)
	, bPrepareCppStructOpsCompleted(false)
{
}

/** Stash a CppStructOps for future use 
	* @param Target Name of the struct 
	* @param InCppStructOps Cpp ops for this struct
**/
void UScriptStruct::DeferCppStructOps(FName Target, ICppStructOps* InCppStructOps)
{
	if (GetDeferredCppStructOps().Contains(Target))
	{
#if !IS_MONOLITHIC
		if (!GIsHotReload) // in hot reload, we will just leak these...they may be in use.
#endif
		{
			check(GetDeferredCppStructOps().FindRef(Target) != InCppStructOps); // if it was equal, then we would be re-adding a now stale pointer to the map
			delete GetDeferredCppStructOps().FindRef(Target);
		}
	}
	GetDeferredCppStructOps().Add(Target,InCppStructOps);
}

/** Look for the CppStructOps if we don't already have it and set the property size **/
void UScriptStruct::PrepareCppStructOps()
{
	if (bPrepareCppStructOpsCompleted)
	{
		return;
	}
	if (!CppStructOps)
	{
		CppStructOps = GetDeferredCppStructOps().FindRef(GetFName());
		if (!CppStructOps)
		{
			if (!GIsUCCMakeStandaloneHeaderGenerator && (StructFlags&STRUCT_Native))
			{
				UE_LOG(LogClass, Fatal,TEXT("Couldn't bind to native struct %s. Headers need to be rebuilt, or a noexport class is missing a IMPLEMENT_STRUCT."),*GetName());
			}
			check(!bPrepareCppStructOpsCompleted); // recursion is unacceptable
			bPrepareCppStructOpsCompleted = true;
			return;
		}
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		// test that the constructor is initializing everything
		if (CppStructOps && !CppStructOps->HasZeroConstructor()
#if !IS_MONOLITHIC
			&& !GIsHotReload // in hot reload, these produce bogus warnings
#endif
			)
		{
			int32 Size = CppStructOps->GetSize();
			uint8* TestData00 = (uint8*)FMemory::Malloc(Size);
			FMemory::Memzero(TestData00,Size);
			CppStructOps->Construct(TestData00);
			uint8* TestDataFF = (uint8*)FMemory::Malloc(Size);
			FMemory::Memset(TestDataFF,0xff,Size);
			CppStructOps->Construct(TestDataFF);

			if (FMemory::Memcmp(TestData00,TestDataFF, Size) != 0)
			{
				FindConstructorUninitialized(this,TestData00,TestDataFF);
			}
			if (CppStructOps->HasDestructor())
			{
				CppStructOps->Destruct(TestData00);
				CppStructOps->Destruct(TestDataFF);
			}
			FMemory::Free(TestData00);
			FMemory::Free(TestDataFF);
		}
#endif
	}
	bCppStructOpsFromBaseClass = false;
	if (!CppStructOps)
	{
		UScriptStruct* Base = Cast<UScriptStruct>(GetSuperStruct());
		if (Base)
		{
			Base->PrepareCppStructOps();
			CppStructOps = Base->GetCppStructOps();
			bCppStructOpsFromBaseClass = true;
		}
	}
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!CppStructOps)
	{
		UScriptStruct* Base = Cast<UScriptStruct>(GetSuperStruct());

		while (Base)
		{
			if ((Base->StructFlags&STRUCT_Native) || Base->GetCppStructOps())
			{
				UE_LOG(LogClass, Fatal,TEXT("Couldn't bind to native BASE struct %s %s."),*GetName(),*Base->GetName());
				break;
			}
			Base = Cast<UScriptStruct>(Base->GetSuperStruct());
		}
	}
#endif
	check(!(StructFlags & STRUCT_ComputedFlags));
	if (CppStructOps)
	{
		if (!bCppStructOpsFromBaseClass) // if this cpp ops from the base class, we do not propagate certain custom aspects
		{
			if (CppStructOps->HasSerializer())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s has a custom serializer."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_SerializeNative );
			}
			if (CppStructOps->HasPostSerialize())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s wants post serialize."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_PostSerializeNative );
			}
			if (CppStructOps->HasNetSerializer())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s has a custom net serializer."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_NetSerializeNative);
			}
			if (CppStructOps->HasNetDeltaSerializer())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s has a custom net delta serializer."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_NetDeltaSerializeNative);
			}

			if (CppStructOps->IsPlainOldData())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s is plain old data."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_IsPlainOldData | STRUCT_NoDestructor);
			}
			else
			{
				if (CppStructOps->HasCopy())
				{
					UE_LOG(LogClass, Verbose, TEXT("Native struct %s has a native copy."),*GetName());
					StructFlags = EStructFlags(StructFlags | STRUCT_CopyNative);
				}
				if (!CppStructOps->HasDestructor())
				{
					UE_LOG(LogClass, Verbose, TEXT("Native struct %s has no destructor."),*GetName());
					StructFlags = EStructFlags(StructFlags | STRUCT_NoDestructor);
				}
			}
			if (CppStructOps->HasZeroConstructor())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s has zero construction."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_ZeroConstructor);
			}
			if (CppStructOps->IsPlainOldData() && !CppStructOps->HasZeroConstructor())
			{
				// hmm, it is safe to see if this can be zero constructed, lets try
				int32 Size = CppStructOps->GetSize();
				uint8* TestData00 = (uint8*)FMemory::Malloc(Size);
				FMemory::Memzero(TestData00,Size);
				CppStructOps->Construct(TestData00);
				CppStructOps->Construct(TestData00); // slightly more like to catch "internal counters" if we do this twice
				bool IsZeroConstruct = true;
				for (int32 Index = 0; Index < Size && IsZeroConstruct; Index++)
				{
					if (TestData00[Index])
					{
						IsZeroConstruct = false;
					}
				}
				FMemory::Free(TestData00);
				if (IsZeroConstruct)
				{
					UE_LOG(LogClass, Verbose, TEXT("Native struct %s has DISCOVERED zero construction. Size = %d"),*GetName(), Size);
					StructFlags = EStructFlags(StructFlags | STRUCT_ZeroConstructor);
				}
			}
			if (CppStructOps->HasIdentical())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s has native identical."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_IdenticalNative);
			}
			if (CppStructOps->HasAddStructReferencedObjects())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s has native AddStructReferencedObjects."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_AddStructReferencedObjects);
			}
			if (CppStructOps->HasExportTextItem())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s has native ExportTextItem."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_ExportTextItemNative);
			}
			if (CppStructOps->HasImportTextItem())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s has native ImportTextItem."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_ImportTextItemNative);
			}
			if (CppStructOps->HasSerializeFromMismatchedTag())
			{
				UE_LOG(LogClass, Verbose, TEXT("Native struct %s has native SerializeFromMismatchedTag."),*GetName());
				StructFlags = EStructFlags(StructFlags | STRUCT_SerializeFromMismatchedTag);
			}
			if (CppStructOps->HasMessageHandling())
			{
				UE_LOG(LogClass, Verbose, TEXT("Natice struct %s is a message type."), *GetName());

				// add to the message type registry
				FMessageTypeMap::MessageTypeMap.Add(GetName(), this);
			}
		}
	}
	check(!bPrepareCppStructOpsCompleted); // recursion is unacceptable
	bPrepareCppStructOpsCompleted = true;
}

void UScriptStruct::PostLoad()
{
	Super::PostLoad();
	ClearCppStructOps(); // we want to be sure to do this from scratch
	PrepareCppStructOps();
}


void UScriptStruct::Serialize( FArchive& Ar )
{
	Super::Serialize(Ar);

	// serialize the struct's flags
	Ar << (uint32&)StructFlags;

	if (Ar.IsLoading())
	{
		ClearCppStructOps(); // we want to be sure to do this from scratch
		PrepareCppStructOps();
	}
}

void UScriptStruct::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);
	if (!HasDefaults()) // if you have CppStructOps, then that is authoritative, otherwise we look at the properties
	{
		StructFlags = EStructFlags(StructFlags | STRUCT_ZeroConstructor | STRUCT_NoDestructor | STRUCT_IsPlainOldData);
		for( UProperty* Property = PropertyLink; Property; Property = Property->PropertyLinkNext )
		{
			if (!Property->HasAnyPropertyFlags(CPF_ZeroConstructor))
			{
				StructFlags = EStructFlags(StructFlags & ~STRUCT_ZeroConstructor);
			}
			if (!Property->HasAnyPropertyFlags(CPF_NoDestructor))
			{
				StructFlags = EStructFlags(StructFlags & ~STRUCT_NoDestructor);
			}
			if (!Property->HasAnyPropertyFlags(CPF_IsPlainOldData))
			{
				StructFlags = EStructFlags(StructFlags & ~STRUCT_IsPlainOldData);
			}
		}
		if (StructFlags & STRUCT_IsPlainOldData)
		{
			UE_LOG(LogClass, Verbose, TEXT("Non-Native struct %s is plain old data."),*GetName());
		}
		if (StructFlags & STRUCT_NoDestructor)
		{
			UE_LOG(LogClass, Verbose, TEXT("Non-Native struct %s has no destructor."),*GetName());
		}
		if (StructFlags & STRUCT_ZeroConstructor)
		{
			UE_LOG(LogClass, Verbose, TEXT("Non-Native struct %s has zero construction."),*GetName());
		}
	}
}

bool UScriptStruct::CompareScriptStruct( const void* A, const void* B, uint32 PortFlags )
{
	check(A);
	if (StructFlags & STRUCT_IdenticalNative)
	{
		UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
		check(TheCppStructOps);
		bool bResult = false;
		if (!B || // if the comparand is NULL, we just call this no-match
			TheCppStructOps->Identical(A, B, PortFlags, bResult))
		{
			return bResult;
		}
	}

	for( TFieldIterator<UProperty> It(this); It; ++It )
	{
		for( int32 i=0; i<It->ArrayDim; i++ )
		{
			if( !It->Identical_InContainer(A,B,i,PortFlags) )
			{
				return false;
			}
		}
	}
	return true;
}


void UScriptStruct::CopyScriptStruct(void* InDest, void const* InSrc, int32 ArrayDim)
{
	uint8 *Dest = (uint8*)InDest;
	check(Dest);
	uint8 const* Src = (uint8 const*)InSrc;
	check(Src);

	int32 Stride = GetStructureSize();

	if (StructFlags & STRUCT_CopyNative)
	{
		check(!(StructFlags & STRUCT_IsPlainOldData)); // should not have both
		UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
		check(TheCppStructOps);
		check(Stride == TheCppStructOps->GetSize() && PropertiesSize == Stride);
		if (TheCppStructOps->Copy(Dest, Src, ArrayDim))
		{
			return;
		}
	}
	if (StructFlags & STRUCT_IsPlainOldData)
	{
		FMemory::Memcpy(Dest, Src, ArrayDim * Stride);
	}
	else
	{
		for( TFieldIterator<UProperty> It(this); It; ++It )
		{
			for (int32 Index = 0; Index < ArrayDim; Index++)
			{
				It->CopyCompleteValue_InContainer((uint8*)Dest + Index * Stride,(uint8*)Src + Index * Stride);
			}
		}
	}
}


void UScriptStruct::InitializeScriptStruct(void* InDest, int32 ArrayDim)
{
	uint8 *Dest = (uint8*)InDest;
	check(Dest);

	int32 Stride = GetStructureSize();

	//@todo UE4 optimize
	FMemory::Memzero(Dest, ArrayDim * Stride);

	int32 InitializedSize = 0;
	UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
	if (TheCppStructOps != NULL)
	{
		if (!TheCppStructOps->HasZeroConstructor())
		{
			for (int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++)
			{
				TheCppStructOps->Construct(Dest + ArrayIndex * Stride);
			}
		}

		InitializedSize = TheCppStructOps->GetSize();
		// here we want to make sure C++ and the property system agree on the size
		check(InheritedCppStructOps() || (Stride == InitializedSize && PropertiesSize == InitializedSize));
	}

	if (PropertiesSize > InitializedSize)
	{
		bool bHitBase = false;
		for (UProperty* Property = PropertyLink; Property && !bHitBase; Property = Property->PropertyLinkNext)
		{
			if (!Property->IsInContainer(InitializedSize))
			{
				for (int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++)
				{
					Property->InitializeValue_InContainer(Dest + ArrayIndex * Stride);
				}
			}
			else
			{
				bHitBase = true;
			}
		}
	}
}

void UScriptStruct::ClearScriptStruct(void* Dest, int32 ArrayDim)
{
	uint8 *Data = (uint8*)Dest;
	int32 Stride = GetStructureSize();

	int32 ClearedSize = 0;
	UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
	if (TheCppStructOps)
	{
		for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
		{
			uint8* PropertyData = Data + ArrayIndex * Stride;
			if (TheCppStructOps->HasDestructor())
			{
				TheCppStructOps->Destruct(PropertyData);
			}
			TheCppStructOps->Construct(PropertyData);
		}
		ClearedSize = TheCppStructOps->GetSize();
		// here we want to make sure C++ and the property system agree on the size
		check(InheritedCppStructOps() || (Stride == ClearedSize && PropertiesSize == ClearedSize));
	}
	if ( PropertiesSize > ClearedSize )
	{
		bool bHitBase = false;
		for ( UProperty* Property = PropertyLink; Property && !bHitBase; Property = Property->PropertyLinkNext )
		{
			if (!Property->IsInContainer(ClearedSize))
			{
				for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
				{
					for ( int32 PropArrayIndex = 0; PropArrayIndex < Property->ArrayDim; PropArrayIndex++ )
					{
						Property->ClearValue_InContainer(Data + ArrayIndex * Stride, PropArrayIndex);
					}
				}
			}
			else
			{
				bHitBase = true;
			}
		}
	}

}

void UScriptStruct::DestroyScriptStruct(void* Dest, int32 ArrayDim)
{
	if (StructFlags & (STRUCT_IsPlainOldData | STRUCT_NoDestructor))
	{
		return; // POD types don't need destructors
	}
	uint8 *Data = (uint8*)Dest;
	int32 Stride = GetStructureSize();
	int32 ClearedSize = 0;

	UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
	if (TheCppStructOps)
	{
		if (TheCppStructOps->HasDestructor())
		{
			for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
			{
				uint8* PropertyData = (uint8*)Dest + ArrayIndex * Stride;
				TheCppStructOps->Destruct(PropertyData);
			}
		}
		ClearedSize = TheCppStructOps->GetSize();
		// here we want to make sure C++ and the property system agree on the size
		check(InheritedCppStructOps() || (Stride == ClearedSize && PropertiesSize == ClearedSize));
	}

	if (PropertiesSize > ClearedSize)
	{
		bool bHitBase = false;
		for (UProperty* P = DestructorLink; P  && !bHitBase; P = P->DestructorLinkNext)
		{
			if (!P->IsInContainer(ClearedSize))
			{
				if (!P->HasAnyPropertyFlags(CPF_NoDestructor))
				{
					for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
					{
						P->DestroyValue_InContainer(Data + ArrayIndex * Stride);
					}
				}
			}
			else
			{
				bHitBase = true;
			}
		}
	}
}

void UScriptStruct::RecursivelyPreload() {}

IMPLEMENT_CORE_INTRINSIC_CLASS(UScriptStruct, UStruct,
	{
	}
);

/*-----------------------------------------------------------------------------
	UClass implementation.
-----------------------------------------------------------------------------*/

void UClass::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		if (ClassAddReferencedObjects == NULL)
		{
			// Default__Class uses its own AddReferencedObjects function.
			ClassAddReferencedObjects = &UClass::AddReferencedObjects;
		}
	}
}

UObject* UClass::GetDefaultSubobjectByName(FName ToFind)
{
	TArray<UObject*> SubObjects;
	GetDefaultObjectSubobjects(SubObjects);
	for (int32 Index = 0; Index < SubObjects.Num(); ++Index)
	{
		if (SubObjects[Index]->GetFName() == ToFind)
		{
			return SubObjects[Index];
		}
	}
	return NULL;
}

void UClass::GetDefaultObjectSubobjects(TArray<UObject*>& OutDefaultSubobjects)
{
	OutDefaultSubobjects.Empty();
	GetObjectsWithOuter(GetDefaultObject(), OutDefaultSubobjects, false);
	for ( int32 SubobjectIndex = 0; SubobjectIndex < OutDefaultSubobjects.Num(); SubobjectIndex++ )
	{
		UObject* PotentialComponent = OutDefaultSubobjects[SubobjectIndex];
		if (!PotentialComponent->IsDefaultSubobject())
		{
			OutDefaultSubobjects.RemoveAtSwap(SubobjectIndex--);
		}
	}
}

/**
 * Callback used to allow an object to register its direct object references that are not already covered by
 * the token stream.
 *
 * @param ObjectArray	array to add referenced objects to via AddReferencedObject
 */
void UClass::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UClass* This = CastChecked<UClass>(InThis);
	for( auto& Inter : This->Interfaces )
	{
		Collector.AddReferencedObject( Inter.Class, This );
	}

	for( auto& Func : This->FuncMap )
	{
		Collector.AddReferencedObject( Func.Value, This );
	}

	Collector.AddReferencedObject( This->ClassWithin, This );
	Collector.AddReferencedObject( This->ClassGeneratedBy, This );

	if ( !Collector.IsIgnoringArchetypeRef() )
	{
		Collector.AddReferencedObject( This->ClassDefaultObject, This );
	}
	else if( This->ClassDefaultObject != NULL)
	{
		// Get the ARO function pointer from the CDO class (virtual functions using static function pointers).
		This->CallAddReferencedObjects(This->ClassDefaultObject, Collector);
	}

	Super::AddReferencedObjects( This, Collector );
}

/**
 * Helper class used to save and restore information across a StaticAllocateObject over the top of an existing UClass.
 */
class FRestoreClassInfo: public FRestoreForUObjectOverwrite
{
	/** Keep a copy of the pointer, which isn't supposed to change **/
	UClass*			Target;
	/** Saved ClassWithin **/
	UClass*			Within;
	/** Save ClassGeneratedBy */
	UObject*		GeneratedBy;
	/** Saved ClassDefaultObject **/
	UObject*		DefaultObject;
	/** Saved ClassFlags **/
	uint32			Flags;
	/** Saved ClassCastFlags **/
	EClassCastFlags	CastFlags;
	/** Saved ClassConstructor **/
	void			(*Constructor)(const class FPostConstructInitializeProperties&);
	/** Saved ClassConstructor **/
	void			(*AddReferencedObjects)(UObject*, FReferenceCollector&);
	/** Saved NativeFunctionLookupTable. */
	TArray<FNativeFunctionLookup> NativeFunctionLookupTable;
public:

	/**
	 * Constructor: remember the info for the class so that we can restore it after we've called
	 * FMemory::Memzero() on the object's memory address, which results in the non-intrinsic classes losing
	 * this data
	 */
	FRestoreClassInfo(UClass *Save) :
		Target(Save),
		Within(Save->ClassWithin),
		GeneratedBy(Save->ClassGeneratedBy),
		DefaultObject(Save->GetDefaultsCount() ? Save->GetDefaultObject() : NULL),
		Flags(Save->ClassFlags & CLASS_Abstract),
		CastFlags(Save->ClassCastFlags),
		Constructor(Save->ClassConstructor),
		AddReferencedObjects(Save->ClassAddReferencedObjects),
		NativeFunctionLookupTable(Save->NativeFunctionLookupTable)
	{
	}
	/** Called once the new object has been reinitialized 
	**/
	virtual void Restore() const
	{
		Target->ClassWithin = Within;
		Target->ClassGeneratedBy = GeneratedBy;
		Target->ClassDefaultObject = DefaultObject;
		Target->ClassFlags |= Flags;
		Target->ClassCastFlags |= CastFlags;
		Target->ClassConstructor = Constructor;
		Target->ClassAddReferencedObjects = AddReferencedObjects;
		Target->NativeFunctionLookupTable = NativeFunctionLookupTable;
	}
};

/**
 * Save information for StaticAllocateObject in the case of overwriting an existing object.
 * StaticAllocateObject will call delete on the result after calling Restore()
 *
 * @return An FRestoreForUObjectOverwrite that can restore the object or NULL if this is not necessary.
 */
FRestoreForUObjectOverwrite* UClass::GetRestoreForUObjectOverwrite()
{
	return new FRestoreClassInfo(this);
}

/**
	* Get the default object from the class, creating it if missing, if requested or under a few other circumstances
	* @return		the CDO for this class
**/
UObject* UClass::CreateDefaultObject()
{
	if ( ClassDefaultObject == NULL )
	{
		UClass* ParentClass = GetSuperClass();
		UObject* ParentDefaultObject = NULL;
		if ( ParentClass != NULL )
		{
			UObjectForceRegistration(ParentClass);
			ParentDefaultObject = ParentClass->GetDefaultObject(); // Force the default object to be constructed if it isn't already
		}

		if ( (ParentDefaultObject != NULL) || (this == UObject::StaticClass()) )
		{
			// If this is a class that can be regenerated, it is potentially not completely loaded.  Preload and Link here to ensure we properly zero memory and read in properties for the CDO
			if( (ClassGeneratedBy != NULL) && (PropertyLink == NULL) && !GIsDuplicatingClassForReinstancing)
			{
				ULinkerLoad* ClassLinker = GetLinker();
				if( ClassLinker )
				{
					UField* FieldIt = Children;
					while(FieldIt && (FieldIt->GetOuter() == this))
					{
						// If we've had cyclic dependencies between classes here, we might need to preload to ensure that we load the rest of the property chain
						if( FieldIt->HasAnyFlags(RF_NeedLoad) )
						{
							ClassLinker->Preload(FieldIt);
						}
						FieldIt = FieldIt->Next;
					}
					
					StaticLink(true);
				}
			}

			// in the case of cyclic dependencies, the above Preload() calls could end up 
			// invoking this method themselves... that means that once we're done with  
			// all the Preload() calls we have to make sure ClassDefaultObject is still 
			// NULL (so we don't invalidate one that has already been setup)
			if (ClassDefaultObject == NULL)
			{
				ClassDefaultObject = StaticAllocateObject(this, GetOuter(), NAME_None, EObjectFlags(RF_Public|RF_ClassDefaultObject));
				check(ClassDefaultObject);
				// Blueprint CDOs have their properties always initialized.
				const bool bShouldInitilizeProperties = !HasAnyClassFlags(CLASS_Native | CLASS_Intrinsic);
				(*ClassConstructor)(FPostConstructInitializeProperties(ClassDefaultObject, ParentDefaultObject, false, bShouldInitilizeProperties));
			}
		}
	}
	return ClassDefaultObject;
}

/**
 * Feedback context implementation for windows.
 */
class FFeedbackContextImportDefaults : public FFeedbackContext
{
	/** Context information for warning and error messages */
	FContextSupplier*	Context;

public:

	// Constructor.
	FFeedbackContextImportDefaults()
		: Context( NULL )
	{
		TreatWarningsAsErrors = true;
	}
	void Serialize( const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category )
	{
		if( Verbosity==ELogVerbosity::Error || Verbosity==ELogVerbosity::Warning )
		{
			if( TreatWarningsAsErrors && Verbosity==ELogVerbosity::Warning )
			{
				Verbosity = ELogVerbosity::Error;
			}

			FString Prefix;
			if( Context )
			{
				Prefix = Context->GetContext() + TEXT(" : ");
			}
			FString Format = Prefix + FOutputDevice::FormatLogLine(Verbosity, Category, V);

			if(Verbosity == ELogVerbosity::Error)
			{
				Errors.Add(Format);
			}
			else
			{
				Warnings.Add(Format);
			}
		}

		if( GLogConsole )
			GLogConsole->Serialize( V, Verbosity, Category );
		if( !GLog->IsRedirectingTo( this ) )
			GLog->Serialize( V, Verbosity, Category );
	}

	void BeginSlowTask( const FText& Task, bool ShowProgressDialog, bool bShowCancelButton=false )
	{
	}
	void EndSlowTask()
	{
	}
	virtual bool StatusUpdate(int32 Numerator, int32 Denominator, const FText& StatusText )
	{
		return true;
	}

	FContextSupplier* GetContext() const
	{
		return Context;
	}
	void SetContext( FContextSupplier* InSupplier )
	{
		Context = InSupplier;
	}
};

FFeedbackContext& UClass::GetDefaultPropertiesFeedbackContext()
{
	static FFeedbackContextImportDefaults FeedbackContextImportDefaults;
	return FeedbackContextImportDefaults;
}

/**
* Get the name of the CDO for the this class
* @return The name of the CDO
*/
FName UClass::GetDefaultObjectName()
{
	FString DefaultName;
	DefaultName.Reserve(NAME_SIZE);
	DefaultName += DEFAULT_OBJECT_PREFIX;
	AppendName(DefaultName);
	return FName(*DefaultName);
}

//
// Register the native class.
//
void UClass::DeferredRegister(UClass *UClassStaticClass,const TCHAR* PackageName,const TCHAR* Name)
{
	Super::DeferredRegister(UClassStaticClass,PackageName,Name);

	// Get stashed registration info.
	const TCHAR* InClassConfigName = *(TCHAR**)&ClassConfigName;
	ClassConfigName = InClassConfigName;

	// Propagate inherited flags.
	if (SuperStruct != NULL)
	{
		UClass* SuperClass = GetSuperClass();
		ClassFlags |= (SuperClass->ClassFlags & CLASS_Inherit);
		ClassCastFlags |= SuperClass->ClassCastFlags;
	}
}

bool UClass::Rename( const TCHAR* InName, UObject* NewOuter, ERenameFlags Flags )
{
	bool bSuccess = Super::Rename( InName, NewOuter, Flags );

	// If we have a default object, rename that to the same package as the class, and rename so it still matches the class name (Default__ClassName)
	if(bSuccess && (ClassDefaultObject != NULL))
	{
		ClassDefaultObject->Rename(*GetDefaultObjectName().ToString(), NewOuter, Flags);
	}

	// Now actually rename the class
	return bSuccess;
}

void UClass::TagSubobjects(EObjectFlags NewFlags)
{
	Super::TagSubobjects(NewFlags);

	if (ClassDefaultObject && !ClassDefaultObject->HasAnyFlags(GARBAGE_COLLECTION_KEEPFLAGS | RF_RootSet))
	{
		ClassDefaultObject->SetFlags(NewFlags);
		ClassDefaultObject->TagSubobjects(NewFlags);
	}
}

/**
 * Find the class's native constructor.
 */
void UClass::Bind()
{
	UStruct::Bind();

	if( !GIsUCCMakeStandaloneHeaderGenerator && !ClassConstructor && HasAnyFlags(RF_Native) )
	{
		UE_LOG(LogClass, Fatal, TEXT("Can't bind to native class %s"), *GetPathName() );
	}

	UClass* SuperClass = GetSuperClass();
	if (SuperClass && (ClassConstructor == NULL || ClassAddReferencedObjects == NULL))
	{
		// Chase down constructor in parent class.
		SuperClass->Bind();
		if (!ClassConstructor)
		{
			ClassConstructor = SuperClass->ClassConstructor;
		}
		if (!ClassAddReferencedObjects)
		{
			ClassAddReferencedObjects = SuperClass->ClassAddReferencedObjects;
		}

		// propagate flags.
		// we don't propagate the inherit flags, that is more of a header generator thing
		ClassCastFlags |= SuperClass->ClassCastFlags;
	}
	//if( !Class && SuperClass )
	//{
	//}
	if( !ClassConstructor )
	{
		UE_LOG(LogClass, Fatal, TEXT("Can't find ClassConstructor for class %s"), *GetPathName() );
	}
}


/**
 * Returns the struct/ class prefix used for the C++ declaration of this struct/ class.
 * Classes deriving from AActor have an 'A' prefix and other UObject classes an 'U' prefix.
 *
 * @return Prefix character used for C++ declaration of this struct/ class.
 */
const TCHAR* UClass::GetPrefixCPP() const
{
	const UClass* TheClass	= this;
	bool	bIsActorClass	= false;
	bool	bIsDeprecated	= TheClass->HasAnyClassFlags(CLASS_Deprecated);
	while( TheClass && !bIsActorClass )
	{
		bIsActorClass	= TheClass->GetFName() == NAME_Actor;
		TheClass		= TheClass->GetSuperClass();
	}

	if( bIsActorClass )
	{
		if( bIsDeprecated )
		{
			return TEXT("ADEPRECATED_");
		}
		else
		{
			return TEXT("A");
		}
	}
	else
	{
		if( bIsDeprecated )
		{
			return TEXT("UDEPRECATED_");
		}
		else
		{
			return TEXT("U");
		}		
	}
}

FString UClass::GetDescription() const
{
	FString Description;

#if WITH_EDITOR
	// See if display name meta data has been specified
	Description = GetDisplayNameText().ToString();
	if (Description.Len())
	{
		return Description;
	}
#endif

	// Look up the the classes name in the legacy int file and return the class name if there is no match.
	//Description = Localize( TEXT("Objects"), *GetName(), *(FInternationalization::Get().GetCurrentCulture()->GetName()), true );
	//if (Description.Len())
	//{
	//	return Description;
	//}

	// Otherwise just return the class name
	return FString( GetName() );
}

//	UClass UObject implementation.

void UClass::FinishDestroy()
{
	// Empty arrays.
	//warning: Must be emptied explicitly in order for intrinsic classes
	// to not show memory leakage on exit.
	NetFields.Empty();

	ClassDefaultObject = NULL;

	Super::FinishDestroy();
}

void UClass::PostLoad()
{
	check(ClassWithin);
	Super::PostLoad();

	// Postload super.
	if( GetSuperClass() )
	{
		GetSuperClass()->ConditionalPostLoad();
	}
}

FString UClass::GetDesc()
{
	return GetName();
}

void UClass::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	check(!bRelinkExistingProperties || !(ClassFlags & CLASS_Intrinsic));
	Super::Link(Ar, bRelinkExistingProperties);

	if (PropertyLink != NULL)
	{
		NetFields.Empty();
		if (SuperStruct)
		{
			ClassReps = GetSuperClass()->ClassReps;
		}
		else
		{
			ClassReps.Empty();
		}

		TArray< UProperty * > NetProperties;		// Track properties so me can ensure they are sorted by offsets at the end

		for( TFieldIterator<UField> It(this,EFieldIteratorFlags::ExcludeSuper); It; ++It )
		{
			UProperty* P;
			UFunction* F;
			if( (P=Cast<UProperty>(*It))!=NULL )
			{
				if ( P->PropertyFlags & CPF_Net )
				{
					NetFields.Add( *It );

					if ( P->GetOuter() == this )
					{
						NetProperties.Add( P );
					}
				}
			}
			else if( (F=Cast<UFunction>(*It))!=NULL )
			{
				check(!F->GetSuperFunction() || (F->GetSuperFunction()->FunctionFlags&FUNC_NetFuncFlags) == (F->FunctionFlags&FUNC_NetFuncFlags));
				if( (F->FunctionFlags&FUNC_Net) && !F->GetSuperFunction() )
					NetFields.Add( *It );
			}
		}

		// Sort NetProperties so that their ClassReps are sorted by memory offset
		struct FCompareUFieldOffsets
		{
			FORCEINLINE bool operator()( UProperty & A, UProperty & B ) const
			{
				return A.GetOffset_ForGC() < B.GetOffset_ForGC();
			}
		};

		Sort( NetProperties.GetTypedData(), NetProperties.Num(), FCompareUFieldOffsets() );

		for ( int32 i = 0; i < NetProperties.Num(); i++ )
		{
			NetProperties[i]->RepIndex = ClassReps.Num();
			for ( int32 j = 0; j < NetProperties[i]->ArrayDim; j++ )
			{
				new( ClassReps )FRepRecord( NetProperties[i], j );
			}
		}

		NetFields.Shrink();

		struct FCompareUFieldNames
		{
			FORCEINLINE bool operator()(UField& A, UField& B) const
			{
				return A.GetName() < B.GetName();
			}
		};
		Sort( NetFields.GetTypedData(), NetFields.Num(), FCompareUFieldNames() );
	}
}

void UClass::SetSuperStruct(UStruct* NewSuperStruct)
{
	UnhashObject(this);
	Super::SetSuperStruct(NewSuperStruct);
	HashObject(this);
}

void UClass::Serialize( FArchive& Ar )
{
	if ( Ar.IsLoading() || Ar.IsModifyingWeakAndStrongReferences() )
	{
		// Rehash since SuperStruct will be serialized in UStruct::Serialize
		UnhashObject(this);
	}

	Super::Serialize( Ar );

	if ( Ar.IsLoading() || Ar.IsModifyingWeakAndStrongReferences() )
	{
		HashObject(this);
	}

	Ar.ThisContainsCode();

	// serialize the function map
	//@TODO: UCREMOVAL: Should we just regenerate the FuncMap post load, instead of serializing it?
	Ar << FuncMap;

	// Class flags first.
	Ar << ClassFlags;
	if (Ar.UE4Ver() < VER_UE4_CLASS_NOTPLACEABLE_ADDED)
	{
		// We need to invert the CLASS_NotPlaceable flag here because it used to mean CLASS_Placeable
		ClassFlags ^= CLASS_NotPlaceable;

		// We can't import a class which is placeable and has a not-placeable base, so we need to check for that here.
		if (ensure(HasAnyClassFlags(CLASS_NotPlaceable) || !GetSuperClass()->HasAnyClassFlags(CLASS_NotPlaceable)))
		{
			// It's good!
		}
		else
		{
			// We'll just make it non-placeable to ensure loading works, even if there's an off-chance that it's already been placed
			ClassFlags |= CLASS_NotPlaceable;
		}
	}

	// Variables.
	Ar << ClassWithin;
	Ar << ClassConfigName;

	if (Ar.UE4Ver() < VER_UE4_STOPPED_SERIALIZING_COMPONENTNAMETODEFAULTOBJECTMAP)
	{
		TArray<UObject*> ComponentNameToDefaultObjectMapSerialized;
		Ar << ComponentNameToDefaultObjectMapSerialized;
	}

	Ar << Interfaces;
	Ar << ClassGeneratedBy;

	if(Ar.IsLoading())
	{
		checkf(!HasAnyClassFlags(CLASS_Native), TEXT("Class %s loaded with CLASS_Native....we should not be loading any native classes."), *GetFullName());
		checkf(!HasAnyClassFlags(CLASS_Intrinsic), TEXT("Class %s loaded with CLASS_Intrinsic....we should not be loading any intrinsic classes."), *GetFullName());
		ClassFlags &= ~ CLASS_ShouldNeverBeLoaded;
		Link(Ar, true);
	}

	if( Ar.UE4Ver() < VER_UE4_DONTSORTCATEGORIES_REMOVED )
	{
		TArray<FName> DeprecatedDontSortCategories;
		Ar << DeprecatedDontSortCategories;
	}

	if (Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_CATEGORY_MOVED_TO_METADATA)
	{
		TArray<FName> TempHideCategories;
		TArray<FName> TempAutoExpandCategories;
		TArray<FName> TempAutoCollapseCategories;
		Ar << TempHideCategories;
		Ar << TempAutoExpandCategories;
		Ar << TempAutoCollapseCategories;
	}

	bool bDeprecatedForceScriptOrder = false;
	Ar << bDeprecatedForceScriptOrder;

	if (Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_CATEGORY_MOVED_TO_METADATA)
	{
		TArray<FName> TempClassGroupNames;
		Ar << TempClassGroupNames;
	}

	if( Ar.UE4Ver() < VER_UE4_CONSOLIDATE_HEADER_PARSER_ONLY_PROPERTIES )
	{
		FString ClassHeaderFilename;
		Ar << ClassHeaderFilename;
	}

	FName Dummy = NAME_None;
	Ar << Dummy;

	if (Ar.UE4Ver() >= VER_UE4_ADD_COOKED_TO_UCLASS)
	{
		if (Ar.IsSaving())
		{
			bCooked = Ar.IsCooking();
		}
		Ar << bCooked;
	}

	// Defaults.

	// mark the archive as serializing defaults
	Ar.StartSerializingDefaults();

	if( Ar.IsLoading() )
	{
		check(GetStructureSize() >= sizeof(UObject));
		check(!GetSuperClass() || !GetSuperClass()->HasAnyFlags(RF_NeedLoad));
		Ar << ClassDefaultObject;
		ClassUnique = 0;
	}
	else
	{
		check(GetDefaultsCount()==GetPropertiesSize());

		// Ensure that we have a valid CDO if this is a non-native class
		if( !HasAnyClassFlags(CLASS_Native) && (ClassDefaultObject == NULL) )
		{
			GetDefaultObject();	
		}

		// only serialize the class default object if the archive allows serialization of ObjectArchetype
		// otherwise, serialize the properties that the ClassDefaultObject references
		// The logic behind this is the assumption that the reason for not serializing the ObjectArchetype
		// is because we are performing some actions on objects of this class and we don't want to perform
		// that action on the ClassDefaultObject.  However, we do want to perform that action on objects that
		// the ClassDefaultObject is referencing, so we'll serialize it's properties instead of serializing
		// the object itself
		if ( !Ar.IsIgnoringArchetypeRef() )
		{
			Ar << ClassDefaultObject;
		}
		else if ( ClassDefaultObject != NULL )
		{
			ClassDefaultObject->Serialize(Ar);
		}
	}

	// mark the archive we that we are no longer serializing defaults
	Ar.StopSerializingDefaults();

	if( Ar.IsLoading() )
	{
		if (ClassDefaultObject == NULL)
		{
			UE_LOG(LogClass, Error, TEXT("CDO for class %s did not load!"), *GetPathName() );
			ensure(ClassDefaultObject != NULL);
			ClassDefaultObject = GetDefaultObject();
		}
	}
}

bool UClass::ImplementsInterface( const class UClass* SomeInterface ) const
{
	if (SomeInterface != NULL && SomeInterface->HasAnyClassFlags(CLASS_Interface) && SomeInterface != UInterface::StaticClass())
	{
		for (const UClass* CurrentClass = this; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
		{
			// SomeInterface might be a base interface of our implemented interface
			for (TArray<FImplementedInterface>::TConstIterator It(CurrentClass->Interfaces); It; ++It)
			{
				const UClass* InterfaceClass = It->Class;
				if (InterfaceClass->IsChildOf(SomeInterface))
				{
					return true;
				}
			}
		}
	}

	return false;
}

/** serializes the passed in object as this class's default object using the given archive
 * @param Object the object to serialize as default
 * @param Ar the archive to serialize from
 */
void UClass::SerializeDefaultObject(UObject* Object, FArchive& Ar)
{
	if (Ar.UE4Ver() < VER_UE4_REMOVE_NET_INDEX && (!(Ar.GetPortFlags() & PPF_Duplicate)))
	{
		int32 OldNetIndex = 0;
		Ar << OldNetIndex;
	}

	// tell the archive that it's allowed to load data for transient properties
	Ar.StartSerializingDefaults();

	if( ((Ar.IsLoading() || Ar.IsSaving()) && !Ar.WantBinaryPropertySerialization()) )
	{
	    // class default objects do not always have a vtable when saved
		// so use script serialization as opposed to native serialization to
	    // guarantee that all property data is loaded into the correct location
	    SerializeTaggedProperties(Ar, (uint8*)Object, GetSuperClass(), (uint8*)Object->GetArchetype());
	}
	else if ( Ar.GetPortFlags() != 0 )
	{
		SerializeBinEx(Ar, Object, Object->GetArchetype(), GetSuperClass() );
	}
	else
	{
		SerializeBin(Ar, Object, 0);
	}
	Ar.StopSerializingDefaults();
}


FArchive& operator<<(FArchive& Ar, FImplementedInterface& A)
{
	Ar << A.Class;
	if (Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_NO_INTERFACE_PROPERTY)
	{
		UObject* Junk = NULL;
		Ar << Junk;
		check(!Junk); // these should be exclusively K2, which should not have an associated property
		A.PointerOffset = 0;
	}
	else
	{
		Ar << A.PointerOffset;
	}
	Ar << A.bImplementedByK2;

	return Ar;
}

void UClass::PurgeClass(bool bRecompilingOnLoad)
{
	ClassConstructor = NULL;
	ClassFlags = 0;
	ClassCastFlags = 0;
	ClassUnique = 0;
	ClassReps.Empty();
	NetFields.Empty();

#if WITH_EDITOR
	if (!bRecompilingOnLoad)
	{
		// this is not safe to do at COL time. The meta data is not loaded yet, so if we attempt to load it, we recursively load the package and that will fail
		RemoveMetaData("HideCategories");
		RemoveMetaData("HideFunctions");
		RemoveMetaData("AutoExpandCategories");
		RemoveMetaData("AutoCollapseCategories");
		RemoveMetaData("ClassGroupNames");
	}
#endif

	ClassDefaultObject = NULL;

	Interfaces.Empty();
	NativeFunctionLookupTable.Empty();
	SetSuperStruct(NULL);
	Children = NULL;
	Script.Empty();
	MinAlignment = 0;
	RefLink = NULL;
	PropertyLink = NULL;
	DestructorLink = NULL;
	ClassAddReferencedObjects = NULL;

	ScriptObjectReferences.Empty();

	FuncMap.Empty();
	PropertyLink = NULL;
}

UClass* UClass::FindCommonBase(UClass* InClassA, UClass* InClassB)
{
	check(InClassA);
	UClass* CommonClass = InClassA;
	while (InClassB && !InClassB->IsChildOf(CommonClass))
	{
		CommonClass = CommonClass->GetSuperClass();

		if( !CommonClass )
			break;
	}
	return CommonClass;
}

UClass* UClass::FindCommonBase(const TArray<UClass*>& InClasses)
{
	check(InClasses.Num() > 0);
	auto ClassIter = InClasses.CreateConstIterator();
	UClass* CommonClass = *ClassIter;
	ClassIter++;

	for (; ClassIter; ++ClassIter)
	{
		CommonClass = UClass::FindCommonBase(CommonClass, *ClassIter);
	}
	return CommonClass;
}

bool UClass::IsFunctionImplementedInBlueprint(FName InFunctionName) const
{
	// Implemented in UBlueprintGeneratedClass
	return false;
}

/*-----------------------------------------------------------------------------
	UClass constructors.
-----------------------------------------------------------------------------*/

/**
 * Internal constructor.
 */
UClass::UClass(const class FPostConstructInitializeProperties& PCIP)
:	UStruct( PCIP )
,	ClassFlags(0)
,	ClassCastFlags(0)
,	ClassUnique(0)
,	ClassWithin( UObject::StaticClass() )
,	ClassGeneratedBy(NULL)
,	ClassDefaultObject(NULL)
,	bCooked(false)
{
	// If you add properties here, please update the other constructors and PurgeClass()
}

/**
 * Create a new UClass given its superclass.
 */
UClass::UClass(const class FPostConstructInitializeProperties& PCIP, UClass* InBaseClass )
:	UStruct( PCIP, InBaseClass )
,	ClassFlags(0)
,	ClassCastFlags(0)
,	ClassUnique(0)
,	ClassWithin( UObject::StaticClass() )
,	ClassGeneratedBy(NULL)
,	ClassDefaultObject(NULL)
,	bCooked(false)
{
	// If you add properties here, please update the other constructors and PurgeClass()

	UClass* ParentClass = GetSuperClass();
	if( ParentClass )
	{
		ClassWithin = ParentClass->ClassWithin;
		Bind();

		// if this is a native class, we may have defined a StaticConfigName() which overrides
		// the one from the parent class, so get our config name from there
		if ( HasAnyFlags(RF_Native) )
		{
			ClassConfigName = StaticConfigName();
		}
		else
		{
			// otherwise, inherit our parent class's config name
			ClassConfigName = ParentClass->ClassConfigName;
		}
	}
}

/**
 * Called when statically linked.
 */
UClass::UClass
(
	EStaticConstructor,
	uint32			InSize,
	uint32			InClassFlags,
	EClassCastFlags	InClassCastFlags,
	const TCHAR*    InConfigName,
	EObjectFlags	InFlags,
	void			(*InClassConstructor)(const class FPostConstructInitializeProperties&),
	void			(*InClassAddReferencedObjects)(UObject*, class FReferenceCollector&)
)
:	UStruct					( EC_StaticConstructor, InSize, InFlags )
,	ClassConstructor		( InClassConstructor )
,	ClassAddReferencedObjects( InClassAddReferencedObjects )
,	ClassFlags				( InClassFlags | CLASS_Native )
,	ClassCastFlags			( InClassCastFlags )
,	ClassUnique				( 0 )
,	ClassWithin				( NULL )
,	ClassGeneratedBy		( NULL )
,	ClassConfigName			()
,	NetFields				()
,	ClassDefaultObject		( NULL )
,	bCooked( false )
{
	// If you add properties here, please update the other constructors and PurgeClass()

	*(const TCHAR**)&ClassConfigName = InConfigName;
}

#if !IS_MONOLITHIC

bool UClass::HotReloadPrivateStaticClass(
	uint32			InSize,
	uint32			InClassFlags,
	EClassCastFlags	InClassCastFlags,
	const TCHAR*    InConfigName,
	void			(*InClassConstructor)(const class FPostConstructInitializeProperties&),
	void			(*InAddReferencedObjects)(UObject*, class FReferenceCollector&),
	class UClass* TClass_Super_StaticClass,
	class UClass* TClass_WithinClass_StaticClass
	)
{
	if (InSize != PropertiesSize)
	{
		UClass::GetDefaultPropertiesFeedbackContext().Logf(ELogVerbosity::Warning, TEXT("Property size mismatch. Will not update class %s (was %d, new %d)."), *GetName(), PropertiesSize, InSize);
		return false;
	}
	//We could do this later, but might as well get it before we start corrupting the object
	UObject* CDO = GetDefaultObject();
	void* OldVTable = *(void**)CDO;


	//@todo safe? ClassFlags = InClassFlags | CLASS_Native;
	//@todo safe? ClassCastFlags = InClassCastFlags;
	//@todo safe? ClassConfigName = InConfigName;
	void(*OldClassConstructor)(const class FPostConstructInitializeProperties&) = ClassConstructor;
	ClassConstructor = InClassConstructor;
	ClassAddReferencedObjects = InAddReferencedObjects;
	/* No recursive ::StaticClass calls allowed. Setup extras. */
	/* @todo safe? 
	if (TClass_Super_StaticClass != this)
	{
		SetSuperStruct(TClass_Super_StaticClass);
	}
	else
	{
		SetSuperStruct(NULL);
	}
	ClassWithin = TClass_WithinClass_StaticClass;
	*/

	UE_LOG(LogClass, Verbose, TEXT("Attempting to change VTable for class %s."),*GetName());
	ClassWithin = UPackage::StaticClass();  // We are just avoiding error checks with this...we don't care about this temp object other than to get the vtable.
	UObject* TempObjectForVTable = StaticConstructObject(this, GetTransientPackage(), NAME_None, RF_NeedLoad | RF_ClassDefaultObject);

	if( !TempObjectForVTable->IsRooted() )
	{
		TempObjectForVTable->MarkPendingKill();
	}
	else
	{
		UE_LOG(LogClass, Warning, TEXT("Hot Reload:  Was not expecting temporary object '%s' for class '%s' to become rooted during construction.  This object cannot be marked pending kill." ), *TempObjectForVTable->GetFName().ToString(), *this->GetName() );
	}

	ClassWithin = TClass_WithinClass_StaticClass;

	void* NewVTable = *(void**)TempObjectForVTable;
	if (NewVTable != OldVTable)
	{
		int32 Count = 0;
		int32 CountClass = 0;
		for ( FRawObjectIterator It; It; ++It )
		{
			UObject* Target = *It;
			if (OldVTable == *(void**)Target)
			{
				*(void**)Target = NewVTable;
				Count++;
			}
			else if (Cast<UClass>(Target))
			{
				UClass *Class = CastChecked<UClass>(Target);
				if (Class->ClassConstructor == OldClassConstructor)
				{
					Class->ClassConstructor = ClassConstructor;
					Class->ClassAddReferencedObjects = ClassAddReferencedObjects;
					CountClass++;
				}
			}
		}
		UE_LOG(LogClass, Verbose, TEXT("Updated the vtable for %d live objects and %d blueprint classes.  %016llx -> %016llx"), Count, CountClass, PTRINT(OldVTable), PTRINT(NewVTable));
	}
	else
	{
		UE_LOG(LogClass, Error, TEXT("VTable for class %s did not change?"),*GetName());
	}
	return true;
}
#endif

/**
* Add a native function to the internal native function table
* @param	InName							name of the function
* @param	InPointer						pointer to the function
**/
void UClass::AddNativeFunction(const ANSICHAR* InName,Native InPointer)
{
	FName InFName(InName);
#if !IS_MONOLITHIC
	if (GIsHotReload)
	{
		// Find the function in the class's native function lookup table.
		for(int32 FunctionIndex = 0;FunctionIndex < NativeFunctionLookupTable.Num();++FunctionIndex)
		{
			FNativeFunctionLookup& NativeFunctionLookup = NativeFunctionLookupTable[FunctionIndex];
			if(NativeFunctionLookup.Name == InFName)
			{
				AddHotReloadFunctionRemap(InPointer, NativeFunctionLookup.Pointer);
				NativeFunctionLookup.Pointer = InPointer;
				return;
			}
		}
		UE_LOG(LogClass, Log, TEXT("Function %s is new."),* InFName.ToString());
	}
#endif
	new(NativeFunctionLookupTable) FNativeFunctionLookup(InFName,InPointer);
}

UFunction* UClass::FindFunctionByName(FName InName, EIncludeSuperFlag::Type IncludeSuper) const
{
	if (!IncludeSuper)
		return FuncMap.FindRef(InName);

	for (const UClass* SearchClass = this; SearchClass; SearchClass = SearchClass->GetSuperClass())
	{
		if (UFunction* Result = SearchClass->FuncMap.FindRef(InName))
			return Result;

		for (auto& Inter : SearchClass->Interfaces)
		{
			if (UFunction* Result = Inter.Class->FuncMap.FindRef(InName))
				return Result;
		}
	}

	return NULL;
}

const FString UClass::GetConfigName() const
{
	if( ClassConfigName == NAME_Engine )
	{
		return GEngineIni;
	}
	else if( ClassConfigName == NAME_Editor )
	{
		return GEditorIni;
	}
	else if( ClassConfigName == NAME_Input )
	{
		return GInputIni;
	}
	else if( ClassConfigName == NAME_Game )
	{
		return GGameIni;
	}
	else if ( ClassConfigName == NAME_EditorGameAgnostic )
	{
		return GEditorGameAgnosticIni;
	}
	else if( ClassConfigName == NAME_None )
	{
		UE_LOG(LogClass, Fatal,TEXT("UObject::GetConfigName() called on class with config name 'None'. Class flags = %d"), ClassFlags );
		return TEXT("");
	}
	else
	{
		// generate the class ini name, and make sure it's up to date
		FString ConfigGameName;
		FConfigCacheIni::LoadGlobalIniFile(ConfigGameName, *ClassConfigName.ToString());
		return ConfigGameName;
	}
}

#if WITH_EDITOR || HACK_HEADER_GENERATOR
void UClass::GetHideCategories(TArray<FString>& OutHideCategories) const
{
	static const FName NAME_HideCategories(TEXT("HideCategories"));
	if (HasMetaData(NAME_HideCategories))
	{
		const FString& HideCategories = GetMetaData(NAME_HideCategories);
		HideCategories.ParseIntoArray(&OutHideCategories, TEXT(" "), true);
	}
}

bool UClass::IsCategoryHidden(const FString& InCategory) const
{
	bool bHidden = false;
	static const FName NAME_HideCategories(TEXT("HideCategories"));
	if (HasMetaData(NAME_HideCategories))
	{
		const FString& HideCategories = GetMetaData(NAME_HideCategories);
		bHidden = !!FCString::StrfindDelim(*HideCategories, *InCategory, TEXT(" "));
		if (!bHidden)
		{
			TArray<FString> SubCategoryList;
			InCategory.ParseIntoArray(&SubCategoryList, TEXT("|"), true);
			for (const FString& SubCategory : SubCategoryList)
			{
				if (!!FCString::StrfindDelim(*HideCategories, *SubCategory, TEXT(" ")))
				{
					bHidden = true;
					break;
				}
			}		
		}
	}
	return bHidden;
}

void UClass::GetHideFunctions(TArray<FString>& OutHideFunctions) const
{
	static const FName NAME_HideFunctions(TEXT("HideFunctions"));
	if (HasMetaData(NAME_HideFunctions))
	{
		const FString& HideFunctions = GetMetaData(NAME_HideFunctions);
		HideFunctions.ParseIntoArray(&OutHideFunctions, TEXT(" "), true);
	}
}

bool UClass::IsFunctionHidden(const TCHAR* InFunction) const
{
	static const FName NAME_HideFunctions(TEXT("HideFunctions"));
	if (HasMetaData(NAME_HideFunctions))
	{
		const FString& HideFunctions = GetMetaData(NAME_HideFunctions);
		return !!FCString::StrfindDelim(*HideFunctions, InFunction, TEXT(" "));
	}
	return false;
}

void UClass::GetAutoExpandCategories(TArray<FString>& OutAutoExpandCategories) const
{
	static const FName NAME_AutoExpandCategories(TEXT("AutoExpandCategories"));
	if (HasMetaData(NAME_AutoExpandCategories))
	{
		const FString& AutoExpandCategories = GetMetaData(NAME_AutoExpandCategories);
		AutoExpandCategories.ParseIntoArray(&OutAutoExpandCategories, TEXT(" "), true);
	}
}

bool UClass::IsAutoExpandCategory(const TCHAR* InCategory) const
{
	static const FName NAME_AutoExpandCategories(TEXT("AutoExpandCategories"));
	if (HasMetaData(NAME_AutoExpandCategories))
	{
		const FString& AutoExpandCategories = GetMetaData(NAME_AutoExpandCategories);
		return !!FCString::StrfindDelim(*AutoExpandCategories, InCategory, TEXT(" "));
	}
	return false;
}

void UClass::GetAutoCollapseCategories(TArray<FString>& OutAutoCollapseCategories) const
{
	static const FName NAME_AutoCollapseCategories(TEXT("AutoCollapseCategories"));
	if (HasMetaData(NAME_AutoCollapseCategories))
	{
		const FString& AutoCollapseCategories = GetMetaData(NAME_AutoCollapseCategories);
		AutoCollapseCategories.ParseIntoArray(&OutAutoCollapseCategories, TEXT(" "), true);
	}
}

bool UClass::IsAutoCollapseCategory(const TCHAR* InCategory) const
{
	static const FName NAME_AutoCollapseCategories(TEXT("AutoCollapseCategories"));
	if (HasMetaData(NAME_AutoCollapseCategories))
	{
		const FString& AutoCollapseCategories = GetMetaData(NAME_AutoCollapseCategories);
		return !!FCString::StrfindDelim(*AutoCollapseCategories, InCategory, TEXT(" "));
	}
	return false;
}

void UClass::GetClassGroupNames(TArray<FString>& OutClassGroupNames) const
{
	static const FName NAME_ClassGroupNames(TEXT("ClassGroupNames"));
	if (HasMetaData(NAME_ClassGroupNames))
	{
		const FString& ClassGroupNames = GetMetaData(NAME_ClassGroupNames);
		ClassGroupNames.ParseIntoArray(&OutClassGroupNames, TEXT(" "), true);
	}
}

bool UClass::IsClassGroupName(const TCHAR* InGroupName) const
{
	static const FName NAME_ClassGroupNames(TEXT("ClassGroupNames"));
	if (HasMetaData(NAME_ClassGroupNames))
	{
		const FString& ClassGroupNames = GetMetaData(NAME_ClassGroupNames);
		return !!FCString::StrfindDelim(*ClassGroupNames, InGroupName, TEXT(" "));
	}
	return false;
}

#endif // WITH_EDITOR || HACK_HEADER_GENERATOR

IMPLEMENT_CORE_INTRINSIC_CLASS(UClass, UStruct,
	{
		Class->ClassAddReferencedObjects = &UClass::AddReferencedObjects;
		Class->EmitObjectReference( STRUCT_OFFSET( UClass, ClassDefaultObject ) );
		Class->EmitObjectReference( STRUCT_OFFSET( UClass, ClassWithin ) );
		Class->EmitObjectReference( STRUCT_OFFSET( UClass, ClassGeneratedBy ) );
		Class->EmitObjectArrayReference( STRUCT_OFFSET( UClass, NetFields ) );
	}
);


/*-----------------------------------------------------------------------------
	UFunction.
-----------------------------------------------------------------------------*/

UFunction::UFunction(const class FPostConstructInitializeProperties& PCIP, UFunction* InSuperFunction, uint32 InFunctionFlags, uint16 InRepOffset, SIZE_T ParamsSize )
: UStruct( PCIP, InSuperFunction, ParamsSize )
, FunctionFlags(InFunctionFlags)
, RepOffset(InRepOffset)
, RPCId(0)
, RPCResponseId(0)
, FirstPropertyToInit(NULL)
{
}

void UFunction::InitializeDerivedMembers()
{
	NumParms = 0;
	ParmsSize = 0;
	ReturnValueOffset = MAX_uint16;

	for (UProperty* Property = Cast<UProperty>(Children); Property; Property = Cast<UProperty>(Property->Next))
	{
		if (Property->PropertyFlags & CPF_Parm)
		{
			NumParms++;
			ParmsSize = Property->GetOffset_ForUFunction() + Property->GetSize();
			if (Property->PropertyFlags & CPF_ReturnParm)
			{
				ReturnValueOffset = Property->GetOffset_ForUFunction();
			}
		}
		else if ((FunctionFlags & FUNC_HasDefaults) != 0)
		{
			if (!Property->HasAnyPropertyFlags(CPF_ZeroConstructor))
			{
				FirstPropertyToInit = Property;
				break;
			}
		}
		else
		{
			break;
		}
	}
}

void UFunction::Invoke(UObject* Obj, FFrame& Stack, RESULT_DECL)
{
	checkSlow(Func);

	UClass* OuterClass = GetOuterUClass();
	if (OuterClass->IsChildOf(UInterface::StaticClass()))
	{
		Obj = (UObject*)Obj->GetInterfaceAddress(OuterClass);
	}

	return (Obj->*Func)(Stack, Result);
}

void UFunction::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );

	Ar.ThisContainsCode();

	Ar << FunctionFlags;

	// Replication info.
	if (FunctionFlags & FUNC_Net)
	{
		Ar << RepOffset;
	}

	// Precomputation.
	if ((Ar.GetPortFlags() & PPF_Duplicate) != 0)
	{
		Ar << NumParms;
		Ar << ParmsSize;
		Ar << ReturnValueOffset;
		Ar << FirstPropertyToInit;
	}
	else
	{
		if (Ar.IsLoading())
		{
			InitializeDerivedMembers();
		}
	}
}

UProperty* UFunction::GetReturnProperty()
{
	for( TFieldIterator<UProperty> It(this); It && (It->PropertyFlags & CPF_Parm); ++It )
	{
		if( It->PropertyFlags & CPF_ReturnParm )
		{
			return *It;
		}
	}
	return NULL;
}

void UFunction::Bind()
{
	UClass* OwnerClass = GetOwnerClass();

	// if this isn't a native function, or this function belongs to a native interface class (which has no C++ version), 
	// use ProcessInternal (call into script VM only) as the function pointer for this function
	if (!HasAnyFunctionFlags(FUNC_Native))
	{
		// Use processing function.
		Func = &UObject::ProcessInternal;
	}
	else
	{
		// Find the function in the class's native function lookup table.
		FName Name = GetFName();
		if (auto* Found = OwnerClass->NativeFunctionLookupTable.FindByPredicate([=](const FNativeFunctionLookup& NativeFunctionLookup){ return Name == NativeFunctionLookup.Name; }))
			{
			Func = Found->Pointer;
		}
#if USE_COMPILED_IN_NATIVES
		else if (!HasAnyFunctionFlags(FUNC_NetRequest))
		{
			UE_LOG(LogClass, Warning,TEXT("Failed to bind native function %s.%s"),*OwnerClass->GetName(),*GetName());
		}
#endif
	}
}

void UFunction::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);

	InitializeDerivedMembers();
}

bool UFunction::IsSignatureCompatibleWith(const UFunction* OtherFunction) const
{
	// Early out if they're exactly the same function
	if (this == OtherFunction)
	{
		return true;
	}

	// The script compiler pointlessly sets CPF_EditInline on components passed as arguments
	const uint64 IgnoreFlags = CPF_EditInline | CPF_ExportObject | CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_ComputedFlags | CPF_ConstParm;//@TODO: UCREMOVAL: CPF_ConstParm added as a hack to get blueprints compiling with a const DamageType parameter.

	// Run thru the parameter property chains to compare each property
	TFieldIterator<UProperty> IteratorA(this);
	TFieldIterator<UProperty> IteratorB(OtherFunction);

	while (IteratorA && (IteratorA->PropertyFlags & CPF_Parm))
	{
		if (IteratorB && (IteratorB->PropertyFlags & CPF_Parm))
		{
			// Compare the two properties to make sure their types are identical
			// Note: currently this requires both to be strictly identical and wouldn't allow functions that differ only by how derived a class is,
			// which might be desirable when binding delegates, assuming there is directionality in the SignatureIsCompatibleWith call
			UProperty* PropA = *IteratorA;
			UProperty* PropB = *IteratorB;

			FString TypeA(PropA->GetCPPType());
			FString TypeB(PropB->GetCPPType());

			// Check the flags as well
			const uint64 PropertyMash = PropA->PropertyFlags ^ PropB->PropertyFlags;
			if ((TypeA != TypeB) || ((PropertyMash & ~IgnoreFlags) != 0))
			{
				// Type mismatch between an argument of A and B
				return false;
			}
		}
		else
		{
			// B ran out of arguments before A did
			return false;
		}
		++IteratorA;
		++IteratorB;
	}

	// They matched all the way thru A's properties, but it could still be a mismatch if B has remaining parameters
	return !(IteratorB && (IteratorB->PropertyFlags & CPF_Parm));
}


IMPLEMENT_CORE_INTRINSIC_CLASS(UFunction, UStruct,
	{
	}
);
