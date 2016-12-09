#ifndef P4_FRONTENDS_16_MODEL_H
#define P4_FRONTENDS_16_MODEL_H

#include "model.h"

namespace P4_16 {

using ::Model::Elem;
using ::Model::Type_Model;
using ::Model::Param_Model;

// Block has a name and a collection of elements
template<typename T>
class Block_Model : public Type_Model {
    std::vector<T> elems;
 public:
    Block_Model(cstring name) : ::Model::Type_Model(name) {}
    void add(T elem) { this->elems.push_back(elem); }
    T get(int index) { return this->elems[index]; }
    std::vector<T> *getElems() { return &elems; }
};

class Enum_Model : public Block_Model<Elem> {
    ::Model::Type_Model type;
 public:
    explicit Enum_Model(cstring name) : Block_Model(name), type("Enum") {}
};

typedef Block_Model<Param_Model> ParameterizedBlock;

class Method_Model : public ParameterizedBlock { 
    ::Model::Type_Model type;
 public:
    explicit Method_Model(cstring name) :
        ParameterizedBlock(name), type("Method") {}
};

class Parser_Model : public ParameterizedBlock { 
    ::Model::Type_Model type;
 public:
    explicit Parser_Model(cstring name) :
        ParameterizedBlock(name), type("Parser") {}
};

class Control_Model : public ParameterizedBlock { 
    ::Model::Type_Model type;
 public:
    explicit Control_Model(cstring name) :
        ParameterizedBlock(name), type("Control") {}
};

class Extern_Model : public Block_Model<Method_Model> {
    ::Model::Type_Model type;
 public:
    explicit Extern_Model(cstring name) :
        Block_Model<Method_Model>(name), type("Extern") {}
};


class V2Model : public ::Model::Model {
 public:
    V2Model() : ::Model::Model("0.2") {
        this->parsers = new std::vector<Parser_Model*>();
        this->controls = new std::vector<Control_Model*>();
        this->externs = new std::vector<Extern_Model*>();
  }

    std::vector<Parser_Model*> *parsers;
    std::vector<Control_Model*> *controls;
    std::vector<Extern_Model*> *externs;
};

} // namespace P4_16

#endif // P4_FRONTENDS_16_MODEL_H
