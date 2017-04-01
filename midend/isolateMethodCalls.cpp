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

#include "frontends/p4/methodInstance.h"
#include "midend/isolateMethodCalls.h"

namespace P4 {

bool ConvertToVoid::isBuiltInMethod(const IR::MethodCallExpression *mce) {
    auto method = MethodInstance::resolve(mce, refMap, typeMap); 
    return method->is<P4::BuiltInMethod>();
}

bool ConvertToVoid::isInCoreLib(const IR::Type_Extern *ex) {
    return ex->getName() == corelib.packetOut.name
           || ex->getName() == corelib.packetIn.name;
}

const IR::Node *ConvertToVoid::postorder(IR::AssignmentStatement *as) {
    auto mce = as->right->to<IR::MethodCallExpression>();
    if (!mce || isBuiltInMethod(mce)) return as;

    // skipping the ones in core lib
    auto member = mce->method->to<IR::Member>();
    auto memberType = typeMap->getType(member->expr)->to<IR::Type_Extern>();
    if (isInCoreLib(memberType)) return as;

    auto newArgs = new IR::Vector<IR::Expression>();
    newArgs->push_back(as->left);
    for (auto a : *mce->arguments) {
        newArgs->push_back(a);
    }
    auto new_mce = new IR::MethodCallExpression(mce->type, mce->method, mce->typeArguments, newArgs);
    auto mcs = new IR::MethodCallStatement(new_mce);
    return mcs;
}

const IR::Node *ConvertToVoid::postorder(IR::Method *method) {
    auto rt = method->type->returnType;
    if (rt == nullptr || rt->is<IR::Type_Void>()) return method;
    auto newParams = new IR::IndexedVector<IR::Parameter>();
    newParams->push_back(
        new IR::Parameter(IR::ID("return"), IR::Direction::Out, rt));
    for (auto param : *method->type->parameters->parameters) {
        newParams->push_back(param);
    }
    auto newPL = new IR::ParameterList(newParams);
    auto newRT = new IR::Type_Void();
    auto newMT = new IR::Type_Method(method->type->typeParameters, newRT, newPL);
    return new IR::Method(method->name, newMT, method->isAbstract);
}

// TODO(pierce): is this necessary?
const IR::Node *ConvertToVoid::preorder(IR::Type_Extern *ex) {
    if (isInCoreLib(ex)) prune();
    return ex;
}

} // namespace P4
