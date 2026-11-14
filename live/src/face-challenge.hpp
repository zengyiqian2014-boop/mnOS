// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental monocular challenge checks, NOT a certified presentation-attack
// detector or a replacement for password/TOTP. No inferred physical depth.
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace mnos_face {
enum class Task { Center, Blink, Left, Right, Color, Complete, Failed };
struct Sample {
    double time=0,yaw=0,match=0;
    unsigned faces=0;
    bool eyes_open=false,usable=false;
    std::array<double,3> color{}; // RGB skin-region means, normalized by brightness
};
class Challenge {
    std::array<Task,3> order_{};
    std::array<unsigned,6> colors_{};
    std::array<std::array<double,3>,6> reflections_{};
    double start_=0,stage_=0,last_=0,baseline_=0,closed_at_=0;
    unsigned task_=0,stable_=0,blink_=0,frame_=0,color_=0;
    Task state_=Task::Center;
public:
    Challenge(const std::array<uint8_t,32> &random,double now):start_(now),stage_(now),last_(now) {
        order_={Task::Blink,Task::Left,Task::Right};
        for(unsigned i=2;i>0;--i)std::swap(order_[i],order_[random[i]%(i+1)]);
        unsigned previous=3;
        for(unsigned i=0;i<colors_.size();++i) {unsigned c=random[8+i]%3;if(c==previous)c=(c+1)%3;colors_[i]=previous=c;}
    }
    Task state() const {return state_;}
    unsigned light() const {return state_==Task::Color?colors_[color_]:3;}
    void fail() {state_=Task::Failed;}
    void sample(const Sample &s) {
        if(state_==Task::Failed||state_==Task::Complete)return;
        if(!std::isfinite(s.time)||s.time<last_||s.time-start_>90||s.time-last_>2) {fail();return;}
        last_=s.time;
        if(!s.usable||s.faces!=1||!std::isfinite(s.yaw)||!std::isfinite(s.match)||s.match<0.55) {
            stable_=0;blink_=0;if(s.time-stage_>15)fail();return;
        }
        auto next=[&] {
            stable_=blink_=0;stage_=s.time;
            state_=task_<order_.size()?order_[task_++]:Task::Color;
        };
        if(state_==Task::Center) {
            if(!s.eyes_open||std::abs(s.yaw)>25) {stable_=0;return;}
            baseline_=s.yaw;if(++stable_>=8)next();return;
        }
        if(state_==Task::Blink) {
            if(std::abs(s.yaw-baseline_)>12) {blink_=stable_=0;return;}
            if(blink_==0&&s.eyes_open) {if(++stable_>=3) {blink_=1;stable_=0;}}
            else if(blink_==1&&!s.eyes_open) {if(++stable_>=2) {blink_=2;closed_at_=s.time;stable_=0;}}
            else if(blink_==2&&s.eyes_open) {
                if(s.time-closed_at_>1.5) {blink_=stable_=0;return;}
                if(++stable_>=2)next();
            }
        } else if(state_==Task::Left||state_==Task::Right) {
            double delta=s.yaw-baseline_;
            bool turned=state_==Task::Left?delta<-17:delta>17;
            if(turned&&s.eyes_open) {if(++stable_>=4)next();}else stable_=0;
        } else if(state_==Task::Color) {
            if(std::abs(s.yaw-baseline_)>12)return;
            if(s.time-stage_>=0.65) {
                for(unsigned i=0;i<3;++i) {
                    if(!std::isfinite(s.color[i])||s.color[i]<0||s.color[i]>1) {fail();return;}
                    reflections_[color_][i]+=s.color[i];
                }
                ++frame_;
            }
            if(s.time-stage_>=1.4&&frame_>=4) {
                for(double &v:reflections_[color_])v/=frame_;
                frame_=0;stage_=s.time;
                if(++color_==colors_.size()) {
                    unsigned consistent=0;
                    for(unsigned i=1;i<colors_.size();++i) {
                        unsigned c=colors_[i],p=colors_[i-1];
                        double response=(reflections_[i][c]-reflections_[i][p])-(reflections_[i-1][c]-reflections_[i-1][p]);
                        if(response>0.012)++consistent;
                    }
                    state_=consistent>=4?Task::Complete:Task::Failed;
                }
            }
        }
        if(state_!=Task::Complete&&s.time-stage_>15)fail();
    }
};
}
