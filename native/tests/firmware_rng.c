/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "efi_entropy.h"
#include "csprng.h"
#include <openssl/evp.h>
#include <assert.h>
#include <stdio.h>
static unsigned enabled, fault, chunk=7, calls, emitted;
static uint64_t get(void *p,const void *algorithm,size_t n,uint8_t *out) {
    (void)p;assert(!algorithm&&n==32);
    for(unsigned i=0;i<n;i++)out[i]=fault==7?0:(uint8_t)(i+1);
    return fault==1?1:0;
}
static uint64_t submit(void *p,uint32_t n,uint8_t *in,uint32_t cap,uint8_t *out) {
    struct mnos_efi_tcg *t=p;unsigned v=(unsigned)(uintptr_t)t->other[0];
    unsigned h=v==2?12:14,requested=efi_entropy_be(in+10,h-10);
    assert(n==h&&cap==46&&requested&&requested<=32);
    assert(efi_entropy_be(in,2)==(v==2?0x8001u:0xc1u));
    assert(efi_entropy_be(in+2,4)==n&&efi_entropy_be(in+6,4)==(v==2?0x17bu:0x46u));
    calls++;if(fault==1&&calls>1)return 1;
    unsigned got=requested<chunk?requested:chunk;
    out[0]=v==2?0x80:0;out[1]=v==2?1:0xc4;out[5]=(uint8_t)(h+got);
    out[h-1]=(uint8_t)got;
    for(unsigned i=0;i<got;i++)out[h+i]=fault==7?0:(uint8_t)(++emitted);
    if(fault==2)out[h-1]=33;
    if(fault==3)out[5]=255;
    if(fault==4)out[1]=0;
    if(fault==5)out[9]=1;
    if(fault==6)out[h-1]=0;
    return 0;
}
static uint64_t locate(const void *id,void *registration,void **out) {
    const struct mnos_efi_guid *g=id;assert(!registration);
    static struct mnos_efi_rng rng={NULL,get};
    static struct mnos_efi_tcg t2={{(void *)2,NULL,NULL},submit},t1={{(void *)1,NULL,NULL},submit};
    *out=NULL;
    if(enabled==1&&g->a==0x3152bca5)*out=&rng;
    if(enabled==2&&g->a==0x607f766c)*out=&t2;
    if(enabled==3&&g->a==0xf541796d)*out=&t1;
    return *out?0:1;
}
static void zero(const void *p,size_t n){const uint8_t *b=p;for(size_t i=0;i<n;i++)assert(!b[i]);}
static void reference(const uint8_t key[32],uint8_t out[64]) {
    uint8_t iv[16]={0},plain[64]={0};int n;
    EVP_CIPHER_CTX *c=EVP_CIPHER_CTX_new();assert(c);
    assert(EVP_EncryptInit_ex(c,EVP_chacha20(),NULL,key,iv)==1);
    assert(EVP_EncryptUpdate(c,out,&n,plain,64)==1&&n==64);EVP_CIPHER_CTX_free(c);
}
int main(void) {
    uint8_t seed[32],out[8192],want[64];
    memset(seed,0xa5,sizeof seed);assert(!efi_entropy_seed(locate,seed));zero(seed,32);
    for(enabled=1;enabled<=3;enabled++) {
        fault=calls=emitted=0;assert(efi_entropy_seed(locate,seed)==enabled);
        for(unsigned i=0;i<32;i++)assert(seed[i]==i+1);
        if(enabled>1)assert(calls==5); /* TPM may return fewer bytes than requested. */
        for(fault=1;fault<=7;fault++) {
            if(enabled==1&&fault>1&&fault<7)continue;
            calls=emitted=0;memset(seed,0xa5,sizeof seed);
            assert(!efi_entropy_seed(locate,seed));zero(seed,32);
        }
    }
    struct csprng r={0};memset(out,0xa5,sizeof out);
    assert(!csprng_read(&r,out,sizeof out));zero(out,sizeof out);
    memset(seed,0,32);assert(!csprng_seed(&r,seed)&&!r.ready);
    for(unsigned i=0;i<32;i++)seed[i]=(uint8_t)(i+1);
    assert(csprng_seed(&r,seed));reference(seed,want);
    assert(csprng_read(&r,out,32));assert(!memcmp(out,want+32,32)&&!memcmp(r.key,want,32));
    reference(want,want);assert(csprng_read(&r,out+32,1));assert(out[32]==want[32]&&!memcmp(r.key,want,32));
    /* Firmware's seed is consumed once, not a finite 4 KiB entropy pool. */
    assert(csprng_read(&r,out,sizeof out));assert(memcmp(out,out+32,32));
    memset(seed,0,32);assert(!csprng_seed(&r,seed));zero(&r,sizeof r);
    puts("FIRMWARE RNG PASS: UEFI/TPM 1.2/TPM 2.0, partial replies, malformed/error replies, seed wiping and independent ChaCha20 oracle");
}
