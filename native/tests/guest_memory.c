/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "guest_memory.h"
#include "guest.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *pages[100];static unsigned allocated;
static uint64_t allocate(void){for(unsigned i=0;i<100;++i)if(!pages[i]){pages[i]=malloc(4096);assert(pages[i]);memset(pages[i],0xa5,4096);++allocated;return (uint64_t)(i+1)*4096;}return 0;}
static void *pointer(uint64_t p){assert(p&&p/4096<=100&&!(p&4095)&&pages[p/4096-1]);return pages[p/4096-1];}
static void release(uint64_t p){uint8_t *b=pointer(p);for(unsigned i=0;i<4096;++i)assert(!b[i]);free(b);pages[p/4096-1]=NULL;--allocated;}
extern const unsigned char linux_dtb_start[],linux_dtb_end[];
int main(void){
    struct guest_memory m={.bytes=UINT64_C(1)<<39,.budget=100,.allocate=allocate,.pointer=pointer,.release=release};
    const uint64_t addresses[]={0,4096,UINT64_C(1)<<21,UINT64_C(1)<<30,(UINT64_C(1)<<38)+4096,(UINT64_C(1)<<39)-4096};
    for(unsigned i=0;i<6;++i){uint8_t *b=guest_memory_page(&m,addresses[i]);assert(b);for(unsigned n=0;n<4096;++n)assert(!b[n]);b[0]=(uint8_t)(i+1);}
    assert(allocated<30);for(unsigned i=0;i<6;++i)assert(*(uint8_t *)guest_memory_page(&m,addresses[i])==i+1);
    assert(!guest_memory_page(&m,UINT64_C(1)<<39));
    struct guest vm;guest_init(&vm,NULL,(size_t)m.bytes);vm.ram_page=guest_memory_page;vm.ram_context=&m;
    assert(vm.x[11]==GUEST_BASE+GUEST_DTB);
    uint8_t payload[100];for(unsigned i=0;i<100;++i)payload[i]=(uint8_t)i;
    assert(guest_copy(&vm,GUEST_BASE+4090,payload,sizeof payload));
    assert(!memcmp((uint8_t *)guest_memory_page(&m,0)+4090,payload,6));
    assert(!memcmp(guest_memory_page(&m,4096),payload+6,94));
    assert(!guest_copy(&vm,GUEST_BASE+m.bytes-1,payload,sizeof payload));
    guest_memory_clear(&m);assert(!allocated&&!m.pages&&!m.root);
    m.budget=3;assert(!guest_memory_page(&m,0)&&m.failed);guest_memory_clear(&m);assert(!allocated&&!m.failed);
    uint8_t tree[512],copy[512];size_t size=(size_t)(linux_dtb_end-linux_dtb_start);assert(size<sizeof tree);
    memcpy(tree,linux_dtb_start,size);assert(guest_dtb_memory(tree,size,UINT64_C(4)<<30));
    memcpy(copy,tree,size);assert(!guest_dtb_memory(tree,size-1,UINT64_C(4)<<30));assert(!memcmp(copy,tree,size));
    assert(!guest_dtb_memory(tree,size,1));assert(!guest_dtb_memory(tree,size,(UINT64_C(1)<<39)+4096));
    tree[8]=255;memcpy(copy,tree,size);assert(!guest_dtb_memory(tree,size,UINT64_C(1)<<30));assert(!memcmp(copy,tree,size));
    puts("PASS sparse guest pages: independent high addresses, zero fill, cross-page copies, bounds, allocation failure, wipe/free, DTB resizing and malformed-input rejection");
}
