/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "factor.h"
#include "auth.h"
#include "../vendor/monocypher/monocypher.h"
#include <string.h>
bool factor_same(const uint8_t *a,const uint8_t *b,size_t n) {
    uint8_t difference=0;for(size_t i=0;i<n;++i)difference|=a[i]^b[i];return difference==0;
}
static uint32_t rol(uint32_t x,unsigned n) {return (x<<n)|(x>>(32-n));}
/* SHA-1 is used ONLY inside standardized HOTP HMAC, never for passwords. */
static void sha1(const uint8_t *data,size_t n,uint8_t out[20]) {
    uint32_t h[]={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0},w[80];
    size_t total=(n+9+63)&~(size_t)63;
    for(size_t at=0;at<total;at+=64) {
        memset(w,0,64);for(unsigned i=0;i<64;++i) {
            size_t p=at+i;uint8_t v=0;
            if(p<n)v=data[p];else if(p==n)v=128;else if(p>=total-8)v=(uint8_t)(((uint64_t)n*8)>>(8*(total-1-p)));
            w[i/4]|=(uint32_t)v<<(24-8*(i%4));
        }
        for(unsigned i=16;i<80;++i)w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
        for(unsigned i=0;i<80;++i) {
            uint32_t f=i<20?((b&c)|(~b&d)):i<40?(b^c^d):i<60?((b&c)|(b&d)|(c&d)):(b^c^d);
            uint32_t k=i<20?0x5a827999:i<40?0x6ed9eba1:i<60?0x8f1bbcdc:0xca62c1d6;
            uint32_t t=rol(a,5)+f+e+k+w[i];e=d;d=c;c=rol(b,30);b=a;a=t;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
    }
    for(unsigned i=0;i<20;++i)out[i]=(uint8_t)(h[i/4]>>(24-8*(i%4)));
    auth_wipe(w,sizeof w);auth_wipe(h,sizeof h);
}
uint32_t factor_hotp(const void *secret,size_t length,uint64_t step,unsigned digits) {
    uint8_t key[64]={0},inner[72],outer[84],mac[20];
    if(length>64)sha1(secret,length,key);else memcpy(key,secret,length);
    for(unsigned i=0;i<64;++i) {inner[i]=key[i]^0x36;outer[i]=key[i]^0x5c;}
    for(unsigned i=0;i<8;++i)inner[71-i]=(uint8_t)(step>>(8*i));
    sha1(inner,sizeof inner,outer+64);sha1(outer,sizeof outer,mac);
    unsigned offset=mac[19]&15;
    uint32_t code=((uint32_t)(mac[offset]&127)<<24)|((uint32_t)mac[offset+1]<<16)|
                  ((uint32_t)mac[offset+2]<<8)|mac[offset+3];
    auth_wipe(key,sizeof key);auth_wipe(inner,sizeof inner);auth_wipe(outer,sizeof outer);
    auth_wipe(mac,sizeof mac);return code%(digits==8?100000000u:1000000u);
}
uint32_t factor_totp(const uint8_t secret[32],uint64_t step,unsigned digits) {return factor_hotp(secret,32,step,digits);}
void factor_base32(const uint8_t secret[32],char out[53]) {
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    uint32_t accumulator=0;unsigned bits=0,at=0;
    for(unsigned i=0;i<32;++i) {
        accumulator=(accumulator<<8)|secret[i];bits+=8;
        while(bits>=5) {bits-=5;out[at++]=alphabet[(accumulator>>bits)&31];}
    }
    if(bits)out[at++]=alphabet[(accumulator<<(5-bits))&31];out[at]=0;
}
static void header(const struct factor_record *r,uint32_t uid,uint8_t out[296]) {
    memcpy(out,"mnOS-TOTP-v1",12);
    for(unsigned i=0;i<4;++i)out[12+i]=(uint8_t)(uid>>(8*i));
    memcpy(out+16,r->salt,16);
    for(unsigned i=0;i<8;++i)out[32+i]=(uint8_t)(r->last_step>>(8*i));
    /* The exact recovery-code set is authenticated with the encrypted seed. */
    memcpy(out+40,r->recovery,sizeof r->recovery);
}
/* Header is 12 + 4 + 16 + 8 + 256 bytes. */
bool factor_open(const struct factor_record *r,uint32_t uid,const uint8_t key[32],uint8_t secret[32]) {
    uint8_t ad[296];header(r,uid,ad);
    int result=crypto_aead_unlock(secret,r->tag,key,r->nonce,ad,sizeof ad,r->ciphertext,32);
    auth_wipe(ad,sizeof ad);
    if(r->enabled!=1||result) {auth_wipe(secret,32);return false;}return true;
}
bool factor_seal(struct factor_record *r,uint32_t uid,const uint8_t key[32],const uint8_t secret[32],factor_random random) {
    uint8_t ad[296],nonce[24];
    if(!random(nonce,sizeof nonce))return false;
    r->enabled=1;memcpy(r->nonce,nonce,sizeof nonce);header(r,uid,ad);
    crypto_aead_lock(r->ciphertext,r->tag,key,r->nonce,ad,sizeof ad,secret,32);
    auth_wipe(ad,sizeof ad);auth_wipe(nonce,sizeof nonce);return true;
}
bool factor_check(const uint8_t secret[32],uint64_t time,uint64_t last,const char *input,uint64_t *accepted) {
    if(strlen(input)!=6||time<1577836800ULL||time>253402300799ULL)return false;
    uint32_t code=0;for(unsigned i=0;i<6;++i) {if(input[i]<'0'||input[i]>'9')return false;code=code*10+(unsigned)(input[i]-'0');}
    uint64_t current=time/30,match=0;
    for(unsigned i=0;i<3;++i) {
        uint64_t candidate=current+i-1;
        uint32_t difference=factor_totp(secret,candidate,6)^code;
        if(!difference&&candidate>last)match=candidate;
    }
    if(!match)return false;*accepted=match;return true;
}
void factor_recovery_hash(const char *code,const uint8_t salt[16],uint8_t out[32]) {
    auth_hmac_sha256(salt,16,code,strlen(code),out);
}
