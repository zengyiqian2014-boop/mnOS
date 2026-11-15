/* SPDX-License-Identifier: GPL-3.0-or-later
 * UEFI RNG / TCG2 / TCG protocol adapter. Only GetRandom is sent to the TPM;
 * no ownership, PCR, NV storage, startup or clear operation is performed. */
#ifndef MNOS_EFI_ENTROPY_H
#define MNOS_EFI_ENTROPY_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct mnos_efi_guid {uint32_t a;uint16_t b,c;uint8_t d[8];};
typedef uint64_t (*mnos_efi_locate)(const void *,void *,void **);
struct mnos_efi_rng {void *info;uint64_t (*get)(void *,const void *,size_t,uint8_t *);};
struct mnos_efi_tcg {void *other[3];uint64_t (*submit)(void *,uint32_t,uint8_t *,uint32_t,uint8_t *);};
static inline void efi_entropy_wipe(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static inline uint32_t efi_entropy_be(const uint8_t *p,unsigned n){uint32_t v=0;while(n--)v=v*256+*p++;return v;}
static inline int efi_entropy_nonzero(const uint8_t seed[32]){unsigned v=0;for(unsigned i=0;i<32;i++)v|=seed[i];return v!=0;}
static inline int efi_entropy_tpm(struct mnos_efi_tcg *t,unsigned version,uint8_t seed[32]) {
    size_t done=0;unsigned attempts=0;
    while(done<32&&attempts++<32) {
        uint8_t request[14]={0},response[46]={0};unsigned header=version==2?12:14;
        request[0]=version==2?0x80:0;request[1]=version==2?1:0xc1;
        request[5]=(uint8_t)header;request[8]=version==2?1:0;
        request[9]=version==2?0x7b:0x46;request[header-1]=(uint8_t)(32-done);
        uint64_t status=t->submit(t,header,request,sizeof response,response);
        unsigned tag=efi_entropy_be(response,2),size=efi_entropy_be(response+2,4);
        unsigned got=efi_entropy_be(response+10,header-10);
        int valid=!status&&tag==(version==2?0x8001u:0xc4u)&&!efi_entropy_be(response+6,4)&&
            got&&got<=32-done&&size==header+got&&size<=sizeof response;
        if(valid){memcpy(seed+done,response+header,got);done+=got;}
        efi_entropy_wipe(response,sizeof response);
        if(!valid)break;
    }
    if(done==32&&efi_entropy_nonzero(seed))return 1;
    efi_entropy_wipe(seed,32);return 0;
}
/* Return the successful source: 1 = EFI RNG, 2 = TPM 2, 3 = TPM 1.2. */
static inline unsigned efi_entropy_seed(mnos_efi_locate locate,uint8_t seed[32]) {
    static const struct mnos_efi_guid rng_id={0x3152bca5,0xeade,0x433d,{0x86,0x2e,0xc0,0x1c,0xdc,0x29,0x1f,0x44}};
    static const struct mnos_efi_guid tcg2_id={0x607f766c,0x7455,0x42be,{0x93,0x0b,0xe4,0xd7,0x6d,0xb2,0x72,0x0f}};
    static const struct mnos_efi_guid tcg1_id={0xf541796d,0xa62e,0x4954,{0xa7,0x75,0x95,0x84,0xf6,0x1b,0x9c,0xdd}};
    struct mnos_efi_rng *rng=NULL;struct mnos_efi_tcg *tcg=NULL;
    efi_entropy_wipe(seed,32);
    if(!locate(&rng_id,NULL,(void **)&rng)&&rng&&rng->get&&
       !rng->get(rng,NULL,32,seed)&&efi_entropy_nonzero(seed))return 1;
    efi_entropy_wipe(seed,32);
    if(!locate(&tcg2_id,NULL,(void **)&tcg)&&tcg&&tcg->submit&&efi_entropy_tpm(tcg,2,seed))return 2;
    tcg=NULL;
    if(!locate(&tcg1_id,NULL,(void **)&tcg)&&tcg&&tcg->submit&&efi_entropy_tpm(tcg,1,seed))return 3;
    efi_entropy_wipe(seed,32);return 0;
}
#endif
