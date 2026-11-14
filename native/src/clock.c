/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "clock.h"
static bool leap(unsigned year) {return year%4==0&&(year%100!=0||year%400==0);}
bool clock_calendar(unsigned year,unsigned month,unsigned day,unsigned hour,unsigned minute,unsigned second,uint64_t *epoch) {
    static const unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(year<2020||year>2099||month<1||month>12||day<1||day>lengths[month-1]+(month==2&&leap(year))||hour>23||minute>59||second>59)return false;
    uint64_t days=0;for(unsigned y=1970;y<year;++y)days+=365+leap(y);
    for(unsigned m=1;m<month;++m)days+=lengths[m-1]+(m==2&&leap(year));
    *epoch=((days+day-1)*24+hour)*3600+minute*60+second;return true;
}
