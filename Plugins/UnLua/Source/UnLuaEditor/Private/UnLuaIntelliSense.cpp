// Tencent is pleased to support the open source community by making UnLua available.
// 
// Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the MIT License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at
//
// http://opensource.org/licenses/MIT
//
// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#include "UnLuaIntelliSense.h"

#include "Binding.h"
#include "ObjectEditorUtils.h"
#include "UnLuaInterface.h"
//<--- modified by wangxu
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
//--->end

namespace UnLua
{
    namespace IntelliSense
    {
        //<--- modified by wangxu
        static FString AdditionalContent;
        TSet<FString> GenDelegates;
        //--->end
        static const FName NAME_ToolTip(TEXT("ToolTip")); // key of ToolTip meta data
        static const FName NAME_LatentInfo = TEXT("LatentInfo"); // tag of latent function
        static FString LuaKeywords[] = {TEXT("local"), TEXT("function"), TEXT("end")};

        static FString GetCommentBlock(const UField* Field)
        {
            const FString ToolTip = Field->GetMetaData(NAME_ToolTip);
            if (ToolTip.IsEmpty())
                return "";
            return "---" + EscapeComments(ToolTip, false) + "\r\n";
        }

        FString Get(const UBlueprint* Blueprint)
        {
            return Get(Blueprint->GeneratedClass);
        }

        FString Get(const UField* Field)
        {
            const UStruct* Struct = Cast<UStruct>(Field);
            if (Struct)
                return Get(Struct);

            const UEnum* Enum = Cast<UEnum>(Field);
            if (Enum)
                return Get(Enum);

            return "";
        }

        FString Get(const UEnum* Enum)
        {
            FString Ret = GetCommentBlock(Enum);

            FString TypeName = GetTypeName(Enum);
            Ret += FString::Printf(TEXT("---@class %s"), *TypeName);

            // fields
            const int32 Num = Enum->NumEnums();
            for (int32 i = 0; i < Num; ++i)
            {
                //<--- modified by wangxu
                FString EnumFiledName = Enum->GetNameStringByIndex(i);
                FString Description = Enum->GetToolTipTextByIndex(i).ToString();
                if (const auto UserDefinedEnum = Cast<UUserDefinedEnum>(Enum); UserDefinedEnum && i != Num - 1) {
                    EnumFiledName = UserDefinedEnum->GetDisplayNameTextByIndex(i).ToString();
                }
                Ret += FString::Printf(TEXT("\r\n---@field %s %s %s @[%d]%s"), TEXT("public"), *EnumFiledName, TEXT("integer"),i,*Description);
                //--->end
            }

            // declaration
            Ret += FString::Printf(TEXT("\r\nlocal %s = {}\r\n"), *EscapeSymbolName(TypeName));

            return Ret;
        }

        FString Get(const UStruct* Struct)
        {
            const UClass* Class = Cast<UClass>(Struct);
            if (Class)
                return Get(Class);

            const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Struct);
            if (ScriptStruct)
                return Get(ScriptStruct);

            const UFunction* Function = Cast<UFunction>(Struct);
            if (Function)
                return Get(Function);

            return "";
        }

        FString Get(const UScriptStruct* ScriptStruct)
        {
            //<--- modified by wangxu
            AdditionalContent.Empty();
            //--->end

            FString Ret = GetCommentBlock(ScriptStruct);

            FString TypeName = GetTypeName(ScriptStruct);
            Ret += "---@class " + TypeName;
            UStruct* SuperStruct = ScriptStruct->GetSuperStruct();
            if (SuperStruct)
                Ret += " : " + GetTypeName(SuperStruct);
            Ret += "\r\n";

            // fields
            for (TFieldIterator<FProperty> It(ScriptStruct, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated); It; ++It)
            {
                const FProperty* Property = *It;
                Ret += Get(Property) += "\r\n";
            }

            // declaration
            Ret += FString::Printf(TEXT("local %s = {}\r\n"), *EscapeSymbolName(TypeName));

            //<--- modified by wangxu
            if(!AdditionalContent.IsEmpty())
            {
                Ret += AdditionalContent;
                Ret += "\r\n";
            }
            //--->end

            return Ret;
        }

        FString Get(const UClass* Class)
        {
            //<--- modified by wangxu
            AdditionalContent.Empty();
            //--->end

            FString Ret = GetCommentBlock(Class);

            const FString TypeName = GetTypeName(Class);
            Ret += "---@class " + TypeName;
            UStruct* SuperStruct = Class->GetSuperStruct();
            if (SuperStruct)
                Ret += " : " + GetTypeName(SuperStruct);
            Ret += "\r\n";

            // fields
            for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated); It; ++It)
            {
                const FProperty* Property = *It;
                Ret += Get(Property) += "\r\n";
            }

            // declaration
            const auto EscapedClassName = EscapeSymbolName(TypeName);
            Ret += FString::Printf(TEXT("local %s = {}\r\n\r\n"), *EscapedClassName);

            TArray<FString> GenFunctionNames;

            // functions
            for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::ExcludeInterfaces); FunctionIt; ++FunctionIt)
            {
                const UFunction* Function = *FunctionIt;
                if (!IsValid(Function))
                    continue;
                if (FObjectEditorUtils::IsFunctionHiddenFromClass(Function, Class))
                    continue;
                Ret += Get(Function) + "\r\n";
                GenFunctionNames.Add(Function->GetName());
            }

            // interface functions
            for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::IncludeInterfaces); FunctionIt; ++FunctionIt)
            {
                const UFunction* Function = *FunctionIt;
                if (!Function->GetOwnerClass()->IsChildOf(UInterface::StaticClass()))
                    continue;
                if (!IsValid(Function))
                    continue;
                if (FObjectEditorUtils::IsFunctionHiddenFromClass(Function, Class))
                    continue;
                if (GenFunctionNames.Contains(Function->GetName()))
                    continue;
                Ret += Get(Function, EscapedClassName) + "\r\n";
            }

            //<--- modified by Yongquan Fan
            // exported functions
            const auto Exported = GetExportedReflectedClasses().FindRef(TypeName);
            if (Exported)
            {
                TArray<IExportedFunction*> ExportedFunctions;
                Exported->GetFunctions(ExportedFunctions);
                for (const auto Function : ExportedFunctions)
                    Function->GenerateIntelliSense(Ret);
            }
            //---> end
            //<--- modified by wangxu
            Ret += FString::Printf(TEXT(R"(
---@return UClass
function %s.StaticClass() end)"), *TypeName);

            if (Class == UWorld::StaticClass()) {
                Ret += FString::Printf(TEXT(R"(
---@generic T : AActor
---@param ActorClass UClass<T>
---@param Transform FTransform
---@param SpawnMethod ESpawnActorCollisionHandlingMethod
---@param OwnerActor AActor
---@param Instigator APawn
---@param BindTableName string
---@param BindTable table @Optional
---@param OverrideLevel ULevel @Optional
---@param ActorName string @Optional
function UWorld:SpawnActor(ActorClass, Transform, SpawnMethod, OwnerActor, Instigator, BindTableName, BindTable, OverrideLevel, ActorName) end

---@generic T : AActor
---@param ActorClass UClass<T>
---@param Transform FTransform
---@param BindTable table @Optional
---@param BindTableName string
---@param ActorSpawnParameters FActorSpawnParameters
---@return T
function UWorld:SpawnActorEx(ActorClass, Transform, BindTable, BindTableName, ActorSpawnParameters) end

---@class FActorSpawnParameters
---@field public Name string @A name to assign as the Name of the Actor being spawned. If no value is specified, the name of the spawned Actor will be automatically generated using the form [Class]_[Number].
---@field public Template AActor @An Actor to use as a template when spawning the new Actor. The spawned Actor will be initialized using the property values of the template Actor. If left NULL the class default object (CDO) will be used to initialize the spawned Actor.
---@field public Owner AActor @The Actor that spawned this Actor. (Can be left as NULL).
---@field public Instigator APawn @The APawn that is responsible for damage done by the spawned Actor. (Can be left as NULL).
---@field public OverrideLevel ULevel @The ULevel to spawn the Actor in, i.e. the Outer of the Actor. If left as NULL the Outer of the Owner is used. If the Owner is NULL the persistent level is used.
---@field public OverrideParentComponent UChildActorComponent @The parent component to set the Actor in.
---@field public SpawnCollisionHandlingOverride ESpawnActorCollisionHandlingMethod @Method for resolving collisions at the spawn point. Undefined means no override, use the actor's setting.
---@field public bNoFail boolean @Determines whether spawning will not fail if certain conditions are not met. If true, spawning will not fail because the class being spawned is `bStatic=true` or because the class of the template Actor is not the same as the class of the Actor being spawned.
---@field public bDeferConstruction boolean @Determines whether the construction script will be run. If true, the construction script will not be run on the spawned Actor. Only applicable if the Actor is being spawned from a Blueprint. 
---@field public bAllowDuringConstructionScript boolean @Determines whether or not the actor may be spawned when running a construction script. If true spawning will fail if a construction script is being run.
---@field public ObjectFlags EObjectFlags @Flags used to describe the spawned actor/object instance.
)"));
            }
            if (Class == UObject::StaticClass()) {
                Ret += FString::Printf(TEXT(R"(
---@param AssetPath string
---@return any
function UObject.Load(AssetPath) end

---@param Object UObject
---@return boolean
function UObject.IsValid(Object) end

---@return string
function UObject:GetName() end

---@return UObject
function UObject:GetOuter() end

---@return UClass
function UObject:GetClass() end

---@return UWorld
function UObject:GetWorld() end

---@param Class UClass
---@return boolean
function UObject:IsA(Class) end

---Force Release object from lua
function UObject:Release() end

)"));
            }
            if (Class == UClass::StaticClass()) {
                Ret += FString::Printf(TEXT(R"(
---Load a class object
---@param Path string @path to the class
---@return UClass
function UClass.Load(Path) end

---Test whether this class is a child of another class
---@param TargetClass UClass @target class
---@return boolean @true if this object is of the specified type.
function UClass:IsChildOf(TargetClass) end

---Get default object of a class.
---@return UObject @class default obejct
function UClass:GetDefaultObject() end

)"));
            }
            if(!AdditionalContent.IsEmpty())
            {
                Ret += "\r\n";
                Ret += AdditionalContent;
                Ret += "\r\n";
            }
            //--->end
            return Ret;
        }

        FString Get(const UFunction* Function, FString ForceClassName)
        {
            FString Ret = GetCommentBlock(Function);
            //<--- modified by wangxu
            FString Properties;
            struct FTempName
            {
                FString Type;
                FString Name;
            };
            TArray<FTempName> Returns;
            //--->end

            for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
            {
                const FProperty* Property = *It;
                if (Property->GetFName() == NAME_LatentInfo)
                    continue;

                FString TypeName = EscapeSymbolName(GetTypeName(Property));
                const FString& PropertyComment = Property->GetMetaData(NAME_ToolTip);
                FString ExtraDesc;

                if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
                {
                    Ret += FString::Printf(TEXT("---@return %s"), *TypeName); // return parameter
                }
                else
                {
                    FString PropertyName = EscapeSymbolName(*Property->GetName());
                    if (Properties.IsEmpty())
                        Properties = PropertyName;
                    else
                        Properties += FString::Printf(TEXT(", %s"), *PropertyName);

                    FName KeyName = FName(*FString::Printf(TEXT("CPP_Default_%s"), *Property->GetName()));
                    const FString& Value = Function->GetMetaData(KeyName);
                    if (!Value.IsEmpty())
                    {
                        ExtraDesc = TEXT("[opt]"); // default parameter
                    }
                    else if (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
                    {
                        ExtraDesc = TEXT("[out]"); // non-const reference
                        //<--- modified by wangxu
                        Returns.Emplace(FTempName{TypeName,PropertyName});
                        //--->end
                    }
                    Ret += FString::Printf(TEXT("---@param %s %s"), *PropertyName, *TypeName);
                }

                if (ExtraDesc.Len() > 0 || PropertyComment.Len() > 0)
                {
                    Ret += TEXT(" @");
                    if (ExtraDesc.Len() > 0)
                        Ret += FString::Printf(TEXT("%s "), *ExtraDesc);
                }

                Ret += TEXT("\r\n");
            }
            //<--- modified by wangxu
            if(!Ret.Contains("return") && Function->IsInBlueprint())
            {
                FString ReturnType;
                FString ReturnComment;
                for (auto R : Returns)
                {
                    if(ReturnType.IsEmpty())
                    {
                        ReturnType = TEXT("---@return ");
                        ReturnComment += TEXT(" @");
                    }else
                    {
                        ReturnType += TEXT(",");
                        ReturnComment += TEXT(",");
                    }
                    ReturnType += R.Type;
                    ReturnComment += R.Name;
                }
                if(!ReturnType.IsEmpty())
                {
                    Ret = Ret + ReturnType + ReturnComment;
                    Ret += TEXT("\r\n");
                }
            }
            //--->end

            const auto ClassName = ForceClassName.IsEmpty() ? EscapeSymbolName(GetTypeName(Function->GetOwnerClass())) : ForceClassName;
            const auto FunctionName = Function->GetName();
            const auto bIsStatic = Function->HasAnyFunctionFlags(FUNC_Static);
            if (IsValidFunctionName(FunctionName))
            {
                Ret += FString::Printf(TEXT("function %s%s%s(%s) end\r\n"), *ClassName, bIsStatic ? TEXT(".") : TEXT(":"), *FunctionName, *Properties);
            }
            else
            {
                const auto Self = bIsStatic ? TEXT("") : TEXT("self");
                const auto Comma = bIsStatic || Properties.IsEmpty() ? TEXT("") : TEXT(", ");
                Ret += FString::Printf(TEXT("%s[\"%s\"] = function(%s%s%s) end\r\n"), *ClassName, *FunctionName, Self, Comma, *Properties);
            }
            return Ret;
        }

        FString Get(const FProperty* Property)
        {
            FString Ret;

            const UStruct* Struct = Property->GetOwnerStruct();

            // access level
            FString AccessLevel;
            if (Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPublic))
                AccessLevel = TEXT("public");
            else if (Property->HasAllPropertyFlags(CPF_NativeAccessSpecifierProtected))
                AccessLevel = TEXT("protected");
            else if (Property->HasAllPropertyFlags(CPF_NativeAccessSpecifierPrivate))
                AccessLevel = TEXT("private");
            else
                AccessLevel = Struct->IsNative() ? "private" : "public";

            FString TypeName = IntelliSense::GetTypeName(Property);
            //<--- modified by wangxu
            FString PropertyName = Property->GetName();
            if(Cast<UUserDefinedStruct>(Struct) || Cast<UUserDefinedEnum>(Struct))
            {
                PropertyName = Property->GetDisplayNameText().ToString();
            }
            Ret += FString::Printf(TEXT("---@field %s %s %s"), *AccessLevel, *PropertyName, *TypeName);
            //--->end

            // comment
            const FString& ToolTip = Property->GetMetaData(NAME_ToolTip);
            if (!ToolTip.IsEmpty())
                Ret += " @" + EscapeComments(ToolTip, true);

            return Ret;
        }

        FString GetUE(const TArray<const UField*> AllTypes)
        {
            FString Content = "UE = {";

            for (const auto Type : AllTypes)
            {
                if (!Type->IsNative()) {
                    //<--- modified by wangxu
                    const UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(Type);
                    if (!Type->IsA<UUserDefinedEnum>() && !Type->IsA<UUserDefinedStruct>()) {
                        continue;
                    }
                    //--->end
                }

                const auto Name = GetTypeName(Type);
                Content += FString::Printf(TEXT("\r\n    ---@type %s\r\n"), *Name);
                Content += FString::Printf(TEXT("    %s = nil,\r\n"), *Name);
            }

            Content += "}\r\n";
            return Content;
        }

        FString GetTypeName(const UObject* Field)
        {
            if (!Field)
                return "";
            if (!Field->IsNative() && Field->GetName().EndsWith("_C"))
                return Field->GetName();
            const UStruct* Struct = Cast<UStruct>(Field);
            if (Struct)
                return Struct->GetPrefixCPP() + Struct->GetName();
            return Field->GetName();
        }

        FString GetTypeName(const FProperty* Property)
        {
            if (!Property)
                return "Unknown";

            //<--- modified by wangxu
            if (const auto ByteProperty= CastField<FByteProperty>(Property)) {
                if(const auto Enum = ByteProperty->GetIntPropertyEnum()) {
                    return Enum->GetName();
                }
                return "integer";
            }
            //--->end

            if (CastField<FInt8Property>(Property))
                return "integer";

            if (CastField<FInt16Property>(Property))
                return "integer";

            if (CastField<FIntProperty>(Property))
                return "integer";

            if (CastField<FInt64Property>(Property))
                return "integer";

            if (CastField<FUInt16Property>(Property))
                return "integer";

            if (CastField<FUInt32Property>(Property))
                return "integer";

            if (CastField<FUInt64Property>(Property))
                return "integer";

            if (CastField<FFloatProperty>(Property))
                return "number";

            if (CastField<FDoubleProperty>(Property))
                return "number";

            if (CastField<FEnumProperty>(Property))
                return ((FEnumProperty*)Property)->GetEnum()->GetName();

            if (CastField<FBoolProperty>(Property))
                return TEXT("boolean");

            if (CastField<FClassProperty>(Property))
            {
                const UClass* Class = ((FClassProperty*)Property)->MetaClass;
                //<--- modified by wangxu
                return FString::Printf(TEXT("TSubclassOf<%s%s>"), Class->IsNative() ? Class->GetPrefixCPP() : TEXT(""), *Class->GetName());
                //--->end
            }

            if (CastField<FSoftObjectProperty>(Property))
            {
                if (((FSoftObjectProperty*)Property)->PropertyClass->IsChildOf(UClass::StaticClass()))
                {
                    const UClass* Class = ((FSoftClassProperty*)Property)->MetaClass;
                    //<--- modified by wangxu
                    return FString::Printf(TEXT("TSoftClassPtr<%s%s>"), Class->IsNative() ? Class->GetPrefixCPP() : TEXT(""), *Class->GetName());
                    //--->end
                }
                const UClass* Class = ((FSoftObjectProperty*)Property)->PropertyClass;
                //<--- modified by wangxu
                return FString::Printf(TEXT("TSoftObjectPtr<%s%s>"), Class->IsNative() ? Class->GetPrefixCPP() : TEXT(""), *Class->GetName());
                //--->end
            }

            if (CastField<FObjectProperty>(Property))
            {
                const UClass* Class = ((FObjectProperty*)Property)->PropertyClass;
                if (Cast<UBlueprintGeneratedClass>(Class))
                {
                    return FString::Printf(TEXT("%s"), *Class->GetName());
                }
                //<--- modified by wangxu
                return FString::Printf(TEXT("%s%s"), Class->IsNative() ? Class->GetPrefixCPP() : TEXT(""), *Class->GetName());
                //--->end
            }

            if (CastField<FWeakObjectProperty>(Property))
            {
                const UClass* Class = ((FWeakObjectProperty*)Property)->PropertyClass;
                return FString::Printf(TEXT("TWeakObjectPtr<%s%s>"), Class->GetPrefixCPP(), *Class->GetName());
            }

            if (CastField<FLazyObjectProperty>(Property))
            {
                const UClass* Class = ((FLazyObjectProperty*)Property)->PropertyClass;
                return FString::Printf(TEXT("TLazyObjectPtr<%s%s>"), Class->GetPrefixCPP(), *Class->GetName());
            }

            if (CastField<FInterfaceProperty>(Property))
            {
                const UClass* Class = ((FInterfaceProperty*)Property)->InterfaceClass;
                return FString::Printf(TEXT("TScriptInterface<%s%s>"), Class->GetPrefixCPP(), *Class->GetName());
            }

            if (CastField<FNameProperty>(Property))
                return "string";

            if (CastField<FStrProperty>(Property))
                return "string";

            if (CastField<FTextProperty>(Property))
                return "string";

            if (CastField<FArrayProperty>(Property))
            {
                const FProperty* Inner = ((FArrayProperty*)Property)->Inner;
                return FString::Printf(TEXT("TArray<%s>"), *GetTypeName(Inner));
            }

            if (CastField<FMapProperty>(Property))
            {
                const FProperty* KeyProp = ((FMapProperty*)Property)->KeyProp;
                const FProperty* ValueProp = ((FMapProperty*)Property)->ValueProp;
                return FString::Printf(TEXT("TMap<%s, %s>"), *GetTypeName(KeyProp), *GetTypeName(ValueProp));
            }

            if (CastField<FSetProperty>(Property))
            {
                const FProperty* ElementProp = ((FSetProperty*)Property)->ElementProp;
                return FString::Printf(TEXT("TSet<%s>"), *GetTypeName(ElementProp));
            }

            if (CastField<FStructProperty>(Property))
                return ((FStructProperty*)Property)->Struct->GetStructCPPName();

            if (CastField<FDelegateProperty>(Property))
            {
                //<--- modified by wangxu
                const FDelegateProperty *TempDelegateProperty = CastField<FDelegateProperty>(Property);
                const UFunction* SignatureFunction = TempDelegateProperty->SignatureFunction;
                const FString ClassName = SignatureFunction->GetPrefixCPP() + SignatureFunction->GetName();
                if (!GenDelegates.Contains(ClassName))
                {
                    GenDelegates.Add(ClassName);
                    FString Parameter, ReturnType;
                    for ( Property = SignatureFunction->PropertyLink; Property; Property = Property->PropertyLinkNext)
                    {
                        FString TypeName = GetTypeName(Property);
                        if (Property->PropertyFlags & CPF_ReturnParm)
                        {
                            ReturnType = TEXT(" : ") + TypeName;
                            continue;
                        }
                        if (!Parameter.IsEmpty())
                        {
                            Parameter += TEXT(", ");
                        }
                        Parameter += Property->GetName() + TEXT(" : ") + TypeName;
                    }
                    const FString CallbackParameter = Parameter.IsEmpty() ? TEXT("") : (TEXT(", ") + Parameter);
                    AdditionalContent += FString::Printf(TEXT(R"(---@class %s
---@field Bind fun(self, SelfFunction : fun(self%s)%s)
---@field Unbind fun()
---@field Execute fun(%s) integer

)"), *ClassName, *CallbackParameter, *ReturnType, *Parameter);
                }
                //--->end
                return ClassName;
            }

            //<--- modified by wangxu
            if (CastField<FMulticastDelegateProperty>(Property))
            {
                const FMulticastDelegateProperty *TempMulticastDelegateProperty = CastField<FMulticastDelegateProperty>(Property);
                const UFunction* SignatureFunction = TempMulticastDelegateProperty->SignatureFunction;
                const FString ClassName = SignatureFunction->GetPrefixCPP() + SignatureFunction->GetName();
                if (!GenDelegates.Contains(ClassName))
                {
                    GenDelegates.Add(ClassName);
                    FString Parameter, ReturnType;
                    for ( Property = SignatureFunction->PropertyLink; Property; Property = Property->PropertyLinkNext)
                    {
                        FString TypeName = GetTypeName(Property);
                        if (Property->PropertyFlags & CPF_ReturnParm)
                        {
                            ReturnType = TEXT(" : ") + TypeName;
                            continue;
                        }
                        if (!Parameter.IsEmpty())
                        {
                            Parameter += TEXT(", ");
                        }
                        Parameter += Property->GetName() + TEXT(" : ") + TypeName;
                    }
                    const FString CallbackParameter = Parameter.IsEmpty() ? TEXT("") : (TEXT(", ") + Parameter);
                    AdditionalContent += FString::Printf(TEXT(R"(---@class %s
---@field Add fun(self, SelfFunction : fun(self%s)%s)
---@field Remove fun(self, SelfFunction : fun(self%s)%s)
---@field Clear fun()
---@field Broadcast fun(%s)
)"), *ClassName, *CallbackParameter, *ReturnType, *CallbackParameter, *ReturnType, *Parameter);
                }
                //--->end
                return ClassName;
            }
            //<--- modified by wangxu
            if (CastField<FFieldPathProperty>(Property)) {
                return TEXT("FFieldPath");
            }
            //--->end

            return "Unknown";
        }

        FString EscapeComments(const FString Comments, const bool bSingleLine)
        {
            if (Comments.IsEmpty())
                return Comments;

            auto Filter = [](const FString Prefix, const FString Line)
            {
                if (Line.StartsWith("@"))
                    return FString();
                return Prefix + Line;
            };

            TArray<FString> Lines;
            Comments.Replace(TEXT("@"), TEXT("@@")).ParseIntoArray(Lines, TEXT("\n"));

            FString Ret = Filter("", Lines[0]);
            if (bSingleLine)
            {
                for (int i = 1; i < Lines.Num(); i++)
                    Ret += Filter(" ", Lines[i]);
            }
            else
            {
                for (int i = 1; i < Lines.Num(); i++)
                    Ret += Filter("\r\n---", Lines[i]);
            }

            return Ret;
        }

        FString EscapeSymbolName(const FString InName)
        {
            FString Name = InName;

            // 和Lua关键字重名就加前缀
            for (const auto& Keyword : LuaKeywords)
            {
                if (Name.Equals(Keyword, ESearchCase::CaseSensitive))
                {
                    Name = TEXT("_") + Name;
                    break;
                }
            }

            // 替换掉除中文外的特殊字符
            for (int32 Index = 0; Index < Name.Len(); Index++)
            {
                const auto Char = Name[Index];
                if ((Char < '0' || Char > '9')
                    && (Char < 'a' || Char > 'z')
                    && (Char < 'A' || Char > 'Z')
                    && Char != '_'
                    && Char < 0x100)
                {
                    //<--- modified by wangxu
                    if (InName.Contains("TArray<")
                        || InName.Contains("TMap<")
                        || InName.Contains("TSubclassOf<")
                        || InName.Contains("TSoftClassPtr<")
                        || InName.Contains("TSoftObjectPtr<")
                        || InName.Contains("TWeakObjectPtr<")
                        || InName.Contains("TLazyObjectPtr<")
                        || InName.Contains("TScriptInterface<")
                        || InName.Contains("TSet<") ) {
                        if (Char == '<'
                            || Char == '>'
                            || Char == ' '
                            || Char == ',') {
                            continue;
                            }
                        }
                    //--->end
                    Name[Index] = TCHAR('_');
                }
            }

            // 数字开头就加前缀
            const auto PrefixChar = Name[0];
            if (PrefixChar >= '0' && PrefixChar <= '9')
            {
                Name = TEXT("_") + Name;
            }

            return Name;
        }

        bool IsValid(const UFunction* Function)
        {
            if (!Function)
                return false;

            if (Function->HasAnyFunctionFlags(FUNC_UbergraphFunction))
                return false;

            // 这个会导致USubsystemBlueprintLibrary.GetWorldSubsystem之类的被过滤掉
            // if (!UEdGraphSchema_K2::CanUserKismetCallFunction(Function))
            //     return false;

            const FString Name = Function->GetName();
            if (Name.IsEmpty())
                return false;

            // 避免运行时生成智能提示，把覆写的函数也生成了
            if (Name.EndsWith("__Overridden"))
                return false;

            return true;
        }

        bool IsValidFunctionName(const FString Name)
        {
            if (Name.IsEmpty())
                return false;

            // 不能有Lua关键字
            for (const auto& Keyword : LuaKeywords)
            {
                if (Name.Equals(Keyword, ESearchCase::CaseSensitive))
                    return false;
            }

            // 不能有中文和特殊字符
            for (const auto& Char : Name)
            {
                if ((Char < '0' || Char > '9')
                    && (Char < 'a' || Char > 'z')
                    && (Char < 'A' || Char > 'Z')
                    && Char != '_')
                    return false;
            }

            // 不能以数字开头
            const auto& PrefixChar = Name[0];
            if (PrefixChar >= '0' && PrefixChar <= '9')
                return false;

            return true;
        }
    }
}
