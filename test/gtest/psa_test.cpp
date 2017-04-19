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

#include "gtest/gtest.h"
#include "ir/ir.h"
#include "helpers.h"
#include "lib/log.h"

#include "frontends/p4/typeMap.h"
#include "frontends/common/resolveReferences/referenceMap.h"
#include "frontends/common/resolveReferences/resolveReferences.h"
#include "frontends/p4/evaluator/evaluator.h"
#include "frontends/p4/inferArchitecture.h"
#include "frontends/p4/createBuiltins.h"
#include "backends/bmv2/jsonconverter.h"

#include "p4/createBuiltins.h"
#include "p4/typeChecking/typeChecker.h"

#include "ir/json_generator.h"

using namespace P4;

TEST(arch, psa_infer) {
    Log::addDebugSpec("inferArchitecture:1");
    Log::addDebugSpec("jsonconverter:1");
    Log::addDebugSpec("psa_test:1");
    std::string program = P4_SOURCE(R"(
        error {
            NoError
        }
        extern packet_in {
            void extract<T> (out T hdr);
        }
        parser Parser<H,M>(packet_in buffer, out H parsed_hdr, inout M user_meta);
        enum CounterType_t { packets, bytes, packets_and_bytes  }
        extern Counter<W> {
            Counter(int<32> number, W size_in_bits, CounterType_t type);
            void count(in W index, in W increment);
        }
        control ingress<H, M> (inout H hdr, inout M meta);
        package PSA<H, M> (Parser<H,M> pr, ingress<H,M> ig);
        struct ParsedHeaders {
            bit<32> hdr;
        }
        struct Metadata {
            bit<32> md;
        }
        parser MyParser (packet_in buffer, out ParsedHeaders p, inout Metadata m) {
            state start { transition accept; }
        }
        control MyIngress (inout ParsedHeaders p, inout Metadata m) {
            apply{
            }
        }
        MyParser() pr;
        MyIngress() ig;
        PSA(pr, ig) main;
    )");
    const IR::P4Program* pgm = parse_string(program);
    ReferenceMap refMap;
    TypeMap      typeMap;
    // frontend passes
    PassManager  passes({
        new CreateBuiltins(),
        new TypeChecking(&refMap, &typeMap),
        new InferArchitecture(&typeMap)
    });
    pgm = pgm->apply(passes);
    // dump(pgm);
    // midend pass
    CompilerOptions options;
    BMV2::JsonConverter converter(options);
    const IR::ToplevelBlock *toplevel = nullptr;
    P4::ConvertEnums::EnumMapping enumMap;
    auto evaluator = new P4::EvaluatorPass(&refMap, &typeMap);
    auto convertEnums = new P4::ConvertEnums(&refMap, &typeMap, new EnumOn32Bits());
    PassManager midpass({
        convertEnums,
        new VisitFunctor([this, convertEnums, &enumMap]() { enumMap = convertEnums->getEnumMapping(); }),
        evaluator,
        new VisitFunctor([this, evaluator, &toplevel]() { toplevel = evaluator->getToplevelBlock(); })
    });
    pgm = pgm->apply(midpass);
    converter.convert(&refMap, &typeMap, toplevel, &enumMap);
    converter.serialize(std::cout);
}

