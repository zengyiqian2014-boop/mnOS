/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original polling VirtIO entropy driver: PCI modern and transitional devices.
 * Random data comes from the device/host CSPRNG, never a clock or fixed seed.
 */
#include "kernel.h"
static uint8_t queue_memory[32768] __attribute__((aligned(4096)));
static uint8_t random_buffer[4096] __attribute__((aligned(4096)));
struct descriptor { uint64_t address; uint32_t length; uint16_t flags, next; };
struct completion { uint32_t id, length; };
static volatile uint8_t *common;
static volatile uint16_t *notify;
static volatile uint16_t *avail, *used;
static struct descriptor *desc;
static uint16_t port, queue_size, seen;
static unsigned random_lock;
static bool ready;
static uint32_t config(unsigned b, unsigned s, unsigned f, unsigned r) {
    outl(0xcf8, 0x80000000u | b << 16 | s << 11 | f << 8 | (r & 0xfc));
    return inl(0xcfc);
}
static uint8_t config8(unsigned b, unsigned s, unsigned f, unsigned r) {
    return config(b,s,f,r) >> ((r & 3) * 8);
}
static void put16(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static uint16_t get16(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static uint16_t c16(unsigned r) { return *(volatile uint16_t *)(common+r); }
static void w16(unsigned r, uint16_t v) { *(volatile uint16_t *)(common+r)=v; }
static void w32(unsigned r, uint32_t v) { *(volatile uint32_t *)(common+r)=v; }
static void w64(unsigned r, uint64_t v) { w32(r,v); w32(r+4,v>>32); }
static void status(uint8_t v) { if(common) common[20]=v; else outb(port+18,v); }
static void reset_failed(void) { status(0); ready=false; }
static uintptr_t capability(unsigned b,unsigned s,unsigned f,unsigned cap,unsigned minimum) {
    unsigned bar=config8(b,s,f,cap+4);
    if(bar>=6) return 0;
    uint32_t low=config(b,s,f,0x10+bar*4);
    if(low&1) return 0;
    uint64_t base=low&~15u;
    if((low&6)==4) {
        if(bar==5) return 0;
        base|=(uint64_t)config(b,s,f,0x14+bar*4)<<32;
    } else if(low&6) return 0;
    uint32_t offset=config(b,s,f,cap+8), length=config(b,s,f,cap+12);
    // The current x86 kernel maps this MMIO window uncached; reject other BARs.
    if(length<minimum || base<0xf0000000ULL || base>=0x100000000ULL ||
       offset>0x100000000ULL-base || length>0x100000000ULL-base-offset) return 0;
    return (uintptr_t)(base+offset);
}
void rng_pci_probe(unsigned b,unsigned s,unsigned f,uint16_t vendor,uint16_t device) {
    if(ready || vendor!=0x1af4 || (device!=0x1005 && device!=0x1044)) return;
    common=NULL; notify=NULL; port=0;
    uintptr_t notification=0; uint32_t multiplier=0, notification_length=0;
    if(config(b,s,f,4)&(1u<<20)) {
        unsigned cap=config8(b,s,f,0x34)&~3u;
        for(unsigned budget=0;cap>=0x40 && cap<=0xec && budget<48;++budget) {
            unsigned next=config8(b,s,f,cap+1)&~3u;
            if(config8(b,s,f,cap)==9 && config8(b,s,f,cap+2)>=16) {
                unsigned type=config8(b,s,f,cap+3);
                if(type==1) common=(void *)capability(b,s,f,cap,56);
                if(type==2 && cap<=0xe8 && config8(b,s,f,cap+2)>=20) {
                    notification=capability(b,s,f,cap,2);
                    notification_length=config(b,s,f,cap+12);
                    multiplier=config(b,s,f,cap+16);
                }
            }
            if(next==cap) break;
            cap=next;
        }
    }
    bool modern=common && notification;
    if(!modern) {
        common=NULL;
        uint32_t bar=config(b,s,f,0x10);
        if(device!=0x1005 || !(bar&1) || (bar&~3u)>65535-20) return;
        port=bar&~3u;
        if(!port) return;
    }
    uint32_t command=config(b,s,f,4)&0xffff;
    outl(0xcf8,0x80000000u|b<<16|s<<11|f<<8|4);
    // Byte lanes in the upper half are PCI status W1C, so write zero there.
    outl(0xcfc,command | 4 | (modern?2:1));
    status(0);
    for(unsigned retry=0;retry<10000;++retry) {
        if(!(common?common[20]:inb(port+18))) break;
        if(retry==9999) return;
        __asm__ volatile("pause");
    }
    status(3);
    if(modern) {
        w32(0,1);
        if(!(*(volatile uint32_t *)(common+4)&1)) {reset_failed();return;}
        w32(8,0); w32(12,0); w32(8,1); w32(12,1); status(11);
        if(!(common[20]&8)) {reset_failed();return;}
        w16(22,0);
        if(c16(24)<8 || c16(28)) {reset_failed();return;}
        uint64_t offset=(uint64_t)c16(30)*multiplier;
        if(offset>notification_length-2 || (offset&1)) {reset_failed();return;}
        notify=(void *)(notification+offset);
        queue_size=8; w16(24,queue_size);
    } else {
        outl(port+4,0); put16(port+14,0); queue_size=get16(port+12);
        if(!queue_size || queue_size>1024 || (queue_size&(queue_size-1)) || inl(port+8)) {reset_failed();return;}
    }
    memset(queue_memory,0,sizeof queue_memory);
    desc=(void *)queue_memory; avail=(void *)(queue_memory+16*queue_size);
    used=(void *)(queue_memory+((16*queue_size+6+2*queue_size+4095)&~4095u)); avail[0]=1; seen=0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if(modern) {
        w64(32,(uintptr_t)desc); w64(40,(uintptr_t)avail); w64(48,(uintptr_t)used); w16(28,1);
    } else outl(port+8,(uintptr_t)queue_memory>>12);
    status(modern?15:7); ready=true;
    log_write(modern?"RNG: VirtIO PCI modern ready\n":"RNG: VirtIO PCI transitional ready\n");
}
bool virtio_random(uint8_t *out,size_t n) {
    if(!ready || n>sizeof random_buffer) return false;
    uint64_t flags=spin_lock(&random_lock);
    uint8_t *start=out; size_t total=n; bool ok=true;
    while(n) {
        desc[0]=(struct descriptor){(uintptr_t)random_buffer,(uint32_t)n,2,0};
        avail[2+avail[1]%queue_size]=0;
        __atomic_thread_fence(__ATOMIC_RELEASE); ++avail[1];
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if(common) *notify=0; else put16(port+16,0);
        unsigned budget=10000000;
        while(used[1]==seen && --budget) __asm__ volatile("pause");
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        volatile struct completion *entry=(void *)(used+2);
        uint32_t id=entry[seen%queue_size].id, got=entry[seen%queue_size].length;
        if(!budget || (uint16_t)(used[1]-seen)!=1 || id || !got || got>n) {
            // Reset stops DMA before any future caller can reuse the buffer.
            reset_failed(); ok=false; break;
        }
        ++seen; memcpy(out,random_buffer,got); out+=got; n-=got;
    }
    volatile uint8_t *erase=random_buffer;
    for(size_t i=0;i<sizeof random_buffer;++i) erase[i]=0;
    if(!ok) memset(start,0,total);
    spin_unlock(&random_lock,flags); return ok;
}
