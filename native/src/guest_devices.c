/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit guest I/O boundary. Nothing exposes a host block device or pathname.
 * Both the native host and hosted integration harness use this implementation. */
#include "guest_devices.h"
#include <string.h>
static bool extent(const struct guest *g,uint64_t p,uint64_t n) {
    return p>=GUEST_BASE&&p-GUEST_BASE<=g->ram_size&&n<=g->ram_size-(p-GUEST_BASE);
}
void guest_device_input(struct guest_devices *d,unsigned type,unsigned code,int32_t value) {
    unsigned next=(d->head+1)%128;if(next==d->tail)return;
    d->events[d->head]=UINT64_C(1)<<63|(uint64_t)(type&0x7fff)<<48|(uint64_t)(code&65535)<<32|(uint32_t)value;d->head=next;
}
int64_t guest_device_call(struct guest *g,unsigned fn,const uint64_t *a,uint64_t *value) {
    struct guest_devices *d=g->opaque;uint8_t bytes[4096];*value=0;
    if(!d)return -2;
    switch(fn) {
    case GD_INFO:*value=1|4|32|(d->receive&&d->transmit?2:0)|(d->random?8:0)|(d->clock?16:0)|(d->disk_read&&d->disk_write?64:0);return 0;
    case GD_SIZE:
        if(a[0]>1)return -3;
        *value=a[0]?(d->disk_read?GUEST_DISK_BYTES/512:0):(d->root_size+511)/512;return 0;
    case GD_READ:case GD_WRITE: {
        uint64_t disk=a[0],offset=a[1],p=a[2],n=a[3];
        uint64_t size=disk?GUEST_DISK_BYTES:((uint64_t)d->root_size+511)&~UINT64_C(511);
        if(disk>1||!n||n>sizeof bytes||(offset&511)||(n&511)||offset>size||n>size-offset||!extent(g,p,n))return -3;
        if(fn==GD_READ) {
            if(!disk){memset(bytes,0,(size_t)n);if(offset<d->root_size){size_t count=d->root_size-offset;if(count>n)count=n;memcpy(bytes,d->root+offset,count);}}
            else if(!d->disk_read||!d->disk_read(d->context,offset,bytes,(size_t)n))return -1;
            return guest_copy(g,p,bytes,(size_t)n)?0:-5;
        }
        if(!disk)return -4;
        if(!guest_read(g,p,bytes,(size_t)n))return -5;
        return d->disk_write&&d->disk_write(d->context,offset,bytes,(size_t)n)?0:-1;
    }
    case GD_SYNC:return a[0]>1?-3:!a[0]?0:d->disk_sync&&d->disk_sync(d->context)?0:-1;
    case GD_RX:case GD_TX:case GD_RANDOM: {
        uint64_t p=a[0],n=a[1];
        if(!n||n>sizeof bytes||!extent(g,p,n))return -3;
        if(fn==GD_TX){if(n<14||n>1514||!guest_read(g,p,bytes,(size_t)n))return -3;
            int sent=d->transmit?d->transmit(d->context,bytes,(size_t)n):-1;if(sent<0)return -1;*value=(unsigned)sent;return 0;}
        if(fn==GD_RANDOM){if(!d->random||!d->random(bytes,(size_t)n))return -1;
            bool ok=guest_copy(g,p,bytes,(size_t)n);volatile uint8_t *wipe=bytes;for(size_t i=0;i<n;i++)wipe[i]=0;return ok?0:-5;}
        int got=d->receive?d->receive(d->context,bytes,(size_t)n):-1;
        if(got<0||(uint64_t)got>n)return -1;
        if(!guest_copy(g,p,bytes,(size_t)got))return -5;*value=(unsigned)got;return 0;
    }
    case GD_MAC:for(unsigned i=0;i<6;i++)*value|=(uint64_t)d->mac[i]<<(8*i);return 0;
    case GD_INPUT:if(d->head!=d->tail){*value=d->events[d->tail];d->tail=(d->tail+1)%128;}return 0;
    case GD_TIME:return d->clock&&d->clock(value)?0:-1;
    default:return -2;
    }
}
