// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine.h"
#include "ListenerManager.h"
#include "Editor/UnrealEd/Classes/UserDefinedStructure/UserDefinedStructEditorData.h"

class UNREALED_API FStructureEditorUtils
{
public:

	class FStructEditorManager : public FListenerManager<UUserDefinedStruct>
	{
		FStructEditorManager();
	public:
		UNREALED_API static FStructEditorManager& Get();
	};

	typedef FStructEditorManager::ListenerType INotifyOnStructChanged;

	template<class TElement>
	struct FFindByNameHelper
	{
		const FName Name;

		FFindByNameHelper(FName InName) : Name(InName) { }

		bool operator() (const TElement& Element) const
		{
			return (Name == Element.VarName);
		}
	};

	template<class TElement>
	struct FFindByGuidHelper
	{
		const FGuid Guid;

		FFindByGuidHelper(FGuid InGuid) : Guid(InGuid) { }

		bool operator() (const TElement& Element) const
		{
			return (Guid == Element.VarGuid);
		}
	};

	//STRUCTURE
	static UUserDefinedStruct* CreateUserDefinedStruct(UObject* InParent, FName Name, EObjectFlags Flags);

	static void CompileStructure(UUserDefinedStruct* Struct);

	static FString GetTooltip(const UUserDefinedStruct* Struct);

	static bool ChangeTooltip(UUserDefinedStruct* Struct, const FString& InTooltip);

	//VARIABLE
	static bool AddVariable(UUserDefinedStruct* Struct, const FEdGraphPinType& VarType);

	static bool RemoveVariable(UUserDefinedStruct* Struct, FGuid VarGuid);

	static bool RenameVariable(UUserDefinedStruct* Struct, FGuid VarGuid, const FString& NewDisplayNameStr);

	static bool ChangeVariableType(UUserDefinedStruct* Struct, FGuid VarGuid, const FEdGraphPinType& NewType);

	static bool ChangeVariableDefaultValue(UUserDefinedStruct* Struct, FGuid VarGuid, const FString& NewDefaultValue);

	static bool IsUniqueVariableDisplayName(const UUserDefinedStruct* Struct, const FString& DisplayName);

	static FString GetVariableDisplayName(const UUserDefinedStruct* Struct, FGuid VarGuid);

	static FString GetVariableTooltip(const UUserDefinedStruct* Struct, FGuid VarGuid);
	
	static bool ChangeVariableTooltip(UUserDefinedStruct* Struct, FGuid VarGuid, const FString& InTooltip);

	static bool ChangeEditableOnBPInstance(UUserDefinedStruct* Struct, FGuid VarGuid, bool bInIsEditable);

	//3D Widget
	static bool CanEnable3dWidget(const UUserDefinedStruct* Struct, FGuid VarGuid);

	static bool Change3dWidgetEnabled(UUserDefinedStruct* Struct, FGuid VarGuid, bool bIsEnabled);

	static bool Is3dWidgetEnabled(const UUserDefinedStruct* Struct, FGuid VarGuid);

	//MISC
	static TArray<FStructVariableDescription>& GetVarDesc(UUserDefinedStruct* Struct);

	static const TArray<FStructVariableDescription>& GetVarDesc(const UUserDefinedStruct* Struct);

	static FStructVariableDescription* GetVarDescByGuid(UUserDefinedStruct* Struct, FGuid VarGuid)
	{
		return Struct ? GetVarDesc(Struct).FindByPredicate(FFindByGuidHelper<FStructVariableDescription>(VarGuid)) : NULL;
	}

	static const FStructVariableDescription* GetVarDescByGuid(const UUserDefinedStruct* Struct, FGuid VarGuid)
	{
		return Struct ? GetVarDesc(Struct).FindByPredicate(FFindByGuidHelper<FStructVariableDescription>(VarGuid)) : NULL;
	}

	static void ModifyStructData(UUserDefinedStruct* Struct);

	static bool UserDefinedStructEnabled();

	static void RemoveInvalidStructureMemberVariableFromBlueprint(UBlueprint* Blueprint);

	/*
	 Default values for member variables in User Defined Structure are stored in meta data "MakeStructureDefaultValue"
	 The following functions are used to fill an instance of user defined struct with those default values.
	 */
	static bool Fill_MakeStructureDefaultValue(const UUserDefinedStruct* Struct, uint8* StructData);

	static bool Fill_MakeStructureDefaultValue(const UProperty* Property, uint8* PropertyData);

	//VALIDATION
	static bool CanHaveAMemberVariableOfType(const UUserDefinedStruct* Struct, const FEdGraphPinType& VarType, FString* OutMsg = NULL);

	enum EStructureError
	{
		Ok, 
		Recursion,
		FallbackStruct,
		NotCompiled,
		NotBlueprintType,
		NotSupportedType,
		EmptyStructure
	};

	/** Can the structure be a member variable for a BPGClass or BPGStruct */
	static EStructureError IsStructureValid(const UScriptStruct* Struct, const UStruct* RecursionParent = NULL, FString* OutMsg = NULL);

	static void OnStructureChanged(UUserDefinedStruct* Struct);
};

