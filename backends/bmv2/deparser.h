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

#ifndef _BACKENDS_BMV2_CONVERTDEPARSER_H_
#define _BACKENDS_BMV2_CONVERTDEPARSER_H_

#include "ir/ir.h"
#include "lib/json.h"
#include "frontends/p4/typeMap.h"
#include "frontends/common/resolveReferences/referenceMap.h"
#include "expression.h"
#include "backend.h"

namespace BMV2 {

class DoDeparserBlockConversion : public Inspector {
    Backend*    backend;

 protected:
    Util::IJson* convertDeparser(const IR::P4Control* ctrl);
    void convertDeparserBody(const IR::Vector<IR::StatOrDecl>* body, Util::JsonArray* result);
 public:
    bool preorder(const IR::PackageBlock* block);
    bool preorder(const IR::ControlBlock* ctrl);

    explicit DoDeparserBlockConversion(Backend* backend) :
        backend(backend) {}
};

class ConvertDeparser final : public PassManager {
 public:
    ConvertDeparser(Backend* backend) {
        passes.push_back(new DoDeparserBlockConversion(backend));
        setName("ConvertDeparser");
    }
};

}  // namespace BMV2

#endif