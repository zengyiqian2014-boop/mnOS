/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_GUEST_DEVICES_H
#define MNOS_GUEST_DEVICES_H
#include "guest.h"
#define GUEST_FB UINT64_C(0x84000000)
#define GUEST_WIDTH 1024u
#define GUEST_HEIGHT 768u
#define GUEST_DISK_BYTES (UINT64_C(8)<<30)
#define GUEST_DISK_IO_MAX (128u * 1024u)
#define GD_FEATURE_BULK_DISK 128u
/* ABI v1, SBI extension 0x09004d4e. All lengths/offsets use bytes, except
 * function 3 (disk size in 512-byte sectors). Input uses Linux input codes.
 * GD_INFO bit 7 permits disk transfers up to GUEST_DISK_IO_MAX; old hosts
 * permit 4096 bytes. All writes complete the backend transaction before
 * returning. GD_SYNC and Linux PREFLUSH/FUA retain their ordering semantics. */
enum { GD_INFO,GD_READ,GD_WRITE,GD_SIZE,GD_SYNC,GD_RX,GD_TX,GD_MAC,GD_INPUT,GD_RANDOM,GD_TIME };
struct guest_devices {
    const uint8_t *root; size_t root_size;
    void *context;
    bool (*disk_read)(void *,uint64_t,void *,size_t);
    bool (*disk_write)(void *,uint64_t,const void *,size_t);
    bool (*disk_sync)(void *);
    int (*receive)(void *,void *,size_t);
    int (*transmit)(void *,const void *,size_t);
    bool (*random)(uint8_t *,size_t);
    bool (*clock)(uint64_t *);
    uint8_t mac[6];
    uint64_t events[128]; unsigned head,tail;
    /* Private staging area: never put a large guest transfer on the native
     * interrupt stack. Owned by this guest and wiped with the device state. */
    uint8_t block_buffer[GUEST_DISK_IO_MAX];
};
int64_t guest_device_call(struct guest *,unsigned,const uint64_t *,uint64_t *);
void guest_device_input(struct guest_devices *,unsigned type,unsigned code,int32_t value);
#endif
