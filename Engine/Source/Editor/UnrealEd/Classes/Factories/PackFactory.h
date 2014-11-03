// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "PackFactory.generated.h"

/**
 * Factory for unpacking upack files
 */
UCLASS(hidecategories=Object)
class UPackFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// Begin UFactory Interface
	virtual UObject* FactoryCreateBinary( UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn ) override;
	// End UFactory Interface
};