#include <core.p4>

header Header {
    bit<32> data1;
    bit<32> data2;
    bit<32> data3;
}

extern void func(in Header h);
extern bit<32> g(inout bit<32> v, in bit<32> w);
parser p1(packet_in p, out Header h) {
    Header[2] stack;
    bool c_1;
    bool d;
    bit<32> tmp_4;
    bit<32> tmp_5;
    bit<32> tmp_6;
    bit<32> tmp_7;
    bit<32> tmp_8;
    state start {
        h.data1 = 32w0;
        func(h);
        tmp_4 = h.data2;
        tmp_5 = h.data2;
        tmp_6 = h.data2;
        tmp_7 = g(tmp_5, tmp_6);
        tmp_8 = tmp_7;
        g(tmp_4, tmp_8);
        h.data2 = h.data3 + 32w1;
        stack[1].isValid();
        transition select(h.isValid()) {
            true: next1;
            false: next2;
            default: noMatch;
        }
    }
    state next1 {
        d = false;
        transition next3;
    }
    state next2 {
        c_1 = true;
        d = c_1;
        transition next3;
    }
    state next3 {
        transition accept;
    }
    state noMatch {
        verify(false, error.NoMatch);
        transition reject;
    }
}

control c(out bit<32> v) {
    bit<32> d_2;
    bit<32> setByAction;
    bit<32> e;
    bool touched;
    @name("a1") action a1_0() {
        setByAction = 32w1;
    }
    @name("a1") action a1_2() {
        setByAction = 32w1;
    }
    @name("a2") action a2_0() {
        setByAction = 32w1;
    }
    @name("t") table t {
        actions = {
            a1_0();
            a2_0();
        }
        default_action = a1_0();
    }
    action act() {
        e = 32w1;
    }
    action act_0() {
        d_2 = 32w1;
    }
    action act_1() {
        touched = true;
    }
    action act_2() {
        e = e + 32w1;
    }
    table tbl_act {
        actions = {
            act_0();
        }
        const default_action = act_0();
    }
    table tbl_act_0 {
        actions = {
            act();
        }
        const default_action = act();
    }
    table tbl_act_1 {
        actions = {
            act_2();
        }
        const default_action = act_2();
    }
    table tbl_act_2 {
        actions = {
            act_1();
        }
        const default_action = act_1();
    }
    table tbl_a1 {
        actions = {
            a1_2();
        }
        const default_action = a1_2();
    }
    apply {
        tbl_act.apply();
        if (e > 32w0)
            tbl_act_0.apply();
        else
            ;
        tbl_act_0.apply();
        switch (t.apply().action_run) {
            a1_0: {
            }
        }

        if (e > 32w0)
            t.apply();
        else
            tbl_a1.apply();
    }
}

parser proto(packet_in p, out Header h);
control cproto(out bit<32> v);
package top(proto _p, cproto _c);
top(p1(), c()) main;
