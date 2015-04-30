// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "Paper2DEditorPrivatePCH.h"
#include "TileMapEditorViewportClient.h"
#include "SceneViewport.h"
#include "EdModeTileMap.h"

#include "PreviewScene.h"
#include "ScopedTransaction.h"
#include "Runtime/Engine/Public/ComponentReregisterContext.h"
#include "CanvasTypes.h"
#include "Engine/Selection.h"
#include "TileMapEditorSettings.h"

#define LOCTEXT_NAMESPACE "TileMapEditor"

//////////////////////////////////////////////////////////////////////////
// FTileMapEditorViewportClient

FTileMapEditorViewportClient::FTileMapEditorViewportClient(TWeakPtr<FTileMapEditor> InTileMapEditor, TWeakPtr<class STileMapEditorViewport> InTileMapEditorViewportPtr)
	: TileMapEditorPtr(InTileMapEditor)
	, TileMapEditorViewportPtr(InTileMapEditorViewportPtr)
{
	// The tile map editor fully supports mode tools and isn't doing any incompatible stuff with the Widget
	Widget->SetUsesEditorModeTools(ModeTools);

	check(TileMapEditorPtr.IsValid() && TileMapEditorViewportPtr.IsValid());

	PreviewScene = &OwnedPreviewScene;
	((FAssetEditorModeManager*)ModeTools)->SetPreviewScene(PreviewScene);

	SetRealtime(true);

	WidgetMode = FWidget::WM_Translate;
	bManipulating = false;
	bManipulationDirtiedSomething = false;
	ScopedTransaction = nullptr;

	DrawHelper.bDrawGrid = GetDefault<UTileMapEditorSettings>()->bShowGridByDefault;
	DrawHelper.bDrawPivot = false;
	bShowPivot = true;

	EngineShowFlags.DisableAdvancedFeatures();
	EngineShowFlags.CompositeEditorPrimitives = true;

	// Create a render component for the tile map being edited
	{
		RenderTileMapComponent = NewObject<UPaperTileMapComponent>();
		UPaperTileMap* TileMap = GetTileMapBeingEdited();
		RenderTileMapComponent->TileMap = TileMap;
		RenderTileMapComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateLambda([](const UPrimitiveComponent*) { return true; });

		PreviewScene->AddComponent(RenderTileMapComponent, FTransform::Identity);
	}

	// Select the render component
	ModeTools->GetSelectedObjects()->Select(RenderTileMapComponent);
}

void FTileMapEditorViewportClient::ActivateEditMode()
{
	// Activate the tile map edit mode
	ModeTools->SetToolkitHost(TileMapEditorPtr.Pin()->GetToolkitHost());
	ModeTools->SetDefaultMode(FEdModeTileMap::EM_TileMap);
	ModeTools->ActivateDefaultMode();
	
	//@TODO: Need to be able to register the widget in the toolbox panel with ToolkitHost, so it can instance the ed mode widgets into it
}

void FTileMapEditorViewportClient::DrawBoundsAsText(FViewport& InViewport, FSceneView& View, FCanvas& Canvas, int32& YPos)
{
	FNumberFormattingOptions NoDigitGroupingFormat;
	NoDigitGroupingFormat.UseGrouping = false;

	UPaperTileMap* TileMap = GetTileMapBeingEdited();
	FBoxSphereBounds Bounds = TileMap->GetRenderBounds();

	const FText DisplaySizeText = FText::Format(LOCTEXT("BoundsSize", "Approx. Size: {0}x{1}x{2}"),
		FText::AsNumber((int32)(Bounds.BoxExtent.X * 2.0f), &NoDigitGroupingFormat),
		FText::AsNumber((int32)(Bounds.BoxExtent.Y * 2.0f), &NoDigitGroupingFormat),
		FText::AsNumber((int32)(Bounds.BoxExtent.Z * 2.0f), &NoDigitGroupingFormat));

	Canvas.DrawShadowedString(
		6,
		YPos,
		*DisplaySizeText.ToString(),
		GEngine->GetSmallFont(),
		FLinearColor::White);
	YPos += 18;
}

void FTileMapEditorViewportClient::DrawCanvas(FViewport& Viewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(Viewport, View, Canvas);

	const bool bIsHitTesting = Canvas.IsHitTesting();
	if (!bIsHitTesting)
	{
		Canvas.SetHitProxy(nullptr);
	}

	if (!TileMapEditorPtr.IsValid())
	{
		return;
	}

	int32 YPos = 42;

	// Draw the render bounds
	DrawBoundsAsText(Viewport, View, Canvas, /*inout*/ YPos);
}

void FTileMapEditorViewportClient::Tick(float DeltaSeconds)
{
	FPaperEditorViewportClient::Tick(DeltaSeconds);

	if (!GIntraFrameDebuggingGameThread)
	{
		OwnedPreviewScene.GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}
}

FLinearColor FTileMapEditorViewportClient::GetBackgroundColor() const
{
	if (UPaperTileMap* TileMap = RenderTileMapComponent->TileMap)
	{
		return TileMap->BackgroundColor;
	}
	else
	{
		return GetDefault<UTileMapEditorSettings>()->DefaultBackgroundColor;
	}
}

void FTileMapEditorViewportClient::ToggleShowTileGrid()
{
	FComponentReregisterContext RefreshComponentHelper(RenderTileMapComponent);
	RenderTileMapComponent->bShowPerTileGridWhenSelected = !RenderTileMapComponent->bShowPerTileGridWhenSelected;
	Invalidate();
}

bool FTileMapEditorViewportClient::IsShowTileGridChecked() const
{
	return RenderTileMapComponent->bShowPerTileGridWhenSelected;
}

void FTileMapEditorViewportClient::ToggleShowLayerGrid()
{
	FComponentReregisterContext RefreshComponentHelper(RenderTileMapComponent);
	RenderTileMapComponent->bShowPerLayerGridWhenSelected = !RenderTileMapComponent->bShowPerLayerGridWhenSelected;
	Invalidate();
}

bool FTileMapEditorViewportClient::IsShowLayerGridChecked() const
{
	return RenderTileMapComponent->bShowPerLayerGridWhenSelected;
}

void FTileMapEditorViewportClient::ToggleShowMeshEdges()
{
	EngineShowFlags.MeshEdges = !EngineShowFlags.MeshEdges;
	Invalidate();
}

bool FTileMapEditorViewportClient::IsShowMeshEdgesChecked() const
{
	return EngineShowFlags.MeshEdges;
}

void FTileMapEditorViewportClient::EndTransaction()
{
	if (bManipulationDirtiedSomething)
	{
		RenderTileMapComponent->TileMap->PostEditChange();
	}
	
	bManipulationDirtiedSomething = false;

	if (ScopedTransaction != nullptr)
	{
		delete ScopedTransaction;
		ScopedTransaction = nullptr;
	}
}

void FTileMapEditorViewportClient::NotifyTileMapBeingEditedHasChanged()
{
	//@TODO: Ideally we do this before switching
	EndTransaction();

	// Update components to know about the new tile map being edited
	UPaperTileMap* TileMap = GetTileMapBeingEdited();
	RenderTileMapComponent->TileMap = TileMap;

	RequestFocusOnSelection(/*bInstant=*/ true);
}

FBox FTileMapEditorViewportClient::GetDesiredFocusBounds() const
{
	return RenderTileMapComponent->Bounds.GetBox();
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE