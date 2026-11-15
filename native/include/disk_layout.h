/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_DISK_LAYOUT_H
#define MNOS_DISK_LAYOUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* 512-byte sectors. ESP starts at 1 MiB and grows to fit the bundled guest.
 * Small original images retain the compatible 64 MiB ESP layout. */
#define MN_ESP_START 2048ULL
#define MN_ESP_SECTORS 131072ULL
#define MN_DATA_START (MN_ESP_START + MN_ESP_SECTORS)
struct disk_io {
    void *context;
    uint64_t sectors;
    bool (*read)(void *, uint64_t, unsigned, void *);
    bool (*write)(void *, uint64_t, unsigned, const void *);
    bool (*flush)(void *);
};
bool disk_data_partition(const struct disk_io *, uint64_t *, uint64_t *);
struct disk_boot_plan {uint64_t esp_sectors,data_start,payload_start;uint32_t fat_sectors;};
bool disk_plan_boot(size_t payload_bytes,struct disk_boot_plan *);
/* Caller must obtain explicit destructive-install confirmation before calling. */
bool disk_install_boot(const struct disk_io *, const void *, size_t, bool arm64,
                       const uint8_t guid[16], void (*progress)(unsigned));
#endif
