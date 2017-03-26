#include <core.p4>
#include <v1model.p4>

struct H {
}

struct M {
    bit<32> hash1;
}

parser ParserI(packet_in pk, out H hdr, inout M meta, inout standard_metadata_t smeta) {
    state start {
        transition accept;
    }
}

control IngressI(inout H hdr, inout M meta, inout standard_metadata_t smeta) {
    @name("drop") action drop_0() {
        smeta.drop = 1w1;
    }
    @name("as") action_selector(HashAlgorithm.identity, 32w1024, 32w10) as_0;
    @name("indirect_ws") table indirect_ws_0 {
        key = {
            meta.hash1: selector @name("meta.hash1") ;
        }
        actions = {
            drop_0();
            NoAction();
        }
        const default_action = NoAction();
        @name("ap_ws") implementation = as_0;
    }
    @name("indirect_ws_1") table indirect_ws_2 {
        key = {
            meta.hash1: selector @name("meta.hash1") ;
        }
        actions = {
            drop_0();
            NoAction();
        }
        const default_action = NoAction();
        @name("ap_ws") implementation = as_0;
    }
    apply {
        indirect_ws_0.apply();
        indirect_ws_2.apply();
    }
}

control EgressI(inout H hdr, inout M meta, inout standard_metadata_t smeta) {
    apply {
    }
}

control DeparserI(packet_out pk, in H hdr) {
    apply {
    }
}

control VerifyChecksumI(in H hdr, inout M meta) {
    apply {
    }
}

control ComputeChecksumI(inout H hdr, inout M meta) {
    apply {
    }
}

V1Switch<H, M>(ParserI(), VerifyChecksumI(), IngressI(), EgressI(), ComputeChecksumI(), DeparserI()) main;
