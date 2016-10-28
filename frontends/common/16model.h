#ifndef P4_FRONTENDS_16_MODEL_H
#define P4_FRONTENDS_16_MODEL_H

#include "model.h"

namespace Model {

namespace P4_16 {

// Block has a name, type, and a collection of elements
template<typename T>
class Block_Model : Type_Model {
  std::vector<T> elems;
 public:
  Block_Model(cstring name) : Type_Model(name) {
    elems = new std::vector<T>();
  }
  void add(T elem) { this->elems.emplace(elem); }
  T get(int index) { return this->elems[index]; }
};

class Enum_Model : public Block_Model<Elem> {
  Type_Model type;
 public:
  explicit Enum_Model(cstring name) : Type_Model(name), type("Enum") {}
};

typedef ParameterizedBlock Block_Model<Param_Model>

class Method_Model : public ParameterizedBlock { 
  Type_Model type;
 public:
  explicit Method_Model(cstring name) :
      ParameterizedBlock(name), type("Method") {}
};

class Parser_Model : public ParameterizedBlock { 
  Type_Model type;
 public:
  explicit Parser_Model(cstring name) :
      ParameterizedBlock(name), type("Parser") {}
};

class Deparser_Model : public ParameterizedBlock { 
  Type_Model type;
 public:
  explicit Deparser_Model(cstring name) :
      ParameterizedBlock(name), type("Deparser") {}
};

class Control_Model : public ParameterizedBlock { 
  Type_Model type;
 public:
  explicit Control_Model(cstring name) :
      ParameterizedBlock(name), type("Control") {}
};

class Extern_Model : public Block_Model<Method> {
  Type_Model type;
 public:
  explicit Extern_Model(cstring name) :
      Block_Model<Method>(name), type("Extern") {}
};

class 16_Model : public Model {
  public:
    std::vector<Parser_Model> parsers;
    std::vector<Deparser_Model> deparsers;
    std::vector<Control_Model> controls;
    std::vector<Extern_Model> externs;

    static 16Model instance;
};

} // namespace P4_16 
} // namespace Model

#endif // P4_FRONTENDS_16_MODEL_H
