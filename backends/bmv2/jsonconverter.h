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

#ifndef _BACKENDS_BMV2_JSONCONVERTER_H_
#define _BACKENDS_BMV2_JSONCONVERTER_H_

#include "lib/json.h"
#include "frontends/common/options.h"
#include "frontends/common/16model.h"
#include "frontends/p4/coreLibrary.h"
#include "midend/convertEnums.h"
#include "analyzer.h"
#include <iomanip>
#include <initializer_list>
// Currently we are requiring a v1model to be used

// This is based on the specification of the BMv2 JSON input format
// https://github.com/p4lang/behavioral-model/blob/master/docs/JSON_format.md

namespace BMV2 {

class BMV2_Model : public ::P4_16::V2Model {
 private:
    struct TableAttributes_Model {
        TableAttributes_Model() : 
                tableImplementation("implementation"),
                directCounter("counters"),
                directMeter("meters"), size("size"),
                supportTimeout("support_timeout") {}
        ::Model::Elem tableImplementation;
        ::Model::Elem directCounter;
        ::Model::Elem directMeter;
        ::Model::Elem size;
        ::Model::Elem supportTimeout;
        const unsigned defaultTableSize = 1024;
    };
    
    struct TableImplementation_Model {
        TableImplementation_Model() :
                actionProfile("action_profile"),
                actionSelector("action_selector") {}
        ::Model::Elem actionProfile;
        ::Model::Elem actionSelector;
    };

 public:
    BMV2_Model(const ::P4_16::V2Model &model)
            : ::P4_16::V2Model(model), tableAttributes(),
              tableImplementations(), selectorMatchType("selector"),
              rangeMatchType("range") { }

    ::Model::Elem             selectorMatchType;
    ::Model::Elem             rangeMatchType;
    TableAttributes_Model     tableAttributes;
    TableImplementation_Model tableImplementations;
};

class ExpressionConverter;

class JsonConverter final {
 public:
    const CompilerOptions& options;
    Util::JsonObject       toplevel;  // output is constructed here

    P4::P4CoreLibrary&     corelib;
    BMV2_Model             model;
    P4::ReferenceMap*      refMap;
    P4::TypeMap*           typeMap;
    ProgramParts           structure;
    cstring                scalarsName;  // name of struct in JSON holding all scalars
    const IR::ToplevelBlock* toplevelBlock;
    const IR::Declaration_Instance *mainInstance{nullptr};

    // this stores the enclosing IApply when converting expressions
    // -- for reference when looking up parameter bindings
    const IR::IApply *enclosingBlock{nullptr};

    ExpressionConverter*   conv;

    const unsigned         boolWidth = 1;
    // We place scalar user metadata fields (i.e., bit<>, bool)
    // in the "scalars" metadata object, so we may need to rename
    // these fields.  This map holds the new names.
    std::map<const IR::StructField*, cstring> scalarMetadataFields;
    // we map error codes to numerical values for bmv2
    using ErrorValue = unsigned int;
    using ErrorCodesMap = std::unordered_map<const IR::IDeclaration *, ErrorValue>;
    ErrorCodesMap errorCodesMap{};
    P4::ConvertEnums::EnumMapping* enumMap;

 private:
    Util::JsonArray *headerTypes;
    std::map<cstring, cstring> headerTypesCreated;
    std::map<cstring, unsigned> headerInstancesCreated;
    Util::JsonArray *headerInstances;
    Util::JsonArray *headerStacks;
    Util::JsonArray *field_lists;
    Util::JsonArray *actions;

    Util::JsonObject *scalarsStruct;
    unsigned scalars_width = 0;
    friend class ExpressionConverter;

 private:
    void padScalars();

 protected:
    void pushFields(const IR::Type_StructLike *st, Util::JsonArray *fields);
    unsigned createHeaderTypeAndInstance(cstring prefix, cstring varName,
                                         const IR::Type_StructLike *type);
    void createNestedStruct(cstring prefix, cstring varName,
                           const IR::Type_StructLike *type, bool usePrefix);
    void createStack(cstring prefix, cstring varName, const IR::Type_Stack *stack);
    unsigned nextId(cstring group);
    void addLocals();
    void convertActionBody(const IR::Vector<IR::StatOrDecl>* body,
                           Util::JsonArray* result);
    Util::IJson* convertTable(const CFG::TableNode* node,
                              Util::JsonArray* counters,
                              Util::JsonArray* action_profiles);
    Util::IJson* convertIf(const CFG::IfNode* node, cstring parent);
    unsigned createAction(const IR::P4Action *action);
    Util::IJson* toJson(const IR::P4Parser* cont, cstring name);
    Util::IJson* toJson(const IR::ParserState* state);
    void convertDeparserBody(const IR::Vector<IR::StatOrDecl>* body,
                             Util::JsonArray* result);
    Util::IJson* convertDeparser(const IR::P4Control* state);
    Util::IJson* convertParserStatement(const IR::StatOrDecl* stat);
    Util::JsonObject *createExternInstance(cstring name, cstring type);
    void addExternAttributes(const IR::Declaration_Instance *di,
                             const IR::ExternBlock *block,
                             Util::JsonArray *attributes);
    Util::IJson* convertControl(const IR::P4Control *cont, cstring name,
                                Util::JsonArray* counters, Util::JsonArray* meters,
                                Util::JsonArray *externs);
    cstring createCalculation(cstring algo, const IR::Expression* fields,
                              Util::JsonArray* calculations);
    Util::IJson* nodeName(const CFG::Node* node) const;
    cstring convertHashAlgorithm(cstring algorithm) const;
    // Return 'true' if the table is 'simple'
    bool handleTableImplementation(const IR::Property* implementation,
                                   const IR::Key* key,
                                   Util::JsonObject* table,
                                   Util::JsonArray* action_profiles);
    void addToFieldList(const IR::Expression* expr, Util::JsonArray* fl);
    // returns id of created field list
    int createFieldList(const IR::Expression* expr, cstring group,
                        cstring listName, Util::JsonArray* fieldLists);

    // Operates on a select keyset
    void convertSimpleKey(const IR::Expression* keySet,
                          mpz_class& value, mpz_class& mask) const;
    unsigned combine(const IR::Expression* keySet,
                     const IR::ListExpression* select,
                     mpz_class& value, mpz_class& mask) const;
    void buildCfg(IR::P4Control* cont);

    // Adds meta information (such as version) to the json
    void addMetaInformation();

    // Adds declared errors to json
    void addErrors();
    // Retrieve assigned numerical value for given error constant
    ErrorValue retrieveErrorValue(const IR::Member* mem) const;

    // Adds declared enums to json
    void addEnums();

    // Check structure composition
    bool hasStructLikeMembers(const IR::Type_StructLike *st);
    bool hasBitMembers(const IR::Type_StructLike *st);
    void checkStructure(const IR::Type_StructLike *st);

    // resolve control/parser params to package locals
    const IR::IDeclaration *resolveParameter(const IR::Parameter *param);

    const IR::InstantiatedBlock *getInstantiatedBlock(cstring name);

    cstring buildQualifiedName(std::vector<cstring> nodes, cstring delim = ".") {
        if (refMap->isV1()) { // name annotations provide unique names in V1
            return *(--(nodes.end()));
        }
        return cstring::join(nodes.begin(), nodes.end(), ".");
    }

 public:
    explicit JsonConverter(const CompilerOptions& options);
    void convert(P4::ReferenceMap* refMap, P4::TypeMap* typeMap,
                 const IR::ToplevelBlock *toplevel,
                 P4::ConvertEnums::EnumMapping* enumMap);
    void serialize(std::ostream& out) const { toplevel.serialize(out); }
};

}  // namespace BMV2

#endif /* _BACKENDS_BMV2_JSONCONVERTER_H_ */
