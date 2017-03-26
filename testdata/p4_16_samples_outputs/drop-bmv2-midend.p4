#include <core.p4>
#include <v1model.p4>

struct H {
}

struct M {
}

parser ParserI(packet_in pk, out H hdr, inout M meta, inout standard_metadata_t smeta) {
    state start {
        transition accept;
    }
}

control IngressI(inout H hdr, inout M meta, inout standard_metadata_t smeta) {
    standard_metadata_t smeta_0;
    @name("drop_1") action drop_0() {
        smeta_0.drop = 1w1;
        smeta.ingress_port = smeta_0.ingress_port;
        smeta.egress_spec = smeta_0.egress_spec;
        smeta.egress_port = smeta_0.egress_port;
        smeta.clone_spec = smeta_0.clone_spec;
        smeta.instance_type = smeta_0.instance_type;
        smeta.drop = smeta_0.drop;
        smeta.recirculate_port = smeta_0.recirculate_port;
        smeta.packet_length = smeta_0.packet_length;
    }
    @name("forward") table forward {
        key = {
        }
        actions = {
            drop_0();
        }
        const default_action = drop_0();
    }
    apply {
        forward.apply();
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
