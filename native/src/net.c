/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original polled VirtIO 1.0 Ethernet driver, PCI and ARM MMIO transports.
 * No borrowed Linux driver or host TCP/IP stack. Only bounded Ethernet frames.
 */
#include "kernel.h"
#include "usb_platform.h"
#include "net.h"
#ifdef __aarch64__
#include "../arch/arm64/platform.h"
#endif
#define NQ 16
#define HEADER 12
struct desc {uint64_t address;uint32_t length;uint16_t flags,next;};
struct avail {uint16_t flags,index,ring[NQ];};
struct used {uint16_t flags,index;struct {uint32_t id,length;} ring[NQ];};
struct queue {
    uint64_t dp,ap,up,bp[NQ];struct desc *desc;struct avail *avail;volatile struct used *used;
    uint8_t *buffer[NQ];volatile uint16_t *notify;uint16_t seen,sent;uint32_t busy;
};
static struct queue queues[2];
static volatile uint8_t *common,*config_base,*notification,*mmio;
static uint32_t notify_size,notify_multiplier;
static bool ready;
static uint16_t rd16(volatile uint8_t *p,unsigned n){return *(volatile uint16_t *)(p+n);}
static uint32_t rd32(volatile uint8_t *p,unsigned n){return *(volatile uint32_t *)(p+n);}
static void wr16(volatile uint8_t *p,unsigned n,uint16_t v){*(volatile uint16_t *)(p+n)=v;}
static void wr32(volatile uint8_t *p,unsigned n,uint32_t v){*(volatile uint32_t *)(p+n)=v;}
static void wr64(volatile uint8_t *p,unsigned n,uint64_t v){wr32(p,n,(uint32_t)v);wr32(p,n+4,(uint32_t)(v>>32));}
static uint8_t status(void){return mmio?(uint8_t)rd32(mmio,0x70):common?common[20]:0;}
static void set_status(uint8_t v){if(mmio)wr32(mmio,0x70,v);else if(common)common[20]=v;}
static bool reset(void){set_status(0);uint64_t end=usb_millis()+100;while(status()&&usb_millis()<end){}return !status();}
static void release_page(uint64_t p){if(p){memset(phys_pointer(p),0,PAGE);page_free(p);}}
void net_stop(void) {
    ready=false;
    /* Never free DMA buffers if the device refuses to stop. */
    if(!reset())return;
    for(unsigned q=0;q<2;q++){
        release_page(queues[q].dp);release_page(queues[q].ap);release_page(queues[q].up);
        for(unsigned i=0;i<NQ;i++)release_page(queues[q].bp[i]);
    }
    memset(queues,0,sizeof queues);common=config_base=notification=mmio=NULL;
}
static bool allocate(struct queue *q) {
    q->dp=page_try_alloc();q->ap=page_try_alloc();q->up=page_try_alloc();
    if(!q->dp||!q->ap||!q->up)return false;
    q->desc=phys_pointer(q->dp);q->avail=phys_pointer(q->ap);q->used=phys_pointer(q->up);
    for(unsigned i=0;i<NQ;i++){q->bp[i]=page_try_alloc();if(!q->bp[i])return false;q->buffer[i]=phys_pointer(q->bp[i]);}
    return true;
}
static void notify(unsigned index) {
    struct queue *q=&queues[index];usb_dma_sync(q->desc,PAGE);usb_dma_sync(q->avail,PAGE);
    if(mmio)wr32(mmio,0x50,index);else *q->notify=(uint16_t)index;
}
static void publish(unsigned index,unsigned id) {
    struct queue *q=&queues[index];
    q->avail->ring[q->avail->index%NQ]=(uint16_t)id;
    __atomic_thread_fence(__ATOMIC_RELEASE);q->avail->index++;notify(index);
}
static bool initialize(uint8_t mac[6]) {
    if(!reset())return false;set_status(3);
    uint32_t lo,hi;
    if(mmio){wr32(mmio,0x14,0);lo=rd32(mmio,0x10);wr32(mmio,0x14,1);hi=rd32(mmio,0x10);}
    else{wr32(common,0,0);lo=rd32(common,4);wr32(common,0,1);hi=rd32(common,4);}
    if(!(hi&1)||!(lo&(1u<<5)))return false;
    if(mmio){wr32(mmio,0x24,0);wr32(mmio,0x20,1u<<5);wr32(mmio,0x24,1);wr32(mmio,0x20,1);}
    else{wr32(common,8,0);wr32(common,12,1u<<5);wr32(common,8,1);wr32(common,12,1);}
    set_status(11);if(!(status()&8))return false;
    for(unsigned i=0;i<6;i++)mac[i]=config_base[i];
    if(mac[0]&1)return false;
    for(unsigned index=0;index<2;index++){
        struct queue *q=&queues[index];
        if(mmio){wr32(mmio,0x30,index);if(rd32(mmio,0x34)<NQ||rd32(mmio,0x44))return false;}
        else{wr16(common,22,index);if(rd16(common,24)<NQ||rd16(common,28))return false;
            uint64_t offset=(uint64_t)rd16(common,30)*notify_multiplier;
            if(offset>notify_size-2||(offset&1))return false;q->notify=(void *)(notification+offset);}
        if(!allocate(q))return false;q->avail->flags=1;
        if(index==0)for(unsigned i=0;i<NQ;i++){
            q->desc[i]=(struct desc){q->bp[i],2048,2,0};q->avail->ring[i]=i;
            usb_dma_sync(q->buffer[i],PAGE);
        }
        if(index==0)q->avail->index=NQ;
        usb_dma_sync(q->desc,PAGE);usb_dma_sync(q->avail,PAGE);usb_dma_sync((void *)q->used,PAGE);
        if(mmio){wr32(mmio,0x38,NQ);wr64(mmio,0x80,q->dp);wr64(mmio,0x90,q->ap);wr64(mmio,0xa0,q->up);wr32(mmio,0x44,1);}
        else{wr16(common,24,NQ);wr64(common,32,q->dp);wr64(common,40,q->ap);wr64(common,48,q->up);wr16(common,28,1);}
    }
    set_status(15);ready=true;notify(0);log_write("NETWORK: original VirtIO Ethernet driver ready\n");return true;
}
static unsigned cfg8(unsigned b,unsigned s,unsigned f,unsigned r){return usb_pci_read(b,s,f,r)>>((r&3)*8)&255;}
static volatile uint8_t *capability(unsigned b,unsigned s,unsigned f,unsigned cap,unsigned minimum) {
    unsigned bar=cfg8(b,s,f,cap+4);if(bar>=6)return NULL;
    uint32_t low=usb_pci_read(b,s,f,0x10+bar*4);if(low&1)return NULL;
    uint64_t base=low&~15u;
    if((low&6)==4){if(bar==5)return NULL;base|=(uint64_t)usb_pci_read(b,s,f,0x14+bar*4)<<32;}else if(low&6)return NULL;
    uint32_t offset=usb_pci_read(b,s,f,cap+8),length=usb_pci_read(b,s,f,cap+12);
    if(!base||length<minimum||length>(1u<<20)||base>UINT64_MAX-offset||base+offset>UINT64_MAX-length)return NULL;
    return usb_map_mmio(base+offset,length);
}
bool net_start(uint8_t mac[6]) {
    net_stop();if(common||mmio)return false;usb_platform_init();
#ifdef __aarch64__
    for(unsigned i=0;i<platform.virtio_count;i++) {
        mmio=usb_map_mmio(platform.virtio[i],4096);
        if(mmio&&rd32(mmio,0)==0x74726976&&rd32(mmio,4)==2&&rd32(mmio,8)==1){
            config_base=mmio+0x100;if(initialize(mac))return true;net_stop();if(mmio)return false;
        }else mmio=NULL;
    }
#endif
    unsigned first,last;if(!usb_pci_window(&first,&last))return false;
    for(unsigned b=first;b<=last;b++)for(unsigned s=0;s<32;s++)for(unsigned f=0;f<8;f++){
        uint32_t id=usb_pci_read(b,s,f,0);if(id==UINT32_MAX){if(!f)break;continue;}
        if((id&65535)!=0x1af4||(id>>16!=0x1000&&id>>16!=0x1041))continue;
        common=config_base=notification=NULL;notify_size=notify_multiplier=0;
        unsigned cap=cfg8(b,s,f,0x34)&~3u;
        for(unsigned budget=0;cap>=0x40&&cap<=0xec&&budget<48;budget++){
            unsigned next=cfg8(b,s,f,cap+1)&~3u,length=cfg8(b,s,f,cap+2),type=cfg8(b,s,f,cap+3);
            if(cfg8(b,s,f,cap)==9&&length>=16){
                if(type==1)common=capability(b,s,f,cap,56);
                if(type==4)config_base=capability(b,s,f,cap,6);
                if(type==2&&length>=20&&cap<=0xe8){notification=capability(b,s,f,cap,2);notify_size=usb_pci_read(b,s,f,cap+12);notify_multiplier=usb_pci_read(b,s,f,cap+16);}
            }
            if(next==cap)break;cap=next;
        }
        if(common&&notification&&config_base){
            usb_pci_write(b,s,f,4,(usb_pci_read(b,s,f,4)&65535)|6);
            if(initialize(mac))return true;net_stop();if(common)return false;
        }else common=config_base=notification=NULL;
    }
    return false;
}
int net_receive(void *out,size_t capacity) {
    if(!ready)return -1;struct queue *q=&queues[0];
    usb_dma_sync((void *)q->used,PAGE);uint16_t count=q->used->index-q->seen;
    if(count>NQ){net_stop();return -1;}if(!count)return 0;
    unsigned slot=q->seen%NQ,id=q->used->ring[slot].id,n=q->used->ring[slot].length;
    if(id>=NQ||n>2048){net_stop();return -1;}q->seen++;
    usb_dma_sync(q->buffer[id],PAGE);
    unsigned size=n>=HEADER+14&&n<=HEADER+1514?n-HEADER:0;
    if(size>capacity)size=0;if(size)memcpy(out,q->buffer[id]+HEADER,size);
    memset(q->buffer[id],0,PAGE);usb_dma_sync(q->buffer[id],PAGE);publish(0,id);
    if(mmio)wr32(mmio,0x64,rd32(mmio,0x60));return (int)size;
}
int net_transmit(const void *data,size_t size) {
    if(!ready||size<14||size>1514)return -1;struct queue *q=&queues[1];
    usb_dma_sync((void *)q->used,PAGE);uint16_t count=q->used->index-q->seen;
    if(count>NQ){net_stop();return -1;}
    while(count--){unsigned id=q->used->ring[q->seen%NQ].id;if(id>=NQ||!(q->busy&(1u<<id))){net_stop();return -1;}q->busy&=~(1u<<id);q->seen++;}
    unsigned id=0;while(id<NQ&&(q->busy&(1u<<id)))id++;if(id==NQ)return 0;q->busy|=1u<<id;
    memset(q->buffer[id],0,HEADER);memcpy(q->buffer[id]+HEADER,data,size);
    q->desc[id]=(struct desc){q->bp[id],(uint32_t)(size+HEADER),0,0};usb_dma_sync(q->buffer[id],PAGE);
    q->sent++;publish(1,id);return (int)size;
}
