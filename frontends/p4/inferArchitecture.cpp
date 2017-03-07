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

#include "inferArchitecture.h"

namespace P4 {

using ::P4_16::Parser_Model;
using ::P4_16::Control_Model;
using ::P4_16::Method_Model;
using ::P4_16::Extern_Model;

using ::Model::Type_Model;
using ::Model::Param_Model;

ArchitecturalBlocks *ArchitecturalBlocks::instance = nullptr;

bool ArchitecturalBlocks::preorder(const IR::P4Program* program) {
    for (auto decl : *program->declarations) {
        if (decl->is<IR::Type_Package>() || decl->is<IR::Type_Extern>()) {
            visit(decl);
        }
    }
    return false;
}

bool ArchitecturalBlocks::preorder(const IR::Type_Control *node) {
    Control_Model *control_m = archModel->getControls()->back();
    uint32_t paramCounter = 0;
    for (auto param : *node->applyParams->parameters) {
        Type_Model paramTypeModel(param->type->toString());
        Param_Model newParam(param->toString(), paramTypeModel, paramCounter++);
        control_m->add(newParam);
    }
    return false;
}

bool ArchitecturalBlocks::preorder(const IR::Type_Parser *node) {
    Parser_Model *parser_m = archModel->getParsers()->back();
    uint32_t paramCounter = 0;
    for (auto param : *node->applyParams->getEnumerator()) {
        Type_Model paramTypeModel(param->type->toString());
        Param_Model newParam(param->toString(), paramTypeModel, paramCounter++);
        parser_m->add(newParam);
    }
    return false;
}

bool ArchitecturalBlocks::preorder(const IR::Type_Package *node) {
    // TODO(pierce): shouldn't these be constructor parameters?
    for (auto param : *node->applyParams->parameters) {
        BUG_CHECK(param->type->is<IR::Type_Specialized>(),
                  "Unexpected Package param type");
        auto baseType = param->type->to<IR::Type_Specialized>()->baseType;
        auto typeObj = typeMap->getType(baseType)->getP4Type();
        if (typeObj->is<IR::Type_Parser>()) {
            archModel->addParser(new Parser_Model(param->toString()));
            visit(typeObj->to<IR::Type_Parser>());
        } else if (typeObj->is<IR::Type_Control>()) {
            archModel->addControl(new Control_Model(param->toString()));
            visit(typeObj->to<IR::Type_Control>());
        }
    }
    return false;
}

bool ArchitecturalBlocks::preorder(const IR::Type_Extern *node) {
    Extern_Model *extern_m = new Extern_Model(node->toString());
    for (auto method : *node->methods) {
        uint32_t paramCounter = 0;
        Method_Model method_m(method->toString());
        for (auto param : *method->type->parameters->getEnumerator()) {
            Type_Model paramTypeModel(param->type->toString());
            Param_Model newParam(param->toString(), paramTypeModel,
                                 paramCounter++);
            method_m.add(newParam);
        }
        extern_m->add(method_m);
    }
    archModel->addExtern(extern_m);
    return false;
}

bool ArchitecturalBlocks::preorder(const IR::Node *node) {
    return false;
}

///////////////////////////////////////////////////////////////////////////////

ResolveToPackageObjects *ResolveToPackageObjects::instance = nullptr;

bool ResolveToPackageObjects::preorder(const IR::Type_Specialized *ts) {
//    printf("Type_Specialized found: %s\n", ts->toString());
    return true;
}

//bool ResolveToPackageObjects::preorder(const IR::P4Package *package) {
//    return true;
//}

//bool ResolveToPackageObjects::preorder(const IR::P4Control *package) {
//}
//
//bool ResolveToPackageObjects::preorder(const IR::P4Parser *parser) {
//}

} // namespace P4
