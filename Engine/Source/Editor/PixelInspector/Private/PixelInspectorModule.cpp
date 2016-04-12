// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "PixelInspectorPrivatePCH.h"
#include "PixelInspectorModule.h"
#include "PixelInspector.h"
#include "PropertyEditorModule.h"
#include "PixelInspectorStyle.h"
#include "PixelInspectorDetailsCustomization.h"

#include "SDockTab.h"

void FPixelInspectorModule::StartupModule()
{
	HPixelInspectorWindow = nullptr;
	bHasRegisteredTabSpawners = false;
}

void FPixelInspectorModule::ShutdownModule()
{
	FPixelInspectorStyle::Shutdown();
}

TSharedRef<SWidget> FPixelInspectorModule::CreatePixelInspectorWidget()
{
	SAssignNew(HPixelInspectorWindow, PixelInspector::SPixelInspector);		
	return HPixelInspectorWindow->AsShared();
}

bool FPixelInspectorModule::IsPixelInspectorEnable()
{
	return HPixelInspectorWindow.IsValid() ? HPixelInspectorWindow->IsPixelInspectorEnable() == ECheckBoxState::Checked : false;
}

bool FPixelInspectorModule::GetViewportRealtime(int32 ViewportUid, bool IsCurrentlyRealtime, bool IsMouseInsideViewport)
{
	bool bContainsOriginalValue = OriginalViewportStates.Contains(ViewportUid);
	if (IsMouseInsideViewport)
	{
		bool bIsPixelInspectorEnable = IsPixelInspectorEnable();
		if (!bContainsOriginalValue && bIsPixelInspectorEnable)
		{
			OriginalViewportStates.Add(ViewportUid, IsCurrentlyRealtime);
		}
		else if (bContainsOriginalValue && !bIsPixelInspectorEnable) //User hit esc key
		{
			return OriginalViewportStates.FindAndRemoveChecked(ViewportUid);
		}
		return bIsPixelInspectorEnable ? true : IsCurrentlyRealtime;
	}
	else
	{
		if (bContainsOriginalValue)
		{
			return OriginalViewportStates.FindAndRemoveChecked(ViewportUid);
		}
	}
	return IsCurrentlyRealtime;
}

void FPixelInspectorModule::CreatePixelInspectorRequest(FIntPoint ScreenPosition, int32 viewportUniqueId, FSceneInterface *SceneInterface)
{
	if (HPixelInspectorWindow.IsValid() == false)
	{
		return;
	}
	HPixelInspectorWindow->CreatePixelInspectorRequest(ScreenPosition, viewportUniqueId, SceneInterface);
}

void FPixelInspectorModule::ReadBackSync()
{
	if (HPixelInspectorWindow.IsValid() == false)
	{
		return;
	}
	HPixelInspectorWindow->ReadBackRequestData();
}

void FPixelInspectorModule::RegisterTabSpawner(const TSharedPtr<FWorkspaceItem>& WorkspaceGroup)
{
	if (bHasRegisteredTabSpawners)
	{
		UnregisterTabSpawner();
	}

	bHasRegisteredTabSpawners = true;
	
	FPixelInspectorStyle::Initialize();

	//Register the UPixelInspectorView detail customization
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(UPixelInspectorView::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FPixelInspectorDetailsCustomization::MakeInstance));
		PropertyModule.NotifyCustomizationModuleChanged();
	}

	{
		FTabSpawnerEntry& SpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner("LevelEditorPixelInspector", FOnSpawnTab::CreateRaw(this, &FPixelInspectorModule::MakePixelInspectorTab))
			.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorPixelInspector", "Pixel Inspector"))
			.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "LevelEditorPixelInspectorTooltipText", "Open the viewport pixel inspector tool."))
			.SetIcon(FSlateIcon(FPixelInspectorStyle::Get()->GetStyleSetName(), "PixelInspector.TabIcon"));

		if (WorkspaceGroup.IsValid())
		{
			SpawnerEntry.SetGroup(WorkspaceGroup.ToSharedRef());
		}
	}
}

void FPixelInspectorModule::UnregisterTabSpawner()
{
	bHasRegisteredTabSpawners = false;

	//Unregister the custom detail layout
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomClassLayout(UPixelInspectorView::StaticClass()->GetFName());

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("LevelEditorPixelInspector");
}

TSharedRef<SDockTab> FPixelInspectorModule::MakePixelInspectorTab(const FSpawnTabArgs&)
{
	TSharedRef<SDockTab> PixelInspectorTab = SNew(SDockTab)
	.Icon(FPixelInspectorStyle::Get()->GetBrush("PixelInspector.TabIcon"))
	.TabRole(ETabRole::NomadTab);
	PixelInspectorTab->SetContent(CreatePixelInspectorWidget());
	return PixelInspectorTab;
}

IMPLEMENT_MODULE(FPixelInspectorModule, PixelInspectorModule);