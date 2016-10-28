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

#ifndef P4C_FRONTENDS_COMMON_MODEL_H_
#define P4C_FRONTENDS_COMMON_MODEL_H_

#include "lib/cstring.h"
#include "ir/id.h"

// Classes for representing various P4 program models inside the compiler

namespace Model {

// Model element
class Elem {
  cstring name;
 public:
  explicit Elem(cstring name) : name(name) {}
  Elem() = delete;

  IR::ID Id() const { return IR::ID(name); }
  const char* str() const { return name.c_str(); }
  cstring toString() const { return name; }
};

class Type_Model : public Elem {
 public:
  explicit Type_Model(cstring name) : Elem(name) {}
};

class Param_Model : public Elem {
  Type_Model type;
  unsigned   index;
  Param(cstring name, Type_Model type, unsigned index) :
      Elem(name), type(type), index(index) {}
 public:
  Type_Model getType() { return type; }
};

class Model {
 public:
  cstring version;
  explicit Model(cstring version) : version(version) {}
};

}  // namespace Model

#endif /* P4C_FRONTENDS_COMMON_MODEL_H_ */
