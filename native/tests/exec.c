/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "exec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char image[1024], original[1024], code[4096], data[4096];
static uint64_t entry;
static void put(size_t off, uint64_t value, size_t n) {
    for (size_t i=0;i<n;i++) image[off+i]=(unsigned char)(value>>(i*8));
}
static void fixture(uint16_t machine, uint64_t base) {
    memset(image,0,sizeof image);
    memcpy(image,"\177ELF\2\1\1",7);
    put(16,2,2); put(18,machine,2); put(20,1,4); put(24,base+8,8);
    put(32,64,8); put(52,64,2); put(54,56,2); put(56,2,2);
    put(64,1,4); put(68,5,4); put(72,256,8); put(80,base,8);
    put(96,16,8); put(104,32,8); put(112,1,8);
    put(120,1,4); put(124,6,4); put(128,512,8); put(136,base+4096,8);
    put(152,16,8); put(160,4096,8); put(168,1,8);
    memset(image+256,0x90,16); memset(image+512,0x42,16);
    memcpy(original,image,sizeof image);
}
static void reject(uint16_t machine,uint64_t base,size_t size) {
    memset(code,0xaa,sizeof code); memset(data,0xbb,sizeof data); entry=99;
    assert(elf_load(image,size,machine,base,code,data,&entry)<0);
    assert(entry==99);
    for (unsigned i=0;i<4096;i++) { assert(code[i]==0xaa); assert(data[i]==0xbb); }
    memcpy(image,original,sizeof image);
}
int main(void) {
    for (unsigned a=0;a<2;a++) {
        uint16_t machine=a?183:62;
        uint64_t base=a?0x700000000000ULL:0x100000000ULL;
        fixture(machine,base);
        assert(!elf_load(image,sizeof image,machine,base,code,data,&entry));
        assert(entry==base+8 && code[0]==0x90 && data[0]==0x42);
        for (unsigned i=16;i<4096;i++) { assert(!code[i]); assert(!data[i]); }
        for (size_t n=0;n<176;n++) reject(machine,base,n);
        put(18,0,2); reject(machine,base,sizeof image);
        put(16,3,2); reject(machine,base,sizeof image); /* PIE unsupported */
        put(32,UINT64_MAX-8,8); reject(machine,base,sizeof image);
        put(56,65535,2); reject(machine,base,sizeof image);
        put(68,7,4); reject(machine,base,sizeof image); /* W+X */
        put(124,7,4); reject(machine,base,sizeof image);
        put(64,3,4); reject(machine,base,sizeof image); /* interpreter */
        put(64,2,4); reject(machine,base,sizeof image); /* dynamic */
        put(72,UINT64_MAX,8); reject(machine,base,sizeof image);
        put(96,33,8); reject(machine,base,sizeof image);
        put(104,4097,8); reject(machine,base,sizeof image);
        put(80,base-1,8); reject(machine,base,sizeof image);
        put(80,UINT64_MAX-1,8); reject(machine,base,sizeof image);
        put(24,base+16,8); reject(machine,base,sizeof image); /* entry in BSS */
        put(24,base+4096,8); reject(machine,base,sizeof image);
        put(112,3,8); reject(machine,base,sizeof image);
        put(112,4096,8); reject(machine,base,sizeof image); /* incongruent offset */
        put(124,5,4); put(136,base,8); reject(machine,base,sizeof image);
        put(120,0x6474e551,4); put(124,7,4); reject(machine,base,sizeof image);
        if(a) { put(24,base+1,8); reject(machine,base,sizeof image); }
        /* Deterministic malformed-input sweep under ASan/UBSan. */
        uint32_t rng=0x12345678;
        for(unsigned i=0;i<20000;i++) {
            memcpy(image,original,sizeof image);
            rng=rng*1664525+1013904223;
            unsigned pos=rng%176;
            rng=rng*1664525+1013904223;
            image[pos]^=(unsigned char)(rng>>24);
            elf_load(image,sizeof image,machine,base,code,data,&entry);
        }
    }
    puts("PASS: ELF64 x86/ARM validation, BSS, failure atomicity, malformed-input sweep");
}
