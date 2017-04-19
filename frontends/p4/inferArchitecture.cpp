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
#include "methodInstance.h"

namespace P4 {

using ::Model::Type_Model;
using ::Model::Param_Model;

bool InferArchitecture::preorder(const IR::P4Program* program) {
    for (auto decl : *program->declarations) {
        if (decl->is<IR::Type_Package>() || decl->is<IR::Type_Extern>()) {
            LOG1("[ " << decl << " ]");
            visit(decl);
        }
    }
    return false;
}

/// initialize control_model parameter
bool InferArchitecture::preorder(const IR::Type_Control *node) {
    Control_Model *control_m = v2model.getControls().back();
    uint32_t paramCounter = 0;
    for (auto param : *node->applyParams->parameters) {
        Type_Model paramTypeModel(param->type->toString());
        Param_Model newParam(param->toString(), paramTypeModel, paramCounter++);
        control_m->add(newParam);
    }
    LOG1("...control [" << node << " ]");
    return false;
}

/// new Parser_Model object
/// add parameters
bool InferArchitecture::preorder(const IR::Type_Parser *node) {
    LOG1("...parser [" << node << " ]");
    // FIXME: insert parser_param, then parser
    Parser_Model *parser_m = v2model.getParsers().back();
    uint32_t paramCounter = 0;
    for (auto param : *node->applyParams->getEnumerator()) {
        Type_Model paramTypeModel(param->type->toString());
        Param_Model newParam(param->toString(), paramTypeModel, paramCounter++);
        parser_m->add(newParam);
        LOG1("...... parser params [ " << param << " ]");
    }
    return false;
}

/// FIXME: going from param to p4type takes a lot of work, should be as simple as
/// p4type = p4Type(param);
/// if (p4type->is<IR::Type_Parser>) { visit(p4type); }
/// else if (p4type->is<IR::Type_Control>) { visit(p4type); }
bool InferArchitecture::preorder(const IR::Type_Package *node) {
    for (auto param : *node->constructorParams->parameters) {
        BUG_CHECK(param->type->is<IR::Type_Specialized>(),
                  "Unexpected Package param type");
        auto baseType = param->type->to<IR::Type_Specialized>()->baseType;
        auto typeObj = typeMap->getType(baseType)->getP4Type();
        if (typeObj->is<IR::Type_Parser>()) {
            v2model.addParser(new Parser_Model(param->toString()));
            visit(typeObj->to<IR::Type_Parser>());
        } else if (typeObj->is<IR::Type_Control>()) {
            v2model.addControl(new Control_Model(param->toString()));
            visit(typeObj->to<IR::Type_Control>());
        }
    }
    LOG1("...package [ " << node << " ]");
    return false;
}

/// create new Extern_Model object
/// add method and its parameters to extern.
bool InferArchitecture::preorder(const IR::Type_Extern *node) {
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
    v2model.addExtern(extern_m);
    LOG1("...extern [ " << node << " ]");
    return false;
}

} // namespace P4
