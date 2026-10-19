/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "disk_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SECTORS (256u * 2048)
static uint8_t *disk;
static unsigned writes;
static bool fail_write;
static bool read_disk(void *c, uint64_t lba, unsigned n, void *p) {
    (void)c;
    if (lba >= SECTORS || n > SECTORS - lba)
        return false;
    memcpy(p, disk + lba * 512, n * 512);
    return true;
}
static bool write_disk(void *c, uint64_t lba, unsigned n, const void *p) {
    (void)c;
    if (fail_write || lba >= SECTORS || n > SECTORS - lba)
        return false;
    memcpy(disk + lba * 512, p, n * 512);
    writes++;
    return true;
}
static bool flush(void *c) {
    (void)c;
    return true;
}
static uint32_t u32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
int main(void) {
    disk = calloc(SECTORS, 512);
    assert(disk);
    struct disk_io io = {NULL, SECTORS, read_disk, write_disk, flush};
    uint8_t guid[16] = {1, 2, 3, 4};
    uint8_t payload[1031];
    for (unsigned i = 0; i < sizeof payload; i++)
        payload[i] = (uint8_t)i;
    uint64_t start, len;
    assert(!disk_data_partition(&io, &start, &len));
    assert(!writes);
    assert(disk_install_boot(&io, payload, sizeof payload, false, guid, NULL));
    assert(disk_data_partition(&io, &start, &len));
    assert(start == MN_DATA_START && len == SECTORS - 33 - start);
    uint8_t *esp = disk + MN_ESP_START * 512;
    assert(!memcmp(esp + 82, "FAT32   ", 8) && esp[510] == 0x55 && esp[511] == 0xaa);
    uint8_t *fat = esp + 32 * 512;
    assert(u32(fat + 5 * 4) == 6 && u32(fat + 6 * 4) == 7 && u32(fat + 7 * 4) == 0x0fffffff);
    assert(!memcmp(fat, fat + 1024 * 512, 1024 * 512));
    uint8_t *data = esp + (32 + 2048) * 512;
    assert(!memcmp(data + 2 * 512 + 64, "BOOTX64 EFI", 11));
    assert(!memcmp(data + 3 * 512, payload, sizeof payload));
    assert(disk[510] == 0x55 && disk[511] == 0xaa && disk[450] == 0xee);
    assert(!memcmp(disk + (SECTORS - 1) * 512, "EFI PART", 8));
    disk[512 + 24] ^= 1;
    assert(!disk_data_partition(&io, &start, &len));
    disk[512 + 24] ^= 1;
    disk[2 * 512 + 128 + 40] ^= 1;
    assert(!disk_data_partition(&io, &start, &len));
    disk[2 * 512 + 128 + 40] ^= 1;
    unsigned before = writes;
    io.sectors = MN_DATA_START;
    assert(!disk_install_boot(&io, payload, sizeof payload, false, guid, NULL) && before == writes);
    io.sectors = SECTORS;
    fail_write = true;
    assert(!disk_install_boot(&io, payload, sizeof payload, false, guid, NULL));
    fail_write = false;
    assert(disk_install_boot(&io, payload, sizeof payload, true, guid, NULL));
    assert(!memcmp(data + 2 * 512 + 64, "BOOTAA64EFI", 11));
    free(disk);
    puts("disk layout: GPT CRC/bounds, duplicate FATs, payload, architecture path and write "
         "failure passed");
}
