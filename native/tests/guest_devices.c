/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "guest_devices.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned reads,writes,transmits,syncs;
static bool io_failure;
static uint8_t disk[2*GUEST_DISK_IO_MAX];
static bool read_disk(void *p,uint64_t off,void *out,size_t n){(void)p;reads++;assert(off+n<=sizeof disk);if(io_failure)return false;memcpy(out,disk+off,n);return true;}
static bool write_disk(void *p,uint64_t off,const void *in,size_t n){(void)p;writes++;assert(off+n<=sizeof disk);if(io_failure)return false;memcpy(disk+off,in,n);return true;}
static bool sync_disk(void *p){(void)p;syncs++;return !io_failure;}
static int transmit(void *p,const void *in,size_t n){(void)p;assert(((const uint8_t *)in)[0]==0x5a);transmits++;return (int)n;}
static int bad_receive(void *p,void *out,size_t n){(void)p;(void)out;return (int)n+1;}
static bool entropy(uint8_t *p,size_t n){memset(p,0xa3,n);return true;}
int main(void){
    struct guest g;uint8_t *ram=calloc(1,GUEST_RAM+32);assert(ram);memset(ram+GUEST_RAM,0x5a,32);guest_init(&g,ram,GUEST_RAM);
    uint8_t root[700];memset(root,0x43,sizeof root);
    struct guest_devices devices={.root=root,.root_size=sizeof root,.disk_read=read_disk,.disk_write=write_disk,.disk_sync=sync_disk,.transmit=transmit,.receive=bad_receive,.random=entropy};
    g.opaque=&devices;g.device=guest_device_call;uint64_t a[4]={0,0,GUEST_BASE+4090,1024},value;
    assert(!guest_device_call(&g,GD_READ,a,&value));assert(ram[4090]==0x43&&ram[4789]==0x43&&ram[4790]==0);
    a[0]=1;a[1]=512;a[3]=512;memset(ram+4090,0x6d,512);
    assert(!guest_device_call(&g,GD_WRITE,a,&value)&&writes==1&&disk[512]==0x6d);
    memset(ram+4090,0,512);assert(!guest_device_call(&g,GD_READ,a,&value)&&reads==1&&ram[4090]==0x6d);
    assert(!guest_device_call(&g,GD_INFO,a,&value)&&(value&GD_FEATURE_BULK_DISK));
    // A request crossing 33 guest pages is one backend transaction, not 32
    // independently flushed 4 KiB writes. Bytes round-trip through real RAM.
    a[1]=4096;a[3]=GUEST_DISK_IO_MAX;
    for(unsigned i=0;i<GUEST_DISK_IO_MAX;i++)ram[4090+i]=(uint8_t)(i*29+(i>>9));
    assert(!guest_device_call(&g,GD_WRITE,a,&value)&&writes==2);
    assert(!memcmp(disk+4096,ram+4090,GUEST_DISK_IO_MAX));
    memset(ram+4090,0,GUEST_DISK_IO_MAX);
    assert(!guest_device_call(&g,GD_READ,a,&value)&&reads==2);
    for(unsigned i=0;i<GUEST_DISK_IO_MAX;i++)assert(ram[4090+i]==(uint8_t)(i*29+(i>>9)));
    a[3]=GUEST_DISK_IO_MAX+512;assert(guest_device_call(&g,GD_WRITE,a,&value)==-3&&writes==2);
    a[3]=GUEST_DISK_IO_MAX;a[2]=GUEST_BASE+GUEST_RAM-GUEST_DISK_IO_MAX+1;
    assert(guest_device_call(&g,GD_WRITE,a,&value)==-3&&writes==2);
    a[2]=GUEST_BASE+4090;a[1]=GUEST_DISK_BYTES-512;
    assert(guest_device_call(&g,GD_READ,a,&value)==-3&&reads==2);
    a[1]=4096;io_failure=true;
    assert(guest_device_call(&g,GD_WRITE,a,&value)==-1&&writes==3);
    assert(guest_device_call(&g,GD_READ,a,&value)==-1&&reads==3);
    assert(guest_device_call(&g,GD_SYNC,a,&value)==-1&&syncs==1);
    io_failure=false;assert(!guest_device_call(&g,GD_SYNC,a,&value)&&syncs==2);
    a[3]=512;a[1]=512;
    a[2]=GUEST_BASE+GUEST_RAM-511;assert(guest_device_call(&g,GD_WRITE,a,&value)==-3&&writes==3);
    a[2]=UINT64_MAX-127;assert(guest_device_call(&g,GD_READ,a,&value)==-3&&reads==3);
    a[2]=GUEST_BASE;a[1]=UINT64_MAX-511;assert(guest_device_call(&g,GD_WRITE,a,&value)==-3&&writes==3);
    a[1]=0;a[0]=0;assert(guest_device_call(&g,GD_WRITE,a,&value)==-4);
    a[0]=2;assert(guest_device_call(&g,GD_READ,a,&value)==-3);
    a[0]=GUEST_BASE;a[1]=1514;ram[0]=0x5a;assert(!guest_device_call(&g,GD_TX,a,&value)&&value==1514&&transmits==1);
    a[1]=4096;assert(guest_device_call(&g,GD_TX,a,&value)==-3&&transmits==1);
    a[1]=512;memset(ram,0,512);assert(guest_device_call(&g,GD_RX,a,&value)==-1&&ram[0]==0);
    assert(!guest_device_call(&g,GD_RANDOM,a,&value)&&ram[0]==0xa3&&ram[511]==0xa3);
    guest_device_input(&devices,3,1,-14);assert(!guest_device_call(&g,GD_INPUT,a,&value));
    assert((value>>63)&&((value>>48)&0x7fff)==3&&((value>>32)&65535)==1&&(int32_t)value==-14);
    assert(!guest_device_call(&g,GD_INPUT,a,&value)&&!value);
    for(unsigned i=0;i<200;i++)guest_device_input(&devices,1,i,1);
    for(unsigned i=0;i<127;i++){assert(!guest_device_call(&g,GD_INPUT,a,&value));assert(((value>>32)&65535)==i);}
    assert(!guest_device_call(&g,GD_INPUT,a,&value)&&!value);
    assert(guest_device_call(&g,99,a,&value)==-2);
    /* An unprivileged ECALL traps inside Linux and never enters host devices. */
    uint8_t code[]={0x73,0,0,0};assert(guest_copy(&g,g.pc,code,4));g.privilege=0;g.x[17]=0x09004d4e;g.x[16]=GD_WRITE;
    guest_run(&g,1);assert(g.cause==8&&writes==3);
    for(unsigned i=0;i<32;i++)assert(ram[GUEST_RAM+i]==0x5a);free(ram);
    puts("GUEST DEVICES PASS: bounded 128 KiB disk transactions, sync/failure propagation, read-only root, crossed pages, network bounds, entropy, event queue and U-mode isolation");
}
