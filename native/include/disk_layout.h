/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_DISK_LAYOUT_H
#define MNOS_DISK_LAYOUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* 512-byte logical sectors. GPT + 64 MiB FAT32 ESP + native MNFS data. */
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
/* Caller must obtain explicit destructive-install confirmation before calling. */
bool disk_install_boot(const struct disk_io *, const void *, size_t, bool arm64,
                       const uint8_t guid[16], void (*progress)(unsigned));
#endif
