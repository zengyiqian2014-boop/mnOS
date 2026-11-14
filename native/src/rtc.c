/* SPDX-License-Identifier: GPL-3.0-or-later: PC RTC is configured as UTC. */
#include "kernel.h"
#include "clock.h"
static uint8_t rtc_read(uint8_t reg) {outb(0x70,reg);return inb(0x71);}
static unsigned bcd(unsigned v) {return (v&15)+(v>>4)*10;}
bool arch_time(uint64_t *epoch) {
    static const uint8_t registers[]={0,2,4,7,8,9,0x32,0x0b};
    uint8_t a[8],b[8];
    for(unsigned attempt=0;attempt<10;++attempt) {
        if(rtc_read(0x0a)&128)continue;
        for(unsigned i=0;i<8;++i)a[i]=rtc_read(registers[i]);
        if(rtc_read(0x0a)&128)continue;
        for(unsigned i=0;i<8;++i)b[i]=rtc_read(registers[i]);
        if(memcmp(a,b,sizeof a))continue;
        unsigned sec=a[0],min=a[1],hour=a[2]&127,day=a[3],month=a[4],year=a[5],century=a[6];
        if(!(a[7]&4)) {sec=bcd(sec);min=bcd(min);hour=bcd(hour);day=bcd(day);month=bcd(month);year=bcd(year);century=bcd(century);}
        if(!(a[7]&2))hour=hour%12+((a[2]&128)?12:0);
        if(century!=20)century=20;
        return clock_calendar(century*100+year,month,day,hour,min,sec,epoch);
    }
    return false;
}
