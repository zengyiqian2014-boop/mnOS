/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "csprng.h"
#include "../vendor/monocypher/monocypher.h"
#include <string.h>
bool csprng_seed(struct csprng *r,const uint8_t seed[32]) {
    unsigned any=0;for(unsigned i=0;i<32;i++)any|=seed[i];
    uint8_t copy[32];memcpy(copy,seed,32);crypto_wipe(r,sizeof *r);
    if(any){memcpy(r->key,copy,32);r->ready=true;}
    crypto_wipe(copy,sizeof copy);return r->ready;
}
bool csprng_read(struct csprng *r,uint8_t *out,size_t n) {
    if(!r->ready){crypto_wipe(out,n);return false;}
    const uint8_t nonce[8]={0};uint8_t block[64];
    while(n) {
        /* Half of each ChaCha20 block replaces the key and is never returned.
         * Old state is erased; a later state disclosure cannot recover earlier
         * output. This expands a real seed; it does not manufacture entropy. */
        crypto_chacha20_djb(block,NULL,64,r->key,nonce,0);
        memcpy(r->key,block,32);size_t take=n<32?n:32;
        memcpy(out,block+32,take);out+=take;n-=take;
        crypto_wipe(block,sizeof block);
    }
    return true;
}
