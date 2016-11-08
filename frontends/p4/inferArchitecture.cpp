#include "inferArchitecture.h"

namespace P4 {

using ::P4_16::Parser_Model;
using ::P4_16::Control_Model;
using ::P4_16::Method_Model;
using ::P4_16::Extern_Model;

using ::Model::Type_Model;
using ::Model::Param_Model;

bool InferArchitecture::preorder(const IR::P4Program* program) {
  for (auto decl : *program->declarations) {
    if (decl->is<IR::Type_Package>() || decl->is<IR::Type_Extern>()) {
      visit(decl);
    }
  }
  return false;
}

bool InferArchitecture::preorder(const IR::Type_Control *node) {
  Control_Model *control_m = this->archModel->controls->back();
  uint32_t paramCounter = 0;
  for (auto param : *node->applyParams->parameters) {
    Type_Model paramTypeModel(param->type->toString());
    Param_Model newParam(param->toString(), paramTypeModel, paramCounter++);
    control_m->add(newParam);
  }
  return false;
}

bool InferArchitecture::preorder(const IR::Type_Parser *node) {
  Parser_Model *parser_m = this->archModel->parsers->back();
  uint32_t paramCounter = 0;
  for (auto param : *node->applyParams->getEnumerator()) {
    Type_Model paramTypeModel(param->type->toString());
    Param_Model newParam(param->toString(), paramTypeModel, paramCounter++);
    parser_m->add(newParam);
  }
  return false;
}

bool InferArchitecture::preorder(const IR::Type_Package *node) {
  for (auto param : *node->getConstructorParameters()->parameters) {
    BUG_CHECK(param->type->is<IR::Type_Specialized>(),
              "Unexpected Package param type");
    auto baseType = param->type->to<IR::Type_Specialized>()->baseType;
    auto typeObj = this->typeMap->getType(baseType)->getP4Type();
    if (typeObj->is<IR::Type_Parser>()) {
      this->archModel->parsers->push_back(new Parser_Model(param->toString()));
      visit(typeObj->to<IR::Type_Parser>());
    } else if (typeObj->is<IR::Type_Control>()) {
      // do deparser logic
      this->archModel->controls->push_back(new Control_Model(param->toString()));
      visit(typeObj->to<IR::Type_Control>());
    }
  }
  return false;
}

bool InferArchitecture::preorder(const IR::Type_Extern *node) {
  Extern_Model *extern_m = new Extern_Model(node->toString());
  for (auto method : *node->methods) {
    uint32_t paramCounter = 0;
    Method_Model method_m(method->toString());
    for (auto param : *method->type->parameters->getEnumerator()) {
      Type_Model paramTypeModel(param->type->toString());
      Param_Model newParam(param->toString(), paramTypeModel, paramCounter++);
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
