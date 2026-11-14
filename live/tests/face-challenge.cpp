// SPDX-License-Identifier: GPL-3.0-or-later
#include "face-challenge.hpp"
#include <cassert>
#include <iostream>
using namespace mnos_face;
static bool exercise(bool move,bool reflect,bool same=true) {
    std::array<uint8_t,32> entropy{};for(unsigned i=0;i<32;++i)entropy[i]=uint8_t(i*7);
    Challenge challenge(entropy,0);Sample s;unsigned frames=0;
    Task last=Task::Failed;unsigned stage=0;
    while(challenge.state()!=Task::Failed&&challenge.state()!=Task::Complete&&frames++<1000) {
        if(challenge.state()!=last) {last=challenge.state();stage=0;}++stage;
        s={};s.time=frames*.1;s.faces=1;s.usable=true;s.eyes_open=true;s.match=same?0.9:0.2;
        if(move) {
            if(last==Task::Left)s.yaw=-22;
            if(last==Task::Right)s.yaw=22;
            if(last==Task::Blink&&stage>=5&&stage<=7)s.eyes_open=false;
        }
        s.color={.34,.33,.33};if(reflect&&challenge.light()<3) {s.color={.29,.29,.29};s.color[challenge.light()]=.42;}
        challenge.sample(s);
    }
    return challenge.state()==Task::Complete;
}
int main() {
    assert(exercise(true,true));assert(!exercise(false,true));assert(!exercise(true,false));assert(!exercise(true,true,false));
    std::array<uint8_t,32> random{};Challenge c(random,10);Sample s;s.time=9;c.sample(s);assert(c.state()==Task::Failed);
    Challenge stalled(random,0);s.time=3;stalled.sample(s);assert(stalled.state()==Task::Failed);
    std::cout<<"PASS experimental face challenge state: randomized prompts, blink/head sequence, color response, static/replayed observations, identity change, monotonic time and stale camera frames\n";
}
