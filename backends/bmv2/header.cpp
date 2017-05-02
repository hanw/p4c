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

#include "ir/ir.h"
#include "backend.h"
#include "header.h"

namespace BMV2 {

Util::JsonArray* ConvertHeaders::pushNewArray(Util::JsonArray* parent) {
    auto result = new Util::JsonArray();
    parent->append(result);
    return result;
}

bool ConvertHeaders::hasStructLikeMember(const IR::Type_StructLike *st, bool meta) {
    for (auto f : st->fields) {
        if (f->type->is<IR::Type_StructLike>() || f->type->is<IR::Type_Stack>()) {
            return true;
        }
    }
    return false;
}

void ConvertHeaders::pushFields(const IR::Type_StructLike *st,
                               Util::JsonArray *fields) {
    LOG4("Visit " << __FUNCTION__);
    for (auto f : st->fields) {
        auto ftype = backend->getTypeMap()->getType(f, true);
        if (ftype->to<IR::Type_StructLike>()) {
            BUG("%1%: nested structure", st);
        } else if (ftype->is<IR::Type_Boolean>()) {
            auto field = pushNewArray(fields);
            field->append(f->name.name);
            field->append(1); // boolWidth
            field->append(0);
        } else if (auto type = ftype->to<IR::Type_Bits>()) {
            auto field = pushNewArray(fields);
            field->append(f->name.name);
            field->append(type->size);
            field->append(type->isSigned);
        } else if (auto type = ftype->to<IR::Type_Varbits>()) {
            auto field = pushNewArray(fields);
            field->append(f->name.name);
            field->append(type->size); // FIXME -- where does length go?
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
        cstring name = backend->getRefMap()->newName("_padding");
        auto field = pushNewArray(fields);
        field->append(name);
        field->append(8 - padding);
        field->append(false);
    }
}

void ConvertHeaders::createHeaderTypeAndInstance(const IR::Type_StructLike* st, bool meta) {
    LOG4("Visit " << __FUNCTION__);
    BUG_CHECK(!hasStructLikeMember(st, meta),
              "%1%: Header has nested structure.", st);

    auto isTypeCreated = headerTypesCreated.find(st) != headerTypesCreated.end();
    if (!isTypeCreated) {
        cstring extName = st->name;
        LOG1("create header " << extName);
        auto result = new Util::JsonObject();
        cstring name = extVisibleName(st);
        result->emplace("name", name);
        result->emplace("id", nextId("header_types"));
        auto fields = mkArrayField(result, "fields");
        pushFields(st, fields);
        backend->headerTypes->append(result);
        headerTypesCreated.insert(st);
    }

    auto isInstanceCreated = headerInstancesCreated.find(st) != headerInstancesCreated.end();
    if (!isInstanceCreated) {
        unsigned id = nextId("headers");
        LOG1("create header instance " << id);
        auto json = new Util::JsonObject();
        //FIXME: fix name
        json->emplace("name", st->name);
        json->emplace("id", id);
        json->emplace("header_type", st->name);
        json->emplace("metadata", meta);
        json->emplace("pi_omit", true);
        headerInstances->append(json);
        headerInstancesCreated.insert(st);
    }
}

void ConvertHeaders::createStack(const IR::Type_Stack *stack, bool meta) {
    LOG4("Visit " << __FUNCTION__);
    auto json = new Util::JsonObject();
    json->emplace("name", "name");
    json->emplace("id", nextId("stack"));
    json->emplace("size", stack->getSize());
    auto type = backend->getTypeMap()->getTypeType(stack->elementType, true);
    auto ht = type->to<IR::Type_Header>();

    auto ht_name = stack->elementType->to<IR::Type_Header>()->name;
    json->emplace("header_type", ht_name);
    auto stackMembers = mkArrayField(json, "header_ids");
    for (unsigned i = 0; i < stack->getSize(); i++) {
        createHeaderTypeAndInstance(ht, meta);
    }
    backend->headerStacks->append(json);
}

void ConvertHeaders::createNestedStruct(const IR::Type_StructLike *st, bool meta) {
    LOG4("Visit " << __FUNCTION__);
    if (!hasStructLikeMember(st, meta)) {
        createHeaderTypeAndInstance(st, meta);
    } else {
        for (auto f : st->fields) {
            if (f->type->is<IR::Type_StructLike>()) {
                createNestedStruct(f->type->to<IR::Type_StructLike>(), meta);
            } else if (f->type->is<IR::Type_Stack>()) {
                createStack(f->type->to<IR::Type_Stack>(), meta);
            }
        }
    }
}

void ConvertHeaders::createJsonType(const IR::Type_StructLike *st) {
    auto isCreated = headerTypesCreated.find(st) != headerTypesCreated.end();
    if (!isCreated) {
        auto typeJson = new Util::JsonObject();
        cstring name = extVisibleName(st);
        typeJson->emplace("name", name);
        typeJson->emplace("id", nextId("header_types"));
        backend->headerTypes->append(typeJson);
        auto fields = mkArrayField(typeJson, "fields");
        pushFields(st, fields);
        headerTypesCreated.insert(st);
    }
}

/**
    Blocks are not in IR tree, use a custom visitor to traverse
*/
bool ConvertHeaders::preorder(const IR::PackageBlock *block) {
    for (auto it : block->constantValue) {
        if (it.second->is<IR::Block>()) {
            visit(it.second->getNode());
        }
    }
    return false;
}

bool ConvertHeaders::preorder(const IR::Type_Control* ctrl) {
    LOG3("Visiting " << dbp(ctrl));
    auto parent = getContext()->node;
    if (parent->is<IR::P4Control>()) {
        return true;
    }
    return false;
}

bool ConvertHeaders::preorder(const IR::Type_Parser* prsr) {
    LOG3("Visiting " << dbp(prsr));
    auto parent = getContext()->node;
    if (parent->is<IR::P4Parser>()) {
        return true;
    }
    return false;
}

bool ConvertHeaders::preorder(const IR::Type_Extern* ext) {
    LOG3("Visiting " << dbp(ext));
    return false;
}

bool ConvertHeaders::preorder(const IR::Parameter* param) {
    auto type = backend->getTypeMap()->getType(param->getNode(), true);
    LOG3("Visiting " << dbp(type));
    if (type->is<IR::Type_StructLike>()) {
        auto st = type->to<IR::Type_StructLike>();
        auto isCreated = headerTypesCreated.find(st) != headerTypesCreated.end();
        if (!isCreated) {
            createNestedStruct(st, false);
        }
    } else if (type->is<IR::Type_Bits>()){
        LOG3("... create scalars " << type);
        auto scalarFields = backend->scalarsStruct->get("fields")->to<Util::JsonArray>();
        CHECK_NULL(scalarFields);
        cstring newName = backend->getRefMap()->newName(type->getName() + "." + f->name);
        if (ft->is<IR::Type_Bits>()) {
            auto tb = ft->to<IR::Type_Bits>();
            auto field = pushNewArray(scalarFields);
            field->append(newName);
            field->append(tb->size);
            field->append(tb->isSigned);
            scalars_width += tb->size;
            backend->scalarMetadataFields.emplace(f, newName);
        } else if (ft->is<IR::Type_Boolean>()) {
            auto field = pushNewArray(scalarFields);
            field->append(newName);
            field->append(boolWidth);
            field->append(0);
            scalars_width += boolWidth;
            backend->scalarMetadataFields.emplace(f, newName);
        } else {
            BUG("%1%: Unhandled type for %2%", ft, f);
        }
    }
    return false;
}

} // namespace BMV2

