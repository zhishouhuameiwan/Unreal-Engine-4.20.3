// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MediaIOCoreDefinitions.h"
#include "Containers/ArrayView.h"
#include "MediaIOCoreCommonDisplayMode.h"
#include "Misc/FrameRate.h"

#define LOCTEXT_NAMESPACE "MediaIOCoreDefinitions"

/**
 * MediaIOCoreDefinitions
 */
namespace MediaIOCoreDefinitions
{
	const int32 InvalidDeviceIdentifier = -1;
	const int32 InvalidDevicePortIdentifier = -1;
	const int32 InvalidDeviceModeIdentifier = -1;
	
	const FName NAME_Protocol = TEXT("protocol");
	
	const TCHAR* DeviceStr = TEXT("device");
	const TCHAR* SingleStr = TEXT("single");
	const TCHAR* DualStr = TEXT("dual");
	const TCHAR* QuadSquareStr = TEXT("quadSQ");
	const TCHAR* QuadTsiStr = TEXT("quadSI");
	
	const TCHAR* GetTransportString(EMediaIOSDITransportType InLinkType, EMediaIOQuadLinkTransportType InQuadLinkType)
	{
		const TCHAR* Result = MediaIOCoreDefinitions::SingleStr;
		switch (InLinkType)
		{
		case EMediaIOSDITransportType::DualLink: Result = MediaIOCoreDefinitions::DualStr; break;
		case EMediaIOSDITransportType::QuadLink:
		{
			Result = MediaIOCoreDefinitions::QuadSquareStr;
			if (InQuadLinkType == EMediaIOQuadLinkTransportType::TwoSampleInterleave)
			{
				Result = MediaIOCoreDefinitions::QuadTsiStr;
			}
			break;
		}
		}
		return Result;
	}
}


/**
 * FMediaIOCoreDevice
 */
FMediaIODevice::FMediaIODevice()
	: DeviceIdentifier(MediaIOCoreDefinitions::InvalidDeviceIdentifier)
{}


bool FMediaIODevice::operator==(const FMediaIODevice& Other) const
{
	return Other.DeviceIdentifier == DeviceIdentifier;
}


bool FMediaIODevice::IsValid() const
{
	return DeviceIdentifier != MediaIOCoreDefinitions::InvalidDeviceIdentifier;
}


/**
 * FMediaIOConnection
 */
FMediaIOConnection::FMediaIOConnection()
	: Protocol(MediaIOCoreDefinitions::NAME_Protocol)
	, TransportType(EMediaIOSDITransportType::SingleLink)
	, QuadTransportType(EMediaIOQuadLinkTransportType::SquareDivision)
	, PortIdentifier(MediaIOCoreDefinitions::InvalidDevicePortIdentifier)
{}


bool FMediaIOConnection::operator==(const FMediaIOConnection& Other) const
{
	return Other.Device == Device
		&& Other.TransportType == TransportType
		&& Other.PortIdentifier == PortIdentifier
		&& (Other.TransportType != EMediaIOSDITransportType::QuadLink || Other.QuadTransportType == QuadTransportType);
}


FString FMediaIOConnection::ToUrl() const
{
	if (IsValid())
	{
		return FString::Printf(TEXT("%s://%s%d/%s%d")
			, *Protocol.ToString()
			, MediaIOCoreDefinitions::DeviceStr
			, Device.DeviceIdentifier
			, MediaIOCoreDefinitions::GetTransportString(TransportType, QuadTransportType)
			, PortIdentifier
			);
	}
	return Protocol.ToString();
}


bool FMediaIOConnection::IsValid() const
{
	return Device.IsValid() && PortIdentifier != MediaIOCoreDefinitions::InvalidDevicePortIdentifier;
}

/**
 * FMediaIOMode
 */
FMediaIOMode::FMediaIOMode()
	: DeviceModeIdentifier(MediaIOCoreDefinitions::InvalidDeviceModeIdentifier)
{}


bool FMediaIOMode::operator==(const FMediaIOMode& Other) const
{
	return Other.DeviceModeIdentifier == DeviceModeIdentifier;
}


FText FMediaIOMode::GetModeName() const
{
	if (IsValid())
	{
		int32 FieldPerFrame = 1;
		FFrameRate FieldFrameRate = FrameRate;
		if (Standard == EMediaIOStandardType::Interlaced)
		{
			FieldPerFrame = 2;
			FieldFrameRate.Numerator /= 2;
		}
		return FMediaIOCommonDisplayModes::GetMediaIOCommonDisplayModeInfoName(Resolution.X, Resolution.Y, FieldFrameRate, FieldPerFrame);
	}
	return LOCTEXT("Invalid", "<Invalid>");
}


bool FMediaIOMode::IsValid() const
{
	return DeviceModeIdentifier != MediaIOCoreDefinitions::InvalidDeviceModeIdentifier;
}


/**
 * FMediaIOConfiguration
 */
FMediaIOConfiguration::FMediaIOConfiguration()
	: bIsInput(true)
{}


bool FMediaIOConfiguration::operator== (const FMediaIOConfiguration& Other) const
{
	return MediaConnection == Other.MediaConnection
		&& MediaMode == Other.MediaMode
		&& bIsInput == Other.bIsInput;
}


bool FMediaIOConfiguration::IsValid() const
{
	return MediaConnection.IsValid() && MediaMode.IsValid();
}


/**
 * FMediaIOConfiguration
 */
FMediaIOOutputConfiguration::FMediaIOOutputConfiguration()
	: OutputType(EMediaIOOutputType::Fill)
	, KeyPortIdentifier(MediaIOCoreDefinitions::InvalidDevicePortIdentifier)
	, OutputReference(EMediaIOReferenceType::FreeRun)
	, ReferencePortIdentifier(MediaIOCoreDefinitions::InvalidDevicePortIdentifier)
{
	MediaConfiguration.bIsInput = false;
}


bool FMediaIOOutputConfiguration::operator== (const FMediaIOOutputConfiguration& Other) const
{
	if (OutputType == Other.OutputType
		&& MediaConfiguration == Other.MediaConfiguration
		&& OutputReference == Other.OutputReference)
	{
		if (OutputType == EMediaIOOutputType::FillAndKey)
		{
			if (KeyPortIdentifier != Other.KeyPortIdentifier)
			{
				return false;
			}
		}
		if (OutputReference == EMediaIOReferenceType::Input)
		{
			if (ReferencePortIdentifier != Other.ReferencePortIdentifier)
			{
				return false;
			}
		}
		return true;
	}
	return false;
}


bool FMediaIOOutputConfiguration::IsValid() const
{
	bool bResult = false;
	if (MediaConfiguration.IsValid())
	{
		bResult = true;
		if (OutputType == EMediaIOOutputType::FillAndKey)
		{
			if (KeyPortIdentifier == MediaIOCoreDefinitions::InvalidDevicePortIdentifier)
			{
				bResult = false;
			}
		}
		if (OutputReference == EMediaIOReferenceType::Input)
		{
			if (ReferencePortIdentifier == MediaIOCoreDefinitions::InvalidDevicePortIdentifier)
			{
				bResult = false;
			}
		}
	}
	return bResult;
}

#undef LOCTEXT_NAMESPACE
