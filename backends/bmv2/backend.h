/*
Copyright 2013-present Barefoot Networks, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef _BACKENDS_BMV2_BACKEND_H_
#define _BACKENDS_BMV2_BACKEND_H_

#include "analyzer.h"
#include "expression.h"
#include "frontends/common/model.h"
#include "frontends/p4/coreLibrary.h"
#include "frontends/p4/fromv1.0/v1model.h"
#include "helpers.h"
#include "ir/ir.h"
#include "lib/error.h"
#include "lib/exceptions.h"
#include "lib/gc.h"
#include "lib/json.h"
#include "lib/log.h"
#include "lib/nullstream.h"
#include "metermap.h"
#include "midend/convertEnums.h"
#include "copyAnnotations.h"

namespace BMV2 {

class Backend : public PassManager {
    using DirectCounterMap = std::map<cstring, const IR::P4Table*>;

    const IR::ToplevelBlock*         tlb;
    ExpressionConverter*             conv;
    P4::ConvertEnums::EnumMapping*   enumMap;
    P4::P4CoreLibrary&               corelib;
    P4::ReferenceMap                 refMap;
    P4::TypeMap                      typeMap;
    ProgramParts                     structure;
    Util::JsonObject                 toplevel;
    P4::V2Model&                     model;
    P4V1::V1Model&                   v1model;
    DirectCounterMap                 directCounterMap;
    DirectMeterMap                   meterMap;
    ErrorCodesMap                    errorCodesMap;

 public:
    Util::JsonArray*                 meta;
    Util::JsonArray*                 actions;
    Util::JsonArray*                 calculations;
    Util::JsonArray*                 checksums;
    Util::JsonArray*                 counters;
    Util::JsonArray*                 deparsers;
    Util::JsonArray*                 enums;
    Util::JsonArray*                 errors;
    Util::JsonArray*                 externs;
    Util::JsonArray*                 field_lists;
    Util::JsonArray*                 headerInstances;
    Util::JsonArray*                 headerStacks;
    Util::JsonArray*                 headerTypes;
    Util::JsonArray*                 learn_lists;
    Util::JsonArray*                 meter_arrays;
    Util::JsonArray*                 parsers;
    Util::JsonArray*                 pipelines;
    Util::JsonArray*                 register_arrays;
    Util::JsonArray*                 field_aliases;

    Util::JsonObject*                scalarsStruct;
    Util::JsonArray*                 scalarFields;
    const unsigned                   boolWidth = 1;
    unsigned                         scalars_width = 0;
    cstring                          scalarsName;
    const IR::Parameter*             userMetadataParameter;
    const IR::Parameter*             stdMetadataParameter;

    // We place scalar user metadata fields (i.e., bit<>, bool)
    // in the "scalars" metadata object, so we may need to rename
    // these fields.  This map holds the new names.
    std::map<const IR::StructField*, cstring> scalarMetadataFields;
    std::set<const IR::Type_StructLike*> headerTypesCreated;
    std::set<const IR::Type*> headerInstancesCreated;

    /// map from block to its type as defined in architecture file
    BlockTypeMap                     blockTypeMap;
    void pushFields(const IR::Type_StructLike *st, Util::JsonArray *fields);
    void createJsonType(const IR::Type_StructLike* st);

 protected:
    ErrorValue retrieveErrorValue(const IR::Member* mem) const;
    void addEnums(Util::JsonArray* enums);
    void addErrors(Util::JsonArray* errors);
    void addLocals();
    void addMetaInformation();
    void convertActionBody(const IR::Vector<IR::StatOrDecl>* body, Util::JsonArray* result);
    void createActions(Util::JsonArray* actions);
    void createScalars();
    void createMetadata();
    void createFieldAliases(const char *remapFile);
    void genExternMethod(Util::JsonArray* result, P4::ExternMethod *em);
    void padScalars();

 public:
    explicit Backend(P4::ConvertEnums::EnumMapping* enumMap) :
        enumMap(enumMap), corelib(P4::P4CoreLibrary::instance),
        model(P4::V2Model::instance), v1model(P4V1::V1Model::instance)
    {}
    void process(const IR::ToplevelBlock* block);
    void convert(const IR::ToplevelBlock* block, CompilerOptions& options);
    void serialize(std::ostream& out) const
    { toplevel.serialize(out); }
    P4::P4CoreLibrary &   getCoreLibrary() const   { return corelib; }
    ErrorCodesMap &       getErrorCodesMap()       { return errorCodesMap; }
    ExpressionConverter * getExpressionConverter() { return conv; };
    DirectCounterMap &    getDirectCounterMap()    { return directCounterMap; }
    DirectMeterMap &      getMeterMap()  { return meterMap; }
    P4::V2Model &         getModel()     { return model; }
    P4::ReferenceMap &    getRefMap()    { return refMap; }
    ProgramParts &        getStructure() { return structure; }
    P4::TypeMap &         getTypeMap()   { return typeMap; }
    const IR::ToplevelBlock* getToplevelBlock() { return tlb; }
};

}  // namespace BMV2

#endif /* _BACKENDS_BMV2_BACKEND_H_ */