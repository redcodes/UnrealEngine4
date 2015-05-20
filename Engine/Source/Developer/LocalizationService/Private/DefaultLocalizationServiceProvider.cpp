// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "LocalizationServicePrivatePCH.h"
#include "DefaultLocalizationServiceProvider.h"
#include "ILocalizationServiceModule.h"
#include "MessageLog.h"

#define LOCTEXT_NAMESPACE "DefaultLocalizationServiceProvider"

void FDefaultLocalizationServiceProvider::Init(bool bForceConnection)
{
	FMessageLog("LocalizationService").Info(LOCTEXT("LocalizationServiceDisabled", "Localization service is disabled"));
}

void FDefaultLocalizationServiceProvider::Close()
{

}

FText FDefaultLocalizationServiceProvider::GetStatusText() const
{
	return LOCTEXT("LocalizationServiceDisabled", "Localization service is disabled");
}

bool FDefaultLocalizationServiceProvider::IsAvailable() const
{
	return false;
}

bool FDefaultLocalizationServiceProvider::IsEnabled() const
{
	return false;
}

const FName& FDefaultLocalizationServiceProvider::GetName(void) const
{
	static FName ProviderName("None"); 
	return ProviderName; 
}

const FText FDefaultLocalizationServiceProvider::GetDisplayName() const
{
	return LOCTEXT("DefaultLocalizationServiceProviderDisplayName", "Localization Service Disabled");
}

ELocalizationServiceOperationCommandResult::Type FDefaultLocalizationServiceProvider::GetState(const TArray<FLocalizationServiceTranslationIdentifier>& InTranslationIds, TArray< TSharedRef<ILocalizationServiceState, ESPMode::ThreadSafe> >& OutState, ELocalizationServiceCacheUsage::Type InStateCacheUsage)
{
	return ELocalizationServiceOperationCommandResult::Failed;
}

ELocalizationServiceOperationCommandResult::Type FDefaultLocalizationServiceProvider::Execute(const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation, const TArray<FLocalizationServiceTranslationIdentifier>& InTranslationIds, ELocalizationServiceOperationConcurrency::Type InConcurrency, const FLocalizationServiceOperationComplete& InOperationCompleteDelegate)
{
	return ELocalizationServiceOperationCommandResult::Failed;
}

bool FDefaultLocalizationServiceProvider::CanCancelOperation( const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation ) const
{
	return false;
}

void FDefaultLocalizationServiceProvider::CancelOperation( const TSharedRef<ILocalizationServiceOperation, ESPMode::ThreadSafe>& InOperation )
{
}

void FDefaultLocalizationServiceProvider::Tick()
{

}

void FDefaultLocalizationServiceProvider::CustomizeSettingsDetails(IDetailCategoryBuilder& DetailCategoryBuilder) const
{

}

void FDefaultLocalizationServiceProvider::CustomizeTargetDetails(IDetailCategoryBuilder& DetailCategoryBuilder, ULocalizationTarget& Target) const
{

}

#undef LOCTEXT_NAMESPACE