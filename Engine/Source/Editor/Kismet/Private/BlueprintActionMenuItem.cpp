// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "BlueprintEditorPrivatePCH.h"
#include "BlueprintActionMenuItem.h"
#include "BlueprintNodeSpawner.h"
#include "KismetEditorUtilities.h"	// for BringKismetToFocusAttentionOnObject()
#include "BlueprintEditorUtils.h"	// for AnalyticsTrackNewNode(), MarkBlueprintAsModified(), etc.
#include "ScopedTransaction.h"
#include "SNodePanel.h"				// for GetSnapGridSize()

#define LOCTEXT_NAMESPACE "BlueprintActionMenuItem"

/*******************************************************************************
 * Static FBlueprintMenuActionItem Helpers
 ******************************************************************************/

namespace FBlueprintMenuActionItemImpl
{
	/**
	 * Utility function for marking blueprints dirty and recompiling them after 
	 * a node has been added.
	 * 
	 * @param  SpawnedNode	The node that was just added to the blueprint.
	 */
	static void DirtyBlueprintFromNewNode(UEdGraphNode* SpawnedNode);
}

//------------------------------------------------------------------------------
static void FBlueprintMenuActionItemImpl::DirtyBlueprintFromNewNode(UEdGraphNode* SpawnedNode)
{
	UEdGraph const* const NodeGraph = SpawnedNode->GetGraph();
	check(NodeGraph != nullptr);
	
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraphChecked(NodeGraph);
	check(Blueprint != nullptr);
	
	UK2Node* K2Node = Cast<UK2Node>(SpawnedNode);
	// see if we need to recompile skeleton after adding this node, or just mark
	// it dirty (default to rebuilding the skel, since there is no way to if
	// non-k2 nodes structurally modify the blueprint)
	if ((K2Node == nullptr) || K2Node->NodeCausesStructuralBlueprintChange())
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

/*******************************************************************************
 * FBlueprintMenuActionItem
 ******************************************************************************/

//------------------------------------------------------------------------------
FBlueprintActionMenuItem::FBlueprintActionMenuItem(UBlueprintNodeSpawner const* NodeSpawner, FSlateBrush const* MenuIcon, FSlateColor const& IconTintIn, int32 MenuGrouping/* = 0*/)
	: IconBrush(MenuIcon)
	, IconTint(IconTintIn)
	, Action(NodeSpawner)
{
	check(Action != nullptr);
	Grouping  = MenuGrouping;
}

//------------------------------------------------------------------------------
FBlueprintActionMenuItem::FBlueprintActionMenuItem(UBlueprintNodeSpawner const* NodeSpawner, IBlueprintNodeBinder::FBindingSet const& InBindings)
	: IconBrush(nullptr)
	, IconTint(FLinearColor::White)
	, Action(NodeSpawner)
	, Bindings(InBindings)
{
	check(Action != nullptr);
}

//------------------------------------------------------------------------------
UEdGraphNode* FBlueprintActionMenuItem::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, FVector2D const Location, bool bSelectNewNode/* = true*/)
{
	using namespace FBlueprintMenuActionItemImpl;
	FScopedTransaction Transaction(LOCTEXT("AddNodeTransaction", "Add Node"));
	
	FVector2D ModifiedLocation = Location;
	if (FromPin != nullptr)
	{
		// for input pins, a new node will generally overlap the node being
		// dragged from... work out if we want add in some spacing from the connecting node
		if (FromPin->Direction == EGPD_Input)
		{
			UEdGraphNode* FromNode = FromPin->GetOwningNode();
			check(FromNode != nullptr);
			float const FromNodeX = FromNode->NodePosX;

			static const float MinNodeDistance = 60.f; // min distance between spawned nodes (to keep them from overlapping)
			if (MinNodeDistance > FMath::Abs(FromNodeX - Location.X))
			{
				ModifiedLocation.X = FromNodeX - MinNodeDistance;
			}
		}
	}

	// this could return an existing node
	UEdGraphNode* SpawnedNode = Action->Invoke(ParentGraph, Bindings, Location);
	
	// if a returned node hasn't been added to the graph yet (it must have been freshly spawned)
	if (ParentGraph->Nodes.Find(SpawnedNode) == INDEX_NONE)
	{
		check(SpawnedNode != nullptr);

		// @TODO: Move Modify()/AddNode() into UBlueprintNodeSpawner and pull selection functionality out here
		ParentGraph->Modify();
		ParentGraph->AddNode(SpawnedNode, /*bFromUI =*/true, bSelectNewNode);
		// @TODO: if this spawned multiple nodes, then we should be selecting all of them

		SpawnedNode->SnapToGrid(SNodePanel::GetSnapGridSize());
		
		if (FromPin != nullptr)
		{
			// modify before the call to AutowireNewNode() below
			FromPin->Modify();
			// make sure to auto-wire after we position the new node (in case
			// the auto-wire creates a conversion node to put between them)
			SpawnedNode->AutowireNewNode(FromPin);
		}
		
		FBlueprintEditorUtils::AnalyticsTrackNewNode(SpawnedNode);
		DirtyBlueprintFromNewNode(SpawnedNode);
	}
	// if this node already existed, then we just want to focus on that node...
	// some node types are only allowed one instance per blueprint (like events)
	else if (SpawnedNode != nullptr)
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(SpawnedNode);
		if (FromPin != nullptr)
		{
			SpawnedNode->AutowireNewNode(FromPin);
		}
	}
	
	return SpawnedNode;
}

//------------------------------------------------------------------------------
UEdGraphNode* FBlueprintActionMenuItem::PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, FVector2D const Location, bool bSelectNewNode/* = true*/)
{
	UEdGraphPin* FromPin = nullptr;
	if (FromPins.Num() > 0)
	{
		FromPin = FromPins[0];
	}
	
	UEdGraphNode* SpawnedNode = PerformAction(ParentGraph, FromPin, Location, bSelectNewNode);
	// try auto-wiring the rest of the pins (if there are any)
	for (int32 PinIndex = 1; PinIndex < FromPins.Num(); ++PinIndex)
	{
		SpawnedNode->AutowireNewNode(FromPins[PinIndex]);
	}
	
	return SpawnedNode;
}

//------------------------------------------------------------------------------
void FBlueprintActionMenuItem::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEdGraphSchemaAction::AddReferencedObjects(Collector);
	
	// these don't get saved to disk, but we want to make sure the objects don't
	// get GC'd while the action array is around
	Collector.AddReferencedObject(Action);
}

//------------------------------------------------------------------------------
FSlateBrush const* FBlueprintActionMenuItem::GetMenuIcon(FSlateColor& ColorOut)
{
	ColorOut = IconTint;
	return IconBrush;
}

//------------------------------------------------------------------------------
UBlueprintNodeSpawner const* FBlueprintActionMenuItem::GetRawAction() const
{
	return Action;
}

#undef LOCTEXT_NAMESPACE