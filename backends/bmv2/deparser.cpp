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

#include "backend.h"
#include "deparser.h"

namespace BMV2 {

void DoDeparserBlockConversion::convertDeparserBody(const IR::Vector<IR::StatOrDecl>* body,
                                                    Util::JsonArray* result) {
    backend->getExpressionConverter()->simpleExpressionsOnly = true;
    for (auto s : *body) {
        LOG1("deparser body " << s);
        if (auto block = s->to<IR::BlockStatement>()) {
            convertDeparserBody(&block->components, result);
            continue;
        } else if (s->is<IR::ReturnStatement>() || s->is<IR::ExitStatement>()) {
            break;
        } else if (s->is<IR::EmptyStatement>()) {
            continue;
        } else if (s->is<IR::MethodCallStatement>()) {
            auto mc = s->to<IR::MethodCallStatement>()->methodCall;
            auto mi = P4::MethodInstance::resolve(mc, &backend->getRefMap(), &backend->getTypeMap());
            if (mi->is<P4::ExternMethod>()) {
                auto em = mi->to<P4::ExternMethod>();
                if (em->originalExternType->name.name == backend->getCoreLibrary().packetOut.name) {
                    if (em->method->name.name == backend->getCoreLibrary().packetOut.emit.name) {
                        BUG_CHECK(mc->arguments->size() == 1,
                                  "Expected exactly 1 argument for %1%", mc);
                        auto arg = mc->arguments->at(0);
                        auto type = backend->getTypeMap().getType(arg, true);
                        if (type->is<IR::Type_Stack>()) {
                            int size = type->to<IR::Type_Stack>()->getSize();
                            for (int i=0; i < size; i++) {
                                auto j = backend->getExpressionConverter()->convert(arg);
                                auto e = j->to<Util::JsonObject>()->get("value");
                                BUG_CHECK(e->is<Util::JsonValue>(),
                                          "%1%: Expected a Json value", e->toString());
                                cstring ref = e->to<Util::JsonValue>()->getString();
                                ref += "[" + Util::toString(i) + "]";
                                result->append(ref);
                            }
                        } else if (type->is<IR::Type_Header>()) {
                            auto j = backend->getExpressionConverter()->convert(arg);
                            result->append(j->to<Util::JsonObject>()->get("value"));
                        } else {
                            ::error("%1%: emit only supports header and stack arguments, not %2%",
                                    arg, type);
                        }
                    }
                    continue;
                }
            }
        }
        ::error("%1%: not supported with a deparser on this target", s);
    }
    backend->getExpressionConverter()->simpleExpressionsOnly = false;
}

Util::IJson* DoDeparserBlockConversion::convertDeparser(const IR::P4Control* ctrl) {
    auto result = new Util::JsonObject();
    result->emplace("name", "deparser");  // at least in simple_router this name is hardwired
    result->emplace("id", nextId("deparser"));
    result->emplace_non_null("source_info", ctrl->sourceInfoJsonObj());
    auto order = mkArrayField(result, "order");
    convertDeparserBody(&ctrl->body->components, order);
    return result;
}

bool DoDeparserBlockConversion::preorder(const IR::PackageBlock* block) {
    for (auto it : block->constantValue) {
        if (it.second->is<IR::ControlBlock>()) {
            visit(it.second->getNode());
        }
    }
    return false;
}

bool DoDeparserBlockConversion::preorder(const IR::ControlBlock* block) {
    auto bt = backend->blockTypeMap.find(block);
    if (bt != backend->blockTypeMap.end()) {
        // only generate control block marked with @deparser
        LOG3("bt " << bt->second);
        if(!bt->second->getAnnotation("deparser")) {
            return false;
        }
    }
    const IR::P4Control* cont = block->container;
    auto deparserJson = convertDeparser(cont);
    backend->deparsers->append(deparserJson);
    return false;
}

} // namespace BMV2