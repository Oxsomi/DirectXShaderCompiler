///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// DxcReflection.cpp                                                         //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include "clang/AST/Attr.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/HlslTypes.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "dxc/DxcReflection/DxcReflection.h"

#undef min
#undef max

using namespace clang;

namespace hlsl {
	
struct DxcRegisterTypeInfo {
  D3D_SHADER_INPUT_TYPE RegisterType;
  D3D_SHADER_INPUT_FLAGS RegisterFlags;
  D3D_SRV_DIMENSION TextureDimension;
  D3D_RESOURCE_RETURN_TYPE TextureValue;
  uint32_t SampleCount;
};

static uint32_t RegisterString(DxcHLSLReflection &Refl,
    const std::string &Name, bool isNonDebug) {

  assert(Name.size() < 32768 && "Strings are limited to 32767");

  if (isNonDebug) {

    assert(Refl.StringsNonDebug.size() < (uint32_t)-1 && "Strings overflow");

    auto it = Refl.StringsToIdNonDebug.find(Name);

    if (it != Refl.StringsToIdNonDebug.end())
      return it->second;

    uint32_t stringId = (uint32_t)Refl.StringsNonDebug.size();

    Refl.StringsNonDebug.push_back(Name);
    Refl.StringsToIdNonDebug[Name] = stringId;
    return stringId;
  }

  assert(Refl.Strings.size() < (uint32_t)-1 && "Strings overflow");

  auto it = Refl.StringsToId.find(Name);

  if (it != Refl.StringsToId.end())
    return it->second;

  uint32_t stringId = (uint32_t) Refl.Strings.size();

  Refl.Strings.push_back(Name);
  Refl.StringsToId[Name] = stringId;
  return stringId;
}

static uint32_t PushNextNodeId(DxcHLSLReflection &Refl, const SourceManager &SM,
                               const LangOptions &LangOpts,
                               const std::string &UnqualifiedName, Decl *Decl,
                               DxcHLSLNodeType Type, uint32_t ParentNodeId,
                               uint32_t LocalId, const SourceRange *Range = nullptr) {

  assert(Refl.Nodes.size() < (uint32_t)(1 << 24) && "Nodes overflow");
  assert(LocalId < (uint32_t)(1 << 24) && "LocalId overflow");

  uint32_t nodeId = Refl.Nodes.size();

  uint32_t annotationStart = (uint32_t) Refl.Annotations.size();
  uint16_t annotationCount = 0;

  if (Decl) {
    for (const Attr *attr : Decl->attrs()) {
      if (const AnnotateAttr *annotate = dyn_cast<AnnotateAttr>(attr)) {

        assert(Refl.Annotations.size() < (1 << 20) && "Out of annotations");

        Refl.Annotations.push_back(DxcHLSLAnnotation(
            RegisterString(Refl, annotate->getAnnotation().str(), true),
            false));

        assert(annotationCount != uint16_t(-1) &&
               "Annotation count out of bounds");
        ++annotationCount;

      } else if (const HLSLShaderAttr *shaderAttr =
                     dyn_cast<HLSLShaderAttr>(attr)) {

        assert(Refl.Annotations.size() < (1 << 20) && "Out of annotations");

        Refl.Annotations.push_back(DxcHLSLAnnotation(
            RegisterString(
                Refl, "shader(\"" + shaderAttr->getStage().str() + "\")", true),
            true));

        assert(annotationCount != uint16_t(-1) &&
               "Annotation count out of bounds");
        ++annotationCount;
      }
    }
  }

  if (Refl.Features & D3D12_HLSL_REFLECTION_FEATURE_SYMBOL_INFO) {

    uint16_t sourceLineCount = 0;
    uint32_t sourceLineStart = 0;
    uint32_t sourceColumnStart = 0;
    uint32_t sourceColumnEnd = 0;

    uint16_t fileNameId = (uint16_t)-1;

    SourceRange range =
        Decl ? Decl->getSourceRange() : (Range ? *Range : SourceRange());

    SourceLocation start = range.getBegin();
    SourceLocation end = range.getEnd();

    if (start.isValid() && end.isValid()) {

      PresumedLoc presumed = SM.getPresumedLoc(start);

      SourceLocation realEnd = SM.getFileLoc(end);
      SourceLocation endOfToken =
          Lexer::getLocForEndOfToken(realEnd, 0, SM, LangOpts);
      PresumedLoc presumedEnd = SM.getPresumedLoc(endOfToken);

      if (presumed.isValid() && presumedEnd.isValid()) {

        uint32_t startLine = presumed.getLine();
        uint32_t startCol = presumed.getColumn();
        uint32_t endLine = presumedEnd.getLine();
        uint32_t endCol = presumedEnd.getColumn();

        std::string fileName = presumed.getFilename();

        assert(fileName == presumedEnd.getFilename() &&
               "End and start are not in the same file");

        auto it = Refl.StringToSourceId.find(fileName);
        uint32_t i;

        if (it == Refl.StringToSourceId.end()) {
          i = (uint32_t)Refl.Sources.size();
          Refl.Sources.push_back(RegisterString(Refl, fileName, false));
          Refl.StringToSourceId[fileName] = i;
        }

        else {
          i = it->second;
        }

        assert(i < 65535 && "Source file count is limited to 16-bit");
        assert((endLine - startLine) < 65535 &&
               "Source line count is limited to 16-bit");
        assert(startLine < 1048576 && "Source line start is limited to 20-bit");
        assert(startCol < 131072 && "Column start is limited to 17-bit");
        assert(endCol < 131072 && "Column end is limited to 17-bit");

        sourceLineCount = uint16_t(endLine - startLine + 1);
        sourceLineStart = startLine;
        sourceColumnStart = startCol;
        sourceColumnEnd = endCol;
        fileNameId = (uint16_t)i;
      }
    }

    uint32_t nameId = RegisterString(Refl, UnqualifiedName, false);

    Refl.NodeSymbols.push_back(
        DxcHLSLNodeSymbol(nameId, fileNameId, sourceLineCount, sourceLineStart,
                          sourceColumnStart, sourceColumnEnd));
  }

  Refl.Nodes.push_back(DxcHLSLNode{Type, LocalId, annotationStart, 0,
                                   ParentNodeId, annotationCount});

  uint32_t parentParent = ParentNodeId;

  while (parentParent != 0) {
    DxcHLSLNode &parent = Refl.Nodes[parentParent];
    parent.IncreaseChildCount();
    parentParent = parent.GetParentId();
  }

  Refl.Nodes[0].IncreaseChildCount();

  return nodeId;
}

static DxcRegisterTypeInfo GetTextureRegisterInfo(ASTContext &ASTCtx,
                                                  std::string TypeName,
                                                  bool IsWrite,
                                                  const CXXRecordDecl *RecordDecl) {
    
  DxcRegisterTypeInfo type = {};
  type.RegisterType = IsWrite ? D3D_SIT_UAV_RWTYPED : D3D_SIT_TEXTURE;
  type.SampleCount = (uint32_t)-1;

  //Parse return type and dimensions

  const ClassTemplateSpecializationDecl *textureTemplate =
      dyn_cast<ClassTemplateSpecializationDecl>(RecordDecl);

  assert(textureTemplate && "Expected texture template");

  const ArrayRef<TemplateArgument>& textureParams = textureTemplate->getTemplateArgs().asArray();

  assert(textureParams.size() == 1 &&
         textureParams[0].getKind() == TemplateArgument::Type &&
         "Expected template args");

  QualType valueType = textureParams[0].getAsType();
  QualType desugared = valueType.getDesugaredType(ASTCtx);

  uint32_t dimensions;

  if (const BuiltinType *bt = dyn_cast<BuiltinType>(desugared))
    dimensions = 1;

  else {

    const RecordType *RT = desugared->getAs<RecordType>();
    assert(RT && "Expected record type");

    const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(RT->getDecl());
    assert(RT && "Expected record decl");

    const ClassTemplateSpecializationDecl *vectorType =
        dyn_cast<ClassTemplateSpecializationDecl>(RD);

    assert(vectorType &&
           "Expected vector type as template inside of texture template");

    const ArrayRef<TemplateArgument> &vectorParams =
        vectorType->getTemplateArgs().asArray();

    assert(vectorParams.size() == 2 &&
           vectorParams[0].getKind() == TemplateArgument::Type &&
           vectorParams[1].getKind() == TemplateArgument::Integral &&
           "Expected vector to be vector<T, N>");

    valueType = vectorParams[0].getAsType();
    desugared = valueType.getDesugaredType(ASTCtx);

    dimensions = vectorParams[1].getAsIntegral().getZExtValue();
  }

  if (desugared->isFloatingType()) {
    type.TextureValue = desugared->isSpecificBuiltinType(BuiltinType::Double)
                            ? D3D_RETURN_TYPE_DOUBLE
                            : D3D_RETURN_TYPE_FLOAT;
  } else if (desugared->isIntegerType()) {
    const auto &semantics = ASTCtx.getTypeInfo(desugared);
    if (semantics.Width == 64) {
      type.TextureValue = D3D_RETURN_TYPE_MIXED;
    } else {
      type.TextureValue = desugared->isUnsignedIntegerType()
                              ? D3D_RETURN_TYPE_UINT
                              : D3D_RETURN_TYPE_SINT;
    }
  }

  else {
    type.TextureValue = D3D_RETURN_TYPE_MIXED;
  }

  switch (dimensions) {
  case 2:
    type.RegisterFlags = (D3D_SHADER_INPUT_FLAGS)D3D_SIF_TEXTURE_COMPONENT_0;
    break;
  case 3:
    type.RegisterFlags = (D3D_SHADER_INPUT_FLAGS)D3D_SIF_TEXTURE_COMPONENT_1;
    break;
  case 4:
    type.RegisterFlags = (D3D_SHADER_INPUT_FLAGS)D3D_SIF_TEXTURE_COMPONENTS;
    break;
  }

  //Parse type

  if (TypeName == "Buffer") {
    type.TextureDimension = D3D_SRV_DIMENSION_BUFFER;
    return type;
  }

  bool isFeedback = false;

  if (TypeName.size() > 8 && TypeName.substr(0, 8) == "Feedback") {
    isFeedback = true;
    TypeName = TypeName.substr(8);
    type.RegisterType = D3D_SIT_UAV_FEEDBACKTEXTURE;
  }

  bool isArray = false;

  if (TypeName.size() > 5 && TypeName.substr(TypeName.size() - 5) == "Array") {
    isArray = true;
    TypeName = TypeName.substr(0, TypeName.size() - 5);
  }

  if (TypeName == "Texture2D")
    type.TextureDimension = D3D_SRV_DIMENSION_TEXTURE2D;

  else if (TypeName == "TextureCube")
    type.TextureDimension = D3D_SRV_DIMENSION_TEXTURECUBE;

  else if (TypeName == "Texture3D")
    type.TextureDimension = D3D_SRV_DIMENSION_TEXTURE3D;

  else if (TypeName == "Texture1D")
    type.TextureDimension = D3D_SRV_DIMENSION_TEXTURE1D;

  else if (TypeName == "Texture2DMS") {
    type.TextureDimension = D3D_SRV_DIMENSION_TEXTURE2DMS;
    type.SampleCount = 0;
  }

  if (isArray)      //Arrays are always 1 behind the regular type
    type.TextureDimension = (D3D_SRV_DIMENSION)(type.TextureDimension + 1);

  return type;
}

static DxcRegisterTypeInfo GetRegisterTypeInfo(ASTContext &ASTCtx,
                                               QualType Type) {

  QualType realType = Type.getDesugaredType(ASTCtx);
  const RecordType *RT = realType->getAs<RecordType>();
  assert(RT && "GetRegisterTypeInfo() type is not a RecordType");

  const CXXRecordDecl *recordDecl = RT->getAsCXXRecordDecl();
  assert(recordDecl && "GetRegisterTypeInfo() type is not a CXXRecordDecl");

  std::string typeName = recordDecl->getNameAsString();

  if (typeName.size() >= 17 &&
      typeName.substr(0, 17) == "RasterizerOrdered") {
    typeName = typeName.substr(17);
  }

  if (typeName == "SamplerState" || typeName == "SamplerComparisonState") {
    return {D3D_SIT_SAMPLER, typeName == "SamplerComparisonState"
                                 ? D3D_SIF_COMPARISON_SAMPLER
                                 : (D3D_SHADER_INPUT_FLAGS)0};
  }
  
  DxcRegisterTypeInfo info = {};

  if (const ClassTemplateSpecializationDecl *spec =
          dyn_cast<ClassTemplateSpecializationDecl>(recordDecl)) {

    const ArrayRef<TemplateArgument> &array =
        spec->getTemplateArgs().asArray();

    if (array.size() == 1)
      info.SampleCount = (uint32_t) (ASTCtx.getTypeSize(array[0].getAsType()) / 8);
  }

  if (typeName == "AppendStructuredBuffer") {
    info.RegisterType = D3D_SIT_UAV_APPEND_STRUCTURED;
    return info;
  }

  if (typeName == "ConsumeStructuredBuffer") {
    info.RegisterType = D3D_SIT_UAV_CONSUME_STRUCTURED;
    return info;
  }

  if (typeName == "RaytracingAccelerationStructure") {
    info.RegisterType = D3D_SIT_RTACCELERATIONSTRUCTURE;
    info.SampleCount = (uint32_t)-1;
    return info;
  }

  if (typeName == "TextureBuffer") {
    info.RegisterType = D3D_SIT_TBUFFER;
    return info;
  }

  if (typeName == "ConstantBuffer") {
    info.RegisterType = D3D_SIT_CBUFFER;
    return info;
  }

  bool isWrite =
      typeName.size() > 2 && typeName[0] == 'R' && typeName[1] == 'W';

  if (isWrite)
    typeName = typeName.substr(2);

  if (typeName == "StructuredBuffer") {
    info.RegisterType =
        isWrite ? D3D_SIT_UAV_RWSTRUCTURED : D3D_SIT_STRUCTURED;
    return info;
  }

  if (typeName == "ByteAddressBuffer") {
    info.RegisterType =
        isWrite ? D3D_SIT_UAV_RWBYTEADDRESS : D3D_SIT_BYTEADDRESS;
    return info;
  }

  return GetTextureRegisterInfo(ASTCtx, typeName, isWrite, recordDecl);
}

static uint32_t PushArray(DxcHLSLReflection &Refl, uint32_t ArraySizeFlat,
                          const std::vector<uint32_t> &ArraySize) {

  if (ArraySizeFlat <= 1 || ArraySize.size() <= 1)
    return (uint32_t)-1;

  assert(Refl.Arrays.size() < (uint32_t)((1u << 31) - 1) && "Arrays would overflow");
  uint32_t arrayId = (uint32_t)Refl.Arrays.size();

  uint32_t arrayCountStart = (uint32_t)Refl.ArraySizes.size();
  uint32_t numArrayElements = std::min((size_t)8, ArraySize.size());
  assert(Refl.ArraySizes.size() + numArrayElements < ((1 << 28) - 1) &&
         "Array elements would overflow");

  for (uint32_t i = 0; i < ArraySize.size() && i < 8; ++i) {

    uint32_t arraySize = ArraySize[i];

    // Flatten rest of array to at least keep consistent array elements
    if (i == 7)
      for (uint32_t j = i + 1; j < ArraySize.size(); ++j)
        arraySize *= ArraySize[j];

    Refl.ArraySizes.push_back(arraySize);
  }

  DxcHLSLArray arr = {numArrayElements, arrayCountStart};

  for (uint32_t i = 0; i < Refl.Arrays.size(); ++i)
    if (Refl.Arrays[i] == arr)
      return i;

  Refl.Arrays.push_back(arr);
  return arrayId;
}

uint32_t GenerateTypeInfo(ASTContext &ASTCtx, DxcHLSLReflection &Refl,
                          QualType Original, bool DefaultRowMaj) {

  // Unwrap array

  uint32_t arraySize = 1;
  QualType underlying = Original, forName = Original;
  std::vector<uint32_t> arrayElem;

  while (const ConstantArrayType *arr =
             dyn_cast<ConstantArrayType>(underlying)) {
    uint32_t current = arr->getSize().getZExtValue();
    arrayElem.push_back(current);
    arraySize *= arr->getSize().getZExtValue();
    forName = arr->getElementType();
    underlying = forName.getCanonicalType();
  }

  underlying = underlying.getCanonicalType();

  // Name; Omit struct, class and const keywords

  PrintingPolicy policy(ASTCtx.getLangOpts());
  policy.SuppressScope = false;
  policy.AnonymousTagLocations = false;
  policy.SuppressTagKeyword = true; 

  bool hasSymbols = Refl.Features & D3D12_HLSL_REFLECTION_FEATURE_SYMBOL_INFO;
  uint32_t nameId =
      hasSymbols
          ? RegisterString(
                Refl, forName.getLocalUnqualifiedType().getAsString(policy),
                false)
          : uint32_t(-1);

  uint32_t arrayId = PushArray(Refl, arraySize, arrayElem);
  uint32_t elementsOrArrayId = 0;

  if (arrayId != (uint32_t)-1)
    elementsOrArrayId = (1u << 31) | arrayId;

  else
    elementsOrArrayId = arraySize > 1 ? arraySize : 0;

  //Unwrap vector and matrix
  //And base type

  D3D_SHADER_VARIABLE_CLASS cls = D3D_SVC_STRUCT;
  uint8_t rows = 0, columns = 0;

  uint32_t membersCount = 0;
  uint32_t membersOffset = 0;

  uint32_t baseType = uint32_t(-1);

  if (const RecordType *record = underlying->getAs<RecordType>()) {

     bool standardType = false;

    RecordDecl *recordDecl = record->getDecl();

    if (const ClassTemplateSpecializationDecl *templateClass =
            dyn_cast<ClassTemplateSpecializationDecl>(recordDecl)) {

      const std::string &name = templateClass->getIdentifier()->getName();

      const ArrayRef<TemplateArgument> &params =
          templateClass->getTemplateArgs().asArray();

      uint32_t magic = 0;
      std::memcpy(&magic, name.c_str(), std::min(sizeof(magic), name.size()));

      std::string_view subs =
          name.size() < sizeof(magic)
              ? std::string_view()
              : std::string_view(name).substr(sizeof(magic));

      switch (magic) {

      case DXC_FOURCC('v', 'e', 'c', 't'):

        if (subs == "or") {

          rows = 1;

          assert(params.size() == 2 &&
                 params[0].getKind() == TemplateArgument::Type &&
                 params[1].getKind() == TemplateArgument::Integral &&
                 "Expected vector to be vector<T, N>");

          underlying = params[0].getAsType();
          columns = params[1].getAsIntegral().getSExtValue();
          cls = D3D_SVC_VECTOR;
          standardType = true;
        }

        break;

      case DXC_FOURCC('m', 'a', 't', 'r'):

        if (subs == "ix") {

          assert(params.size() == 3 &&
                 params[0].getKind() == TemplateArgument::Type &&
                 params[1].getKind() == TemplateArgument::Integral &&
                 params[2].getKind() == TemplateArgument::Integral &&
                 "Expected matrix to be matrix<T, C, R>");

          underlying = params[0].getAsType();
          columns = params[1].getAsIntegral().getSExtValue();
          rows = params[2].getAsIntegral().getSExtValue();

          bool isRowMajor = DefaultRowMaj;

          HasHLSLMatOrientation(Original, &isRowMajor);

          if (!isRowMajor)
            std::swap(rows, columns);

          cls = isRowMajor ? D3D_SVC_MATRIX_ROWS : D3D_SVC_MATRIX_COLUMNS;
          standardType = true;
        }

        break;
      }

      // TODO:
      //           D3D_SVT_TEXTURE	= 5,
      //  D3D_SVT_TEXTURE1D	= 6,
      //  D3D_SVT_TEXTURE2D	= 7,
      //  D3D_SVT_TEXTURE3D	= 8,
      //  D3D_SVT_TEXTURECUBE	= 9,
      //  D3D_SVT_SAMPLER	= 10,
      //  D3D_SVT_SAMPLER1D	= 11,
      //  D3D_SVT_SAMPLER2D	= 12,
      //  D3D_SVT_SAMPLER3D	= 13,
      //  D3D_SVT_SAMPLERCUBE	= 14,
      //  D3D_SVT_BUFFER	= 25,
      //  D3D_SVT_CBUFFER	= 26,
      //  D3D_SVT_TBUFFER	= 27,
      //  D3D_SVT_TEXTURE1DARRAY	= 28,
      //  D3D_SVT_TEXTURE2DARRAY	= 29,
      //  D3D_SVT_TEXTURE2DMS	= 32,
      //  D3D_SVT_TEXTURE2DMSARRAY	= 33,
      //  D3D_SVT_TEXTURECUBEARRAY	= 34,
      //  D3D_SVT_RWTEXTURE1D	= 40,
      //  D3D_SVT_RWTEXTURE1DARRAY	= 41,
      //  D3D_SVT_RWTEXTURE2D	= 42,
      //  D3D_SVT_RWTEXTURE2DARRAY	= 43,
      //  D3D_SVT_RWTEXTURE3D	= 44,
      //  D3D_SVT_RWBUFFER	= 45,
      //  D3D_SVT_BYTEADDRESS_BUFFER	= 46,
      //  D3D_SVT_RWBYTEADDRESS_BUFFER	= 47,
      //  D3D_SVT_STRUCTURED_BUFFER	= 48,
      //  D3D_SVT_RWSTRUCTURED_BUFFER	= 49,
      //  D3D_SVT_APPEND_STRUCTURED_BUFFER	= 50,
      //  D3D_SVT_CONSUME_STRUCTURED_BUFFER	= 51,
    }

    // Fill members

    if (!standardType && recordDecl->isCompleteDefinition()) {

      //Base types

      if (CXXRecordDecl *cxxRecordDecl = dyn_cast<CXXRecordDecl>(recordDecl))
        if (cxxRecordDecl->getNumBases()) {
          for (auto &I : cxxRecordDecl->bases()) {

            QualType qualType = I.getType();
            CXXRecordDecl *BaseDecl =
                cast<CXXRecordDecl>(qualType->castAs<RecordType>()->getDecl());

            // TODO: Interfaces?
            if (BaseDecl->isInterface())
              continue;

            assert(baseType == uint32_t(-1) &&
                   "Multiple base types isn't supported in HLSL");

            baseType = GenerateTypeInfo(ASTCtx, Refl, qualType, DefaultRowMaj);
          }
        }

      //Inner types

      for (Decl *decl : recordDecl->decls()) {

        // TODO: We could query other types VarDecl

        FieldDecl *fieldDecl = dyn_cast<FieldDecl>(decl);

        if (!fieldDecl)
          continue;

        QualType original = fieldDecl->getType();
        std::string name = fieldDecl->getName();

        uint32_t nameId = hasSymbols ? RegisterString(Refl, name, false) : uint32_t(-1);
        uint32_t typeId =
            GenerateTypeInfo(ASTCtx, Refl, original, DefaultRowMaj);

        if (!membersCount)
          membersOffset = (uint32_t) Refl.MemberTypeIds.size();

        assert(Refl.MemberTypeIds.size() <= (uint32_t)-1 &&
               "Members out of bounds");

        Refl.MemberTypeIds.push_back(typeId);

        if (hasSymbols)
          Refl.MemberNameIds.push_back(nameId);

        ++membersCount;
      }
    }
  }

  //Type name

  D3D_SHADER_VARIABLE_TYPE type = D3D_SVT_VOID;

  if (const BuiltinType *bt = dyn_cast<BuiltinType>(underlying)) {

    if (!rows)
      rows = columns = 1;

    if (cls == D3D_SVC_STRUCT)
      cls = D3D_SVC_SCALAR;

    switch (bt->getKind()) {

    case BuiltinType::Void:
      type = D3D_SVT_VOID;
      break;

    case BuiltinType::Min10Float:
      type = D3D_SVT_MIN10FLOAT;
      break;

    case BuiltinType::Min16Float:
      type = D3D_SVT_MIN16FLOAT;
      break;

    case BuiltinType::HalfFloat:
    case BuiltinType::Half:
      type = D3D_SVT_FLOAT16;
      break;

    case BuiltinType::Short:
      type = D3D_SVT_INT16;
      break;

    case BuiltinType::Min12Int:
      type = D3D_SVT_MIN12INT;
      break;

    case BuiltinType::Min16Int:
      type = D3D_SVT_MIN16INT;
      break;

    case BuiltinType::Min16UInt:
      type = D3D_SVT_MIN16UINT;
      break;

    case BuiltinType::UShort:
      type = D3D_SVT_UINT16;
      break;

    case BuiltinType::Float:
      type = D3D_SVT_FLOAT;
      break;

    case BuiltinType::Int:
      type = D3D_SVT_INT;
      break;

    case BuiltinType::UInt:
      type = D3D_SVT_UINT;
      break;

    case BuiltinType::Bool:
      type = D3D_SVT_BOOL;
      break;

    case BuiltinType::Double:
      type = D3D_SVT_DOUBLE;
      break;

    case BuiltinType::ULongLong:
      type = D3D_SVT_UINT64;
      break;

    case BuiltinType::LongLong:
      type = D3D_SVT_INT64;
      break;

    default:
      assert(false && "Invalid builtin type");
      break;
    }
  }

  //Insert

  assert(Refl.Types.size() < (uint32_t)-1 && "Type id out of bounds");

  DxcHLSLType hlslType(baseType, elementsOrArrayId, cls, type, rows,
                   columns, membersCount, membersOffset);

  uint32_t i = 0, j = (uint32_t)Refl.Types.size();

  for (; i < j; ++i)
    if (Refl.Types[i] == hlslType)
      break;

  if (i == j) {

    if (hasSymbols)
      Refl.TypeNameIds.push_back(nameId);

    Refl.Types.push_back(hlslType);
  }

  return i;
}

static D3D_CBUFFER_TYPE GetBufferType(uint8_t Type) {

  switch (Type) {

  case D3D_SIT_CBUFFER:
    return D3D_CT_CBUFFER;

  case D3D_SIT_TBUFFER:
    return D3D_CT_TBUFFER;

  case D3D_SIT_STRUCTURED:
  case D3D_SIT_UAV_RWSTRUCTURED:
  case D3D_SIT_UAV_APPEND_STRUCTURED:
  case D3D_SIT_UAV_CONSUME_STRUCTURED:
  case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
    return D3D_CT_RESOURCE_BIND_INFO;

  default:
    return D3D_CT_INTERFACE_POINTERS;
  }
}

static void FillReflectionRegisterAt(
    const DeclContext &Ctx, ASTContext &ASTCtx, const SourceManager &SM,
    DiagnosticsEngine &Diag, QualType Type, uint32_t ArraySizeFlat,
    ValueDecl *ValDesc, const std::vector<uint32_t> &ArraySize,
    DxcHLSLReflection &Refl, uint32_t AutoBindingSpace, uint32_t ParentNodeId,
    bool DefaultRowMaj) {

  ArrayRef<hlsl::UnusualAnnotation *> UA = ValDesc->getUnusualAnnotations();

  hlsl::RegisterAssignment *reg = nullptr;

  for (auto It = UA.begin(), E = UA.end(); It != E; ++It) {

    if ((*It)->getKind() != hlsl::UnusualAnnotation::UA_RegisterAssignment)
      continue;

    reg = cast<hlsl::RegisterAssignment>(*It);
  }

  assert(reg && "Found a register missing a RegisterAssignment, even though "
                "GenerateConsistentBindings should have already generated it");

  DxcRegisterTypeInfo inputType = GetRegisterTypeInfo(ASTCtx, Type);

  uint32_t nodeId = PushNextNodeId(
      Refl, SM, ASTCtx.getLangOpts(), ValDesc->getName(), ValDesc,
      DxcHLSLNodeType::Register, ParentNodeId, (uint32_t)Refl.Registers.size());

  uint32_t arrayId = PushArray(Refl, ArraySizeFlat, ArraySize);

  uint32_t bufferId = 0;
  D3D_CBUFFER_TYPE bufferType = GetBufferType(inputType.RegisterType);
  
  if(bufferType != D3D_CT_INTERFACE_POINTERS) {
    bufferId = (uint32_t) Refl.Buffers.size();
    Refl.Buffers.push_back({bufferType, nodeId});
  }

  DxcHLSLRegister regD3D12 = {

      inputType.RegisterType,
      reg->RegisterNumber,
      ArraySizeFlat,
      (uint32_t)inputType.RegisterFlags,
      inputType.TextureValue,
      inputType.TextureDimension,
      inputType.SampleCount,
      reg->RegisterSpace.hasValue() ? reg->RegisterSpace.getValue()
                                    : AutoBindingSpace,
      nodeId,
      arrayId,
      bufferId
  };

  Refl.Registers.push_back(regD3D12);

  bool isListType = true;

  switch (inputType.RegisterType) {

  case D3D_SIT_CBUFFER:
  case D3D_SIT_TBUFFER:
    isListType = false;
    [[fallthrough]];

  case D3D_SIT_STRUCTURED:
  case D3D_SIT_UAV_RWSTRUCTURED:
  case D3D_SIT_UAV_APPEND_STRUCTURED:
  case D3D_SIT_UAV_CONSUME_STRUCTURED:
  case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER: {

    const RecordType *recordType = Type->getAs<RecordType>();

    assert(recordType && "Invalid type (not RecordType)");

    const ClassTemplateSpecializationDecl *templateDesc =
        dyn_cast<ClassTemplateSpecializationDecl>(recordType->getDecl());

    assert(templateDesc && "Invalid template type");

    const ArrayRef<TemplateArgument> &params =
        templateDesc->getTemplateArgs().asArray();

    assert(params.size() == 1 &&
           params[0].getKind() == TemplateArgument::Type && "Expected Type<T>");

    QualType innerType = params[0].getAsType();

    // The name of the inner struct is $Element if 'array', otherwise equal to
    // register name

    uint32_t typeId = GenerateTypeInfo(ASTCtx, Refl, innerType, DefaultRowMaj);

    SourceRange sourceRange = ValDesc->getSourceRange();

    PushNextNodeId(Refl, SM, ASTCtx.getLangOpts(),
                   isListType ? "$Element" : ValDesc->getName(), nullptr,
                   DxcHLSLNodeType::Variable, nodeId, typeId, &sourceRange);

    break;
  }
  }
}

//TODO: Debug code
class PrintfStream : public llvm::raw_ostream {
public:
  PrintfStream() { SetUnbuffered(); }

private:
  void write_impl(const char *Ptr, size_t Size) override {
    printf("%.*s\n", (int)Size, Ptr); // Print the raw buffer directly
  }

  uint64_t current_pos() const override { return 0; }
};

template<typename T>
void RecurseBuffer(ASTContext &ASTCtx, const SourceManager &SM,
                   DxcHLSLReflection &Refl, const T &Decls,
                   bool DefaultRowMaj, uint32_t ParentId) {

  for (Decl *decl : Decls) {

    ValueDecl *valDecl = dyn_cast<ValueDecl>(decl);
    assert(valDecl && "Decl was expected to be a ValueDecl but wasn't");
    QualType original = valDecl->getType();

    const std::string &name = valDecl->getName();

    uint32_t typeId = GenerateTypeInfo(ASTCtx, Refl, original, DefaultRowMaj);

    uint32_t nodeId = PushNextNodeId(Refl, SM, ASTCtx.getLangOpts(), name, decl,
                   DxcHLSLNodeType::Variable, ParentId, typeId);

    //Handle struct recursion

    if (RecordDecl *recordDecl = dyn_cast<RecordDecl>(decl)) {

      if (!recordDecl->isCompleteDefinition())
        continue;

      RecurseBuffer(ASTCtx, SM, Refl, recordDecl->fields(), DefaultRowMaj, nodeId);
    }
  }
}

uint32_t RegisterBuffer(ASTContext &ASTCtx, DxcHLSLReflection &Refl,
                        const SourceManager &SM, DeclContext *Buffer,
                        uint32_t NodeId, D3D_CBUFFER_TYPE Type,
                        bool DefaultRowMaj) {

  assert(Refl.Buffers.size() < (uint32_t)-1 && "Buffer id out of bounds");
  uint32_t bufferId = (uint32_t)Refl.Buffers.size();

  RecurseBuffer(ASTCtx, SM, Refl, Buffer->decls(), DefaultRowMaj, NodeId);

  Refl.Buffers.push_back({Type, NodeId});

  return bufferId;
}

/*
static void AddFunctionParameters(ASTContext &ASTCtx, QualType Type, Decl *Decl,
                                  DxcHLSLReflection &Refl, const SourceManager &SM,
                                  uint32_t ParentNodeId) {

  PrintingPolicy printingPolicy(ASTCtx.getLangOpts());

  QualType desugared = Type.getDesugaredType(ASTCtx);

  PrintfStream str;
  desugared.print(str, printingPolicy);

  if (Decl)
    Decl->print(str);

  //Generate parameter

  uint32_t nodeId = PushNextNodeId(
      Refl, SM, ASTCtx.getLangOpts(),
      Decl && dyn_cast<NamedDecl>(Decl) ? dyn_cast<NamedDecl>(Decl)->getName()
                                        : "",
      Decl, DxcHLSLNodeType::Parameter, ParentNodeId,
      (uint32_t)Refl.Parameters.size());

  std::string semanticName;

  if (NamedDecl *ValDesc = dyn_cast<NamedDecl>(Decl)) {

    ArrayRef<hlsl::UnusualAnnotation *> UA = ValDesc->getUnusualAnnotations();

    for (auto It = UA.begin(), E = UA.end(); It != E; ++It) {

      if ((*It)->getKind() != hlsl::UnusualAnnotation::UA_SemanticDecl)
        continue;

      semanticName = cast<hlsl::SemanticDecl>(*It)->SemanticName;
    }
  }

  DxcHLSLParameter parameter{std::move(semanticName)};

  type, clss, rows, columns, interpolationMode, flags;
  parameter.NodeId = nodeId;

  Refl.Parameters.push_back(parameter);

  //It's a struct, add parameters recursively
}*/

static void RecursiveReflectHLSL(const DeclContext &Ctx, ASTContext &ASTCtx,
                                 DiagnosticsEngine &Diags,
                                 const SourceManager &SM,
                                 DxcHLSLReflection &Refl,
                                 uint32_t AutoBindingSpace,
                                 uint32_t Depth,
                                 D3D12_HLSL_REFLECTION_FEATURE Features,
                                 uint32_t ParentNodeId, bool DefaultRowMaj) {

  PrintfStream pfStream;

  PrintingPolicy printingPolicy(ASTCtx.getLangOpts());

  printingPolicy.SuppressInitializers = true;
  printingPolicy.AnonymousTagLocations = false;
  printingPolicy.TerseOutput =
      true; // No inheritance list, trailing semicolons, etc.
  printingPolicy.PolishForDeclaration = true; // Makes it print as a decl
  printingPolicy.SuppressSpecifiers = false; // Prints e.g. "static" or "inline"
  printingPolicy.SuppressScope = true;

  // Traverse AST to grab reflection data

  //TODO: Niels, scopes (if/switch/for/empty scope)

  for (Decl *it : Ctx.decls()) {

    SourceLocation Loc = it->getLocation();
    if (Loc.isInvalid() || SM.isInSystemHeader(Loc))
      continue;

    if (HLSLBufferDecl *CBuffer = dyn_cast<HLSLBufferDecl>(it)) {

      if (!(Features & D3D12_HLSL_REFLECTION_FEATURE_BASICS))
        continue;
      
      // TODO: Add for reflection even though it might not be important

      if (Depth != 0)
        continue;

      hlsl::RegisterAssignment *reg = nullptr;
      ArrayRef<hlsl::UnusualAnnotation *> UA = CBuffer->getUnusualAnnotations();

      for (auto It = UA.begin(), E = UA.end(); It != E; ++It) {

        if ((*It)->getKind() != hlsl::UnusualAnnotation::UA_RegisterAssignment)
          continue;

        reg = cast<hlsl::RegisterAssignment>(*It);
      }

      assert(reg &&
             "Found a cbuffer missing a RegisterAssignment, even though "
             "GenerateConsistentBindings should have already generated it");

      uint32_t nodeId =
          PushNextNodeId(Refl, SM, ASTCtx.getLangOpts(), CBuffer->getName(),
                         CBuffer, DxcHLSLNodeType::Register, ParentNodeId,
                         (uint32_t)Refl.Registers.size());
      
      uint32_t bufferId = RegisterBuffer(ASTCtx, Refl, SM, CBuffer, nodeId,
                                         D3D_CT_CBUFFER, DefaultRowMaj);

      DxcHLSLRegister regD3D12 = {

          D3D_SIT_CBUFFER,
          reg->RegisterNumber,
          1,
          (uint32_t) D3D_SIF_USERPACKED,
          (D3D_RESOURCE_RETURN_TYPE) 0,
          D3D_SRV_DIMENSION_UNKNOWN,
          0,
          reg->RegisterSpace.hasValue() ? reg->RegisterSpace.getValue()
                                        : AutoBindingSpace,
          nodeId,
          (uint32_t)-1,
          bufferId
      };

      Refl.Registers.push_back(regD3D12);
    }

    else if (FunctionDecl *Func = dyn_cast<FunctionDecl>(it)) {

      if (!(Features & D3D12_HLSL_REFLECTION_FEATURE_FUNCTIONS))
        continue;

      const FunctionDecl *Definition = nullptr;

      uint32_t nodeId =
          PushNextNodeId(Refl, SM, ASTCtx.getLangOpts(), Func->getName(), Func,
                         DxcHLSLNodeType::Function, ParentNodeId,
                         (uint32_t)Refl.Functions.size());

      bool hasDefinition = Func->hasBody(Definition);
      DxcHLSLFunction func = {nodeId, Func->getNumParams(),
                              !Func->getReturnType().getTypePtr()->isVoidType(),
                              hasDefinition};

      /*
      for (uint32_t i = 0; i < func.NumParameters; ++i)
        AddFunctionParameters(ASTCtx, Func->getParamDecl(i)->getType(),
                              Func->getParamDecl(i), Refl, SM, nodeId);

      if (func.HasReturn)
        AddFunctionParameters(ASTCtx, Func->getReturnType(), nullptr, Refl, SM,
                              nodeId);*/

      Refl.Functions.push_back(std::move(func));

      if (hasDefinition && (Features & D3D12_HLSL_REFLECTION_FEATURE_SCOPES)) {
        RecursiveReflectHLSL(*Definition, ASTCtx, Diags, SM, Refl,
                             AutoBindingSpace, Depth + 1, Features,
                             nodeId, DefaultRowMaj);
      }
    }

    else if (TypedefDecl *Typedef = dyn_cast<TypedefDecl>(it)) {

      if (!(Features & D3D12_HLSL_REFLECTION_FEATURE_USER_TYPES))
        continue;

      // Typedef->print(pfStream, printingPolicy);
    }

    else if (TypeAliasDecl *TypeAlias = dyn_cast<TypeAliasDecl>(it)) {

      if (!(Features & D3D12_HLSL_REFLECTION_FEATURE_USER_TYPES))
        continue;

      // TypeAlias->print(pfStream, printingPolicy);
    }

    else if (EnumDecl *Enum = dyn_cast<EnumDecl>(it)) {

      if (!(Features & D3D12_HLSL_REFLECTION_FEATURE_USER_TYPES))
        continue;

      uint32_t nodeId = PushNextNodeId(
          Refl, SM, ASTCtx.getLangOpts(), Enum->getName(), Enum,
          DxcHLSLNodeType::Enum, ParentNodeId, (uint32_t)Refl.Enums.size());

      for (EnumConstantDecl *EnumValue : Enum->enumerators()) {

        uint32_t childNodeId =
            PushNextNodeId(Refl, SM, ASTCtx.getLangOpts(), EnumValue->getName(),
                           EnumValue, DxcHLSLNodeType::EnumValue, nodeId,
                           (uint32_t)Refl.EnumValues.size());

        Refl.EnumValues.push_back(
            {EnumValue->getInitVal().getSExtValue(), childNodeId});
      }

      assert(Refl.EnumValues.size() < (uint32_t)(1 << 30) &&
             "Enum values overflow");

      QualType enumType = Enum->getIntegerType();
      QualType desugared = enumType.getDesugaredType(ASTCtx);
      const auto &semantics = ASTCtx.getTypeInfo(desugared);

      D3D12_HLSL_ENUM_TYPE type;

      switch (semantics.Width) {

      default:
      case 32:
        type = desugared->isUnsignedIntegerType() ? D3D12_HLSL_ENUM_TYPE_UINT
                                                  : D3D12_HLSL_ENUM_TYPE_INT;
        break;

      case 16:
        type = desugared->isUnsignedIntegerType()
                   ? D3D12_HLSL_ENUM_TYPE_UINT16_T
                   : D3D12_HLSL_ENUM_TYPE_INT16_T;
        break;

      case 64:
        type = desugared->isUnsignedIntegerType()
                   ? D3D12_HLSL_ENUM_TYPE_UINT64_T
                   : D3D12_HLSL_ENUM_TYPE_INT64_T;
        break;
      }

      Refl.Enums.push_back({nodeId, type});
    }

    else if (ValueDecl *ValDecl = dyn_cast<ValueDecl>(it)) {

      if (!(Features & D3D12_HLSL_REFLECTION_FEATURE_BASICS))
        continue;

      //TODO: Handle values

      //ValDecl->print(pfStream);

      uint32_t arraySize = 1;
      QualType type = ValDecl->getType();
      std::vector<uint32_t> arrayElem;

      while (const ConstantArrayType *arr =
              dyn_cast<ConstantArrayType>(type)) {
        uint32_t current = arr->getSize().getZExtValue();
        arrayElem.push_back(current);
        arraySize *= arr->getSize().getZExtValue();
        type = arr->getElementType();
      }

      if (!IsHLSLResourceType(type))
        continue;

      // TODO: Add for reflection even though it might not be important

      if (Depth != 0)
        continue;

      FillReflectionRegisterAt(Ctx, ASTCtx, SM, Diags, type, arraySize, ValDecl,
                               arrayElem, Refl, AutoBindingSpace, ParentNodeId,
                               DefaultRowMaj);
    }

    else if (RecordDecl *RecDecl = dyn_cast<RecordDecl>(it)) {

      if (!(Features & D3D12_HLSL_REFLECTION_FEATURE_USER_TYPES))
        continue;

      //RecDecl->print(pfStream, printingPolicy);
      /*RecursiveReflectHLSL(*RecDecl, ASTCtx, Diags, SM, Refl,
                           AutoBindingSpace, Depth + 1, InclusionFlags, nodeId);*/
    }

    else if (NamespaceDecl *Namespace = dyn_cast<NamespaceDecl>(it)) {

      if (!(Features & D3D12_HLSL_REFLECTION_FEATURE_NAMESPACES))
        continue;

      uint32_t nodeId = PushNextNodeId(
          Refl, SM, ASTCtx.getLangOpts(), Namespace->getName(), Namespace,
          DxcHLSLNodeType::Namespace, ParentNodeId, 0);

      RecursiveReflectHLSL(*Namespace, ASTCtx, Diags, SM, Refl,
                           AutoBindingSpace, Depth + 1, Features, nodeId,
                           DefaultRowMaj);
    }
  }
}

DxcHLSLReflection::DxcHLSLReflection(clang::CompilerInstance &Compiler,
                                     clang::TranslationUnitDecl &Ctx,
                                     uint32_t AutoBindingSpace,
                                     D3D12_HLSL_REFLECTION_FEATURE Features,
                                     bool DefaultRowMaj) {

  DiagnosticsEngine &Diags = Ctx.getParentASTContext().getDiagnostics();
  const SourceManager &SM = Compiler.getSourceManager();

  *this = {};
  this->Features = Features;

  if (Features & D3D12_HLSL_REFLECTION_FEATURE_SYMBOL_INFO) {
    Strings.push_back("");
    StringsToId[""] = 0;
    NodeSymbols.push_back(DxcHLSLNodeSymbol(0, 0, 0, 0, 0, 0));
  }

  Nodes.push_back(
      DxcHLSLNode{DxcHLSLNodeType::Namespace, 0, 0, 0, 0xFFFF, 0});

  RecursiveReflectHLSL(Ctx, Compiler.getASTContext(), Diags, SM, *this,
                       AutoBindingSpace, 0, Features, 0, DefaultRowMaj);
}

//TODO: Debug print code

static char RegisterGetSpaceChar(const DxcHLSLRegister &reg) {

  switch (reg.Type) {

  case D3D_SIT_UAV_RWTYPED:
  case D3D_SIT_UAV_RWSTRUCTURED:
  case D3D_SIT_UAV_RWBYTEADDRESS:
  case D3D_SIT_UAV_APPEND_STRUCTURED:
  case D3D_SIT_UAV_CONSUME_STRUCTURED:
  case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
  case D3D_SIT_UAV_FEEDBACKTEXTURE:
    return 'u';

  case D3D_SIT_CBUFFER:
    return 'b';

  case D3D_SIT_SAMPLER:
    return 's';

  default:
    return 't';
  }
}

static std::string RegisterGetArraySize(const DxcHLSLReflection &Refl, const DxcHLSLRegister &reg) {

  if (reg.ArrayId != (uint32_t)-1) {

    DxcHLSLArray arr = Refl.Arrays[reg.ArrayId];
    std::string str;

    for (uint32_t i = 0; i < arr.ArrayElem(); ++i)
      str += "[" + std::to_string(Refl.ArraySizes[arr.ArrayStart() + i]) + "]";

    return str;
  }

  return reg.BindCount > 1 ? "[" + std::to_string(reg.BindCount) + "]" : "";
}

static std::string EnumTypeToString(D3D12_HLSL_ENUM_TYPE type) {

  static const char *arr[] = {
      "uint", "int", "uint64_t", "int64_t", "uint16_t", "int16_t",
  };

  return arr[type];
}

static std::string NodeTypeToString(DxcHLSLNodeType type) {

  static const char *arr[] = {"Register",  "Function", "Enum",  "EnumValue",
                              "Namespace", "Typedef",  "Using", "Variable"};

  return arr[(int)type];
}

static std::string GetBuiltinTypeName(const DxcHLSLReflection &Refl,
                               const DxcHLSLType &Type) {

  std::string type;

  if (Type.Class != D3D_SVC_STRUCT) {

    static const char *arr[] = {"void",
                                "bool",
                                "int",
                                "float",
                                "string",
                                NULL,
                                "Texture1D",
                                "Texture2D",
                                "Texture3D",
                                "TextureCube",
                                "SamplerState",
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                "uint",
                                "uint8_t",
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                "Buffer",
                                "ConstantBuffer",
                                NULL,
                                "Texture1DArray",
                                "Texture2DArray",
                                NULL,
                                NULL,
                                "Texture2DMS",
                                "Texture2DMSArray",
                                "TextureCubeArray",
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                "double",
                                "RWTexture1D",
                                "RWTexture1DArray",
                                "RWTexture2D",
                                "RWTexture2DArray",
                                "RWTexture3D",
                                "RWBuffer",
                                "ByteAddressBuffer",
                                "RWByteAddressBuffer",
                                "StructuredBuffer",
                                "RWStructuredBuffer",
                                "AppendStructuredBuffer",
                                "ConsumeStructuredBuffer",
                                "min8float",
                                "min10float",
                                "min16float",
                                "min12int",
                                "min16int",
                                "min16uint",
                                "int16_t",
                                "uint16_t",
                                "float16_t",
                                "int64_t",
                                "uint64_t"};

    const char *ptr = arr[Type.Type];

    if (ptr)
      type = ptr;
  }

  switch (Type.Class) {

  case D3D_SVC_MATRIX_ROWS:
  case D3D_SVC_VECTOR:

    type += std::to_string(Type.Columns);

    if (Type.Class == D3D_SVC_MATRIX_ROWS)
      type += "x" + std::to_string(Type.Rows);

    break;

  case D3D_SVC_MATRIX_COLUMNS:
    type += std::to_string(Type.Rows) + "x" + std::to_string(Type.Columns);
    break;
  }

  return type;
}

static std::string PrintTypeInfo(const DxcHLSLReflection &Refl,
                          const DxcHLSLType &Type,
                          const std::string &PreviousTypeName) {

  std::string result;

  if (Type.IsMultiDimensionalArray()) {

    const DxcHLSLArray &arr = Refl.Arrays[Type.GetMultiDimensionalArrayId()];

    for (uint32_t i = 0; i < arr.ArrayElem(); ++i)
      result +=
          "[" + std::to_string(Refl.ArraySizes[arr.ArrayStart() + i]) + "]";

  }

  else if (Type.IsArray())
    result += "[" + std::to_string(Type.Get1DElements()) + "]";

  // Obtain type name (returns empty if it's not a builtin type)

  std::string underlyingTypeName = GetBuiltinTypeName(Refl, Type);

  if (PreviousTypeName != underlyingTypeName && underlyingTypeName.size())
    result += " (" + underlyingTypeName + ")";

  return result;
}

static void RecursePrintType(const DxcHLSLReflection &Refl, uint32_t TypeId,
                      uint32_t Depth, const char *Prefix = "") {

  const DxcHLSLType &type = Refl.Types[TypeId];

  bool hasSymbols = Refl.Features & D3D12_HLSL_REFLECTION_FEATURE_SYMBOL_INFO;

  std::string name = hasSymbols ? Refl.Strings[Refl.TypeNameIds[TypeId]]
                                : GetBuiltinTypeName(Refl, type);

  if (name.empty() && !hasSymbols)
    name = "(unknown)";

  printf("%s%s%s%s\n", std::string(Depth, '\t').c_str(), Prefix, name.c_str(),
         PrintTypeInfo(Refl, type, name).c_str());

  if (type.BaseClass != uint32_t(-1))
    RecursePrintType(Refl, type.BaseClass, Depth + 1, Prefix);

  for (uint32_t i = 0; i < type.GetMemberCount(); ++i) {

    uint32_t memberId = type.GetMemberStart() + i;
    std::string prefix =
        (hasSymbols ? Refl.Strings[Refl.MemberNameIds[memberId]] : "(unknown)") + ": ";

    RecursePrintType(Refl, Refl.MemberTypeIds[memberId], Depth + 1, prefix.c_str());
  }
}

uint32_t RecursePrint(const DxcHLSLReflection &Refl, uint32_t NodeId,
                      uint32_t Depth, uint32_t IndexInParent) {

  const DxcHLSLNode &node = Refl.Nodes[NodeId];

  uint32_t typeToPrint = (uint32_t)-1;

  if (NodeId) {

    bool hasSymbols = Refl.Features & D3D12_HLSL_REFLECTION_FEATURE_SYMBOL_INFO;

    printf("%s%s %s\n", std::string(Depth - 1, '\t').c_str(),
           NodeTypeToString(node.GetNodeType()).c_str(),
           hasSymbols ? Refl.Strings[Refl.NodeSymbols[NodeId].NameId].c_str()
                      : "(unknown)");

    for (uint32_t i = 0; i < node.GetAnnotationCount(); ++i) {

      const DxcHLSLAnnotation &annotation =
          Refl.Annotations[node.GetAnnotationStart() + i];

      printf(annotation.GetIsBuiltin() ? "%s[%s]\n" : "%s[[%s]]\n",
             std::string(Depth, '\t').c_str(),
             Refl.StringsNonDebug[annotation.GetStringNonDebug()]
              .c_str());
    }

    uint32_t localId = node.GetLocalId();

    switch (node.GetNodeType()) {
        
    case DxcHLSLNodeType::Register: {
      const DxcHLSLRegister &reg = Refl.Registers[localId];
      printf("%s%s : register(%c%u, space%u);\n",
             std::string(Depth, '\t').c_str(),
             RegisterGetArraySize(Refl, reg).c_str(), RegisterGetSpaceChar(reg),
             reg.BindPoint, reg.Space);
      break;
    }
        
    case DxcHLSLNodeType::Variable:
      typeToPrint = localId;
      break;

    case DxcHLSLNodeType::Function: {
      const DxcHLSLFunction &func = Refl.Functions[localId];
      printf("%sreturn: %s, hasDefinition: %s, numParams: %u\n",
             std::string(Depth, '\t').c_str(),
             func.HasReturn() ? "true" : "false",
             func.HasDefinition() ? "true" : "false", func.GetNumParameters());

      break;
    }

    case DxcHLSLNodeType::Enum:
      printf("%s: %s\n", std::string(Depth, '\t').c_str(),
             EnumTypeToString(Refl.Enums[localId].Type).c_str());
      break;

    case DxcHLSLNodeType::EnumValue: {
      printf("%s#%u = %" PRIi64 "\n", std::string(Depth, '\t').c_str(),
             IndexInParent, Refl.EnumValues[localId].Value);
        break;
    }

    //TODO:
    case DxcHLSLNodeType::Typedef:
    case DxcHLSLNodeType::Using:
        break;

    case DxcHLSLNodeType::Namespace:
    default:
      break;
    }
  }

  if (typeToPrint != (uint32_t)-1)
    RecursePrintType(Refl, typeToPrint, Depth);

  for (uint32_t i = 0, j = 0; i < node.GetChildCount(); ++i, ++j)
    i += RecursePrint(Refl, NodeId + 1 + i, Depth + 1, j);

  return node.GetChildCount();
}

struct DxcHLSLHeader {

  uint32_t MagicNumber;
  uint16_t Version;
  uint16_t Sources;

  D3D12_HLSL_REFLECTION_FEATURE Features;
  uint32_t StringsNonDebug;

  uint32_t Strings;
  uint32_t Nodes;

  uint32_t Registers;
  uint32_t Functions;

  uint32_t Enums;
  uint32_t EnumValues;

  uint32_t Annotations;
  uint32_t Arrays;

  uint32_t ArraySizes;
  uint32_t Members;

  uint32_t Types;
  uint32_t Buffers;
};

template <typename T>
T &UnsafeCast(std::vector<std::byte> &Bytes, uint64_t Offset) {
  return *(T *)(Bytes.data() + Offset);
}

template <typename T>
const T &UnsafeCast(const std::vector<std::byte> &Bytes, uint64_t Offset) {
  return *(const T *)(Bytes.data() + Offset);
}

template <typename T>
void SkipPadding(uint64_t& Offset) {
  Offset = (Offset + alignof(T) - 1) / alignof(T) * alignof(T);
}

template <typename T>
void Skip(uint64_t& Offset, const std::vector<T>& Vec) {
  Offset += Vec.size() * sizeof(T);
}

template <typename T>
void Advance(uint64_t& Offset, const std::vector<T>& Vec) {
  SkipPadding<T>(Offset);
  Skip(Offset, Vec);
}

template <>
void Advance<std::string>(uint64_t &Offset,
                          const std::vector<std::string> &Vec) {
  for (const std::string &str : Vec) {
    Offset += str.size() >= 128 ? 2 : 1;
    Offset += str.size();
  }
}

template <typename T, typename T2, typename ...args>
void Advance(uint64_t& Offset, const std::vector<T>& Vec, const std::vector<T2>& Vec2, args... arg) {
  Advance(Offset, Vec);
  Advance(Offset, Vec2, arg...);
}

template<typename T>
void Append(std::vector<std::byte> &Bytes, uint64_t &Offset,
            const std::vector<T> &Vec) {
  static_assert(std::is_pod_v<T>, "Append only works on POD types");
  SkipPadding<T>(Offset);
  std::memcpy(&UnsafeCast<uint8_t>(Bytes, Offset), Vec.data(),
              Vec.size() * sizeof(T));
  Skip(Offset, Vec);
}

template <>
void Append<std::string>(std::vector<std::byte> &Bytes, uint64_t &Offset,
                         const std::vector<std::string> &Vec) {

  for (const std::string &str : Vec) {

    if (str.size() >= 128) {
      UnsafeCast<uint8_t>(Bytes, Offset++) =
          (uint8_t)(str.size() & 0x7F) | 0x80;
      UnsafeCast<uint8_t>(Bytes, Offset++) = (uint8_t)(str.size() >> 7);
    }

    else
      UnsafeCast<uint8_t>(Bytes, Offset++) = (uint8_t)str.size();

    std::memcpy(&UnsafeCast<char>(Bytes, Offset), str.data(), str.size());
    Offset += str.size();
  }
}

template <typename T, typename T2, typename ...args>
void Append(std::vector<std::byte> &Bytes, uint64_t &Offset,
            const std::vector<T> &Vec, const std::vector<T2> &Vec2,
            args... arg) {
  Append(Bytes, Offset, Vec);
  Append(Bytes, Offset, Vec2, arg...);
}

template <typename T, typename = std::enable_if_t<std::is_pod_v<T>>>
void Consume(const std::vector<std::byte> &Bytes, uint64_t &Offset, T &t) {

  static_assert(std::is_pod_v<T>, "Consume only works on POD types");

  SkipPadding<T>(Offset);

  if (Offset + sizeof(T) > Bytes.size())
    throw std::out_of_range("Couldn't consume; out of bounds!");

  std::memcpy(&t, &UnsafeCast<uint8_t>(Bytes, Offset), sizeof(T));
  Offset += sizeof(T);
}

template <typename T>
void Consume(const std::vector<std::byte> &Bytes, uint64_t &Offset, T *target,
             uint64_t Len) {

  static_assert(std::is_pod_v<T>, "Consume only works on POD types");

  SkipPadding<T>(Offset);

  if (Offset + sizeof(T) * Len > Bytes.size())
    throw std::out_of_range("Couldn't consume; out of bounds!");

  std::memcpy(target, &UnsafeCast<uint8_t>(Bytes, Offset), sizeof(T) * Len);
  Offset += sizeof(T) * Len;
}

template <typename T>
void Consume(const std::vector<std::byte> &Bytes, uint64_t &Offset,
            std::vector<T> &Vec, uint64_t Len) {
  Vec.resize(Len);
  Consume(Bytes, Offset, Vec.data(), Len);
}

template <>
void Consume<std::string>(const std::vector<std::byte> &Bytes, uint64_t &Offset,
                          std::vector<std::string> &Vec, uint64_t Len) {
  Vec.resize(Len);

  for (uint64_t i = 0; i < Len; ++i) {

      if (Offset >= Bytes.size())
        throw std::out_of_range("Couldn't consume string len; out of bounds!");
  
      uint16_t ourLen = uint8_t(Bytes.at(Offset++));

      if (ourLen >> 7) {

        if (Offset >= Bytes.size())
          throw std::out_of_range("Couldn't consume string len; out of bounds!");

        ourLen &= ~(1 << 7);
        ourLen |= uint16_t(Bytes.at(Offset++)) << 7;
      }

      if (Offset + ourLen > Bytes.size())
        throw std::out_of_range("Couldn't consume string len; out of bounds!");

      Vec[i].resize(ourLen);
      std::memcpy(Vec[i].data(), Bytes.data() + Offset, ourLen);
      Offset += ourLen;
  }
}

template <typename T, typename T2, typename ...args>
void Consume(const std::vector<std::byte> &Bytes, uint64_t &Offset,
             std::vector<T> &Vec, uint64_t Len, std::vector<T2> &Vec2,
             uint64_t Len2, args&... arg) {
  Consume(Bytes, Offset, Vec, Len);
  Consume(Bytes, Offset, Vec2, Len2, arg...);
}

static constexpr uint32_t DxcReflectionDataMagic = DXC_FOURCC('D', 'H', 'R', 'D');
static constexpr uint16_t DxcReflectionDataVersion = 0;

void DxcHLSLReflection::StripSymbols() {
  Strings.clear();
  StringsToId.clear();
  Sources.clear();
  StringToSourceId.clear();
  FullyResolvedToNodeId.clear();
  NodeIdToFullyResolved.clear();
  FullyResolvedToMemberId.clear();
  NodeSymbols.clear();
  TypeNameIds.clear();
  MemberNameIds.clear();
  Features &= ~D3D12_HLSL_REFLECTION_FEATURE_SYMBOL_INFO;
}

void RecurseNameGenerationType(DxcHLSLReflection &Refl, uint32_t TypeId,
                               uint32_t LocalId, const std::string &Parent) {

  const DxcHLSLType &type = Refl.Types[TypeId];

  if (type.Class == D3D_SVC_STRUCT)
      for (uint32_t i = 0; i < type.GetMemberCount(); ++i) {

        uint32_t memberId = i + type.GetMemberStart();
        std::string memberName =
            Parent + "." + Refl.Strings[Refl.MemberNameIds[memberId]];

        Refl.FullyResolvedToMemberId[memberName] = memberId;

        RecurseNameGenerationType(Refl, Refl.MemberTypeIds[memberId], i,
                                  memberName);
      }
}

uint32_t RecurseNameGeneration(DxcHLSLReflection &Refl, uint32_t NodeId,
                               uint32_t LocalId, const std::string &Parent,
                               bool IsDot) {

  const DxcHLSLNode &node = Refl.Nodes[NodeId];
  std::string self = Refl.Strings[Refl.NodeSymbols[NodeId].NameId];

  if (self.empty() && NodeId)
      self = std::to_string(LocalId);

  self = Parent.empty() ? self : Parent + (IsDot ? "." : "::") + self;
  Refl.FullyResolvedToNodeId[self] = NodeId;
  Refl.NodeIdToFullyResolved[NodeId] = self;

  bool isDotChild = node.GetNodeType() == DxcHLSLNodeType::Register;
  bool isVar = node.GetNodeType() == DxcHLSLNodeType::Variable;

  for (uint32_t i = 0, j = 0; i < node.GetChildCount(); ++i, ++j)
      i += RecurseNameGeneration(Refl, NodeId + 1 + i, j, self, isDotChild);

  if (isVar) {

      uint32_t typeId = node.GetLocalId();
      const DxcHLSLType &type = Refl.Types[typeId];

      if (type.Class == D3D_SVC_STRUCT)
        for (uint32_t i = 0; i < type.GetMemberCount(); ++i) {

          uint32_t memberId = i + type.GetMemberStart();
          std::string memberName =
              self + "." + Refl.Strings[Refl.MemberNameIds[memberId]];

          Refl.FullyResolvedToMemberId[memberName] = memberId;

          RecurseNameGenerationType(Refl, Refl.MemberTypeIds[memberId], i,
                                    memberName);
        }
  }

  return node.GetChildCount();
}

bool DxcHLSLReflection::GenerateNameLookupTable() {

  if (!(Features & D3D12_HLSL_REFLECTION_FEATURE_SYMBOL_INFO) || Nodes.empty())
      return false;

  NodeIdToFullyResolved.resize(Nodes.size());
  RecurseNameGeneration(*this, 0, 0, "", false);
  return true;
}

void DxcHLSLReflection::Dump(std::vector<std::byte> &Bytes) const {

  uint64_t toReserve = sizeof(DxcHLSLHeader);

  Advance(toReserve, Strings, StringsNonDebug, Sources, Nodes, NodeSymbols, Registers, Functions,
          Enums, EnumValues, Annotations, ArraySizes, MemberTypeIds, MemberNameIds, Types, TypeNameIds, Buffers);

  Bytes.resize(toReserve);

  toReserve = 0;

  UnsafeCast<DxcHLSLHeader>(Bytes, toReserve) = {
      DxcReflectionDataMagic,           DxcReflectionDataVersion,
      uint16_t(Sources.size()),         Features,
      uint32_t(StringsNonDebug.size()), uint32_t(Strings.size()),
      uint32_t(Nodes.size()),           uint32_t(Registers.size()),
      uint32_t(Functions.size()),       uint32_t(Enums.size()),
      uint32_t(EnumValues.size()),      uint32_t(Annotations.size()),
      uint32_t(Arrays.size()),          uint32_t(ArraySizes.size()),
      uint32_t(MemberTypeIds.size()),   uint32_t(Types.size()),
      uint32_t(Buffers.size())};

  toReserve += sizeof(DxcHLSLHeader);

  Append(Bytes, toReserve, Strings, StringsNonDebug, Sources, Nodes,
         NodeSymbols, Registers,
         Functions, Enums, EnumValues, Annotations, ArraySizes, MemberTypeIds,
         MemberNameIds, Types, TypeNameIds, Buffers);
}

DxcHLSLReflection::DxcHLSLReflection(const std::vector<std::byte> &Bytes,
                                     bool MakeNameLookupTable) {

  uint64_t off = 0;
  DxcHLSLHeader header;
  Consume<DxcHLSLHeader>(Bytes, off, header);

  if (header.MagicNumber != DxcReflectionDataMagic)
    throw std::invalid_argument("Invalid magic number");

  if (header.Version != DxcReflectionDataVersion)
    throw std::invalid_argument("Unrecognized version number");

  Features = header.Features;

  bool hasSymbolInfo = Features & D3D12_HLSL_REFLECTION_FEATURE_SYMBOL_INFO;

  if (!hasSymbolInfo && (header.Sources || header.Strings))
    throw std::invalid_argument("Sources are invalid without symbols");

  uint32_t nodeSymbolCount = hasSymbolInfo ? header.Nodes : 0;
  uint32_t memberSymbolCount = hasSymbolInfo ? header.Members : 0;
  uint32_t typeSymbolCount = hasSymbolInfo ? header.Types : 0;

  Consume(Bytes, off, Strings, header.Strings, StringsNonDebug,
          header.StringsNonDebug, Sources, header.Sources, Nodes, header.Nodes,
          NodeSymbols, nodeSymbolCount, Registers, header.Registers, Functions,
          header.Functions, Enums, header.Enums, EnumValues, header.EnumValues,
          Annotations, header.Annotations, ArraySizes, header.ArraySizes,
          MemberTypeIds, header.Members, MemberNameIds, memberSymbolCount,
          Types, header.Types, TypeNameIds, typeSymbolCount, Buffers,
          header.Buffers);

  // Validation errors are throws to prevent accessing invalid data

  if (off != Bytes.size())
    throw std::invalid_argument("Reflection info had unrecognized data on the back");

  for(uint32_t i = 0; i < header.Sources; ++i)
    if(Sources[i] >= header.Strings)
      throw std::invalid_argument("Source path out of bounds");

  for(uint32_t i = 0; i < header.Nodes; ++i) {

    const DxcHLSLNode &node = Nodes[i];

    if (hasSymbolInfo && (NodeSymbols[i].NameId >= header.Strings ||
                          (NodeSymbols[i].FileNameId != uint16_t(-1) &&
                           NodeSymbols[i].FileNameId >= header.Sources)))
      throw std::invalid_argument("Node " + std::to_string(i) +
                                  " points to invalid name or file name");

    if(
      node.GetAnnotationStart() + node.GetAnnotationCount() > header.Annotations ||
      node.GetNodeType() > DxcHLSLNodeType::End ||
      (i && node.GetParentId() >= i) ||
      i + node.GetChildCount() > header.Nodes
    )
      throw std::invalid_argument("Node " + std::to_string(i) + " is invalid");

    uint32_t maxValue = 1;

    switch(node.GetNodeType()) {
      case DxcHLSLNodeType::Register:
        maxValue = header.Registers;
        break;
      case DxcHLSLNodeType::Function:
        maxValue = header.Functions;
        break;
      case DxcHLSLNodeType::Enum:
        maxValue = header.Enums;
        break;
      case DxcHLSLNodeType::EnumValue:
        maxValue = header.EnumValues;
        break;
      case DxcHLSLNodeType::Typedef:
      case DxcHLSLNodeType::Using:
      case DxcHLSLNodeType::Variable:
        maxValue = header.Types;
        break;
    }

    if(node.GetLocalId() >= maxValue)
      throw std::invalid_argument("Node " + std::to_string(i) + " has invalid localId");
  }

  for(uint32_t i = 0; i < header.Registers; ++i) {

    const DxcHLSLRegister &reg = Registers[i];

    if(
      reg.NodeId >= header.Nodes || 
      Nodes[reg.NodeId].GetNodeType() != DxcHLSLNodeType::Register ||
      Nodes[reg.NodeId].GetLocalId() != i
    )
      throw std::invalid_argument("Register " + std::to_string(i) + " points to an invalid nodeId");

    if (reg.Type > D3D_SIT_UAV_FEEDBACKTEXTURE ||
        reg.ReturnType > D3D_RETURN_TYPE_CONTINUED ||
        reg.Dimension > D3D_SRV_DIMENSION_BUFFEREX || !reg.BindCount ||
        (reg.ArrayId != uint32_t(-1) && reg.ArrayId >= header.Arrays) ||
        (reg.ArrayId != uint32_t(-1) && reg.BindCount <= 1))
      throw std::invalid_argument(
          "Register " + std::to_string(i) +
          " invalid type, returnType, bindCount, array or dimension");
    
    D3D_CBUFFER_TYPE bufferType = GetBufferType(reg.Type);

    if(bufferType != D3D_CT_INTERFACE_POINTERS) {

      if (reg.BufferId >= header.Buffers ||
          Buffers[reg.BufferId].NodeId != reg.NodeId ||
          Buffers[reg.BufferId].Type != bufferType)
          throw std::invalid_argument("Register " + std::to_string(i) +
                                      " invalid buffer referenced by register");
    }
  }

  for(uint32_t i = 0; i < header.Functions; ++i) {
    
    const DxcHLSLFunction &func = Functions[i];

    if (func.NodeId >= header.Nodes ||
        Nodes[func.NodeId].GetNodeType() != DxcHLSLNodeType::Function ||
        Nodes[func.NodeId].GetLocalId() != i)
      throw std::invalid_argument("Function " + std::to_string(i) +
                                  " points to an invalid nodeId");
  }

  for(uint32_t i = 0; i < header.Enums; ++i) {
    
    const DxcHLSLEnumDesc &enm = Enums[i];

    if (enm.NodeId >= header.Nodes ||
        Nodes[enm.NodeId].GetNodeType() != DxcHLSLNodeType::Enum ||
        Nodes[enm.NodeId].GetLocalId() != i)
      throw std::invalid_argument("Function " + std::to_string(i) +
                                  " points to an invalid nodeId");

    if (enm.Type < D3D12_HLSL_ENUM_TYPE_START ||
        enm.Type > D3D12_HLSL_ENUM_TYPE_END)
      throw std::invalid_argument("Enum " + std::to_string(i) +
                                  " has an invalid type");

    const DxcHLSLNode &node = Nodes[enm.NodeId];

    for (uint32_t j = 0; j < node.GetChildCount(); ++j) {
    
        const DxcHLSLNode &child = Nodes[enm.NodeId + 1 + j];

        if (child.GetChildCount() != 0 ||
            child.GetNodeType() != DxcHLSLNodeType::EnumValue)
          throw std::invalid_argument("Enum " + std::to_string(i) +
                                      " has an invalid enum value");
    }
  }

  for(uint32_t i = 0; i < header.EnumValues; ++i) {
    
    const DxcHLSLEnumValue &enumVal = EnumValues[i];

    if (enumVal.NodeId >= header.Nodes ||
        Nodes[enumVal.NodeId].GetNodeType() != DxcHLSLNodeType::EnumValue ||
        Nodes[enumVal.NodeId].GetLocalId() != i ||
        Nodes[Nodes[enumVal.NodeId].GetParentId()].GetNodeType() !=
            DxcHLSLNodeType::Enum)
      throw std::invalid_argument("Enum " + std::to_string(i) +
                                  " points to an invalid nodeId");
  }

  for (uint32_t i = 0; i < header.Arrays; ++i) {

    const DxcHLSLArray &arr = Arrays[i];

    if (arr.ArrayElem() <= 1 || arr.ArrayElem() > 8 ||
        arr.ArrayStart() + arr.ArrayElem() > header.ArraySizes)
      throw std::invalid_argument("Array " + std::to_string(i) +
                                  " points to an invalid array element");
  }

  for (uint32_t i = 0; i < header.Annotations; ++i)
    if (Annotations[i].GetStringNonDebug() >= header.StringsNonDebug)
      throw std::invalid_argument("Annotation " + std::to_string(i) +
                                  " points to an invalid string");

  for (uint32_t i = 0; i < header.Buffers; ++i) {

    const DxcHLSLBuffer &buf = Buffers[i];

    if (buf.NodeId >= header.Nodes ||
        Nodes[buf.NodeId].GetNodeType() != DxcHLSLNodeType::Register ||
        Nodes[buf.NodeId].GetLocalId() >= header.Registers ||
        Registers[Nodes[buf.NodeId].GetLocalId()].BufferId != i)
      throw std::invalid_argument("Buffer " + std::to_string(i) +
                                  " points to an invalid nodeId");

    const DxcHLSLNode &node = Nodes[buf.NodeId];

    if (!node.GetChildCount())
      throw std::invalid_argument("Buffer " + std::to_string(i) +
                                  " requires at least one Variable child");

    for (uint32_t j = 0; j < node.GetChildCount(); ++j) {

      const DxcHLSLNode &child = Nodes[buf.NodeId + 1 + j];

      if (child.GetChildCount() != 0 ||
          child.GetNodeType() != DxcHLSLNodeType::Variable)
          throw std::invalid_argument("Buffer " + std::to_string(i) +
                                      " has to have only Variable child nodes");
    }
  }

  for (uint32_t i = 0; i < header.Members; ++i) {

    if (MemberTypeIds[i] >= header.Types)
      throw std::invalid_argument("Member " + std::to_string(i) +
                                  " points to an invalid type");

    if (hasSymbolInfo && MemberNameIds[i] >= header.Strings)
      throw std::invalid_argument("Member " + std::to_string(i) +
                                  " points to an invalid string");
  }
  
  for (uint32_t i = 0; i < header.Types; ++i) {

    const DxcHLSLType &type = Types[i];

    if (hasSymbolInfo && TypeNameIds[i] >= header.Strings)
      throw std::invalid_argument("Type " + std::to_string(i) +
                                  " points to an invalid string");

    if ((type.BaseClass != uint32_t(-1) && type.BaseClass >= header.Types) ||
        type.GetMemberStart() + type.GetMemberCount() > header.Members ||
        (type.ElementsOrArrayId >> 31 &&
         (type.ElementsOrArrayId << 1 >> 1) >= header.Arrays))
      throw std::invalid_argument(
          "Type " + std::to_string(i) +
          " points to an invalid string, base class or member");

    switch (type.Class) {

    case D3D_SVC_SCALAR:

      if (type.Columns != 1)
          throw std::invalid_argument("Type (scalar) " + std::to_string(i) +
                                      " should have columns == 1");

      [[fallthrough]];

    case D3D_SVC_VECTOR:

      if (type.Rows != 1)
          throw std::invalid_argument("Type (scalar/vector) " +
                                      std::to_string(i) +
                                      " should have rows == 1");

      [[fallthrough]];

    case D3D_SVC_MATRIX_ROWS:
    case D3D_SVC_MATRIX_COLUMNS:

        if (!type.Rows || !type.Columns || type.Rows > 128 || type.Columns > 128)
          throw std::invalid_argument("Type (scalar/vector/matrix) " +
                                      std::to_string(i) +
                                      " has invalid rows or columns");
        
        switch (type.Type) {
        case D3D_SVT_BOOL:
        case D3D_SVT_INT:
        case D3D_SVT_FLOAT:
        case D3D_SVT_MIN8FLOAT:
        case D3D_SVT_MIN10FLOAT:
        case D3D_SVT_MIN16FLOAT:
        case D3D_SVT_MIN12INT:
        case D3D_SVT_MIN16INT:
        case D3D_SVT_MIN16UINT:
        case D3D_SVT_INT16:
        case D3D_SVT_UINT16:
        case D3D_SVT_FLOAT16:
        case D3D_SVT_INT64:
        case D3D_SVT_UINT64:
        case D3D_SVT_UINT:
        case D3D_SVT_DOUBLE:
          break;

        default:
          throw std::invalid_argument("Type (scalar/matrix/vector) " +
                                      std::to_string(i) +
                                      " is of invalid type");
        }

        break;

    case D3D_SVC_STRUCT:

        if (!type.GetMemberCount())
          throw std::invalid_argument("Type (struct) " + std::to_string(i) +
                                      " is missing children");
        if (type.Type)
          throw std::invalid_argument("Type (struct) " +
                                      std::to_string(i) +
                                      " shouldn't have rows or columns");

        if (type.Rows || type.Columns)
          throw std::invalid_argument("Type (struct) " +
                                      std::to_string(i) +
                                      " shouldn't have rows or columns");

        break;

    case D3D_SVC_OBJECT:

        switch (type.Type) {
            
        case D3D_SVT_STRING:
        case D3D_SVT_TEXTURE1D:
        case D3D_SVT_TEXTURE2D:
        case D3D_SVT_TEXTURE3D:
        case D3D_SVT_TEXTURECUBE:
        case D3D_SVT_SAMPLER:
        case D3D_SVT_BUFFER:
        case D3D_SVT_CBUFFER:
        case D3D_SVT_TBUFFER:
        case D3D_SVT_TEXTURE1DARRAY:
        case D3D_SVT_TEXTURE2DARRAY:
        case D3D_SVT_TEXTURE2DMS:
        case D3D_SVT_TEXTURE2DMSARRAY:
        case D3D_SVT_TEXTURECUBEARRAY:
        case D3D_SVT_RWTEXTURE1D:
        case D3D_SVT_RWTEXTURE1DARRAY:
        case D3D_SVT_RWTEXTURE2D:
        case D3D_SVT_RWTEXTURE2DARRAY:
        case D3D_SVT_RWTEXTURE3D:
        case D3D_SVT_RWBUFFER:
        case D3D_SVT_BYTEADDRESS_BUFFER:
        case D3D_SVT_RWBYTEADDRESS_BUFFER:
        case D3D_SVT_STRUCTURED_BUFFER:
        case D3D_SVT_RWSTRUCTURED_BUFFER:
        case D3D_SVT_APPEND_STRUCTURED_BUFFER:
        case D3D_SVT_CONSUME_STRUCTURED_BUFFER:
          break;

        default:
          throw std::invalid_argument("Type (object) " + std::to_string(i) +
                                      " is of invalid type");
        }

        if (type.Rows || type.Columns)
          throw std::invalid_argument("Type (object) " +
                                      std::to_string(i) +
                                      " shouldn't have rows or columns");

      break;

    default:
      throw std::invalid_argument("Type " + std::to_string(i) +
                                  " has an invalid class");
    }
  }

  if (MakeNameLookupTable)
    GenerateNameLookupTable();
};


void DxcHLSLReflection::Printf() const { RecursePrint(*this, 0, 0, 0); }

}
