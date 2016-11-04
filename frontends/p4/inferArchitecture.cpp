#include "inferArchitecture.h"

namespace P4 {

bool InferArchitecture::preorder(const IR::P4Program* program) {
  for (auto decl : *program->declarations) {
    if (decl->is<IR::Type_Package>() || decl->is<IR::Type_Extern>()) {
      visit(decl);
    }
  }
  return false;
}

bool InferArchitecture::preorder(const IR::Type_Control *node) {
  ::P4_16::Control_Model *control_m = this->archModel->controls->back();
  uint32_t paramCounter = 0;
  for (auto param : *node->applyParams->parameters) {
    ::Model::Type_Model paramTypeModel(param->type->toString());
    ::Model::Param_Model newParam(param->toString(), paramTypeModel, paramCounter++);
    control_m->add(newParam);
  }
  return false;
}

bool InferArchitecture::preorder(const IR::Type_Parser *node) {
  ::P4_16::Parser_Model *parser_m = this->archModel->parsers->back();
  uint32_t paramCounter = 0;
  for (auto param : *node->applyParams->getEnumerator()) {
    ::Model::Type_Model paramTypeModel(param->type->toString());
    ::Model::Param_Model newParam(param->toString(), paramTypeModel, paramCounter++);
    parser_m->add(newParam);
  }
  return false;
}

bool InferArchitecture::preorder(const IR::Type_Package *node) {
  for (auto param : *node->getConstructorParameters()->parameters) {
    auto mappedType = this->typeMap->getType(param->type);
    BUG_CHECK(mappedType->is<IR::Type_SpecializedCanonical>(), "Unexpected Package param type");
    auto baseType = mappedType->to<IR::Type_SpecializedCanonical>()->baseType;
    if (baseType->is<IR::Type_Parser>()) {
      this->archModel->parsers->push_back(new ::P4_16::Parser_Model(param->toString()));
      visit(baseType->to<IR::Type_Parser>());
    } else if (baseType->is<IR::Type_Control>()) {
      // do deparser logic
      this->archModel->controls->push_back(new ::P4_16::Control_Model(param->toString()));
      visit(baseType->to<IR::Type_Control>());
    }
  }
  return false;
}

bool InferArchitecture::preorder(const IR::Type_Extern *node) {
  ::P4_16::Extern_Model *extern_m = new ::P4_16::Extern_Model(node->toString());
  for (auto method : *node->methods) {
    uint32_t paramCounter = 0;
    ::P4_16::Method_Model method_m(method->toString());
    for (auto param : *method->type->parameters->getEnumerator()) {
      ::Model::Type_Model paramTypeModel(param->type->toString());
      ::Model::Param_Model newParam(param->toString(), paramTypeModel, paramCounter++);
      method_m.add(newParam);
    }
    extern_m->add(method_m);
  }
  return false;
}

bool InferArchitecture::preorder(const IR::Node *node) {
  return false;
}

} // namespace P4
