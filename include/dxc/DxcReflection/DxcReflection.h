///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// DxcReflection.h                                                         //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <assert.h>

#include "d3d12shader.h"
#include "dxc/dxctools.h"

namespace clang {
class TranslationUnitDecl;
class CompilerInstance;
}

#pragma warning(disable : 4201)

namespace hlsl {
	
enum class DxcHLSLNodeType : uint64_t {

  Register,
  Function,
  Enum,
  EnumValue,
  Namespace,
  Typedef,
  Using,
  Variable,         //localId points to the type for a variable
  //Parameter,

  Start = Register,
  End = Variable
};

struct DxcHLSLNode {

  uint32_t LocalIdParentLo;             //24 : 8
  uint32_t ParentHiAnnotationsType;     //16 : 10 : 6
  uint32_t ChildCountPad;               //24 : 8
  uint32_t AnnotationStartPad;          //20 : 12

  DxcHLSLNode() = default;

  DxcHLSLNode(DxcHLSLNodeType NodeType, uint32_t LocalId,
              uint32_t AnnotationStart, uint32_t ChildCount, uint32_t ParentId,
              uint16_t AnnotationCount)
      : LocalIdParentLo(LocalId | (ParentId << 24)),
        ChildCountPad(ChildCount),
        AnnotationStartPad(AnnotationStart),
        ParentHiAnnotationsType(
            (uint32_t(NodeType) << 26) |
            (uint32_t(AnnotationCount) << 16) |
            (ParentId >> 8)) {

    assert(NodeType >= DxcHLSLNodeType::Start &&
           NodeType <= DxcHLSLNodeType::End && "Invalid enum value");

    assert(LocalId < ((1 << 24) - 1) && "LocalId out of bounds");
    assert(ParentId < ((1 << 24) - 1) && "ParentId out of bounds");
    assert(ChildCount < ((1 << 24) - 1) && "ChildCount out of bounds");
    assert(AnnotationCount < (1 << 10) && "AnnotationCount out of bounds");

    assert(AnnotationStart < ((1 << 20) - 1) &&
           "AnnotationStart out of bounds");
  }

  // For example if Enum, maps into Enums[LocalId]
  uint32_t GetLocalId() const { return LocalIdParentLo << 8 >> 8; }
  uint32_t GetAnnotationStart() const { return AnnotationStartPad; }

  DxcHLSLNodeType GetNodeType() const {
    return DxcHLSLNodeType(ParentHiAnnotationsType >> 26);
  }

  // Includes recursive children
  uint32_t GetChildCount() const { return ChildCountPad; }

  uint32_t GetAnnotationCount() const {
    return uint32_t(ParentHiAnnotationsType << 6 >> (32 - 10));
  }

  uint32_t GetParentId() const {
    return uint32_t(LocalIdParentLo >> 24) | uint32_t(ParentHiAnnotationsType << 16 >> 8);
  }

  void IncreaseChildCount() {
    assert(ChildCountPad < ((1 << 24) - 1) && "Child count out of bounds");
    ++ChildCountPad;
  }

  bool operator==(const DxcHLSLNode &other) const {
    return LocalIdParentLo == other.LocalIdParentLo &&
           ParentHiAnnotationsType == other.ParentHiAnnotationsType &&
           ChildCountPad == other.ChildCountPad;
  }
};

struct DxcHLSLNodeSymbol {

  union {
    struct {
      uint32_t NameId; // Local name (not including parent's name)

      uint16_t FileNameId;          //-1 == no file info
      uint16_t SourceLineCount;
    };
    uint64_t NameIdFileNameIdSourceLineCount;
  };

  union {
    struct {
      uint16_t SourceColumnStartLo;
      uint16_t SourceColumnEndLo;
      uint32_t ColumnHiSourceLinePad; // 2 : 20 : 10
    };
    uint64_t SourceColumnStartEndLo;
  };

  DxcHLSLNodeSymbol() = default;

  DxcHLSLNodeSymbol(uint32_t NameId, uint16_t FileNameId,
                    uint16_t SourceLineCount, uint32_t SourceLineStart,
                    uint32_t SourceColumnStart, uint32_t SourceColumnEnd)
      : NameId(NameId), FileNameId(FileNameId),
        SourceLineCount(SourceLineCount),
        SourceColumnStartLo(uint16_t(SourceColumnStart)),
        SourceColumnEndLo(uint16_t(SourceColumnEnd)),
        ColumnHiSourceLinePad((SourceColumnStart >> 16) |
                              (SourceColumnEnd >> 16 << 1) |
                              (SourceLineStart << 2)) {

    assert(SourceColumnStart < (1 << 17) && "SourceColumnStart out of bounds");
    assert(SourceColumnEnd < (1 << 17) && "SourceColumnEnd out of bounds");

    assert(SourceLineStart < ((1 << 20) - 1) &&
           "SourceLineStart out of bounds");
  }

  uint32_t GetSourceLineStart() const {
    return uint32_t(ColumnHiSourceLinePad >> 2);
  }

  uint32_t GetSourceColumnStart() const {
    return SourceColumnStartLo | ((ColumnHiSourceLinePad & 1) << 16);
  }

  uint32_t GetSourceColumnEnd() const {
    return SourceColumnEndLo | ((ColumnHiSourceLinePad & 2) << 15);
  }

  bool operator==(const DxcHLSLNodeSymbol &other) const {
    return NameIdFileNameIdSourceLineCount ==
               other.NameIdFileNameIdSourceLineCount &&
           SourceColumnStartEndLo == other.SourceColumnStartEndLo;
  }
};

struct DxcHLSLEnumDesc {

  uint32_t NodeId;
  D3D12_HLSL_ENUM_TYPE Type;

  bool operator==(const DxcHLSLEnumDesc &other) const {
    return NodeId == other.NodeId && Type == other.Type;
  }
};

struct DxcHLSLEnumValue {

  int64_t Value;
  uint32_t NodeId;

  bool operator==(const DxcHLSLEnumValue &other) const {
    return Value == other.Value &&
           NodeId == other.NodeId;
  }
};

/*
struct DxcHLSLParameter { // Mirrors D3D12_PARAMETER_DESC (ex.
                          // First(In/Out)(Register/Component)), but with
                          // std::string and NodeId

  std::string SemanticName;
  D3D_SHADER_VARIABLE_TYPE Type;            // Element type.
  D3D_SHADER_VARIABLE_CLASS Class;          // Scalar/Vector/Matrix.
  uint32_t Rows;                            // Rows are for matrix parameters.
  uint32_t Columns;                         // Components or Columns in matrix.
  D3D_INTERPOLATION_MODE InterpolationMode; // Interpolation mode.
  D3D_PARAMETER_FLAGS Flags;                // Parameter modifiers.
  uint32_t NodeId;

  // TODO: Array info
};*/

struct DxcHLSLFunction {

  uint32_t NodeId;
  uint32_t NumParametersHasReturnAndDefinition;

  DxcHLSLFunction() = default;

  DxcHLSLFunction(uint32_t NodeId, uint32_t NumParameters, bool HasReturn,
                  bool HasDefinition)
      : NodeId(NodeId),
        NumParametersHasReturnAndDefinition(NumParameters |
                                            (HasReturn ? (1 << 30) : 0) |
                                            (HasDefinition ? (1 << 31) : 0)) {

    assert(NumParameters < (1 << 30) && "NumParameters out of bounds");
  }

  uint32_t GetNumParameters() const {
    return NumParametersHasReturnAndDefinition << 2 >> 2;
  }

  bool HasReturn() const {
    return (NumParametersHasReturnAndDefinition >> 30) & 1;
  }

  bool HasDefinition() const {
    return (NumParametersHasReturnAndDefinition >> 31) & 1;
  }

  bool operator==(const DxcHLSLFunction &other) const {
    return NodeId == other.NodeId &&
           NumParametersHasReturnAndDefinition ==
               other.NumParametersHasReturnAndDefinition;
  }
};

struct DxcHLSLRegister { // Almost maps to D3D12_SHADER_INPUT_BIND_DESC, minus
                         // the Name (and uID replaced with NodeID) and added
                         // arrayIndex and better packing

  union {
    struct {
      uint8_t Type;       // D3D_SHADER_INPUT_TYPE
      uint8_t Dimension;  // D3D_SRV_DIMENSION
      uint8_t ReturnType; // D3D_RESOURCE_RETURN_TYPE
      uint8_t uFlags;

      uint32_t BindPoint;
    };
    uint64_t TypeDimensionReturnTypeFlagsBindPoint;
  };

  union {
    struct {
      uint32_t Space;
      uint32_t BindCount;
    };
    uint64_t SpaceBindCount;
  };

  union {
    struct {
      uint32_t NumSamples;
      uint32_t NodeId;
    };
    uint64_t NumSamplesNodeId;
  };

  union {
    struct {
      uint32_t ArrayId;  // Only if BindCount > 1 and the array is 2D+ (else -1)
      uint32_t BufferId; // If cbuffer or structured buffer
    };
    uint64_t ArrayIdBufferId;
  };

  DxcHLSLRegister() = default;
  DxcHLSLRegister(D3D_SHADER_INPUT_TYPE Type, uint32_t BindPoint,
                  uint32_t BindCount, uint32_t uFlags,
                  D3D_RESOURCE_RETURN_TYPE ReturnType,
                  D3D_SRV_DIMENSION Dimension, uint32_t NumSamples,
                  uint32_t Space, uint32_t NodeId, uint32_t ArrayId,
                  uint32_t BufferId)
      : Type(Type), BindPoint(BindPoint), BindCount(BindCount), uFlags(uFlags),
        ReturnType(ReturnType), Dimension(Dimension),
        NumSamples(NumSamples), Space(Space), NodeId(NodeId),
        ArrayId(ArrayId), BufferId(BufferId) {

    assert(Type >= D3D_SIT_CBUFFER && Type <= D3D_SIT_UAV_FEEDBACKTEXTURE &&
           "Invalid type");

    assert(ReturnType >= 0 && ReturnType <= D3D_RETURN_TYPE_CONTINUED &&
           "Invalid return type");

    assert(Dimension >= D3D_SRV_DIMENSION_UNKNOWN &&
           Dimension <= D3D_SRV_DIMENSION_BUFFEREX && "Invalid srv dimension");

    assert(!(uFlags >> 8) && "Invalid user flags");
  }

  bool operator==(const DxcHLSLRegister &other) const {
    return TypeDimensionReturnTypeFlagsBindPoint ==
               other.TypeDimensionReturnTypeFlagsBindPoint &&
           SpaceBindCount == other.SpaceBindCount &&
           NumSamplesNodeId == other.NumSamplesNodeId &&
           ArrayIdBufferId == other.ArrayIdBufferId;
  }
};

struct DxcHLSLArray {

  uint32_t ArrayElemStart;

  DxcHLSLArray() = default;
  DxcHLSLArray(uint32_t ArrayElem, uint32_t ArrayStart)
      : ArrayElemStart((ArrayElem << 28) | ArrayStart) {

    assert(ArrayElem <= 8 && ArrayElem > 1 && "ArrayElem out of bounds");
    assert(ArrayStart < (1 << 28) && "ArrayStart out of bounds");
  }

  bool operator==(const DxcHLSLArray &Other) const {
    return Other.ArrayElemStart == ArrayElemStart;
  }

  uint32_t ArrayElem() const { return ArrayElemStart >> 28; }
  uint32_t ArrayStart() const { return ArrayElemStart << 4 >> 4; }
};

using DxcHLSLMember = uint32_t;     //typeId

struct DxcHLSLType { // Almost maps to CShaderReflectionType and
                     // D3D12_SHADER_TYPE_DESC, but tightly packed and
                     // easily serializable
  union {
    struct {
      uint32_t MemberData; //24 : 8 (start, count)
      uint8_t Class;   // D3D_SHADER_VARIABLE_CLASS
      uint8_t Type;    // D3D_SHADER_VARIABLE_TYPE
      uint8_t Rows;
      uint8_t Columns;
    };
    uint64_t MemberDataClassTypeRowsColums;
  };

  union {
    struct {
      uint32_t ElementsOrArrayId;
      uint32_t BaseClass; // -1 if none, otherwise a type index
    };
    uint64_t ElementsOrArrayIdBaseClass;
  };

  bool operator==(const DxcHLSLType &Other) const {
    return Other.MemberDataClassTypeRowsColums ==
               MemberDataClassTypeRowsColums &&
           ElementsOrArrayIdBaseClass == Other.ElementsOrArrayIdBaseClass;
  }

  uint32_t GetMemberCount() const { return MemberData >> 24; }
  uint32_t GetMemberStart() const { return MemberData << 8 >> 8; }

  bool IsMultiDimensionalArray() const { return ElementsOrArrayId >> 31; }
  bool IsArray() const { return ElementsOrArrayId; }
  bool Is1DArray() const { return IsArray() && !IsMultiDimensionalArray(); }

  uint32_t Get1DElements() const {
    return IsMultiDimensionalArray() ? 0 : ElementsOrArrayId;
  }

  uint32_t GetMultiDimensionalArrayId() const {
    return IsMultiDimensionalArray() ? (ElementsOrArrayId << 1 >> 1)
                                     : (uint32_t)-1;
  }

  DxcHLSLType() = default;
  DxcHLSLType(uint32_t BaseClass, uint32_t ElementsOrArrayId,
              D3D_SHADER_VARIABLE_CLASS Class, D3D_SHADER_VARIABLE_TYPE Type,
              uint8_t Rows, uint8_t Columns, uint32_t MembersCount,
              uint32_t MembersStart)
      : MemberData(MembersStart | (MembersCount << 24)),
        Class(Class), Type(Type), Rows(Rows), Columns(Columns),
        ElementsOrArrayId(ElementsOrArrayId), BaseClass(BaseClass) {

    assert(Class >= D3D_SVC_SCALAR && Class <= D3D_SVC_INTERFACE_POINTER &&
           "Invalid class");
    assert(Type >= D3D_SVT_VOID && Type <= D3D_SVT_UINT64 && "Invalid type");
    assert(MembersStart < (1 << 24) && "Member start out of bounds");
    assert(MembersCount < (1 << 8) && "Member count out of bounds");
  }
};

struct DxcHLSLBuffer { // Almost maps to CShaderReflectionConstantBuffer and
                       // D3D12_SHADER_BUFFER_DESC

  D3D_CBUFFER_TYPE Type;
  uint32_t NodeId;

  bool operator==(const DxcHLSLBuffer &other) const {
    return Type == other.Type && NodeId == other.NodeId;
  }
};

struct DxcHLSLAnnotation {

  uint32_t StringNonDebugAndIsBuiltin;

  DxcHLSLAnnotation() = default;

  DxcHLSLAnnotation(uint32_t StringNonDebug, bool IsBuiltin)
      : StringNonDebugAndIsBuiltin(StringNonDebug |
                                   (IsBuiltin ? (1u << 31) : 0)) {
    assert(StringNonDebug < (1u << 31) && "String non debug out of bounds");
  }

  bool operator==(const DxcHLSLAnnotation &other) const {
    return StringNonDebugAndIsBuiltin == other.StringNonDebugAndIsBuiltin;
  }

  bool GetIsBuiltin() const { return StringNonDebugAndIsBuiltin >> 31; }

  uint32_t GetStringNonDebug() const {
    return StringNonDebugAndIsBuiltin << 1 >> 1;
  }
};

struct DxcHLSLReflection {

  D3D12_HLSL_REFLECTION_FEATURE Features;

  std::vector<std::string> Strings;
  std::unordered_map<std::string, uint32_t> StringsToId;

  std::vector<std::string> StringsNonDebug;
  std::unordered_map<std::string, uint32_t> StringsToIdNonDebug;

  std::vector<uint32_t> Sources;
  std::unordered_map<std::string, uint16_t> StringToSourceId;

  std::vector<DxcHLSLNode> Nodes; // 0 = Root node (global scope)

  std::vector<DxcHLSLRegister> Registers;
  std::vector<DxcHLSLFunction> Functions;

  std::vector<DxcHLSLEnumDesc> Enums;
  std::vector<DxcHLSLEnumValue> EnumValues;

  // std::vector<DxcHLSLParameter> Parameters;
  std::vector<DxcHLSLAnnotation> Annotations;

  std::vector<DxcHLSLArray> Arrays;
  std::vector<uint32_t> ArraySizes;

  std::vector<DxcHLSLMember> MemberTypeIds;
  std::vector<DxcHLSLType> Types;
  std::vector<DxcHLSLBuffer> Buffers;

  // Can be stripped if !(D3D12_HLSL_REFLECTION_FEATURE_SYMBOL_INFO)

  std::vector<DxcHLSLNodeSymbol> NodeSymbols;
  std::vector<uint32_t> MemberNameIds;
  std::vector<uint32_t> TypeNameIds;

  // Only generated if deserialized with MakeNameLookupTable or
  // GenerateNameLookupTable is called (and if symbols aren't stripped)

  std::unordered_map<std::string, uint32_t> FullyResolvedToNodeId;
  std::vector<std::string> NodeIdToFullyResolved;
  std::unordered_map<std::string, uint32_t> FullyResolvedToMemberId;

  void Dump(std::vector<std::byte> &Bytes) const;
  void Printf() const;
  void StripSymbols();
  bool GenerateNameLookupTable();

  DxcHLSLReflection() = default;
  DxcHLSLReflection(const std::vector<std::byte> &Bytes,
                    bool MakeNameLookupTable);

  DxcHLSLReflection(clang::CompilerInstance &Compiler,
                    clang::TranslationUnitDecl &Ctx,
                    uint32_t AutoBindingSpace,
                    D3D12_HLSL_REFLECTION_FEATURE Features, bool DefaultRowMaj);

  bool IsSameNonDebug(const DxcHLSLReflection &other) const {
    return StringsNonDebug == other.StringsNonDebug && Nodes == other.Nodes &&
           Registers == other.Registers && Functions == other.Functions &&
           Enums == other.Enums && EnumValues == other.EnumValues &&
           Annotations == other.Annotations && Arrays == other.Arrays &&
           ArraySizes == other.ArraySizes &&
           MemberTypeIds == other.MemberTypeIds && Types == other.Types &&
           Buffers == other.Buffers;
  }

  bool operator==(const DxcHLSLReflection &other) const {
    return IsSameNonDebug(other) && Strings == other.Strings &&
           Sources == other.Sources && NodeSymbols == other.NodeSymbols &&
           MemberNameIds == other.MemberNameIds &&
           TypeNameIds == other.TypeNameIds;
  }
};

} // namespace hlsl

#pragma warning(default : 4201)
