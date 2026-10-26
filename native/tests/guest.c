/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "guest.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void put64(unsigned char *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(n>>(i*8));}
static void ins(struct guest *g,uint32_t n){unsigned char b[4];for(unsigned i=0;i<4;i++)b[i]=(unsigned char)(n>>(i*8));assert(guest_copy(g,g->pc,b,4));}
static void reset(struct guest *g,unsigned char *ram){memset(ram,0,GUEST_RAM);guest_init(g,ram,GUEST_RAM);}
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
    reset(&g,ram);g.x[1]=GUEST_BASE+GUEST_RAM;ins(&g,0x0000b023);guest_run(&g,1);
    assert(g.stopped==2&&g.cause==7&&g.tval==GUEST_BASE+GUEST_RAM);
    for(unsigned i=0;i<32;i++)assert(ram[GUEST_RAM+i]==0xa5);
    reset(&g,ram);g.x[1]=GUEST_BASE+1;ins(&g,0x0000b103);guest_run(&g,1);assert(g.cause==4);
    reset(&g,ram);g.privilege=0;ins(&g,0x18002173);guest_run(&g,1);assert(g.cause==2); /* U-mode satp read */
    /* Sv39: map virtual 0x400000 to a user RX page. The hardware-set A bit
     * must appear; a store through it must fault even after a fetch TLB hit. */
    reset(&g,ram);
    put64(ram,((GUEST_BASE+4096)>>12)<<10|1);
    put64(ram+4096+2*8,((GUEST_BASE+8192)>>12)<<10|1);
    put64(ram+8192,((GUEST_BASE+GUEST_LOAD)>>12)<<10|1|2|8|16);
    ins(&g,0x00100093);g.satp=(UINT64_C(8)<<60)|(GUEST_BASE>>12);g.pc=0x400000;g.privilege=0;
    guest_run(&g,1);assert(g.x[1]==1&&g.pc==0x400004);assert(ram[8192]&64);
    unsigned char store[]={0x23,0xb0,0x00,0x00};assert(guest_copy(&g,GUEST_BASE+GUEST_LOAD+4,store,4));
    g.x[1]=0x400000;guest_run(&g,1);assert(g.cause==15&&g.tval==0x400000);
    reset(&g,ram);ins(&g,0x0200006f);g.vector=GUEST_BASE+0x100;g.status=2;g.ie=32;g.deadline=0;
    guest_run(&g,1);assert(g.pc==g.vector&&g.cause==((UINT64_C(1)<<63)|5)&&g.epc==GUEST_BASE+GUEST_LOAD);
    reset(&g,ram);g.ie=32;g.deadline=2;ins(&g,0x10500073);
    unsigned char wake[]={0x93,0x00,0x70,0x00};assert(guest_copy(&g,g.pc+4,wake,4));
    guest_run(&g,2);assert(!g.waiting&&g.x[1]==7); /* WFI wake even with SIE=0 */
    /* Deterministic malformed instructions stay inside the checked machine. */
    uint32_t r=0x496e7374;
    for(unsigned k=0;k<10000;k++) {
        guest_init(&g,ram,GUEST_RAM);r=r*1664525u+1013904223u;ins(&g,r);
        g.x[1]=UINT64_MAX;g.x[2]=GUEST_BASE+GUEST_RAM-1;guest_run(&g,4);
        for(unsigned i=0;i<32;i++)assert(ram[GUEST_RAM+i]==0xa5);
    }
    free(ram);puts("GUEST CORE PASS: arithmetic, C decode, bounds, misalignment, U/S permissions, Sv39 RX, timer and malformed instructions");
}
