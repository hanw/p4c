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

#ifndef _BACKENDS_BMV2_CONVERTPARSER_H_
#define _BACKENDS_BMV2_CONVERTPARSER_H_

#include "ir/ir.h"
#include "lib/json.h"
#include "frontends/p4/typeMap.h"
#include "frontends/common/resolveReferences/referenceMap.h"
#include "expression.h"

namespace BMV2 {

class Parser : public Inspector {
    Backend* backend;
    //P4::ReferenceMap*    refMap;
    //P4::TypeMap*         typeMap;
    //ExpressionConverter* conv;
    //Util::JsonArray*     parsers;
    P4::P4CoreLibrary&   corelib;
    std::map<const IR::P4Parser*, Util::IJson*> parser_map;
    std::map<const IR::ParserState*, Util::IJson*> state_map;
    std::vector<Util::IJson*> context;
 protected:
    void convertSimpleKey(const IR::Expression* keySet, mpz_class& value, mpz_class& mask) const;
    unsigned combine(const IR::Expression* keySet, const IR::ListExpression* select,
                     mpz_class& value, mpz_class& mask) const;
    Util::IJson* stateName(IR::ID state);
    Util::IJson* toJson(const IR::P4Parser* cont);
    Util::IJson* toJson(const IR::ParserState* state);
    Util::IJson* convertParserStatement(const IR::StatOrDecl* stat);

 public:
    bool preorder(const IR::P4Parser* p) override;
    bool preorder(const IR::PackageBlock* b) override;
    //explicit Parser(P4::ReferenceMap* refMap, P4::TypeMap* typeMap,
    //                ExpressionConverter* conv, Util::JsonArray* parsers) :
    //refMap(refMap), typeMap(typeMap), conv(conv), parsers(parsers),
    explicit Parser(Backend* backend) : backend(backend),
    corelib(P4::P4CoreLibrary::instance) {}
};

class ConvertParser final : public PassManager {
 public:
    ConvertParser(Backend* backend) {
    //ConvertParser(P4::ReferenceMap* refMap, P4::TypeMap* typeMap,
    //              ExpressionConverter* conv,
    //              Util::JsonArray* parsers) {
    //    passes.push_back(new Parser(refMap, typeMap, conv, parsers));
        passes.push_back(new Parser(backend));
        setName("ConvertParser");
    }
};

}  // namespace BMV2

#endif
