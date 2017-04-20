#include <core.p4>
#include <v1model.p4>

header data_t {
    bit<32> f1_1;
    bit<32> f1_2;
    bit<32> f1_3;
    bit<32> f1_4;
    bit<32> f1_5;
    bit<32> f2_1;
    bit<32> f2_2;
    bit<32> f2_3;
    bit<32> f2_4;
    bit<32> f2_5;
    bit<32> f3_1;
    bit<32> f3_2;
    bit<32> f3_3;
    bit<32> f3_4;
    bit<32> f3_5;
    bit<32> f4_1;
    bit<32> f4_2;
    bit<32> f4_3;
    bit<32> f4_4;
    bit<32> f4_5;
    bit<32> f5_1;
    bit<32> f5_2;
    bit<32> f5_3;
    bit<32> f5_4;
    bit<32> f5_5;
    bit<32> f6_1;
    bit<32> f6_2;
    bit<32> f6_3;
    bit<32> f6_4;
    bit<32> f6_5;
    bit<32> f7_1;
    bit<32> f7_2;
    bit<32> f7_3;
    bit<32> f7_4;
    bit<32> f7_5;
    bit<32> f8_1;
    bit<32> f8_2;
    bit<32> f8_3;
    bit<32> f8_4;
    bit<32> f8_5;
}

struct metadata {
}

struct headers {
    @name("data") 
    data_t data;
}

parser ParserImpl(packet_in packet, out headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    @name("start") state start {
        packet.extract<data_t>(hdr.data);
        transition accept;
    }
}

control ingress(inout headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    @name(".set1") action set1_0(bit<32> v1, bit<32> v2, bit<32> v3, bit<32> v4, bit<32> v5) {
        hdr.data.f1_1 = v1;
        hdr.data.f1_2 = v2;
        hdr.data.f1_3 = v3;
        hdr.data.f1_4 = v4;
        hdr.data.f1_5 = v5;
    }
    @name(".noop") action noop_0() {
    }
    @name(".set2") action set2_0(bit<32> v1, bit<32> v2, bit<32> v3, bit<32> v4, bit<32> v5) {
        hdr.data.f2_1 = v1;
        hdr.data.f2_2 = v2;
        hdr.data.f2_3 = v3;
        hdr.data.f2_4 = v4;
        hdr.data.f2_5 = v5;
    }
    @name(".set3") action set3_0(bit<32> v1, bit<32> v2, bit<32> v3, bit<32> v4, bit<32> v5) {
        hdr.data.f3_1 = v1;
        hdr.data.f3_2 = v2;
        hdr.data.f3_3 = v3;
        hdr.data.f3_4 = v4;
        hdr.data.f3_5 = v5;
    }
    @name(".set4") action set4_0(bit<32> v1, bit<32> v2, bit<32> v3, bit<32> v4, bit<32> v5) {
        hdr.data.f4_1 = v1;
        hdr.data.f4_2 = v2;
        hdr.data.f4_3 = v3;
        hdr.data.f4_4 = v4;
        hdr.data.f4_5 = v5;
    }
    @name(".set5") action set5_0(bit<32> v1, bit<32> v2, bit<32> v3, bit<32> v4, bit<32> v5) {
        hdr.data.f5_1 = v1;
        hdr.data.f5_2 = v2;
        hdr.data.f5_3 = v3;
        hdr.data.f5_4 = v4;
        hdr.data.f5_5 = v5;
    }
    @name(".set6") action set6_0(bit<32> v1, bit<32> v2, bit<32> v3, bit<32> v4, bit<32> v5) {
        hdr.data.f6_1 = v1;
        hdr.data.f6_2 = v2;
        hdr.data.f6_3 = v3;
        hdr.data.f6_4 = v4;
        hdr.data.f6_5 = v5;
    }
    @name(".set7") action set7_0(bit<32> v1, bit<32> v2, bit<32> v3, bit<32> v4, bit<32> v5) {
        hdr.data.f7_1 = v1;
        hdr.data.f7_2 = v2;
        hdr.data.f7_3 = v3;
        hdr.data.f7_4 = v4;
        hdr.data.f7_5 = v5;
    }
    @name(".set8") action set8_0(bit<32> v1, bit<32> v2, bit<32> v3, bit<32> v4, bit<32> v5) {
        hdr.data.f8_1 = v1;
        hdr.data.f8_2 = v2;
        hdr.data.f8_3 = v3;
        hdr.data.f8_4 = v4;
        hdr.data.f8_5 = v5;
    }
    @name("tbl1") table tbl1_0 {
        actions = {
            set1_0();
            noop_0();
            @default_only NoAction();
        }
        key = {
            hdr.data.f1_1: exact @name("hdr.data.f1_1") ;
        }
        default_action = NoAction();
    }
    @name("tbl2") table tbl2_0 {
        actions = {
            set2_0();
            noop_0();
            @default_only NoAction();
        }
        key = {
            hdr.data.f2_1: exact @name("hdr.data.f2_1") ;
        }
        default_action = NoAction();
    }
    @name("tbl3") table tbl3_0 {
        actions = {
            set3_0();
            noop_0();
            @default_only NoAction();
        }
        key = {
            hdr.data.f3_1: exact @name("hdr.data.f3_1") ;
        }
        default_action = NoAction();
    }
    @name("tbl4") table tbl4_0 {
        actions = {
            set4_0();
            noop_0();
            @default_only NoAction();
        }
        key = {
            hdr.data.f4_1: exact @name("hdr.data.f4_1") ;
        }
        default_action = NoAction();
    }
    @name("tbl5") table tbl5_0 {
        actions = {
            set5_0();
            noop_0();
            @default_only NoAction();
        }
        key = {
            hdr.data.f5_1: exact @name("hdr.data.f5_1") ;
        }
        default_action = NoAction();
    }
    @name("tbl6") table tbl6_0 {
        actions = {
            set6_0();
            noop_0();
            @default_only NoAction();
        }
        key = {
            hdr.data.f6_1: exact @name("hdr.data.f6_1") ;
        }
        default_action = NoAction();
    }
    @name("tbl7") table tbl7_0 {
        actions = {
            set7_0();
            noop_0();
            @default_only NoAction();
        }
        key = {
            hdr.data.f7_1: exact @name("hdr.data.f7_1") ;
        }
        default_action = NoAction();
    }
    @name("tbl8") table tbl8_0 {
        actions = {
            set8_0();
            noop_0();
            @default_only NoAction();
        }
        key = {
            hdr.data.f8_1: exact @name("hdr.data.f8_1") ;
        }
        default_action = NoAction();
    }
    apply {
        tbl1_0.apply();
        tbl2_0.apply();
        tbl3_0.apply();
        tbl4_0.apply();
        tbl5_0.apply();
        tbl6_0.apply();
        tbl7_0.apply();
        tbl8_0.apply();
    }
}

control egress(inout headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    apply {
    }
}

control DeparserImpl(packet_out packet, in headers hdr) {
    apply {
        packet.emit<data_t>(hdr.data);
    }
}

control verifyChecksum(in headers hdr, inout metadata meta) {
    apply {
    }
}

control computeChecksum(inout headers hdr, inout metadata meta) {
    apply {
    }
}

V1Switch<headers, metadata>(ParserImpl(), verifyChecksum(), ingress(), egress(), computeChecksum(), DeparserImpl()) main;
