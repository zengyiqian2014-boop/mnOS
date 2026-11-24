/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "guest.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void put64(unsigned char *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(n>>(i*8));}
static void ins(struct guest *g,uint32_t n){unsigned char b[4];for(unsigned i=0;i<4;i++)b[i]=(unsigned char)(n>>(i*8));assert(guest_copy(g,g->pc,b,4));}
static void reset(struct guest *g,unsigned char *ram){memset(ram,0,GUEST_RAM);guest_init(g,ram,GUEST_RAM);}
static void permission_map(struct guest *g,unsigned char *ram,unsigned code,unsigned data) {
    reset(g,ram);
    put64(ram,((GUEST_BASE+4096)>>12)<<10|1);
    put64(ram+4096+2*8,((GUEST_BASE+8192)>>12)<<10|1);
    put64(ram+8192,((GUEST_BASE+GUEST_LOAD)>>12)<<10|code);
    put64(ram+8200,((GUEST_BASE+0x5000)>>12)<<10|data);
    ins(g,0x0005b503); /* ld a0,0(a1) */
    put64(ram+0x5000,0x12345678);g->x[11]=0x401000;
    g->satp=(UINT64_C(8)<<60)|(GUEST_BASE>>12);g->pc=0x400000;
}
int main(void) {
    unsigned char *ram=malloc(GUEST_RAM+32);assert(ram);struct guest g;
    memset(ram+GUEST_RAM,0xa5,32);
    reset(&g,ram);ins(&g,0xfff00093);guest_run(&g,1);assert(g.x[1]==UINT64_MAX);
    ins(&g,0x00100113);guest_run(&g,1);assert(g.x[2]==1);
    ins(&g,0x0220c1b3);guest_run(&g,1);assert(g.x[3]==UINT64_MAX); /* div -1/1 */
    g.x[1]=UINT64_C(1)<<63;g.x[2]=UINT64_MAX;ins(&g,0x0220c1b3);guest_run(&g,1);assert(g.x[3]==(UINT64_C(1)<<63));
    g.x[2]=0;ins(&g,0x0220c1b3);guest_run(&g,1);assert(g.x[3]==UINT64_MAX);
    /* Compressed C.LI x1,-1 and C.ADDI x1,1, no borrowed decode helper. */
    unsigned char code[]={0xfd,0x50,0x85,0x00};assert(guest_copy(&g,g.pc,code,4));guest_run(&g,2);assert(g.x[1]==0);
    /* A cached decode must follow new instruction bytes at the same PC. */
    reset(&g,ram);uint64_t changed_pc=g.pc;
    ins(&g,0x0505);guest_run(&g,1);assert(g.x[10]==1);
    g.pc=changed_pc;ins(&g,0x0509);guest_run(&g,1);assert(g.x[10]==3);
    g.pc=changed_pc;ins(&g,0x0505);guest_run(&g,1);assert(g.x[10]==4);
    reset(&g,ram);g.x[1]=GUEST_BASE+GUEST_RAM;ins(&g,0x0000b023);guest_run(&g,1);
    assert(g.stopped==2&&g.cause==7&&g.tval==GUEST_BASE+GUEST_RAM);
    for(unsigned i=0;i<32;i++)assert(ram[GUEST_RAM+i]==0xa5);
    reset(&g,ram);g.x[1]=GUEST_BASE+1;ins(&g,0x0000b103);guest_run(&g,1);assert(g.cause==4);
    reset(&g,ram);g.privilege=0;ins(&g,0x18002173);guest_run(&g,1);assert(g.cause==2); /* U-mode satp read */
    reset(&g,ram);g.privilege=0;ins(&g,0xc0102773);guest_run(&g,1);assert(g.x[14]==g.time&&!g.stopped); /* vDSO rdtime */
    g.counteren=0;ins(&g,0xc0102773);guest_run(&g,1);assert(g.cause==2); /* S-mode may revoke access */
    /* Sv39: map virtual 0x400000 to a user RX page. The hardware-set A bit
     * must appear; a store through it must fault even after a fetch TLB hit. */
    reset(&g,ram);
    put64(ram,((GUEST_BASE+4096)>>12)<<10|1);
    put64(ram+4096+2*8,((GUEST_BASE+8192)>>12)<<10|1);
    put64(ram+8192,((GUEST_BASE+GUEST_LOAD)>>12)<<10|1|2|8|16);
    ins(&g,0x00100093);g.satp=(UINT64_C(8)<<60)|(GUEST_BASE>>12);g.pc=0x400000;g.privilege=0;
    guest_run(&g,1);assert(g.x[1]==1&&g.pc==0x400004);assert(ram[8192]&64);
    /* The instruction-page fast path must still read changed bytes, reject
     * noncanonical aliases and follow permission/remap fences. */
    uint32_t changed=0x00200093;
    assert(guest_copy(&g,GUEST_BASE+GUEST_LOAD,&changed,4));g.pc=0x400000;
    guest_run(&g,1);assert(g.x[1]==2);
    g.pc=0x400000|(UINT64_C(1)<<39);guest_run(&g,1);
    assert(g.cause==12&&g.tval==(0x400000|(UINT64_C(1)<<39)));
    permission_map(&g,ram,0x1b,0x13);g.privilege=0;
    guest_run(&g,1);assert(g.x[10]==0x12345678);
    uint32_t replacement=0x00700513;
    assert(guest_copy(&g,GUEST_BASE+0x6000,&replacement,4));
    put64(ram+8192,((GUEST_BASE+0x6000)>>12)<<10|0x1b);
    guest_flush(&g);g.pc=0x400000;guest_run(&g,1);assert(g.x[10]==7);
    put64(ram+8192,0x13);guest_flush(&g);g.pc=0x400000;guest_run(&g,1);
    assert(g.cause==12&&g.tval==0x400000);
    /* A cached first half of a straddling instruction cannot bypass execute
     * permission on the second page or assume physically adjacent pages. */
    for(unsigned executable=0;executable<2;executable++) {
        permission_map(&g,ram,0x1b,executable?0x1b:0x13);g.privilege=0;
        guest_run(&g,1);
        const unsigned char low[]={0x93,0x00},high[]={0x50,0x00};
        assert(guest_copy(&g,GUEST_BASE+GUEST_LOAD+4094,low,2));
        assert(guest_copy(&g,GUEST_BASE+0x5000,high,2));g.pc=0x400ffe;
        guest_run(&g,1);
        if(executable)assert(!g.stopped&&g.x[1]==5&&g.pc==0x401002);
        else assert(g.cause==12&&g.tval==0x401000&&g.x[1]==0);
    }
    /* Restore the original user RX mapping for the store-permission check. */
    permission_map(&g,ram,0x1b,0x13);g.privilege=0;guest_run(&g,1);
    unsigned char store[]={0x23,0xb0,0x00,0x00};assert(guest_copy(&g,GUEST_BASE+GUEST_LOAD+4,store,4));
    g.x[1]=0x400000;guest_run(&g,1);assert(g.cause==15&&g.tval==0x400000);
    /* Revoke SUM/MXR or change privilege without flushing the warm TLB. */
    permission_map(&g,ram,0x0b,0x13);g.status=1u<<18;
    guest_run(&g,1);assert(g.x[10]==0x12345678);
    g.pc=0x400000;g.status=0;guest_run(&g,1);assert(g.cause==13&&g.tval==0x401000);
    permission_map(&g,ram,0x0b,0x09);g.status=1u<<19;
    guest_run(&g,1);assert(g.x[10]==0x12345678);
    g.pc=0x400000;g.status=0;guest_run(&g,1);assert(g.cause==13&&g.tval==0x401000);
    permission_map(&g,ram,0x0b,0x03);guest_run(&g,1);assert(g.x[10]==0x12345678);
    g.pc=0x400000;g.privilege=0;guest_run(&g,1);assert(g.cause==12&&g.tval==0x400000);
    permission_map(&g,ram,0x1b,0x13);g.privilege=0;guest_run(&g,1);assert(g.x[10]==0x12345678);
    g.pc=0x400000;g.privilege=1;g.status=1u<<18;guest_run(&g,1);assert(g.cause==12&&g.tval==0x400000);
    /* Both explicit fences and satp writes invalidate changed page tables. */
    const uint32_t fences[]={0x12000073,0x18029073};
    for(unsigned i=0;i<2;i++) {
        permission_map(&g,ram,0x0b,0x03);guest_run(&g,1);assert(g.x[10]==0x12345678);
        g.x[5]=g.satp;put64(ram+8200,0);
        assert(guest_copy(&g,GUEST_BASE+GUEST_LOAD+4,&fences[i],4));
        uint32_t load=0x0005b503;assert(guest_copy(&g,GUEST_BASE+GUEST_LOAD+8,&load,4));
        guest_run(&g,2);assert(g.cause==13&&g.tval==0x401000);
    }
    reset(&g,ram);ins(&g,0x0200006f);g.vector=GUEST_BASE+0x100;g.status=2;g.ie=32;g.deadline=0;
    guest_run(&g,1);assert(g.pc==g.vector&&g.cause==((UINT64_C(1)<<63)|5)&&g.epc==GUEST_BASE+GUEST_LOAD);
    reset(&g,ram);g.ie=32;g.deadline=2;ins(&g,0x10500073);
    unsigned char wake[]={0x93,0x00,0x70,0x00};assert(guest_copy(&g,g.pc+4,wake,4));
    guest_run(&g,2);assert(!g.waiting&&g.x[1]==7); /* WFI wake even with SIE=0 */
    /* A slower interpreter must not slow wall time or advance it on input. */
    reset(&g,ram);g.external_time=1;g.yield_on_idle=1;g.time=100;g.ie=32;g.deadline=110;
    ins(&g,0x10500073);assert(guest_copy(&g,g.pc+4,wake,4));guest_run(&g,10);
    assert(g.waiting&&g.time==100&&g.retired==1);
    guest_run(&g,10);assert(g.waiting&&g.time==100&&g.retired==1);
    g.time=110;guest_run(&g,1);assert(!g.waiting&&g.time==110&&g.x[1]==7);
    /* Real F/D encodings, NaN boxing, CSR state and compressed stack forms. */
    reset(&g,ram);g.status=1u<<13;g.f[1]=0x3ff8000000000000ULL;g.f[2]=0x4004000000000000ULL;
    ins(&g,(1u<<25)|(2u<<20)|(1u<<15)|(3u<<7)|0x53);guest_run(&g,1);
    assert(g.f[3]==0x4010000000000000ULL&&((g.status>>13)&3)==3&&(g.status>>63)); /* 1.5+2.5 */
    ins(&g,(0x61u<<25)|(2u<<20)|(3u<<15)|(4u<<7)|0x53);guest_run(&g,1);assert(g.x[4]==4); /* fcvt.l.d */
    g.x[5]=GUEST_BASE+0x5000;put64(ram+0x5000,0x4024000000000000ULL);
    ins(&g,(5u<<15)|(3u<<12)|(6u<<7)|7);guest_run(&g,1);assert(g.f[6]==0x4024000000000000ULL);
    ins(&g,(6u<<20)|(5u<<15)|(3u<<12)|0x27);guest_run(&g,1); /* fsd */
    g.f[1]=0x3f800000;g.f[2]=0xffffffff3f800000ULL;
    ins(&g,(2u<<20)|(1u<<15)|(3u<<7)|0x53);guest_run(&g,1);
    assert(g.f[3]==0xffffffff7fc00000ULL); /* unboxed binary32 is canonical NaN */
    g.x[1]=3;ins(&g,(2u<<20)|(1u<<15)|(1u<<12)|0x73);guest_run(&g,1);assert(g.fcsr==96); /* frm */
    g.f[1]=0x3ff0000000000000ULL;g.f[2]=0;
    ins(&g,(0x0du<<25)|(2u<<20)|(1u<<15)|(3u<<7)|0x53);guest_run(&g,1);assert((g.fcsr&31)==8);
    g.x[2]=GUEST_BASE+0x5000;
    unsigned char fldsp[]={0x82,0x23}; /* c.fldsp f7,0(sp) */
    assert(guest_copy(&g,g.pc,fldsp,2));guest_run(&g,1);assert(g.f[7]==0x4024000000000000ULL);
    /* Decode caching must not cache authorization to use FP registers. */
    g.status=0;assert(guest_copy(&g,g.pc,fldsp,2));guest_run(&g,1);assert(g.cause==2);
    reset(&g,ram);
    g.status=0;ins(&g,(1u<<25)|(2u<<20)|(1u<<15)|(3u<<7)|0x53);guest_run(&g,1);assert(g.cause==2);
    /* Deterministic malformed instructions stay inside the checked machine. */
    uint32_t r=0x496e7374;
    for(unsigned k=0;k<10000;k++) {
        guest_init(&g,ram,GUEST_RAM);r=r*1664525u+1013904223u;ins(&g,r);
        g.x[1]=UINT64_MAX;g.x[2]=GUEST_BASE+GUEST_RAM-1;guest_run(&g,4);
        for(unsigned i=0;i<32;i++)assert(ram[GUEST_RAM+i]==0xa5);
    }
    free(ram);puts("GUEST CORE PASS: arithmetic, C decode, bounds, misalignment, U/S permissions, checked instruction pages, Sv39 RX, timer and malformed instructions");
}
