/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "guest_float.h"
#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static uint64_t seed=0xf123abd721849a25ULL;
static uint64_t random_bits(void){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;}
static uint64_t raw(double v){uint64_t n;memcpy(&n,&v,8);return n;}
static uint64_t raw32(float v){uint32_t n;memcpy(&n,&v,4);return n;}
static double real(uint64_t n){double v;memcpy(&v,&n,8);return v;}
static float real32(uint32_t n){float v;memcpy(&v,&n,4);return v;}
static unsigned flags(void){int f=fetestexcept(FE_ALL_EXCEPT);return ((f&FE_INEXACT)?GF_NX:0)|((f&FE_UNDERFLOW)?GF_UF:0)|((f&FE_OVERFLOW)?GF_OF:0)|((f&FE_DIVBYZERO)?GF_DZ:0)|((f&FE_INVALID)?GF_NV:0);}
static void check(struct guest_float got,uint64_t expected,unsigned f,unsigned bits,const char *op,uint64_t a,uint64_t b,uint64_t c,unsigned mode){
    unsigned cls=guest_float_class(expected,bits);if(cls&(256|512))expected=bits==32?0x7fc00000:UINT64_C(0x7ff8000000000000);
    if(got.bits!=expected||got.flags!=f){fprintf(stderr,"FAIL %s/%u mode=%u a=%016llx b=%016llx c=%016llx got=%016llx/%x expected=%016llx/%x\n",op,bits,mode,(unsigned long long)a,(unsigned long long)b,(unsigned long long)c,(unsigned long long)got.bits,got.flags,(unsigned long long)expected,f);assert(0);}
}
int main(void){
 const int modes[]={FE_TONEAREST,FE_TOWARDZERO,FE_DOWNWARD,FE_UPWARD};
 for(unsigned mode=0;mode<4;++mode){
  assert(fesetround(modes[mode])==0);
  for(unsigned bits=32;bits<=64;bits+=32)for(unsigned i=0;i<20000;++i){
   uint64_t a=random_bits(),b=random_bits(),c=random_bits();
   if(bits==32){a=(uint32_t)a;b=(uint32_t)b;c=(uint32_t)c;}
   /* Random non-NaN operands; explicit signalling cases below avoid compiler
    * or ABI-dependent signalling-NaN quieting in the host reference calls. */
   if((guest_float_class(a,bits)|guest_float_class(b,bits)|guest_float_class(c,bits))&(256|512))continue;
   volatile double x=real(a),y=real(b),z=real(c),rd=0;
   volatile float xf=real32(a),yf=real32(b),zf=real32(c),rf=0;
   uint64_t expected;unsigned f;
#define ORACLE(expr,expr32) do{feclearexcept(FE_ALL_EXCEPT);if(bits==64){rd=(expr);expected=raw(rd);}else{rf=(expr32);expected=raw32(rf);}f=flags();}while(0)
   ORACLE(x+y,xf+yf);check(guest_float_add(a,b,bits,mode),expected,f,bits,"add",a,b,c,mode);
   ORACLE(x*y,xf*yf);check(guest_float_mul(a,b,bits,mode),expected,f,bits,"mul",a,b,c,mode);
   ORACLE(x/y,xf/yf);check(guest_float_div(a,b,bits,mode),expected,f,bits,"div",a,b,c,mode);
   ORACLE(sqrt(x),sqrtf(xf));check(guest_float_sqrt(a,bits,mode),expected,f,bits,"sqrt",a,b,c,mode);
   ORACLE(fma(x,y,z),fmaf(xf,yf,zf));check(guest_float_fma(a,b,c,bits,mode),expected,f,bits,"fma",a,b,c,mode);
   if(bits==64){feclearexcept(FE_ALL_EXCEPT);rf=(float)x;expected=raw32(rf);f=flags();check(guest_float_convert(a,64,32,mode),expected,f,32,"convert",a,b,c,mode);}
   else{feclearexcept(FE_ALL_EXCEPT);rd=(double)xf;expected=raw(rd);f=flags();check(guest_float_convert(a,32,64,mode),expected,f,64,"convert",a,b,c,mode);}
   int64_t si=(int64_t)random_bits();uint64_t ui=random_bits();
   ORACLE((double)si,(float)si);check(guest_float_from_int((uint64_t)si,false,64,bits,mode),expected,f,bits,"from signed",si,0,0,mode);
   ORACLE((double)ui,(float)ui);check(guest_float_from_int(ui,true,64,bits,mode),expected,f,bits,"from unsigned",ui,0,0,mode);
  }
 }
 assert(fesetround(FE_TONEAREST)==0);
 struct guest_float q=guest_float_add(0x7ff0000000000001ULL,0,64,0);assert(q.bits==0x7ff8000000000000ULL&&q.flags==GF_NV);
 q=guest_float_fma(0x7ff0000000000000ULL,0,0x7ff8000000000000ULL,64,0);assert(q.flags==GF_NV);
 q=guest_float_to_int(raw(-0.5),true,64,64,0);assert(q.bits==0&&q.flags==GF_NX);
 q=guest_float_to_int(raw(-1),true,64,64,0);assert(q.bits==0&&q.flags==GF_NV);
 q=guest_float_to_int(raw(4294967295.0),true,32,64,0);assert(q.bits==UINT64_MAX&&!q.flags);
 q=guest_float_to_int(raw(-9223372036854775808.0),false,64,64,0);assert(q.bits==0x8000000000000000ULL&&!q.flags);
 q=guest_float_to_int(raw(9223372036854775808.0),false,64,64,0);assert(q.bits==0x7fffffffffffffffULL&&q.flags==GF_NV);
 q=guest_float_to_int(raw(2.5),false,64,64,4);assert(q.bits==3&&q.flags==GF_NX);
 puts("PASS integer-only guest binary32/64: arithmetic, fused rounding, exceptions, conversions, subnormals and directed rounding");
}
