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

#include <algorithm>

#include "jsonconverter.h"
#include "lib/gmputil.h"
#include "frontends/p4/coreLibrary.h"
#include "ir/ir.h"
#include "frontends/p4/methodInstance.h"
#include "frontends/p4/enumInstance.h"
#include "analyzer.h"
#include "lower.h"

namespace BMV2 {

namespace {
// BMv2 does not do arithmetic operations according to the P4 v1.1 and later spec.
// So we have to do it in the compiler.
class ArithmeticFixup : public Transform {
    P4::TypeMap* typeMap;

 public:
    explicit ArithmeticFixup(P4::TypeMap* typeMap) : typeMap(typeMap)
    { CHECK_NULL(typeMap); }
    const IR::Expression* fix(const IR::Expression* expr, const IR::Type_Bits* type) {
        unsigned width = type->size;
        if (!type->isSigned) {
            auto mask = new IR::Constant(Util::SourceInfo(), type,
                                         Util::mask(width), 16);
            typeMap->setType(mask, type);
            auto result = new IR::BAnd(expr->srcInfo, expr, mask);
            typeMap->setType(result, type);
            return result;
        } else {
            auto result = new IR::IntMod(expr->srcInfo, expr, width);
            typeMap->setType(result, type);
            return result;
        }
        return expr;
    }

    const IR::Node* updateType(const IR::Expression* expression) {
        if (*expression != *getOriginal()) {
            auto type = typeMap->getType(getOriginal(), true);
            typeMap->setType(expression, type);
        }
        return expression;
    }

    const IR::Node* postorder(IR::Expression* expression) override {
        return updateType(expression);
    }
    const IR::Node* postorder(IR::Operation_Binary* expression) override {
        auto type = typeMap->getType(getOriginal(), true);
        if (expression->is<IR::BAnd>() || expression->is<IR::BOr>() ||
            expression->is<IR::BXor>())
            // no need to clamp these
            return updateType(expression);
        if (type->is<IR::Type_Bits>())
            return fix(expression, type->to<IR::Type_Bits>());
        return updateType(expression);
    }
    const IR::Node* postorder(IR::Neg* expression) override {
        auto type = typeMap->getType(getOriginal(), true);
        if (type->is<IR::Type_Bits>())
            return fix(expression, type->to<IR::Type_Bits>());
        return updateType(expression);
    }
    const IR::Node* postorder(IR::Cast* expression) override {
        auto type = typeMap->getType(getOriginal(), true);
        if (type->is<IR::Type_Bits>())
            return fix(expression, type->to<IR::Type_Bits>());
        return updateType(expression);
    }
};

class ErrorCodesVisitor : public Inspector {
    JsonConverter* converter;
 public:
    explicit ErrorCodesVisitor(JsonConverter* converter) : converter(converter)
    { CHECK_NULL(converter); }

    bool preorder(const IR::Type_Error* errors) override {
        auto &map = converter->errorCodesMap;
        for (auto m : *errors->getDeclarations()) {
            BUG_CHECK(map.find(m) == map.end(), "Duplicate error");
            map[m] = map.size();
        }
        return false;
    }
};

// This pass makes sure that when several match tables share a selector, they use the same input for
// the selection algorithm. This is because bmv2 considers that the selection key is part of the
// action_selector while v1model.p4 considers that it belongs to the table match key definition.
class SharedActionSelectorCheck : public Inspector {
    JsonConverter* converter;
    using Input = std::vector<const IR::Expression *>;
    std::map<const IR::Declaration_Instance *, Input> selector_input_map{};

  static bool checkSameKeyExpr(const IR::Expression* expr0, const IR::Expression* expr1) {
      if (expr0->node_type_name() != expr1->node_type_name())
          return false;
      if (auto pe0 = expr0->to<IR::PathExpression>()) {
          auto pe1 = expr1->to<IR::PathExpression>();
          return pe0->path->name == pe1->path->name &&
              pe0->path->absolute == pe1->path->absolute;
      } else if (auto mem0 = expr0->to<IR::Member>()) {
          auto mem1 = expr1->to<IR::Member>();
          return checkSameKeyExpr(mem0->expr, mem1->expr) && mem0->member == mem1->member;
      } else if (auto l0 = expr0->to<IR::Literal>()) {
          auto l1 = expr1->to<IR::Literal>();
          return *l0 == *l1;
      } else if (auto ai0 = expr0->to<IR::ArrayIndex>()) {
          auto ai1 = expr1->to<IR::ArrayIndex>();
          return checkSameKeyExpr(ai0->left, ai1->left) && checkSameKeyExpr(ai0->right, ai1->right);
      }
      return false;
  }

 public:
    explicit SharedActionSelectorCheck(JsonConverter* converter) : converter(converter)
    { CHECK_NULL(converter); }

    const Input &get_selector_input(const IR::Declaration_Instance* selector) const {
        return selector_input_map.at(selector);
    }

    bool preorder(const IR::P4Table* table) override {
        auto model = converter->model;
        auto refMap = converter->refMap;
        auto typeMap = converter->typeMap;

        auto implementation = table->properties->getProperty(
            model.tableAttributes.tableImplementation.name);
        if (implementation == nullptr) return false;
        if (!implementation->value->is<IR::ExpressionValue>()) {
          ::error("%1%: expected expression for property", implementation);
          return false;
        }
        auto propv = implementation->value->to<IR::ExpressionValue>();
        if (!propv->expression->is<IR::PathExpression>()) return false;
        auto pathe = propv->expression->to<IR::PathExpression>();
        auto decl = refMap->getDeclaration(pathe->path, true);
        if (!decl->is<IR::Declaration_Instance>()) {
            ::error("%1%: expected a reference to an instance", pathe);
            return false;
        }
        const auto &apname = decl->externalName();
        auto dcltype = typeMap->getType(pathe, true);
        if (!dcltype->is<IR::Type_Extern>()) {
            ::error("%1%: unexpected type for implementation", dcltype);
            return false;
        }
        auto type_extern_name = dcltype->to<IR::Type_Extern>()->name;
        if (type_extern_name != model.tableImplementations.actionSelector.name)
            return false;

        auto key = table->getKey();
        Input input;
        for (auto ke : *key->keyElements) {
            auto mt = refMap->getDeclaration(ke->matchType->path, true)->to<IR::Declaration_ID>();
            BUG_CHECK(mt != nullptr, "%1%: could not find declaration", ke->matchType);
            if (mt->name.name != model.selectorMatchType.name) continue;
            input.push_back(ke->expression);
        }
        auto decl_instance = decl->to<IR::Declaration_Instance>();
        auto it = selector_input_map.find(decl_instance);
        if (it == selector_input_map.end()) {
            selector_input_map[decl_instance] = input;
            return false;
        }

        // returns true if inputs are the same, false otherwise
        auto cmp_inputs = [](const Input &i1, const Input &i2) {
            for (auto e1 : i1) {
                auto cmp_e = [e1](const IR::Expression *e2) {
                    return checkSameKeyExpr(e1, e2);
                };
                if (std::find_if(i2.begin(), i2.end(), cmp_e) == i2.end()) return false;
            }
            return true;
        };

        if (!cmp_inputs(it->second, input)) {
            ::error(
                 "Action selector '%1%' is used by multiple tables with different selector inputs",
                 decl);
        }

        return false;
    }
};

}  // namespace

static cstring stringRepr(mpz_class value, unsigned bytes = 0) {
    cstring sign = "";
    const char* r;
    cstring filler = "";
    if (value < 0) {
        value =- value;
        r = mpz_get_str(nullptr, 16, value.get_mpz_t());
        sign = "-";
    } else {
        r = mpz_get_str(nullptr, 16, value.get_mpz_t());
    }

    if (bytes > 0) {
        int digits = bytes * 2 - strlen(r);
        BUG_CHECK(digits >= 0, "Cannot represent %1% on %2% bytes", value, bytes);
        filler = std::string(digits, '0');
    }
    return sign + "0x" + filler + r;
}

static Util::JsonObject* mkPrimitive(cstring name, Util::JsonArray* appendTo) {
    auto result = new Util::JsonObject();
    result->emplace("op", name);
    appendTo->append(result);
    return result;
}

static Util::JsonArray* mkArrayField(Util::JsonObject* parent, cstring name) {
    auto result = new Util::JsonArray();
    parent->emplace(name, result);
    return result;
}

static Util::JsonArray* pushNewArray(Util::JsonArray* parent) {
    auto result = new Util::JsonArray();
    parent->append(result);
    return result;
}

static Util::JsonArray* mkParameters(Util::JsonObject* object) {
    return mkArrayField(object, "parameters");
}

// Convert each expression into Json
// Place corresponding result in map.
class ExpressionConverter : public Inspector {
    std::map<const IR::Expression*, Util::IJson*> map;
    JsonConverter* converter;
    bool leftValue; // true if converting a left value

 public:
    explicit ExpressionConverter(JsonConverter* converter) :
            converter(converter), leftValue(false), simpleExpressionsOnly(false),
            createFieldLists(false) {}
    bool simpleExpressionsOnly; // if set we fail to convert complex expressions
    bool createFieldLists; // if set, ListExpressions will be turned into field lists

    Util::IJson* get(const IR::Expression* expression) const {
        auto result = ::get(map, expression);
        BUG_CHECK(result, "%1%: could not convert to Json", expression);
        return result;
    }

    void postorder(const IR::BoolLiteral* expression) override {
        auto result = new Util::JsonObject();
        result->emplace("type", "bool");
        result->emplace("value", expression->value);
        map.emplace(expression, result);
    }

    void postorder(const IR::MethodCallExpression* expression) override {
        auto instance = P4::MethodInstance::resolve(
            expression, converter->refMap, converter->typeMap);
        if (instance->is<P4::ExternMethod>()) {
            auto em = instance->to<P4::ExternMethod>();
            if (em->originalExternType->name == converter->corelib.packetIn.name &&
                em->method->name == converter->corelib.packetIn.lookahead.name) {
                BUG_CHECK(expression->typeArguments->size() == 1,
                          "Expected 1 type parameter for %1%", em->method);
                auto targ = expression->typeArguments->at(0);
                auto typearg = converter->typeMap->getTypeType(targ, true);
                int width = typearg->width_bits();
                BUG_CHECK(width > 0, "%1%: unknown width", targ);
                auto j = new Util::JsonObject();
                j->emplace("type", "lookahead");
                auto v = mkArrayField(j, "value");
                v->append(0);
                v->append(width);
                map.emplace(expression, j);
                return;
            } else {
                BUG("%1%: trying to evaluate method call as expression",
                    expression);
            }
        } else if (instance->is<P4::BuiltInMethod>()) {
            auto bim = instance->to<P4::BuiltInMethod>();
            if (bim->name == IR::Type_Header::isValid) {
                auto result = new Util::JsonObject();
                result->emplace("type", "expression");
                auto e = new Util::JsonObject();
                result->emplace("value", e);
                e->emplace("op", "valid");
                e->emplace("left", Util::JsonValue::null);
                auto l = get(bim->appliedTo);
                e->emplace("right", l);
                map.emplace(expression, result);
                return;
            }
        }
        BUG("%1%: unhandled case", expression);
    }

//<<<<<<< HEAD
//    void postorder(const IR::Shr* expression) override {
//        // special handling for shift of a lookahead -> current
//        auto l = get(expression->left);
//        if (l->is<Util::JsonObject>()) {
//            auto jo = l->to<Util::JsonObject>();
//            auto type = jo->get("type");
//            if (type != nullptr && type->is<Util::JsonValue>()) {
//                auto val = type->to<Util::JsonValue>();
//                if (val->isString() && val->getString() == "lookahead") {
//                    auto r = jo->get("value");
//                    CHECK_NULL(r);
//                    auto arr = r->to<Util::JsonArray>();
//                    CHECK_NULL(arr);
//                    auto second = arr->at(1);
//                    BUG_CHECK(second->is<Util::JsonValue>(),
//                              "%1%: expected a value", second);
//                    auto max = second->to<Util::JsonValue>()->getInt();
//
//                    BUG_CHECK(expression->right->is<IR::Constant>(),
//                                     "Not implemented: %1%", expression);
//                    auto amount = expression->right->to<IR::Constant>()->asInt();
//
//                    auto j = new Util::JsonObject();
//                    j->emplace("type", "lookahead");
//                    auto v = mkArrayField(j, "value");
//                    v->append(amount);
//                    v->append(max - amount);
//                    map.emplace(expression, j);
//                    return;
//                }
//            }
//        }
//        binary(expression);
//    }
//
//=======
//>>>>>>> fc65efe282f4fdb73f86157ace8eb4c2e3145004
    void postorder(const IR::Cast* expression) override {
        // nothing to do for casts - the ArithmeticFixup pass
        // should have handled them already
        auto j = get(expression->expr);
        map.emplace(expression, j);
    }

//<<<<<<< HEAD
//    void postorder(const IR::Slice* expression) override {
//        // Special case for parser select: look for
//        // packet.lookahead<T>()[h:l]. Convert to current(l, h - l).
//        // Only correct within a select() expression, but we cannot check that
//        // since the caller invokes the converter directly with the select argument.
//        auto m = get(expression->e0);
//        if (m->is<Util::JsonObject>()) {
//            auto val = m->to<Util::JsonObject>()->get("type");
//            if (val != nullptr && val->is<Util::JsonValue>() &&
//                *val->to<Util::JsonValue>() == "lookahead") {
//                int h = expression->getH();
//                int l = expression->getL();
//                auto j = new Util::JsonObject();
//                j->emplace("type", "lookahead");
//                auto bounds = mkArrayField(j, "value");
//                bounds->append(l);
//                bounds->append(h + 1);
//                map.emplace(expression, j);
//                return;
//            }
//        }
//        BUG("%1%: unhandled case", expression);
//    }
//
//=======
//>>>>>>> fc65efe282f4fdb73f86157ace8eb4c2e3145004
    void postorder(const IR::Constant* expression) override {
        auto result = new Util::JsonObject();
        result->emplace("type", "hexstr");

        cstring repr = stringRepr(expression->value,
                                    ROUNDUP(expression->type->width_bits(), 8));
        result->emplace("value", repr);
        result->emplace("bitwidth", expression->type->width_bits());
        map.emplace(expression, result);
    }

    const IR::Parameter* enclosingParamReference(const IR::Expression* expression) {
        CHECK_NULL(expression);
        if (!expression->is<IR::PathExpression>()) {
            return nullptr;
        }

        auto pe = expression->to<IR::PathExpression>();
        auto decl = converter->refMap->getDeclaration(pe->path, true);
        auto param = decl->to<IR::Parameter>();
        if (param == nullptr) {
            return param;
        }
        if (converter->structure.nonActionParameters.count(param) > 0) {
            return param;
        }
        return nullptr;
    }

    void postorder(const IR::ArrayIndex* expression) override {
        auto result = new Util::JsonObject();
        result->emplace("type", "header");
        cstring elementAccess;

        // This is can be either a header, which is part of the "headers" parameter
        // or a temporary array.
        if (expression->left->is<IR::Member>()) {
            // This is a header part of the parameters
            auto mem = expression->left->to<IR::Member>();
            auto param = enclosingParamReference(mem->expr);
            auto packageObject = converter->resolveParameter(param);
            if (packageObject != nullptr) {
                elementAccess = converter->buildQualifiedName(
                        {packageObject->externalName(), mem->member.name});
            } else {
                elementAccess = mem->member.name;
            }
        } else if (expression->left->is<IR::PathExpression>()) {
            // This is a temporary variable with type stack.
            auto path = expression->left->to<IR::PathExpression>();
            elementAccess = path->path->name;
        }

        if (!expression->right->is<IR::Constant>()) {
            ::error("%1%: all array indexes must be constant on this architecture",
                    expression->right);
        } else {
            int index = expression->right->to<IR::Constant>()->asInt();
            elementAccess += "[" + Util::toString(index) + "]";
        }
        result->emplace("value", elementAccess);
        map.emplace(expression, result);
    }

    // Non-null if the expression refers to a parameter from the enclosing control
    void postorder(const IR::Member* expression) override {
        // TODO: deal with references that return bool
        auto result = new Util::JsonObject();

        auto parentType = converter->typeMap->getType(expression->expr, true);
        cstring fieldName = expression->member.name;
        if (parentType->is<IR::Type_StructLike>()) {
            auto st = parentType->to<IR::Type_StructLike>();
            auto field = st->getField(expression->member);
            if (field != nullptr)
                // field could be a method call, i.e., isValid.
                fieldName = field->externalName();
        }

        // handle the 'error' type
        {
            auto type = converter->typeMap->getType(expression, true);
            if (type->is<IR::Type_Error>()) {
                result->emplace("type", "hexstr");
                auto errorValue = converter->retrieveErrorValue(expression);
                result->emplace("value", Util::toString(errorValue));
                map.emplace(expression, result);
                return;
            }
        }

        auto param = enclosingParamReference(expression->expr);
        if (param != nullptr) {
            auto packageObject = converter->resolveParameter(param);
            BUG_CHECK(packageObject != nullptr, "All parameters should resolve "
                      "to package objects...no?");
            auto type = converter->typeMap->getType(expression, true);
            auto ptype = converter->typeMap->getType(param, true);
            // TODO(pierce): not sure this check scales
            // TODO(pierce): consider the current version of this in master
            if (ptype->is<IR::Type_Struct>()
                && (type->is<IR::Type_Bits>() || type->is<IR::Type_Boolean>())) { 
                result->emplace("type", "field");
                auto e = mkArrayField(result, "value");
                e->append(packageObject->externalName());
                e->append(expression->member);
            } else {
                if (type->is<IR::Type_Stack>()) {
                    result->emplace("type", "header_stack");
                    result->emplace("value",
                            converter->buildQualifiedName(
                                {packageObject->externalName(),
                                 expression->member.name}
                            )
                    );
                } else {
                    // This may be wrong, but the caller will handle it 
                    // properly (e.g., this can be a method,
                    // such as packet.lookahead)
                    result->emplace("type", "header");
                    result->emplace("value",
                            converter->buildQualifiedName(
                                {packageObject->externalName(),
                                 expression->member.name}
                            )
                    );
                }
            }
        } else {
            bool done = false;
            if (expression->expr->is<IR::Member>()) {
                // array.next.field => type: "stack_field",
                // value: [ array, field ]
                auto mem = expression->expr->to<IR::Member>();
                auto memtype = converter->typeMap->getType(mem->expr, true);
                if (memtype->is<IR::Type_Stack>()
                    && mem->member == IR::Type_Stack::last) {
                    auto l = get(mem->expr);
                    CHECK_NULL(l);
                    result->emplace("type", "stack_field");
                    auto e = mkArrayField(result, "value");
                    if (l->is<Util::JsonObject>())
                        e->append(l->to<Util::JsonObject>()->get("value"));
                    else
                        e->append(l);
                    e->append(fieldName);
                    done = true;
                }
            } else if (expression->expr->is<IR::PathExpression>()) {
                auto pathExp = expression->expr->to<IR::PathExpression>();
                auto decl = converter->refMap->getDeclaration(pathExp->path, true);
                if (decl->is<IR::Declaration_Instance>()) {
                    // TODO(pierce): BUG: trying to evaluate extern methodcall
                    // in expression???
                }
            } else if (expression->expr->is<IR::TypeNameExpression>()) {
                // the case of enumeration
                auto tne = expression->expr->to<IR::TypeNameExpression>();
                result->emplace("type", "string");

                std::stringstream ss;
                ss << expression->toString();

                result->emplace("value", ss.str().c_str());
                done = true;
            }

            if (!done) {
                auto l = get(expression->expr);
                CHECK_NULL(l);
                result->emplace("type", "field");
                auto e = mkArrayField(result, "value");
                if (l->is<Util::JsonObject>()) {
                    auto lv = l->to<Util::JsonObject>()->get("value");
                    if (lv->is<Util::JsonArray>()) {
                        // TODO: is this case still necessary after eliminating nested structs?
                        // nested struct reference [ ["m", "f"], "x" ] => [ "m", "f.x" ]
                        auto array = lv->to<Util::JsonArray>();
                        BUG_CHECK(array->size() == 2, "expected 2 elements");
                        auto first = array->at(0);
                        auto second = array->at(1);
                        BUG_CHECK(second->is<Util::JsonValue>(),
                                  "expected a value");
                        e->append(first);
                        cstring nestedField = second->to<Util::JsonValue>()->getString();
                        nestedField += "." + fieldName;
                        e->append(nestedField);
                    } else if (lv->is<Util::JsonValue>()) {
                        e->append(lv);
                        e->append(fieldName);
                    } else {
                        BUG("%1%: Unexpected json", lv);
                    }
                } else {
                    e->append(l);
                    e->append(fieldName);
                }
            }
        }
        map.emplace(expression, result);
    }

    static Util::IJson* fixLocal(Util::IJson* json) {
        if (json->is<Util::JsonObject>()) {
            auto jo = json->to<Util::JsonObject>();
            auto to = jo->get("type");
            if (to != nullptr && to->to<Util::JsonValue>() != nullptr &&
                (*to->to<Util::JsonValue>()) == "runtime_data") {
                auto result = new Util::JsonObject();
                result->emplace("type", "local");
                result->emplace("value", jo->get("value"));
                return result;
            }
        }
        return json;
    }

    void postorder(const IR::Mux* expression) override {
        auto result = new Util::JsonObject();
        map.emplace(expression, result);
        if (simpleExpressionsOnly) {
            ::error("%1%: expression to complex for this target", expression);
            return;
        }

        result->emplace("type", "expression");
        auto e = new Util::JsonObject();
        result->emplace("value", e);
        e->emplace("op", "?");
        auto l = get(expression->e1);
        e->emplace("left", fixLocal(l));
        auto r = get(expression->e2);
        e->emplace("right", fixLocal(r));
        auto c = get(expression->e0);
        e->emplace("cond", fixLocal(c));
    }

    void postorder(const IR::IntMod* expression) override {
        auto result = new Util::JsonObject();
        map.emplace(expression, result);
        result->emplace("type", "expression");
        auto e = new Util::JsonObject();
        result->emplace("value", e);
        e->emplace("op", "two_comp_mod");
        auto l = get(expression->expr);
        e->emplace("left", fixLocal(l));
        auto r = new Util::JsonObject();
        r->emplace("type", "hexstr");
        cstring repr = stringRepr(expression->width);
        r->emplace("value", repr);
        e->emplace("right", r);
    }

    void postorder(const IR::Operation_Binary* expression) override {
        binary(expression);
    }

    void binary(const IR::Operation_Binary* expression) {
        auto result = new Util::JsonObject();
        map.emplace(expression, result);
        if (simpleExpressionsOnly) {
            ::error("%1%: expression to complex for this target", expression);
            return;
        }

        result->emplace("type", "expression");
        auto e = new Util::JsonObject();
        result->emplace("value", e);
        cstring op = expression->getStringOp();
        if (op == "&&")
            op = "and";
        else if (op == "||")
            op = "or";
        e->emplace("op", op);
        auto l = get(expression->left);
        e->emplace("left", fixLocal(l));
        auto r = get(expression->right);
        e->emplace("right", fixLocal(r));
    }

    void postorder(const IR::ListExpression* expression) override {
        if (simpleExpressionsOnly) {
            ::error("%1%: expression to complex for this target", expression);
            return;
        }

        if (createFieldLists) {
            int id = converter->createFieldList(expression, "field_lists",
                                                converter->refMap->newName("fl"),
                                                converter->field_lists);
            auto cst = new IR::Constant(id);
            converter->typeMap->setType(cst, IR::Type_Bits::get(32));
            auto conv = new ExpressionConverter(converter);
            auto result = conv->convert(cst);
            map.emplace(expression, result);
        } else {
            auto result = new Util::JsonArray();
            for (auto e : *expression->components) {
                auto t = get(e);
                result->append(t);
            }
            map.emplace(expression, result);
        }
    }

    void postorder(const IR::Operation_Unary* expression) override {
        auto result = new Util::JsonObject();
        map.emplace(expression, result);
        if (simpleExpressionsOnly) {
            ::error("%1%: expression to complex for this target", expression);
            return;
        }

        result->emplace("type", "expression");
        auto e = new Util::JsonObject();
        result->emplace("value", e);
        cstring op = expression->getStringOp();
        if (op == "!")
            op = "not";
        e->emplace("op", op);
        e->emplace("left", Util::JsonValue::null);
        auto r = get(expression->expr);
        e->emplace("right", fixLocal(r));
    }

    void postorder(const IR::PathExpression* expression) override {
        // This is useful for action bodies mostly
        auto decl = converter->refMap->getDeclaration(expression->path, true);
        if (auto param = decl->to<IR::Parameter>()) {
            if (converter->structure.nonActionParameters.find(param) !=
                converter->structure.nonActionParameters.end()) {
                map.emplace(expression, new Util::JsonValue(param->name.name));
                return;
            }

            auto result = new Util::JsonObject();
            result->emplace("type", "runtime_data");
            unsigned paramIndex = ::get(converter->structure.index, param);
            result->emplace("value", paramIndex);
            map.emplace(expression, result);
        } else if (auto var = decl->to<IR::Declaration_Variable>()) {
            auto result = new Util::JsonObject();
            auto type = converter->typeMap->getType(var, true);
            if (type->is<IR::Type_StructLike>()) {
                result->emplace("type", "header");
                result->emplace("value", var->name);
            } else if (type->is<IR::Type_Bits>()
                       || (type->is<IR::Type_Boolean>() && leftValue)) {
                // no convertion d2b when writing (leftValue is true) to a boolean
                result->emplace("type", "field");
                auto e = mkArrayField(result, "value");
                e->append(converter->scalarsName);
                e->append(var->name);
            } else if (type->is<IR::Type_Boolean>()) {
                // Boolean variables are stored as ints,
                // so we have to insert a conversion when
                // reading such a variable
                result->emplace("type", "expression");
                auto e = new Util::JsonObject();
                result->emplace("value", e);
                e->emplace("op", "d2b");    // data to Boolean cast
                e->emplace("left", Util::JsonValue::null);
                auto r = new Util::JsonObject();
                e->emplace("right", r);
                r->emplace("type", "field");
                auto f = mkArrayField(r, "value");
                f->append(converter->scalarsName);
                f->append(var->name);
            } else if (type->is<IR::Type_Stack>()) {
                result->emplace("type", "header_stack");
                result->emplace("value", var->name);
            } else if (type->is<IR::Type_Error>()) {
                result->emplace("type", "field");
                auto f = mkArrayField(result, "value");
                f->append(converter->scalarsName);
                f->append(var->name);
            } else {
                BUG("%1%: type not yet handled", type);
            }
            map.emplace(expression, result);
        } else if (auto inst = decl->to<IR::Declaration_Instance>()) {
            BUG("%1%: trying to evaluate complex type in expression", inst);
        }
    }

    void postorder(const IR::TypeNameExpression*) override {
    }

    void postorder(const IR::Expression* expression) override {
        BUG("%1%: Unhandled case", expression);
    }

    // doFixup = true -> insert masking operations for proper arithmetic
    // implementation, see below for wrap
    Util::IJson* convert(const IR::Expression* e, bool doFixup = true,
                         bool wrap = true, bool convertBool = false) {
        const IR::Expression *expr = e;
        if (doFixup) {
            ArithmeticFixup af(converter->typeMap);
            auto r = e->apply(af);
            CHECK_NULL(r);
            expr = r->to<IR::Expression>();
            CHECK_NULL(expr);
        }
        expr->apply(*this);
        auto result = ::get(map, expr->to<IR::Expression>());
        if (result == nullptr)
            BUG("%1%: Could not convert expression", e);

        if (convertBool) {
            auto obj = new Util::JsonObject();
            obj->emplace("type", "expression");
            auto conv = new Util::JsonObject();
            obj->emplace("value", conv);
            conv->emplace("op", "b2d");    // boolean to data cast
            conv->emplace("left", Util::JsonValue::null);
            conv->emplace("right", result);
            result = obj;
        }

        std::set<cstring> to_wrap({"expression", "stack_field"});

        // This is weird, but that's how it is: expression and stack_field must be wrapped in
        // another outer object. In a future version of the bmv2 JSON, this will not be needed
        // anymore as expressions will be treated in a more uniform way.
        if (wrap && result->is<Util::JsonObject>()) {
            auto to = result->to<Util::JsonObject>()->get("type");
            if (to != nullptr && to->to<Util::JsonValue>() != nullptr) {
                auto jv = *to->to<Util::JsonValue>();
                if (jv.isString() && to_wrap.find(jv.getString()) != to_wrap.end()) {
                    auto rwrap = new Util::JsonObject();
                    rwrap->emplace("type", "expression");
                    rwrap->emplace("value", result);
                    result = rwrap;
                }
            }
        }
        return result;
    }

    Util::IJson* convertLeftValue(const IR::Expression* e) {
        leftValue = true;
        const IR::Expression *expr = e;
        ArithmeticFixup af(converter->typeMap);
        auto r = e->apply(af);
        CHECK_NULL(r);
        expr = r->to<IR::Expression>();
        CHECK_NULL(expr);
        expr->apply(*this);
        auto result = ::get(map, expr->to<IR::Expression>());
        if (result == nullptr)
            BUG("%1%: Could not convert expression", e);
        leftValue = false;
        return result;
    }
};

class ResolveToPackageObjects : public Inspector {
 private:
    using ParameterMap =
        std::unordered_map<const IR::Parameter*, const IR::IDeclaration*>;
 private:
    ParameterMap *parameterMap{nullptr};
    P4::TypeMap *typeMap;
    P4::ReferenceMap *refMap;
    static ResolveToPackageObjects *instance;

 private:
    void setParameterMapping(
            const IR::Parameter *param, const IR::IDeclaration *packageLocal) {
        CHECK_NULL(param); CHECK_NULL(packageLocal);
        parameterMap->emplace(param, packageLocal);
    }
    
 public:
    ResolveToPackageObjects(P4::TypeMap *tm, P4::ReferenceMap *rm)
            : parameterMap(new ParameterMap()), typeMap(tm), refMap(rm) {
        instance = this;
    }

    static const ResolveToPackageObjects *getInstance() {
        return instance;
    }

    const IR::IDeclaration *getParameterMapping(const IR::Parameter *p) const {
        auto ret = parameterMap->find(p);
        if (ret == parameterMap->end()) {
            return nullptr;
        }
        return ret->second;
    }

 public:
    bool preorder(const IR::MethodCallStatement *m) {
        auto mi = P4::MethodInstance::resolve(m->methodCall, refMap, typeMap);
        if (mi->isApply()) {
            auto apply = mi->to<P4::ApplyMethod>()->applyObject;
            auto applyMethodType = apply->getApplyMethodType();
    
            auto mit = m->methodCall->arguments->begin();
            for (auto p : *applyMethodType->parameters->parameters) {
                auto pathExp = (*mit)->to<IR::PathExpression>();
                setParameterMapping(p->to<IR::Parameter>(),
                                    refMap->getDeclaration(pathExp->path));
                ++mit;
            }
        }
        return false;
    }
    
    bool preorder(const IR::Type_Package *p) {
        for (auto s : *p->body->components) {
            visit(s);
        }
        return false;
    }
    
    bool preorder(const IR::P4Program *p) {
        for (auto decl : *p->getDeclarations()) {
            if (decl->is<IR::Type_Package>()) {
                visit(decl->to<IR::Type_Package>());
            }
        }
        return false;
    }
};

ResolveToPackageObjects *ResolveToPackageObjects::instance = nullptr;

JsonConverter::JsonConverter(const CompilerOptions& options)
        : options(options), corelib(P4::P4CoreLibrary::instance), model(),
          refMap(nullptr), typeMap(nullptr), toplevelBlock(nullptr),
          conv(new ExpressionConverter(this)) { }

void
JsonConverter::convertActionBody(const IR::Vector<IR::StatOrDecl>* body,
                                 Util::JsonArray* result) {
    conv->createFieldLists = true;
    for (auto s : *body) {
        if (!s->is<IR::Statement>()) {
            continue;
        } else if (s->is<IR::BlockStatement>()) {
            convertActionBody(s->to<IR::BlockStatement>()->components, result);
            continue;
        } else if (s->is<IR::ReturnStatement>()) {
            break;
        } else if (s->is<IR::ExitStatement>()) {
            auto primitive = mkPrimitive("exit", result);
            (void)mkParameters(primitive);
            break;
        } else if (s->is<IR::AssignmentStatement>()) {
            const IR::Expression* l, *r;
            auto assign = s->to<IR::AssignmentStatement>();
            l = assign->left;
            r = assign->right;

            cstring operation;
            auto type = typeMap->getType(l, true);
            if (type->is<IR::Type_StructLike>()) {
                operation = "copy_header";
            } else {
                operation = "modify_field";
            }
            auto primitive = mkPrimitive(operation, result);
            auto parameters = mkParameters(primitive);
            auto left = conv->convertLeftValue(l);
            parameters->append(left);
            bool convertBool = type->is<IR::Type_Boolean>();
            auto right = conv->convert(r, true, true, convertBool);
            parameters->append(right);
            continue;
        } else if (s->is<IR::EmptyStatement>()) {
            continue;
        } else if (s->is<IR::MethodCallStatement>()) {
            auto mc = s->to<IR::MethodCallStatement>()->methodCall;
            auto mi = P4::MethodInstance::resolve(mc, refMap, typeMap);
            if (mi->is<P4::ActionCall>()) {
                BUG("%1%: action call should have been inlined", mc);
                continue;
            } else if (mi->is<P4::BuiltInMethod>()) {
                auto builtin = mi->to<P4::BuiltInMethod>();

                cstring prim;
                auto parameters = new Util::JsonArray();
                auto obj = conv->convert(builtin->appliedTo);
                parameters->append(obj);

                if (builtin->name == IR::Type_Header::setValid) {
                    prim = "add_header";
                } else if (builtin->name == IR::Type_Header::setInvalid) {
                    prim = "remove_header";
                } else if (builtin->name == IR::Type_Stack::push_front) {
                    BUG_CHECK(mc->arguments->size() == 1,
                              "Expected 1 argument for %1%", mc);
                    auto arg = conv->convert(mc->arguments->at(0));
                    prim = "push";
                    parameters->append(arg);
                } else if (builtin->name == IR::Type_Stack::pop_front) {
                    BUG_CHECK(mc->arguments->size() == 1,
                              "Expected 1 argument for %1%", mc);
                    auto arg = conv->convert(mc->arguments->at(0));
                    prim = "pop";
                    parameters->append(arg);
                } else {
                    BUG("%1%: Unexpected built-in method", s);
                }
                auto primitive = mkPrimitive(prim, result);
                primitive->emplace("parameters", parameters);
                continue;
            } else if (mi->is<P4::ExternMethod>()) {
                auto em = mi->to<P4::ExternMethod>();

                // build the primitive name
                std::stringstream ss;
                ss << "_"
                   << em->actualExternType->toString()
                   << "_"
                   << em->method->toString();

                // special handling for packet out
                if (em->originalExternType->name.name == corelib.packetOut.name
                    && em->method->name.name == corelib.packetOut.emit.name) {
                    conv->simpleExpressionsOnly = true;
                    if (mc->arguments->size() == 1 && typeMap->getType(
                        mc->arguments->at(0))->is<IR::Type_Stack>()) {
                        ss.str(std::string()); // clear the primitive name
                        ss << "_packet_out_emit_stack";
                    }

                    // shouldn't this happen a little earlier?
                    auto arg = mc->arguments->at(0);
                    auto type = typeMap->getType(arg, true);
                    if (!(type->is<IR::Type_Stack>()
                        || type->is<IR::Type_Header>())) {
                        ::error("%1%: emit only supports header and stack "
                                "arguments, not %2%", arg, type);
                    }
                }

                auto prim = mkPrimitive(ss.str().c_str(), result);
                auto params = mkParameters(prim);
               
                auto self = new Util::JsonObject();
                self->emplace("type", "extern");

                if (em->object->is<IR::Parameter>()) {
                    auto param = em->object->to<IR::Parameter>();
                    auto packageObject = resolveParameter(param);
                    self->emplace("value", packageObject->getName());
                } else {
                    self->emplace("value", em->object->getName());
                }

                params->append(self);

                for (auto a : *mc->arguments) {
                    auto arg = conv->convert(a);
                    params->append(arg);
                }
                conv->simpleExpressionsOnly = false;
                continue;
            } else if (mi->is<P4::ExternFunction>()) {
                auto ef = mi->to<P4::ExternFunction>();
                auto prim = mkPrimitive(ef->method->name.toString(), result);
                auto params = mkParameters(prim);
                for (auto a : *mc->arguments) {
                    params->append(conv->convert(a));
                }
                continue;
            }
        }
        ::error("%1%: not yet supported on this target", s);
    }
    conv->createFieldLists = false;
}

void JsonConverter::addToFieldList(const IR::Expression* expr, Util::JsonArray* fl) {
    if (expr->is<IR::ListExpression>()) {
        auto le = expr->to<IR::ListExpression>();
        for (auto e : *le->components) {
            addToFieldList(e, fl);
        }
        return;
    }
    auto type = typeMap->getType(expr, true);
    if (type->is<IR::Type_StructLike>()) {
        // recursively add all fields
        auto st = type->to<IR::Type_StructLike>();
        for (auto f : *st->fields) {
            auto member = new IR::Member(Util::SourceInfo(), expr, f->name);
            typeMap->setType(member, typeMap->getType(f, true));
            addToFieldList(member, fl);
        }
        return;
    }
    auto newConv = new ExpressionConverter(this);
    auto j = newConv->convert(expr);
    fl->append(j);
}

// returns id of created field list
int JsonConverter::createFieldList(const IR::Expression* expr, cstring group,
        cstring listName, Util::JsonArray* field_lists) {
    auto fl = new Util::JsonObject();
    field_lists->append(fl);
    int id = nextId(group);
    fl->emplace("id", id);
    fl->emplace("name", listName);
    auto elements = mkArrayField(fl, "elements");
    addToFieldList(expr, elements);
    return id;
}

unsigned JsonConverter::createAction(const IR::P4Action *action,
                                     Util::JsonArray *actions) {
    cstring name = action->externalName();
    auto jact = new Util::JsonObject();
    jact->emplace("name", name);
    unsigned id = nextId("actions");
    jact->emplace("id", id);
    auto params = mkArrayField(jact, "runtime_data");
    for (auto p : *action->parameters->getEnumerator()) {
        // The P4 v1.0 compiler removes unused action parameters!
        // We have to do the same, although this seems wrong.
        if (!refMap->isUsed(p)) {
            ::warning("Removing unused action parameter %1% for "
                      "compatibility reasons", p);
            continue;
        }

        auto param = new Util::JsonObject();
        param->emplace("name", p->name);
        auto type = typeMap->getType(p, true);
        if (!type->is<IR::Type_Bits>())
            ::error("%1%: Action parameters can only be bit<> "
                    "or int<> on this target", p);
            param->emplace("bitwidth", type->width_bits());
            params->append(param);
    }
    auto body = mkArrayField(jact, "primitives");
    convertActionBody(action->body->components, body);
    actions->append(jact);
    structure.ids.emplace(action, id);
    return id;
}

Util::IJson* JsonConverter::nodeName(const CFG::Node* node) const {
    if (node->name.isNullOrEmpty())
        return Util::JsonValue::null;
    else
        return new Util::JsonValue(node->name);
}

Util::IJson* JsonConverter::convertIf(const CFG::IfNode* node, cstring) {
    auto result = new Util::JsonObject();
    result->emplace("name", node->name);
    result->emplace("id", nextId("conditionals"));
    auto j = conv->convert(node->statement->condition, true, false);
    CHECK_NULL(j);
    result->emplace("expression", j);
    for (auto e : node->successors.edges) {
        Util::IJson* dest = nodeName(e->endpoint);
        cstring label = Util::toString(e->getBool());
        label += "_next";
        result->emplace(label, dest);
    }
    return result;
}

bool JsonConverter::handleTableImplementation(const IR::Property* implementation,
                                              const IR::Key* key,
                                              Util::JsonObject* table,
                                              Util::JsonArray* action_profiles) {
    if (implementation == nullptr) {
        table->emplace("type", "simple");
        return true;
    }

    if (!implementation->value->is<IR::ExpressionValue>()) {
        ::error("%1%: expected expression for property", implementation);
        return false;
    }
    auto propv = implementation->value->to<IR::ExpressionValue>();

    bool isSimpleTable = true;
    Util::JsonObject* action_profile;
    cstring apname;

    if (propv->expression->is<IR::ConstructorCallExpression>()) {
        auto cc = P4::ConstructorCall::resolve(
            propv->expression->to<IR::ConstructorCallExpression>(), refMap, typeMap);
        if (!cc->is<P4::ExternConstructorCall>()) {
            ::error("%1%: expected extern object for property", implementation);
            return false;
        }
        auto ecc = cc->to<P4::ExternConstructorCall>();
        auto implementationType = ecc->type;
        auto arguments = ecc->cce->arguments;
        apname = implementation->externalName(refMap->newName("action_profile"));
        action_profile = new Util::JsonObject();
        action_profiles->append(action_profile);
        action_profile->emplace("name", apname);
        action_profile->emplace("id", nextId("action_profiles"));

        auto add_size = [&action_profile, &arguments](size_t arg_index) {
            auto size_expr = arguments->at(arg_index);
            int size;
            if (!size_expr->is<IR::Constant>()) {
                ::error("%1% must be a constant", size_expr);
                size = 0;
            } else {
                size = size_expr->to<IR::Constant>()->asInt();
            }
            action_profile->emplace("max_size", size);
        };

        if (implementationType->name
            == model.tableImplementations.actionSelector.name) {
            BUG_CHECK(arguments->size() == 3, "%1%: expected 3 arguments", arguments);
            isSimpleTable = false;
            auto selector = new Util::JsonObject();
            table->emplace("type", "indirect_ws");
            action_profile->emplace("selector", selector);
            add_size(1);
            auto hash = arguments->at(0);
            auto ei = P4::EnumInstance::resolve(hash, typeMap);
            if (ei == nullptr) {
                ::error("%1%: must be a constant on this target", hash);
            } else {
                cstring algo = convertHashAlgorithm(ei->name);
                selector->emplace("algo", algo);
            }
            auto input = mkArrayField(selector, "input");
            for (auto ke : *key->keyElements) {
                auto mt = refMap->getDeclaration(ke->matchType->path, true)
                        ->to<IR::Declaration_ID>();
                BUG_CHECK(mt != nullptr, "%1%: could not find declaration", ke->matchType);
                if (mt->name.name != model.selectorMatchType.name)
                    continue;

                auto expr = ke->expression;
                auto jk = conv->convert(expr);
                input->append(jk);
            }
        } else if (implementationType->name
                   == model.tableImplementations.actionProfile.name) {
            isSimpleTable = false;
            table->emplace("type", "indirect");
            add_size(0);
        } else {
            ::error("%1%: unexpected value for property", propv);
        }
    } else if (propv->expression->is<IR::PathExpression>()) {
        auto pathe = propv->expression->to<IR::PathExpression>();
        auto decl = refMap->getDeclaration(pathe->path, true);
        if (!decl->is<IR::Declaration_Instance>()) {
            ::error("%1%: expected a reference to an instance", pathe);
            return false;
        }
        apname = decl->externalName();
        auto dcltype = typeMap->getType(pathe, true);
        if (!dcltype->is<IR::Type_Extern>()) {
            ::error("%1%: unexpected type for implementation", dcltype);
            return false;
        }
        auto type_extern_name = dcltype->to<IR::Type_Extern>()->name;
        if (type_extern_name == model.tableImplementations.actionProfile.name) {
            table->emplace("type", "indirect");
        } else if (type_extern_name
                   == model.tableImplementations.actionSelector.name) {
            table->emplace("type", "indirect_ws");
        } else {
            ::error("%1%: unexpected type for implementation", dcltype);
            return false;
        }
        isSimpleTable = false;
    } else {
        ::error("%1%: unexpected value for property", propv);
        return false;
    }
    table->emplace("action_profile", apname);
    return isSimpleTable;
}

cstring JsonConverter::convertHashAlgorithm(cstring algorithm) const {
    return algorithm;
}

Util::IJson*
JsonConverter::convertTable(const CFG::TableNode* node,
                            Util::JsonArray* action_profiles,
                            Util::JsonArray* actions) {
    auto table = node->table;
    LOG3("Processing " << dbp(table));
    auto result = new Util::JsonObject();
    cstring name = table->externalName();
    result->emplace("name", name);
    result->emplace("id", nextId("tables"));
    cstring table_match_type = "exact";
    auto key = table->getKey();
    auto tkey = mkArrayField(result, "key");
    conv->simpleExpressionsOnly = true;

    if (key != nullptr) {
        for (auto ke : *key->keyElements) {
            auto decl = refMap->getDeclaration(ke->matchType->path, true);
            auto mt = decl->to<IR::Declaration_ID>();
            BUG_CHECK(mt != nullptr, "%1%: could not find declaration",
                      ke->matchType);
            auto expr = ke->expression;
            mpz_class mask;
            if (expr->is<IR::Slice>()) {
                auto slice = expr->to<IR::Slice>();
                expr = slice->e0;
                int h = slice->getH();
                int l = slice->getL();
                mask = Util::maskFromSlice(h, l);
            }

            cstring match_type = mt->name.name;
            if (mt->name.name == corelib.exactMatch.name) {
                if (expr->is<IR::MethodCallExpression>()) {
                    auto mce = expr->to<IR::MethodCallExpression>();
                    auto mi = P4::MethodInstance::resolve(mce, refMap, typeMap);
                    if (mi->is<P4::BuiltInMethod>()) {
                        auto bim = mi->to<P4::BuiltInMethod>();
                        if (bim->name == IR::Type_Header::isValid) {
                            expr = bim->appliedTo;
                            match_type = "valid";
                        }
                    }
                }
            } else if (mt->name.name == corelib.ternaryMatch.name) {
                if (table_match_type == "exact")
                    table_match_type = "ternary";
                if (expr->is<IR::MethodCallExpression>()) {
                    auto mce = expr->to<IR::MethodCallExpression>();
                    auto mi = P4::MethodInstance::resolve(mce, refMap, typeMap);
                    if (mi->is<P4::BuiltInMethod>()) {
                        auto bim = mi->to<P4::BuiltInMethod>();
                        if (bim->name == IR::Type_Header::isValid) {
                            expr = new IR::Member(IR::Type::Boolean::get(),
                                                  bim->appliedTo, "$valid$");
                            typeMap->setType(expr, expr->type);
                        }
                    }
                }
            } else if (mt->name.name == corelib.lpmMatch.name) {
                if (table_match_type != "lpm") {
                    table_match_type = "lpm";
                }
            } else if (mt->name.name == model.rangeMatchType.name) {
                continue;
            } else if (mt->name.name == model.selectorMatchType.name) {
                continue;
            } else {
                ::error("%1%: match type not supported on this target", mt);
            }

            auto keyelement = new Util::JsonObject();
            keyelement->emplace("match_type", match_type);
            auto jk = conv->convert(expr);
            keyelement->emplace("target", jk->to<Util::JsonObject>()->get("value"));
            if (mask != 0)
                keyelement->emplace("mask", stringRepr(mask));
            else
                keyelement->emplace("mask", Util::JsonValue::null);
            tkey->append(keyelement);
        }
    }
    result->emplace("match_type", table_match_type);
    conv->simpleExpressionsOnly = false;

    auto impl = table->properties->getProperty(
        model.tableAttributes.tableImplementation.name);
    bool simple = handleTableImplementation(impl, key, result, action_profiles);

    unsigned size = 0;
    auto sz = table->properties->getProperty(model.tableAttributes.size.name);
    if (sz != nullptr) {
        if (sz->value->is<IR::ExpressionValue>()) {
            auto expr = sz->value->to<IR::ExpressionValue>()->expression;
            if (!expr->is<IR::Constant>()) {
                ::error("%1% must be a constant", sz);
                size = 0;
            } else {
                size = expr->to<IR::Constant>()->asInt();
            }
        } else {
            ::error("%1%: expected a number", sz);
        }
    }
    if (size == 0)
        size = model.tableAttributes.defaultTableSize;

    result->emplace("max_size", size);
//<<<<<<< HEAD
    result->emplace("with_counters", false);
//=======
//    auto ctrs = table->properties->getProperty(v1model.tableAttributes.directCounter.name);
//    if (ctrs != nullptr) {
//        if (ctrs->value->is<IR::ExpressionValue>()) {
//            auto expr = ctrs->value->to<IR::ExpressionValue>()->expression;
//            if (expr->is<IR::ConstructorCallExpression>()) {
//                auto type = typeMap->getType(expr, true);
//                if (type == nullptr)
//                    return result;
//                if (!type->is<IR::Type_Extern>()) {
//                    ::error("%1%: Unexpected type %2% for property", ctrs, type);
//                    return result;
//                }
//                auto te = type->to<IR::Type_Extern>();
//                if (te->name != v1model.directCounter.name && te->name != v1model.counter.name) {
//                    ::error("%1%: Unexpected type %2% for property", ctrs, type);
//                    return result;
//                }
//                result->emplace("with_counters", true);
//                auto jctr = new Util::JsonObject();
//                cstring ctrname = ctrs->externalName("counter");
//                jctr->emplace("name", ctrname);
//                jctr->emplace("id", nextId("counter_arrays"));
//                bool direct = te->name == v1model.directCounter.name;
//                jctr->emplace("is_direct", direct);
//                jctr->emplace("binding", name);
//                counters->append(jctr);
//            } else if (expr->is<IR::PathExpression>()) {
//                auto pe = expr->to<IR::PathExpression>();
//                auto decl = refMap->getDeclaration(pe->path, true);
//                if (!decl->is<IR::Declaration_Instance>()) {
//                    ::error("%1%: expected an instance", decl->getNode());
//                    return result;
//                }
//                cstring ctrname = decl->externalName();
//                auto it = directCountersMap.find(ctrname);
//                LOG3("Looking up " << ctrname);
//                if (it != directCountersMap.end()) {
//                    ::error("%1%: Direct cannot be attached to multiple tables %2% and %3%",
//                            decl, it->second, table);
//                    return result;
//                }
//                directCountersMap.emplace(ctrname, table);
//            } else {
//                ::error("%1%: expected a counter", ctrs);
//            }
//        }
//    } else {
//        result->emplace("with_counters", false);
//    }
//>>>>>>> fc65efe282f4fdb73f86157ace8eb4c2e3145004

    bool sup_to = false;
    auto timeout =
        table->properties->getProperty(model.tableAttributes.supportTimeout.name);
    if (timeout != nullptr) {
        if (timeout->value->is<IR::ExpressionValue>()) {
            auto expr = timeout->value->to<IR::ExpressionValue>()->expression;
            if (!expr->is<IR::BoolLiteral>()) {
                ::error("%1% must be true/false", timeout);
            } else {
                sup_to = expr->to<IR::BoolLiteral>()->value;
            }
        } else {
            ::error("%1%: expected a Boolean", timeout);
        }
    }
    result->emplace("support_timeout", sup_to);
//<<<<<<< HEAD
    result->emplace("direct_meters", Util::JsonValue::null);
//=======
//
//    auto dm = table->properties->getProperty(v1model.tableAttributes.directMeter.name);
//    if (dm != nullptr) {
//        if (dm->value->is<IR::ExpressionValue>()) {
//            auto expr = dm->value->to<IR::ExpressionValue>()->expression;
//            if (!expr->is<IR::PathExpression>()) {
//                ::error("%1%: expected a reference to a meter declaration", expr);
//            } else {
//                auto pe = expr->to<IR::PathExpression>();
//                auto decl = refMap->getDeclaration(pe->path, true);
//                auto type = typeMap->getType(expr, true);
//                if (type == nullptr)
//                    return result;
//                if (type->is<IR::Type_SpecializedCanonical>())
//                    type = type->to<IR::Type_SpecializedCanonical>()->baseType;
//                if (!type->is<IR::Type_Extern>()) {
//                    ::error("%1%: Unexpected type %2% for property", dm, type);
//                    return result;
//                }
//                auto te = type->to<IR::Type_Extern>();
//                if (te->name != v1model.directMeter.name) {
//                    ::error("%1%: Unexpected type %2% for property", dm, type);
//                    return result;
//                }
//                if (!decl->is<IR::Declaration_Instance>()) {
//                    ::error("%1%: expected an instance", decl->getNode());
//                    return result;
//                }
//                meterMap.setTable(decl, table);
//                meterMap.setSize(decl, size);
//                cstring name = decl->externalName();
//                result->emplace("direct_meters", name);
//            }
//        } else {
//            ::error("%1%: expected a meter", dm);
//        }
//    } else {
//        result->emplace("direct_meters", Util::JsonValue::null);
//    }
//>>>>>>> fc65efe282f4fdb73f86157ace8eb4c2e3145004

    auto action_ids = mkArrayField(result, "action_ids");
    auto table_actions = mkArrayField(result, "actions");
    auto al = table->getActionList();

    std::map<cstring, cstring> useActionName;
    for (auto a : *al->actionList) {
        if (a->expression->is<IR::MethodCallExpression>()) {
            auto mce = a->expression->to<IR::MethodCallExpression>();
            if (mce->arguments->size() > 0)
                ::error("%1%: Actions in action list with arguments not supported", a);
        }
        auto decl = refMap->getDeclaration(a->getPath(), true);
        BUG_CHECK(decl->is<IR::P4Action>(), "%1%: should be an action name", a);
        auto action = decl->to<IR::P4Action>();
        unsigned id = createAction(action, actions);
        action_ids->append(id);
        auto name = action->externalName();
        table_actions->append(name);
        useActionName.emplace(action->name, name);
    }

    auto next_tables = new Util::JsonObject();

    CFG::Node* nextDestination = nullptr; // if no action is executed
    CFG::Node* defaultLabelDestination = nullptr; // if the "default" label is executed
    // Note: the "default" label is not the default_action.
    bool hitMiss = false;
    for (auto s : node->successors.edges) {
        if (s->isUnconditional())
            nextDestination = s->endpoint;
        else if (s->isBool())
            hitMiss = true;
        else if (s->label == "default")
            defaultLabelDestination = s->endpoint;
    }

    Util::IJson* nextLabel = nullptr;
    if (!hitMiss) {
        BUG_CHECK(nextDestination, "Could not find default destination for %1%",
                  node->invocation);
        nextLabel = nodeName(nextDestination);
        result->emplace("base_default_next", nextLabel);
        // So if a "default:" switch case exists we set the nextLabel
        // to be the destination of the default: label.
        if (defaultLabelDestination != nullptr)
            nextLabel = nodeName(defaultLabelDestination);
    } else {
        result->emplace("base_default_next", Util::JsonValue::null);
    }

    std::set<cstring> labelsDone;
    for (auto s : node->successors.edges) {
        cstring label;
        if (s->isBool()) {
            label = s->getBool() ? "__HIT__" : "__MISS__";
        } else if (s->isUnconditional()) {
            continue;
        } else {
            label = s->label;
            if (label == "default")
                continue;
            label = ::get(useActionName, label);
        }
        next_tables->emplace(label, nodeName(s->endpoint));
        labelsDone.emplace(label);
    }

    // Generate labels which don't show up and send them to
    // the nextLabel.
    if (!hitMiss) {
        for (auto a : *al->actionList) {
            cstring name = a->getName().name;
            cstring label = ::get(useActionName, name);
            if (labelsDone.find(label) == labelsDone.end())
                next_tables->emplace(label, nextLabel);
        }
    }

    result->emplace("next_tables", next_tables);
    auto defact =
        table->properties->getProperty(IR::TableProperties::defaultActionPropertyName);
    if (defact != nullptr) {
        if (!simple) {
            ::warning("Target does not support default_action for %1% "
                      "(due to action profiles)", table);
            return result;
        }

        if (!defact->value->is<IR::ExpressionValue>()) {
            ::error("%1%: expected an action", defact);
            return result;
        }
        auto expr = defact->value->to<IR::ExpressionValue>()->expression;
        const IR::P4Action* action = nullptr;
        const IR::Vector<IR::Expression>* args = nullptr;

        if (expr->is<IR::PathExpression>()) {
            auto decl =
                refMap->getDeclaration(expr->to<IR::PathExpression>()->path, true);
            BUG_CHECK(decl->is<IR::P4Action>(), "%1%: should be an action name", expr);
            action = decl->to<IR::P4Action>();
        } else if (expr->is<IR::MethodCallExpression>()) {
            auto mce = expr->to<IR::MethodCallExpression>();
            auto mi = P4::MethodInstance::resolve(mce, refMap, typeMap);
            BUG_CHECK(mi->is<P4::ActionCall>(), "%1%: expected an action", expr);
            action = mi->to<P4::ActionCall>()->action;
            args = mce->arguments;
        } else {
            BUG("%1%: unexpected expression", expr);
        }

        unsigned actionid = get(structure.ids, action);
        auto entry = new Util::JsonObject();
        entry->emplace("action_id", actionid);
        entry->emplace("action_const", false);
        auto fields = mkArrayField(entry, "action_data");
        if (args != nullptr) {
            for (auto a : *args) {
                if (a->is<IR::Constant>()) {
                    cstring repr = stringRepr(a->to<IR::Constant>()->value);
                    fields->append(repr);
                } else {
                    ::error("%1%: argument must evaluate to a constant integer", a);
                    return result;
                }
            }
        }
        entry->emplace("action_entry_const", defact->isConstant);
        result->emplace("default_entry", entry);
    }
    return result;
}

Util::JsonObject *JsonConverter::createExternInstance(cstring name, cstring type) {
    auto j = new Util::JsonObject();
    j->emplace("name", name);
    j->emplace("id", nextId("extern_instances"));
    j->emplace("type", type);
    return j;
}

void JsonConverter::addExternAttributes(const IR::Declaration_Instance *di,
                                        const IR::ExternBlock *block,
                                        Util::JsonArray *attributes) {
    auto paramIt = block->getConstructorParameters()->parameters->begin();
    for (auto arg : *di->arguments) {
        auto j = new Util::JsonObject();
        j->emplace("name", (*paramIt)->toString()); 
        if (arg->is<IR::Constant>()) {
            auto constVal = arg->to<IR::Constant>();
            if (arg->type->is<IR::Type_Bits>()) {
                j->emplace("type", "hexstr");
                j->emplace("value", stringRepr(constVal->value));
            } else {
                BUG("%1%: unhandled constant constructor param",
                    constVal->toString());
            }
        // TODO(pierce): is this still necessary with enums?
        } else if (arg->is<IR::Declaration_ID>()) {
            auto declID = arg->to<IR::Declaration_ID>();
            j->emplace("type", "string");
            j->emplace("value", declID->toString());
        } else if (arg->type->is<IR::Type_Enum>()) {
            j->emplace("type", "string");
            j->emplace("value", arg->toString());
        } else {
            BUG("%1%: unknown constructor param type", arg->type);
        }
        attributes->append(j);
        ++paramIt;
    }
}

Util::IJson* JsonConverter::convertControl(const IR::P4Control* cont,
                                           cstring name,
                                           Util::JsonArray *externs,
                                           Util::JsonArray *actions) {
    auto instantiatedBlock = getInstantiatedBlock(name);
    auto controlBlock = instantiatedBlock->to<IR::ControlBlock>();

    enclosingBlock = cont;

    LOG3("Processing " << dbp(cont));
    auto result = new Util::JsonObject();
    result->emplace("name", name);
    result->emplace("id", nextId("control"));

    auto cfg = new CFG();
    cfg->build(cont, refMap, typeMap);
    cfg->checkForCycles();

    if (cfg->entryPoint->successors.size() == 0) {
        result->emplace("init_table", Util::JsonValue::null);
    } else {
        BUG_CHECK(cfg->entryPoint->successors.size() == 1,
                  "Expected 1 start node for %1%", cont);
        auto start = (*(cfg->entryPoint->successors.edges.begin()))->endpoint;
        result->emplace("init_table", nodeName(start));
    }
    auto tables = mkArrayField(result, "tables");
    auto action_profiles = mkArrayField(result, "action_profiles");
    auto conditionals = mkArrayField(result, "conditionals");

    SharedActionSelectorCheck selector_check(this);
    cont->apply(selector_check);

    // Tables are created prior to the other local declarations
    for (auto node : cfg->allNodes) {
        if (node->is<CFG::TableNode>()) {
            auto j = convertTable(node->to<CFG::TableNode>(),
                                  action_profiles, actions);
            if (::errorCount() > 0)
                return nullptr;
            tables->append(j);
        } else if (node->is<CFG::IfNode>()) {
            auto j = convertIf(node->to<CFG::IfNode>(), cont->name);
            if (::errorCount() > 0)
                return nullptr;
            conditionals->append(j);
        }
    }

    // special handling for externs as parameters
    for (auto p : *cont->type->applyParams->parameters) {
        auto type = typeMap->getType(p);
        if (type->is<IR::Type_Extern>()) {
            auto ex = type->to<IR::Type_Extern>();
            auto packageObject = resolveParameter(p);
            auto inst = createExternInstance(packageObject->toString(),
                                             ex->getName());
            mkArrayField(inst, "attribute_values");
            externs->append(inst);
        }
    }

    for (auto c : *cont->controlLocals) {
        if (c->is<IR::Declaration_Constant>() ||
            c->is<IR::Declaration_Variable>() ||
            c->is<IR::P4Action>() ||
            c->is<IR::P4Table>())
            continue;
        if (c->is<IR::Declaration_Instance>()) {
            auto di = c->to<IR::Declaration_Instance>();
            auto bl = controlBlock->getValue(c);
            cstring diName = c->name;
            if (bl->is<IR::ExternBlock>()) {
                auto eb = bl->to<IR::ExternBlock>();
                // Special handling for action_profile/action_selector externs
                // as they appear in v1model.p4
                if (eb->type->name == model.tableImplementations.actionProfile.name ||
                    eb->type->name == model.tableImplementations.actionSelector.name) {
                    auto action_profile = new Util::JsonObject();
                    action_profile->emplace("name", diName);
                    action_profile->emplace("id", nextId("action_profiles"));

                    auto add_size = [&action_profile, &eb](const cstring &pname) {
                      auto sz = eb->getParameterValue(pname);
                      BUG_CHECK(sz->is<IR::Constant>(),
                                "%1%: expected a constant", sz);
                      action_profile->emplace("max_size",
                                              sz->to<IR::Constant>()->value);
                    };

                    if (eb->type->name
                        == model.tableImplementations.actionProfile.name) {
                        add_size(eb->getConstructorParameters()->parameters->at(0)->name);
                    } else {
                        add_size(eb->getConstructorParameters()->parameters->at(1)->name);
                        auto selector = new Util::JsonObject();
                        auto hash = eb->getParameterValue(
                                eb->getConstructorParameters()->parameters->at(0)->name);
                        BUG_CHECK(hash->is<IR::Declaration_ID>(),
                                  "%1%: expected a member", hash);
                        auto algo = convertHashAlgorithm(
                                hash->to<IR::Declaration_ID>()->name);
                        selector->emplace("algo", algo);
                        const auto &input = selector_check.get_selector_input(
                            c->to<IR::Declaration_Instance>());
                        auto j_input = mkArrayField(selector, "input");
                        for (auto expr : input) {
                            auto jk = conv->convert(expr);
                            j_input->append(jk);
                        }
                        action_profile->emplace("selector", selector);
                    }
                    action_profiles->append(action_profile);
                    continue;
                } else {
                    auto json = createExternInstance(diName, eb->type->name);
                    auto attributes = mkArrayField(json, "attribute_values");
                    addExternAttributes(di, eb, attributes);
                    externs->append(json);
                }
                continue;
            }
        }
        BUG("%1%: not yet handled", c);
    }
    return result;
}

unsigned JsonConverter::nextId(cstring group) {
    static std::map<cstring, unsigned> counters;
    return counters[group]++;
}

bool JsonConverter::hasBitMembers(const IR::Type_StructLike *st) {
    for (auto f : *st->fields) {
        if (f->type->is<IR::Type_Bits>()) {
            return true;
        }
    }
    return false;
}

bool JsonConverter::hasStructLikeMembers(const IR::Type_StructLike *st) {
    for (auto f : *st->fields) {
        if (f->type->is<IR::Type_StructLike>() || f->type->is<IR::Type_Stack>()) {
            return true;
        }
    }
    return false;
}

void JsonConverter::checkStructure(const IR::Type_StructLike *st) {
    BUG_CHECK(!(hasBitMembers(st) && hasStructLikeMembers(st)),
              "Cannot mix bit<>-fields and struct-fields in struct: %1%", st);
}

void JsonConverter::pushFields(const IR::Type_StructLike *st,
                               Util::JsonArray *fields) {
    for (auto f : *st->fields) {
        auto ftype = typeMap->getType(f, true);
        if (auto type = ftype->to<IR::Type_Bits>()) {
            auto field = pushNewArray(fields);
            field->append(f->name.name);
            field->append(type->size);
            field->append(type->isSigned);
        } else if (auto type = ftype->to<IR::Type_Varbits>()) {
            auto field = pushNewArray(fields);
            field->append(f->name.name);
            field->append(type->size);    // FIXME -- where does length go?
        } else if (ftype->is<IR::Type_Boolean>()) {
            auto field = pushNewArray(fields);
            field->append(f->name.name);
            field->append(boolWidth);
            field->append(0);
        } else if (ftype->to<IR::Type_Stack>()) {
            BUG("%1%: nested stack", st);
        } else {
            BUG("%1%: unexpected type for %2%.%3%", ftype, st, f->name);
        }
    }
        // must add padding
    unsigned width = st->width_bits();
    unsigned padding = width % 8;
    if (padding != 0) {
        cstring name = refMap->newName("_padding");
        auto field = pushNewArray(fields);
        field->append(name);
        field->append(8 - padding);
        field->append(false);
    }
}

unsigned
JsonConverter::createHeaderTypeAndInstance(cstring prefix, cstring varName,
                                           const IR::Type_StructLike *st) {
    BUG_CHECK(!hasStructLikeMembers(st),
              "%1%: Header has nested structure", st);

    cstring fullName = prefix + st->name;
    cstring fullExternalName = st->externalName();

    if (!headerTypesCreated.count(fullName)) {
        // create the header type
        auto typeJson = new Util::JsonObject();
        headerTypesCreated[fullName] = fullExternalName;
        typeJson->emplace("name", fullExternalName);
        typeJson->emplace("id", nextId("header_types"));
        headerTypes->append(typeJson);
        auto fields = mkArrayField(typeJson, "fields");
        pushFields(st, fields);
    }

    if (!headerInstancesCreated.count(varName)) {
        // create the instance
        auto json = new Util::JsonObject();
        json->emplace("name", varName);
        unsigned id = nextId("headers");
        headerInstancesCreated[varName] = id;
        json->emplace("id", id);
        json->emplace("header_type", fullExternalName);
        json->emplace("metadata", !st->is<IR::Type_Header>());
        json->emplace("pi_omit", true);  // Don't expose in PI.
        headerInstances->append(json);
        return id;
    } else {
        return headerInstancesCreated[varName];
    }
}

void JsonConverter::createStack(cstring prefix, cstring varName,
                                const IR::Type_Stack *stack) {
    auto json = new Util::JsonObject();
    json->emplace("name", varName);
    json->emplace("id", nextId("stack"));
    json->emplace("size", stack->getSize());
    auto type = typeMap->getTypeType(stack->elementType, true);
    BUG_CHECK(type->is<IR::Type_Header>(), "%1% not a header type",
              stack->elementType);
    auto ht = type->to<IR::Type_Header>();

    cstring header_type = stack->elementType->to<IR::Type_Header>()->name;
    json->emplace("header_type", header_type);
    auto stackMembers = mkArrayField(json, "header_ids");
    for (unsigned i=0; i < stack->getSize(); i++) {
        cstring name = varName + "[" + Util::toString(i) + "]";
        unsigned id = createHeaderTypeAndInstance("", name, ht);
        stackMembers->append(id);
    }
    headerStacks->append(json);
}

void JsonConverter::createNestedStruct(cstring prefix, cstring varName,
                                       const IR::Type_StructLike *st,
                                       bool usePrefix) {
    checkStructure(st);
    if (!hasStructLikeMembers(st)) {
        createHeaderTypeAndInstance(prefix, varName, st);
    } else {
        for (auto f : *st->fields) {
            cstring newPrefix = buildQualifiedName({prefix, st->name});
            if (f->type->is<IR::Type_StructLike>()) {
                createNestedStruct(usePrefix ? newPrefix : "",
                                   buildQualifiedName({varName, f->name}),
                                   f->type->to<IR::Type_StructLike>(), usePrefix);
            } else if (f->type->is<IR::Type_Stack>()) {
                createStack(usePrefix ? newPrefix : "",
                            buildQualifiedName({varName, f->name}),
                            f->type->to<IR::Type_Stack>());
            }
        }
    }
}

void JsonConverter::addLocals() {
    // We synthesize a "header_type" for each local which has a struct type
    // and we pack all the scalar-typed locals into a scalarsStruct
    auto scalarFields = scalarsStruct->get("fields")->to<Util::JsonArray>();
    CHECK_NULL(scalarFields);

    for (auto v : structure.variables) {
        LOG3("Creating local " << v);
        auto type = typeMap->getType(v, true);
        if (auto st = type->to<IR::Type_StructLike>()) {
            createNestedStruct("", v->name, st, !refMap->isV1());
        } else if (auto stack = type->to<IR::Type_Stack>()) {
            createStack("", v->name, stack);
        } else if (type->is<IR::Type_Bits>()) {
            auto tb = type->to<IR::Type_Bits>();
            auto field = pushNewArray(scalarFields);
            field->append(v->name.name);
            field->append(tb->size);
            field->append(tb->isSigned);
            scalars_width += tb->size;
        } else if (type->is<IR::Type_Boolean>()) {
            auto field = pushNewArray(scalarFields);
            field->append(v->name.name);
            field->append(boolWidth);
            field->append(0);
            scalars_width += boolWidth;
        } else if (type->is<IR::Type_Error>()) {
            auto field = pushNewArray(scalarFields);
            field->append(v->name.name);
            field->append(32);  // using 32-bit fields for errors
            field->append(0);
            scalars_width += 32;
        } else if (type->is<IR::Type_Extern>()) {
            // handled elsewhere
        } else if (type->is<IR::Type_Var>()) {
            // Now that we have package definitions with locals instantiated with
            // generic types, we tend to see this. Currently I'm just skipping it.
        } else {
            BUG("%1%: type not yet handled on this target", type);
        }
    }

    // insert the scalars type
    headerTypesCreated[scalarsName] = scalarsName;
    headerTypes->append(scalarsStruct);

    // insert the scalars instance
    auto json = new Util::JsonObject();
    json->emplace("name", scalarsName);
    json->emplace("id", nextId("headers"));
    json->emplace("header_type", scalarsName);
    json->emplace("metadata", true);
    json->emplace("pi_omit", true);  // Don't expose in PI.
    headerInstances->append(json);
}

void JsonConverter::padScalars() {
    unsigned padding = scalars_width % 8;
    auto scalarFields = (*scalarsStruct)["fields"]->to<Util::JsonArray>();
    if (padding != 0) {
        cstring name = refMap->newName("_padding");
        auto field = pushNewArray(scalarFields);
        field->append(name);
        field->append(8 - padding);
        field->append(false);
    }
}

void JsonConverter::addMetaInformation() {
  auto meta = new Util::JsonObject();

  static constexpr int version_major = 2;
  static constexpr int version_minor = 6;
  auto version = mkArrayField(meta, "version");
  version->append(version_major);
  version->append(version_minor);

  meta->emplace("compiler", "https://github.com/p4lang/p4c");

  toplevel.emplace("__meta__", meta);
}


const IR::InstantiatedBlock *JsonConverter::getInstantiatedBlock(cstring name) {
    BUG_CHECK(toplevelBlock != nullptr, "Must set toplevelBlock first");
    auto block = toplevelBlock->getMain()->getParameterValue(name);
    CHECK_NULL(block);
    return block->to<IR::InstantiatedBlock>();
}

const IR::IDeclaration *JsonConverter::resolveParameter(
        const IR::Parameter *param) {
    if (param == nullptr) {
        return nullptr;
    }

    auto package = toplevelBlock->getMain()->type;
    auto parameterIt = package->constructorParams->parameters->begin();

    BUG_CHECK(enclosingBlock != nullptr, "Trying to resolve parameter but "
              "enclosing block is null");

    for (; parameterIt != package->constructorParams->parameters->end();
         ++parameterIt) {
        const IR::IApply *iapply = nullptr;
        auto b = getInstantiatedBlock((*parameterIt)->toString());
        if (b->is<IR::ParserBlock>()) {
            iapply = b->to<IR::ParserBlock>()->container->to<IR::IApply>();
        } else if (b->is<IR::ControlBlock>()) {
            iapply = b->to<IR::ControlBlock>()->container->to<IR::IApply>();
        }
        if (enclosingBlock == iapply) {
            break;
        }
    }

    for (auto c : *package->body->components) {
        if (!c->is<IR::MethodCallStatement>()) {
            ::error("Only method call statements are allowed in "
                    "package apply body: %1%", c);
        }
        auto methodCall = c->to<IR::MethodCallStatement>()->methodCall;
        auto member = methodCall->method->to<IR::Member>();
        if (member->expr->toString() == (*parameterIt)->toString()) {
            auto mi = P4::MethodInstance::resolve(methodCall, refMap, typeMap);
            if (!mi->isApply()) {
                ::error("Only apply method calls are allowed in "
                      "package body: %1%", methodCall);
            }
            auto apply = mi->to<P4::ApplyMethod>()->applyObject;
            auto applyMethodType = apply->getApplyMethodType();

            auto applyPIt = applyMethodType->parameters->parameters->begin();
            auto enclosingApplyMethod = enclosingBlock->getApplyMethodType();
            for (auto p : *enclosingApplyMethod->parameters->parameters) {
                if (param->getName() == p->getName()) {
                    return ResolveToPackageObjects::getInstance()->
                        getParameterMapping(*applyPIt);
                }
                ++applyPIt;
            }
        }
    }
    return nullptr;
}

void JsonConverter::convert(P4::ReferenceMap* refMap, P4::TypeMap* typeMap,
                            const IR::ToplevelBlock* toplevelBlock,
                            P4::ConvertEnums::EnumMapping* enumMap) {
    this->toplevelBlock = toplevelBlock;
    this->refMap = refMap;
    this->typeMap = typeMap;
    this->enumMap = enumMap;
    CHECK_NULL(typeMap);
    CHECK_NULL(refMap);
    CHECK_NULL(enumMap);

    auto package = toplevelBlock->getMain()->type;

    if (package == nullptr) {
        ::error("No output to generate");
        return;
    }

    if (!package->hasDefinition()) {
        ::error("BMV2 requires compilation with a package definition.");
        return;
    }

    ResolveToPackageObjects resolver(typeMap, refMap);
    toplevelBlock->getProgram()->apply(resolver);

    structure.analyze(toplevelBlock, typeMap);
        if (::errorCount() > 0) {
        return;
    }

    toplevel.emplace("program", options.file);
    addMetaInformation();

    headerTypes = mkArrayField(&toplevel, "header_types");
    headerInstances = mkArrayField(&toplevel, "headers");
    headerStacks = mkArrayField(&toplevel, "header_stacks");

    (void)nextId("field_lists");    // field list IDs must start at 1; 0 is reserved
    (void)nextId("learn_lists");    // idem

    scalarsStruct = new Util::JsonObject();
    scalarsName = refMap->newName("scalars");
    scalarsStruct->emplace("name", scalarsName);
    scalarsStruct->emplace("id", nextId("header_types"));
    scalars_width = 0;
    auto scalarFields = mkArrayField(scalarsStruct, "fields");

    headerTypesCreated.clear();
    headerInstancesCreated.clear();

    addLocals();
    padScalars();

    ErrorCodesVisitor errorCodesVisitor(this);
    toplevelBlock->getProgram()->apply(errorCodesVisitor);
    addErrors();

    addEnums();

    field_lists = mkArrayField(&toplevel, "field_lists");

    auto pipelines = mkArrayField(&toplevel, "pipelines");
    auto externs = mkArrayField(&toplevel, "extern_instances");
    auto prsrs = mkArrayField(&toplevel, "parsers");
    auto actions = mkArrayField(&toplevel, "actions");

    auto isCalled = [package, refMap, typeMap](const IR::Parameter *p) -> bool {
        for (auto c : *package->body->components) {
            if (!c->is<IR::MethodCallStatement>()) {
                ::error("Only method call statements are allowed in "
                        "package apply body: %1%", c);
            }
            auto mce = c->to<IR::MethodCallStatement>()->methodCall;
            auto mi = P4::MethodInstance::resolve(mce, refMap, typeMap);
            if (!mi->isApply()) {
                ::error("Only apply method calls are allowed in "
                        "package apply body: %1%", mce);
            }
            if (p->toString() == mi->object->toString()) return true;
        }
        return false;
    };

    auto packageParamIt = package->constructorParams->parameters->begin();
    for (;packageParamIt != package->constructorParams->parameters->end();
         ++packageParamIt) {
        if (!isCalled(*packageParamIt)) continue;
        if (::errorCount() > 0) return;
        auto block = getInstantiatedBlock((*packageParamIt)->toString());
        if (block->is<IR::ParserBlock>()) {
            auto parserBlock = block->to<IR::ParserBlock>();
            auto parser = parserBlock->container;
            auto parserJson = toJson(parser, (*packageParamIt)->toString());
            prsrs->append(parserJson);
        } else if (block->is<IR::ControlBlock>()) {
            auto controlBlock = block->to<IR::ControlBlock>();
            auto control = controlBlock->container;
            auto controlJson = convertControl(
                    control, (*packageParamIt)->toString(), externs, actions);
            pipelines->append(controlJson);
        }
    }
}

Util::IJson* JsonConverter::toJson(const IR::P4Parser* parser, cstring name) {
    enclosingBlock = parser;
    auto result = new Util::JsonObject();
    result->emplace("name", name);
    result->emplace("id", nextId("parser"));
    result->emplace("init_state", IR::ParserState::start);
    auto states = mkArrayField(result, "parse_states");

    for (auto state : *parser->states) {
        auto json = toJson(state);
        if (json != nullptr) {
            states->append(json);
        }
    }
    return result;
}

Util::IJson* JsonConverter::convertParserStatement(const IR::StatOrDecl* stat) {
    auto result = new Util::JsonObject();
    auto params = mkArrayField(result, "parameters");
    if (stat->is<IR::AssignmentStatement>()) {
        auto assign = stat->to<IR::AssignmentStatement>();
        result->emplace("op", "set");
        auto l = conv->convertLeftValue(assign->left);
        auto type = typeMap->getType(assign->left, true);
        bool convertBool = type->is<IR::Type_Boolean>();
        auto r = conv->convert(assign->right, true, true, convertBool);
        params->append(l);
        params->append(r);
        return result;
    } else if (stat->is<IR::MethodCallStatement>()) {
        auto mce = stat->to<IR::MethodCallStatement>()->methodCall;
        auto minst = P4::MethodInstance::resolve(mce, refMap, typeMap);
        if (minst->is<P4::ExternMethod>()) {
            auto extmeth = minst->to<P4::ExternMethod>();
            if (extmeth->method->name.name == corelib.packetIn.extract.name) {
                result->emplace("op", "extract");
                if (mce->arguments->size() == 1) {
                    auto arg = mce->arguments->at(0);
                    auto argtype = typeMap->getType(arg, true);
                    if (!argtype->is<IR::Type_Header>()) {
                        ::error("%1%: extract only accepts arguments "
                                "with header types, not %2%",
                                arg, argtype);
                        return result;
                    }
                    auto param = new Util::JsonObject();
                    params->append(param);
                    cstring type;
                    Util::IJson* j = nullptr;

                    if (arg->is<IR::Member>()) {
                        auto mem = arg->to<IR::Member>();
                        auto baseType = typeMap->getType(mem->expr, true);
                        if (baseType->is<IR::Type_Stack>()) {
                            if (mem->member == IR::Type_Stack::next) {
                                type = "stack";
                                j = conv->convert(mem->expr);
                            } else {
                                BUG("%1%: unsupported", mem);
                            }
                        }
                    }
                    if (j == nullptr) {
                        type = "regular";
                        j = conv->convert(arg);
                    }
                    auto value = j->to<Util::JsonObject>()->get("value");
                    param->emplace("type", type);
                    param->emplace("value", value);
                    return result;
                }
            }
        } else if (minst->is<P4::ExternFunction>()) {
            auto extfn = minst->to<P4::ExternFunction>();
            if (extfn->method->name.name == IR::ParserState::verify) {
                result->emplace("op", "verify");
                BUG_CHECK(mce->arguments->size() == 2, "%1%: Expected 2 arguments", mce);
                {
                    auto cond = mce->arguments->at(0);
                    // false means don't wrap in an outer expression object, which is not needed
                    // here
                    auto jexpr = conv->convert(cond, true, false);
                    params->append(jexpr);
                }
                {
                    auto error = mce->arguments->at(1);
                    // false means don't wrap in an outer expression object, which is not needed
                    // here
                    auto jexpr = conv->convert(error, true, false);
                    params->append(jexpr);
                }
                return result;
            }
        } else if (minst->is<P4::BuiltInMethod>()) {
            auto bi = minst->to<P4::BuiltInMethod>();
            if (bi->name == IR::Type_Header::setValid || bi->name == IR::Type_Header::setInvalid) {
                auto mem = new IR::Member(Util::SourceInfo(), bi->appliedTo, "$valid$");
                typeMap->setType(mem, IR::Type_Void::get());
                auto jexpr = conv->convert(mem, true, false);
                result->emplace("op", "set");
                params->append(jexpr);

                auto bl = new IR::BoolLiteral(bi->name == IR::Type_Header::setValid);
                auto r = conv->convert(bl, true, true, true);
                params->append(r);
                return result;
            }
        }
    }
    ::error("%1%: not supported in parser on this target", stat);
    return result;
}

// Operates on a select keyset
void JsonConverter::convertSimpleKey(const IR::Expression* keySet,
                                     mpz_class& value, mpz_class& mask) const {
    if (keySet->is<IR::Mask>()) {
        auto mk = keySet->to<IR::Mask>();
        if (!mk->left->is<IR::Constant>()) {
            ::error("%1% must evaluate to a compile-time constant", mk->left);
            return;
        }
        if (!mk->right->is<IR::Constant>()) {
            ::error("%1% must evaluate to a compile-time constant", mk->right);
            return;
        }
        value = mk->left->to<IR::Constant>()->value;
        mask = mk->right->to<IR::Constant>()->value;
    } else if (keySet->is<IR::Constant>()) {
        value = keySet->to<IR::Constant>()->value;
        mask = -1;
    } else if (keySet->is<IR::BoolLiteral>()) {
        value = keySet->to<IR::BoolLiteral>()->value ? 1 : 0;
        mask = -1;
    } else {
        ::error("%1% must evaluate to a compile-time constant", keySet);
        value = 0;
        mask = 0;
    }
}

unsigned JsonConverter::combine(const IR::Expression* keySet,
                                const IR::ListExpression* select,
                                mpz_class& value, mpz_class& mask) const {
    // From the BMv2 spec: For values and masks, make sure that you
    // use the correct format. They need to be the concatenation (in
    // the right order) of all byte padded fields (padded with 0
    // bits). For example, if the transition key consists of a 12-bit
    // field and a 2-bit field, each value will need to have 3 bytes
    // (2 for the first field, 1 for the second one). If the
    // transition value is 0xaba, 0x3, the value attribute will be set
    // to 0x0aba03.
    // Return width in bytes
    value = 0;
    mask = 0;
    unsigned totalWidth = 0;
    if (keySet->is<IR::DefaultExpression>()) {
        return totalWidth;
    } else if (keySet->is<IR::ListExpression>()) {
        auto le = keySet->to<IR::ListExpression>();
        BUG_CHECK(le->components->size() == select->components->size(),
                    "%1%: mismatched select", select);
        unsigned index = 0;

        bool noMask = true;
        for (auto it = select->components->begin();
                 it != select->components->end(); ++it) {
            auto e = *it;
            auto keyElement = le->components->at(index);

            auto type = typeMap->getType(e, true);
            int width = type->width_bits();
            BUG_CHECK(width > 0, "%1%: unknown width", e);

            mpz_class key_value, mask_value;
            convertSimpleKey(keyElement, key_value, mask_value);
            unsigned w = 8 * ROUNDUP(width, 8);
            totalWidth += ROUNDUP(width, 8);
            value = Util::shift_left(value, w) + key_value;
            if (mask_value != -1) {
                mask = Util::shift_left(mask, w) + mask_value;
                noMask = false;
            }
            LOG3("Shifting " << " into key " << key_value << " &&& " << mask_value <<
                 " result is " << value << " &&& " << mask);
            index++;
        }

        if (noMask)
            mask = -1;
        return totalWidth;
    } else {
        BUG_CHECK(select->components->size() == 1,
                  "%1%: mismatched select/label", select);
        convertSimpleKey(keySet, value, mask);
        auto type = typeMap->getType(select->components->at(0), true);
        return type->width_bits() / 8;
    }
}

static Util::IJson* stateName(IR::ID state) {
    if (state.name == IR::ParserState::accept) {
        return Util::JsonValue::null;
    } else if (state.name == IR::ParserState::reject) {
        ::warning("Explicit transition to %1% not supported on this target", state);
        return Util::JsonValue::null;
    } else {
        return new Util::JsonValue(state.name);
    }
}

Util::IJson* JsonConverter::toJson(const IR::ParserState* state) {
    if (state->name == IR::ParserState::reject
        || state->name == IR::ParserState::accept) {
        return nullptr;
    }

    auto result = new Util::JsonObject();
    result->emplace("name", state->externalName());
    result->emplace("id", nextId("parse_states"));
    auto operations = mkArrayField(result, "parser_ops");
    for (auto s : *state->components) {
        auto j = convertParserStatement(s);
        operations->append(j);
    }

    Util::IJson* key;
    auto transitions = mkArrayField(result, "transitions");
    if (state->selectExpression != nullptr) {
        if (state->selectExpression->is<IR::SelectExpression>()) {
            auto se = state->selectExpression->to<IR::SelectExpression>();
            key = conv->convert(se->select, false);
            for (auto sc : se->selectCases) {
                auto trans = new Util::JsonObject();
                mpz_class value, mask;
                unsigned bytes = combine(sc->keyset, se->select, value, mask);
                if (mask == 0) {
                    trans->emplace("value", "default");
                    trans->emplace("mask", Util::JsonValue::null);
                    trans->emplace("next_state", stateName(sc->state->path->name));
                } else {
                    trans->emplace("value", stringRepr(value, bytes));
                    if (mask == -1) {
                        trans->emplace("mask", Util::JsonValue::null);
                    } else {
                        trans->emplace("mask", stringRepr(mask, bytes));
                    }
                    trans->emplace("next_state", stateName(sc->state->path->name));
                }
                transitions->append(trans);
            }
        } else if (state->selectExpression->is<IR::PathExpression>()) {
        auto pe = state->selectExpression->to<IR::PathExpression>();
        key = new Util::JsonArray();
        auto trans = new Util::JsonObject();
        trans->emplace("value", "default");
        trans->emplace("mask", Util::JsonValue::null);
        trans->emplace("next_state", stateName(pe->path->name));
        transitions->append(trans);
        } else {
            BUG("%1%: unexpected selectExpression", state->selectExpression);
        }
    } else {
        key = new Util::JsonArray();
        auto trans = new Util::JsonObject();
        trans->emplace("value", "default");
        trans->emplace("mask", Util::JsonValue::null);
        trans->emplace("next_state", Util::JsonValue::null);
        transitions->append(trans);
    }
    result->emplace("transition_key", key);
    return result;
}

void JsonConverter::addErrors() {
    auto errors = mkArrayField(&toplevel, "errors");
    for (const auto &p : errorCodesMap) {
        auto name = p.first->getName().name.c_str();
        auto entry = pushNewArray(errors);
        entry->append(name);
        entry->append(p.second);
    }
}

JsonConverter::ErrorValue JsonConverter::retrieveErrorValue(const IR::Member* mem) const {
    auto type = typeMap->getType(mem, true);
    BUG_CHECK(type->is<IR::Type_Error>(), "Not an error constant");
    auto decl = type->to<IR::Type_Error>()->getDeclByName(mem->member.name);
    return errorCodesMap.at(decl);
}

void JsonConverter::addEnums() {
    CHECK_NULL(enumMap);
    auto enums = mkArrayField(&toplevel, "enums");
    for (const auto &pEnum : *enumMap) {
        auto enumName = pEnum.first->getName().name.c_str();
        auto enumObj = new Util::JsonObject();
        enumObj->emplace("name", enumName);
        auto entries = mkArrayField(enumObj, "entries");
        for (const auto &pEntry : *pEnum.second) {
            auto entry = pushNewArray(entries);
            entry->append(pEntry.first);
            entry->append(pEntry.second);
        }
        enums->append(enumObj);
    }
}

}  // namespace BMV2
