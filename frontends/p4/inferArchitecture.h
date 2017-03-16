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

#ifndef _P4_INFER_ARCHITECTURE_H_
#define _P4_INFER_ARCHITECTURE_H_

#include "ir/ir.h"
#include "ir/visitor.h"
#include "common/16model.h"
#include "typeMap.h"

namespace P4 {

// Saves achitecture description in cpp datastructure

class ArchitecturalBlocks : public Inspector {
 private:
    ::P4_16::V2Model *archModel;
    TypeMap *typeMap;
    static ArchitecturalBlocks *instance;

 public:
    ArchitecturalBlocks(TypeMap *typeMap)
        : typeMap(typeMap), archModel(new ::P4_16::V2Model()) {
        instance = this;
    }

    ::P4_16::V2Model *getModel() const { return archModel; }

    static const ArchitecturalBlocks *getInstance() { return instance; }

 public:
    bool preorder(const IR::Type_Control *node) override;
    bool preorder(const IR::Type_Parser *node) override;
    bool preorder(const IR::Type_Extern *node) override;
    bool preorder(const IR::Type_Package *node) override;

    bool preorder(const IR::P4Program* program) override;

    // don't care
    bool preorder(const IR::Node *node) override;
};

class ResolveToPackageObjects : public Inspector {
 private:
    using ParameterMap =
        std::unordered_map<const IR::Parameter*, const IR::IDeclaration*>;
 private:
    ParameterMap *parameterMap{nullptr};
    TypeMap *typeMap;
    ReferenceMap *refMap;
    static ResolveToPackageObjects *instance;

 private:
    void setParameterMapping(
            const IR::Parameter *param, const IR::IDeclaration *packageLocal) {
        CHECK_NULL(param); CHECK_NULL(packageLocal);
        parameterMap->emplace(param, packageLocal);
    }
    
    const IR::IDeclaration *getParameterMapping(const IR::Parameter *p) {
        auto ret = parameterMap->find(p);
        return nullptr;
    }

 public:
    ResolveToPackageObjects(TypeMap *tm, ReferenceMap *rm)
            : parameterMap(new ParameterMap()), typeMap(tm), refMap(rm) { }

    static const ResolveToPackageObjects *getInstance() {
        return instance;
    }

 public:
    bool preorder(const IR::MethodCallStatement *m) override;
    bool preorder(const IR::Type_Package *p) override;
    bool preorder(const IR::P4Program *p) override;
};

class InferArchitecture : public PassManager {
 public:
    InferArchitecture(TypeMap *typeMap, ReferenceMap *refMap) {
        passes.push_back(new ArchitecturalBlocks(typeMap));
        passes.push_back(new ResolveToPackageObjects(typeMap, refMap));
    }
};

} // namespace P4

#endif
