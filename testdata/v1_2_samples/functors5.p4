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

parser p1()(bit<2> a) {
    state start {
        bit<2> w = 2;
    }
}

parser p2()(bit<2> a) {
    p1(a) x;
    state start {
        x.apply();
    }
}

parser nothing();
package m(nothing n);
m(p2(2w1)) main;
