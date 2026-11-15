/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Original integer arithmetic. Finite add/multiply/FMA are exact until their
 * single rounding step; exponent-aligned limbs also handle extreme cancellation.
 * Division and square root keep guard/round/sticky information. The host kernel
 * may keep its hardware FPU disabled throughout. */
#include "guest_float.h"
#include <string.h>
#define LIMBS 68
struct integer { uint64_t word[LIMBS]; unsigned count; };
struct decoded { uint64_t significand; int exponent; bool sign, infinity, nan, signalling; };
static unsigned precision(unsigned bits) { return bits==32?24:53; }
static int bias(unsigned bits) { return bits==32?127:1023; }
static uint64_t sign_bit(unsigned bits) { return UINT64_C(1)<<(bits-1); }
static uint64_t infinity(unsigned bits) { return (bits==32?UINT64_C(255):2047)<<(precision(bits)-1); }
static uint64_t quiet_nan(unsigned bits) { return infinity(bits)|(UINT64_C(1)<<(precision(bits)-2)); }
static unsigned bit_length(uint64_t value) { unsigned n=0;while(value){value>>=1;++n;}return n; }
static struct decoded decode(uint64_t raw,unsigned bits) {
    unsigned fraction=precision(bits)-1;
    uint64_t e=(raw>>fraction)&(bits==32?255:2047), mask=(UINT64_C(1)<<fraction)-1;
    struct decoded v={.significand=raw&mask,.sign=(raw&sign_bit(bits))!=0};
    if(e==(bits==32?255u:2047u)) {
        v.nan=v.significand!=0;v.infinity=!v.nan;
        v.signalling=v.nan&&!(v.significand&(UINT64_C(1)<<(fraction-1)));
    } else {
        if(e)v.significand|=UINT64_C(1)<<fraction;
        v.exponent=(e?(int)e:1)-bias(bits)-(int)fraction;
    }
    return v;
}
static void trim(struct integer *a) { while(a->count&&!a->word[a->count-1])--a->count; }
static void set(struct integer *a,uint64_t v,unsigned shift) {
    memset(a,0,sizeof *a);if(!v)return;
    unsigned i=shift/64,n=shift%64;
    a->word[i]=v<<n;a->count=i+1;
    if(n&&(v>>(64-n))){a->word[i+1]=v>>(64-n);++a->count;}
}
static unsigned length(const struct integer *a) { return a->count?(a->count-1)*64+bit_length(a->word[a->count-1]):0; }
static bool bit(const struct integer *a,unsigned i) { return i/64<a->count&&((a->word[i/64]>>(i%64))&1); }
static bool lower(const struct integer *a,unsigned n) {
    unsigned whole=n/64;
    for(unsigned i=0;i<whole&&i<a->count;++i)if(a->word[i])return true;
    return whole<a->count&&(n%64)&&(a->word[whole]&((UINT64_C(1)<<(n%64))-1));
}
static uint64_t shifted(const struct integer *a,unsigned n) {
    unsigned i=n/64,s=n%64;
    uint64_t v=i<a->count?a->word[i]>>s:0;
    if(s&&i+1<a->count)v|=a->word[i+1]<<(64-s);
    return v;
}
static int compare(const struct integer *a,const struct integer *b) {
    if(a->count!=b->count)return a->count>b->count?1:-1;
    for(unsigned i=a->count;i;--i)if(a->word[i-1]!=b->word[i-1])return a->word[i-1]>b->word[i-1]?1:-1;
    return 0;
}
static void plus(struct integer *a,const struct integer *b) {
    unsigned count=a->count>b->count?a->count:b->count;uint64_t carry=0;
    for(unsigned i=0;i<count;++i){uint64_t x=a->word[i],s=x+b->word[i],c=s<x,t=s+carry;carry=c||(t<s);a->word[i]=t;}
    a->count=count;if(carry)a->word[a->count++]=1;
}
static void minus(struct integer *a,const struct integer *b) {
    uint64_t borrow=0;
    for(unsigned i=0;i<a->count;++i){uint64_t x=a->word[i],y=b->word[i],t=x-y,s=t-borrow;borrow=x<y||t<borrow;a->word[i]=s;}
    trim(a);
}
static void move_left(struct integer *a,unsigned shift) {
    if(!a->count||!shift)return;
    struct integer out={0};unsigned w=shift/64,n=shift%64;
    for(unsigned i=0;i<a->count;++i){out.word[i+w]|=a->word[i]<<n;if(n)out.word[i+w+1]|=a->word[i]>>(64-n);}
    out.count=a->count+w+(n?1:0);trim(&out);*a=out;
}
static struct integer product(uint64_t a,uint64_t b) {
    uint64_t al=(uint32_t)a,ah=a>>32,bl=(uint32_t)b,bh=b>>32;
    uint64_t lo=al*bl,mid=ah*bl+(lo>>32),carry=mid>>32;
    mid=(uint32_t)mid+al*bh;
    struct integer out={0};out.word[0]=a*b;out.word[1]=ah*bh+carry+(mid>>32);out.count=2;trim(&out);return out;
}
static bool increment(bool sign,unsigned mode,uint64_t rounded,bool half,bool rest) {
    if(mode==0)return half&&(rest||(rounded&1));
    if(mode==2)return sign&&(half||rest);
    if(mode==3)return !sign&&(half||rest);
    if(mode==4)return half;
    return false;
}
static struct guest_float rounded(const struct integer *value,int exponent,bool sign,unsigned bits,unsigned mode) {
    uint64_t s=sign?sign_bit(bits):0;
    unsigned n=length(value),p=precision(bits);int minimum=1-bias(bits);
    if(!n)return (struct guest_float){s,0};
    int shift=(int)n-(int)p,subnormal=minimum-(int)(p-1)-exponent;
    if(subnormal>shift)shift=subnormal;
    bool half=false,rest=false;
    uint64_t q;
    if(shift>0){q=shifted(value,(unsigned)shift);half=bit(value,(unsigned)shift-1);rest=lower(value,(unsigned)shift-1);}
    else q=value->word[0]<<-shift;
    unsigned flags=half||rest?GF_NX:0;
    if(increment(sign,mode,q,half,rest))++q;
    int e=exponent+shift+(int)p-1;
    if(q>=(UINT64_C(1)<<p)){q>>=1;++e;}
    if(e>bias(bits)){
        bool finite=mode==1||(mode==2&&!sign)||(mode==3&&sign);
        return (struct guest_float){s|(infinity(bits)-(finite?1:0)),GF_OF|GF_NX};
    }
    if(q<(UINT64_C(1)<<(p-1))){if(flags)flags|=GF_UF;return (struct guest_float){s|q,flags};}
    return (struct guest_float){s|((uint64_t)(e+bias(bits))<<(p-1))|(q&((UINT64_C(1)<<(p-1))-1)),flags};
}
static struct guest_float invalid(unsigned bits) { return (struct guest_float){quiet_nan(bits),GF_NV}; }
static struct guest_float nan_result(unsigned bits,bool signalling) { return (struct guest_float){quiet_nan(bits),signalling?GF_NV:0}; }
static struct guest_float combine(struct integer a,int ae,bool as,struct integer b,int be,bool bs,unsigned bits,unsigned mode) {
    int e=ae<be?ae:be;
    move_left(&a,(unsigned)(ae-e));move_left(&b,(unsigned)(be-e));
    bool sign=as;
    if(as==bs)plus(&a,&b);
    else {int c=compare(&a,&b);if(c<0){minus(&b,&a);a=b;sign=bs;}else {minus(&a,&b);if(!c)sign=mode==2;}}
    return rounded(&a,e,sign,bits,mode);
}
struct guest_float guest_float_add(uint64_t a,uint64_t b,unsigned bits,unsigned mode) {
    struct decoded x=decode(a,bits),y=decode(b,bits);
    if(x.nan||y.nan)return nan_result(bits,x.signalling||y.signalling);
    if(x.infinity&&y.infinity&&x.sign!=y.sign)return invalid(bits);
    if(x.infinity||y.infinity)return (struct guest_float){(x.infinity?a:b)&(sign_bit(bits)|infinity(bits)),0};
    struct integer u,v;set(&u,x.significand,0);set(&v,y.significand,0);
    return combine(u,x.exponent,x.sign,v,y.exponent,y.sign,bits,mode);
}
struct guest_float guest_float_mul(uint64_t a,uint64_t b,unsigned bits,unsigned mode) {
    struct decoded x=decode(a,bits),y=decode(b,bits);bool sign=x.sign!=y.sign;
    if(x.nan||y.nan)return nan_result(bits,x.signalling||y.signalling);
    if((x.infinity&&!y.infinity&&!y.significand)||(y.infinity&&!x.infinity&&!x.significand))return invalid(bits);
    if(x.infinity||y.infinity)return (struct guest_float){infinity(bits)|(sign?sign_bit(bits):0),0};
    struct integer u=product(x.significand,y.significand);
    return rounded(&u,x.exponent+y.exponent,sign,bits,mode);
}
struct guest_float guest_float_fma(uint64_t a,uint64_t b,uint64_t c,unsigned bits,unsigned mode) {
    struct decoded x=decode(a,bits),y=decode(b,bits),z=decode(c,bits);bool sign=x.sign!=y.sign;
    bool bad=(x.infinity&&!y.infinity&&!y.nan&&!y.significand)||(y.infinity&&!x.infinity&&!x.nan&&!x.significand);
    if(x.nan||y.nan||z.nan)return nan_result(bits,bad||x.signalling||y.signalling||z.signalling);
    if(bad)return invalid(bits);
    if(x.infinity||y.infinity){if(z.infinity&&sign!=z.sign)return invalid(bits);return (struct guest_float){infinity(bits)|(sign?sign_bit(bits):0),0};}
    if(z.infinity)return (struct guest_float){c,0};
    struct integer u=product(x.significand,y.significand),v;set(&v,z.significand,0);
    return combine(u,x.exponent+y.exponent,sign,v,z.exponent,z.sign,bits,mode);
}
struct guest_float guest_float_div(uint64_t a,uint64_t b,unsigned bits,unsigned mode) {
    struct decoded x=decode(a,bits),y=decode(b,bits);bool sign=x.sign!=y.sign;
    uint64_t s=sign?sign_bit(bits):0;
    if(x.nan||y.nan)return nan_result(bits,x.signalling||y.signalling);
    if((x.infinity&&y.infinity)||(!x.infinity&&!y.infinity&&!x.significand&&!y.significand))return invalid(bits);
    if(x.infinity)return (struct guest_float){s|infinity(bits),0};
    if(y.infinity||!x.significand)return (struct guest_float){s,0};
    if(!y.significand)return (struct guest_float){s|infinity(bits),GF_DZ};
    unsigned shift=precision(bits)+3+bit_length(y.significand)-bit_length(x.significand);
    struct integer u;set(&u,x.significand,shift);uint64_t remainder=0,q=0;
    for(unsigned i=length(&u);i;--i){remainder=remainder*2+bit(&u,i-1);q<<=1;if(remainder>=y.significand){remainder-=y.significand;q|=1;}}
    if(remainder)q|=1;
    set(&u,q,0);return rounded(&u,x.exponent-y.exponent-(int)shift,sign,bits,mode);
}
struct guest_float guest_float_sqrt(uint64_t a,unsigned bits,unsigned mode) {
    struct decoded x=decode(a,bits);
    if(x.nan)return nan_result(bits,x.signalling);
    if(x.sign&&(x.infinity||x.significand))return invalid(bits);
    if(x.infinity||!x.significand)return (struct guest_float){a,0};
    int top=x.exponent+(int)bit_length(x.significand)-1;
    int e=(top>=0?top/2:-((-top+1)/2))-(int)precision(bits)-2;
    struct integer u;set(&u,x.significand,(unsigned)(x.exponent-2*e));
    uint64_t remainder=0,root=0;
    for(unsigned pairs=(length(&u)+1)/2;pairs;--pairs){
        unsigned i=(pairs-1)*2;remainder=(remainder<<2)|(bit(&u,i+1)?2u:0u)|(bit(&u,i)?1u:0u);
        uint64_t trial=(root<<2)|1;root<<=1;
        if(remainder>=trial){remainder-=trial;root|=1;}
    }
    if(remainder)root|=1;
    set(&u,root,0);return rounded(&u,e,false,bits,mode);
}
struct guest_float guest_float_convert(uint64_t a,unsigned from,unsigned to,unsigned mode) {
    struct decoded x=decode(a,from);
    if(x.nan)return nan_result(to,x.signalling);
    if(x.infinity)return (struct guest_float){infinity(to)|(x.sign?sign_bit(to):0),0};
    struct integer u;set(&u,x.significand,0);return rounded(&u,x.exponent,x.sign,to,mode);
}
struct guest_float guest_float_from_int(uint64_t a,bool unsign,unsigned width,unsigned bits,unsigned mode) {
    if(width==32)a=unsign?(uint32_t)a:(uint64_t)(int64_t)(int32_t)a;
    bool sign=!unsign&&(int64_t)a<0;
    struct integer u;set(&u,sign?0-a:a,0);return rounded(&u,0,sign,bits,mode);
}
struct guest_float guest_float_to_int(uint64_t a,bool unsign,unsigned width,unsigned bits,unsigned mode) {
    struct decoded x=decode(a,bits);
    uint64_t max=unsign?(width==32?UINT32_MAX:UINT64_MAX):((UINT64_C(1)<<(width-1))-1);
    uint64_t minimum=unsign?0:UINT64_C(1)<<(width-1);
    uint64_t value=0;bool half=false,rest=false,bad=x.nan||x.infinity;
    if(!bad&&x.significand){
        if(x.exponent>=0){if((unsigned)x.exponent+bit_length(x.significand)>64)bad=true;else value=x.significand<<x.exponent;}
        else {unsigned shift=(unsigned)-x.exponent;struct integer u;set(&u,x.significand,0);value=shifted(&u,shift);half=bit(&u,shift-1);rest=lower(&u,shift-1);}
        if(!bad&&increment(x.sign,mode,value,half,rest)){if(value==UINT64_MAX)bad=true;else ++value;}
        if(!bad&&((x.sign&&value>minimum)||(!x.sign&&value>max)))bad=true;
    }
    unsigned flags=half||rest?GF_NX:0;
    if(bad){value=x.sign&&!x.nan?minimum:max;flags=GF_NV;}
    else if(x.sign)value=0-value;
    if(width==32)value=(uint64_t)(int64_t)(int32_t)value;
    return (struct guest_float){value,flags};
}
unsigned guest_float_class(uint64_t a,unsigned bits) {
    struct decoded x=decode(a,bits);
    if(x.nan)return 1u<<(x.signalling?8:9);
    if(x.infinity)return 1u<<(x.sign?0:7);
    if(!x.significand)return 1u<<(x.sign?3:4);
    bool normal=x.significand&(UINT64_C(1)<<(precision(bits)-1));
    return 1u<<(normal?(x.sign?1:6):(x.sign?2:5));
}
bool guest_float_less(uint64_t a,uint64_t b,unsigned bits) {
    uint64_t mask=bits==32?UINT32_MAX:UINT64_MAX;a&=mask;b&=mask;
    uint64_t s=sign_bit(bits);
    if((a&~s)==0&&(b&~s)==0)return false;
    if((a^b)&s)return (a&s)!=0;
    return (a&s)?a>b:a<b;
}
