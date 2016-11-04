#ifndef _P4_INFER_ARCHITECTURE_H_
#define _P4_INFER_ARCHITECTURE_H_

#include "ir/ir.h"
#include "ir/visitor.h"
#include "common/16model.h"
#include "typeMap.h"

namespace P4 {

// Saves achitecture description in cpp datastructure

class InferArchitecture : public Inspector {
 private:
  ::P4_16::V2Model *archModel;
  TypeMap *typeMap;

 public:
  InferArchitecture(TypeMap *tm) {
    this->archModel = new ::P4_16::V2Model();
    this->typeMap = tm;
  }

  // Control bock will need to "infer" if it is a deparser
  bool preorder(const IR::Type_Control *node) override;
  bool preorder(const IR::Type_Parser *node) override;
  bool preorder(const IR::Type_Extern *node) override;
  bool preorder(const IR::Type_Package *node) override;

  // Needed just to start the traversal
  bool preorder(const IR::P4Program* program) override;

  // don't care
  bool preorder(const IR::Node *node) override;

};

} // namespace P4

#endif
