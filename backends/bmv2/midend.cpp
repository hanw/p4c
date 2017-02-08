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

#include "midend.h"
#include "lower.h"
#if 0
#include "inlining.h"
#endif
#include "frontends/common/constantFolding.h"
#include "frontends/common/resolveReferences/resolveReferences.h"
#include "frontends/p4/evaluator/evaluator.h"
#include "frontends/p4/fromv1.0/v1model.h"
#include "frontends/p4/moveDeclarations.h"
#include "frontends/p4/simplify.h"
#include "frontends/p4/simplifyParsers.h"
#include "frontends/p4/strengthReduction.h"
#include "frontends/p4/typeChecking/typeChecker.h"
#include "frontends/p4/typeMap.h"
#include "frontends/p4/uniqueNames.h"
#include "frontends/p4/unusedDeclarations.h"
#include "midend/actionsInlining.h"
#include "midend/actionSynthesis.h"
#include "midend/convertEnums.h"
#include "midend/copyStructures.h"
#include "midend/eliminateTuples.h"
#include "midend/local_copyprop.h"
#include "midend/localizeActions.h"
#include "midend/moveConstructors.h"
#include "midend/nestedStructs.h"
#include "midend/removeLeftSlices.h"
#include "midend/removeParameters.h"
#include "midend/removeReturns.h"
#include "midend/simplifyKey.h"
#include "midend/simplifySelect.h"
#include "midend/validateProperties.h"
#include "midend/compileTimeOps.h"
#include "midend/isolateMethodCalls.h"
#include "midend/predication.h"

namespace BMV2 {

#if 0
void MidEnd::setup_for_P4_14(CompilerOptions&) {
    auto evaluator = new P4::EvaluatorPass(&refMap, &typeMap);
    // Inlining is simpler for P4 v1.0/1.1 programs, so we have a
    // specialized code path, which also generates slighly nicer
    // human-readable results.
    addPasses({
        evaluator,
        new P4::DiscoverInlining(&controlsToInline, &refMap, &typeMap, evaluator),
        new P4::InlineDriver(&controlsToInline, new SimpleControlsInliner(&refMap)),
        new P4::RemoveAllUnusedDeclarations(&refMap),
        new P4::TypeChecking(&refMap, &typeMap),
        new P4::DiscoverActionsInlining(&actionsToInline, &refMap, &typeMap),
        new P4::InlineActionsDriver(&actionsToInline, new SimpleActionsInliner(&refMap)),
        new P4::RemoveAllUnusedDeclarations(&refMap),
    });
}
#endif

class EnumOn32Bits : public P4::ChooseEnumRepresentation {
    bool convert(const IR::Type_Enum* type) const override {
        if (type->srcInfo.isValid()) {
            unsigned line = type->srcInfo.getStart().getLineNumber();
            auto sfl = Util::InputSources::instance->getSourceLine(line);
            cstring sourceFile = sfl.fileName;
            if (sourceFile.endsWith(P4V1::V1Model::instance.file.name))
                // Don't convert any of the standard enums
                return false;
        }
        return true;
    }
    unsigned enumSize(unsigned) const override
    { return 32; }
};

class SkipControls : public P4::ActionSynthesisPolicy {
    const std::set<cstring> *skip;

 public:
    explicit SkipControls(const std::set<cstring> *skip) : skip(skip) { CHECK_NULL(skip); }
    bool convert(const IR::P4Control* control) const {
        if (skip->find(control->name) != skip->end())
            return false;
        return true;
    }
};

MidEnd::MidEnd(CompilerOptions& options) {
    bool isv1 = options.isv1();
    setName("MidEnd");
    refMap.setIsV1(isv1);  // must be done BEFORE creating passes
    auto evaluator = new P4::EvaluatorPass(&refMap, &typeMap);
    auto convertEnums = new P4::ConvertEnums(&refMap, &typeMap, new EnumOn32Bits());
    auto v1controls = new std::set<cstring>();

    addPasses({
        convertEnums,
        // TODO(pierce): position this pass correctly
        new P4::IsolateMethodCalls(&refMap, &typeMap),
        new VisitFunctor([this, convertEnums]() { enumMap = convertEnums->getEnumMapping(); }),
        new P4::RemoveReturns(&refMap),
        new P4::MoveConstructors(&refMap),
        new P4::RemoveAllUnusedDeclarations(&refMap),
        new P4::ClearTypeMap(&typeMap),
        evaluator,
        new P4::Inline(&refMap, &typeMap, evaluator),
        new P4::InlineActions(&refMap, &typeMap),
        new P4::LocalizeAllActions(&refMap),
        new P4::UniqueNames(&refMap),
        new P4::UniqueParameters(&refMap, &typeMap),
        new P4::SimplifyControlFlow(&refMap, &typeMap),
        new P4::RemoveTableParameters(&refMap, &typeMap),
        new P4::RemoveActionParameters(&refMap, &typeMap),
        new P4::SimplifyKey(&refMap, &typeMap,
                            new P4::NonLeftValue(&refMap, &typeMap)),
        new P4::ConstantFolding(&refMap, &typeMap),
        new P4::StrengthReduction(),
        new P4::SimplifySelect(&refMap, &typeMap, true),  // require constant keysets
        new P4::SimplifyParsers(&refMap),
        new P4::StrengthReduction(),
        new P4::EliminateTuples(&refMap, &typeMap),
        new P4::CopyStructures(&refMap, &typeMap),
        new P4::NestedStructs(&refMap, &typeMap),
        new P4::Predication(&refMap),
        new P4::ConstantFolding(&refMap, &typeMap),
        new P4::LocalCopyPropagation(&refMap, &typeMap),
        new P4::ConstantFolding(&refMap, &typeMap),
        new P4::MoveDeclarations(),
        new P4::ValidateTableProperties({ "implementation", "size", "counters",
                                          "meters", "size", "support_timeout" }),
        new P4::SimplifyControlFlow(&refMap, &typeMap),
        new P4::CompileTimeOperations(),
        new P4::SynthesizeActions(&refMap, &typeMap, new SkipControls(v1controls)),
        new P4::MoveActionsToTables(&refMap, &typeMap),
        // Proper back-end
        new P4::TypeChecking(&refMap, &typeMap),
        new P4::SimplifyControlFlow(&refMap, &typeMap),
        new P4::RemoveLeftSlices(&refMap, &typeMap),
        new P4::TypeChecking(&refMap, &typeMap),
        new LowerExpressions(&typeMap),
        new P4::ConstantFolding(&refMap, &typeMap, false),
        evaluator,
        new VisitFunctor([this, evaluator]() { toplevel = evaluator->getToplevelBlock(); })
    });
}

}  // namespace BMV2
