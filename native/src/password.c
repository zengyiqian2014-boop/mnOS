/* SPDX-License-Identifier: GPL-3.0-or-later
 * SHA-256 (FIPS 180-4), HMAC (RFC 2104), PBKDF2 (RFC 8018), 32-byte output.
 * Incremental work keeps password stretching out of a single long interrupt.
 */
#include "auth.h"
#include <string.h>
static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static uint32_t rr(uint32_t v, unsigned n) { return (v >> n) | (v << (32 - n)); }
void auth_wipe(void *p, size_t n) { volatile uint8_t *b = p; while (n--) *b++ = 0; }
static void compress(struct sha256 *s) {
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i) { const uint8_t *b = s->block + i * 4;
        w[i] = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]; }
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t a = w[i-15], b = w[i-2];
        w[i] = w[i-16] + (rr(a,7)^rr(a,18)^(a>>3)) + w[i-7] + (rr(b,17)^rr(b,19)^(b>>10));
    }
    uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for (unsigned i=0;i<64;++i) {
        uint32_t t=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^((~e)&g))+k[i]+w[i];
        uint32_t u=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
    auth_wipe(w,sizeof w);
}
static void init(struct sha256 *s) {
    *s=(struct sha256){.h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
}
static void update(struct sha256 *s, const void *data, size_t n) {
    const uint8_t *p=data; s->bytes+=n;
    while(n) { size_t take=64-s->used; if(take>n) take=n;
        memcpy(s->block+s->used,p,take);s->used+=take;p+=take;n-=take;
        if(s->used==64) { compress(s);s->used=0; }
    }
}
static void finish(struct sha256 *s,uint8_t out[32]) {
    uint64_t bits=s->bytes*8; uint8_t one=0x80,zero=0;
    update(s,&one,1);while(s->used!=56) update(s,&zero,1);
    uint8_t size[8];for(unsigned i=0;i<8;++i) size[7-i]=(uint8_t)(bits>>(i*8));
    update(s,size,8);
    for(unsigned i=0;i<32;++i) out[i]=(uint8_t)(s->h[i/4]>>(24-8*(i%4)));
    auth_wipe(s,sizeof *s);
}
void sha256_digest(const void *data,size_t n,uint8_t out[32]) { struct sha256 s;init(&s);update(&s,data,n);finish(&s,out); }
static void hmac(struct password_kdf *p,const void *data,size_t n,uint8_t out[32]) {
    struct sha256 s=p->inner;uint8_t digest[32];update(&s,data,n);finish(&s,digest);
    s=p->outer;update(&s,digest,32);finish(&s,out);auth_wipe(digest,sizeof digest);
}
void password_begin(struct password_kdf *p,const char *password,const uint8_t salt[16],uint32_t rounds) {
    memset(p,0,sizeof *p);uint8_t key[64]={0},pad[64],message[20];size_t n=strlen(password);
    if(n>64) sha256_digest(password,n,key);else memcpy(key,password,n);
    for(unsigned i=0;i<64;++i) pad[i]=key[i]^0x36;
    init(&p->inner);update(&p->inner,pad,64);
    for(unsigned i=0;i<64;++i) pad[i]=key[i]^0x5c;
    init(&p->outer);update(&p->outer,pad,64);
    memcpy(message,salt,16);memset(message+16,0,4);message[19]=1;
    hmac(p,message,20,p->u);memcpy(p->result,p->u,32);p->done=1;p->rounds=rounds;
    auth_wipe(key,sizeof key);auth_wipe(pad,sizeof pad);auth_wipe(message,sizeof message);
}
bool password_step(struct password_kdf *p,unsigned budget) {
    while(budget-- && p->done<p->rounds) {
        hmac(p,p->u,32,p->u);for(unsigned i=0;i<32;++i) p->result[i]^=p->u[i];++p->done;
    }
    return p->done>=p->rounds;
}
