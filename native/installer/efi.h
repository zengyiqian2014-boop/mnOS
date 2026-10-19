/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_EFI_H
#define MNOS_EFI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint64_t status;
struct guid {
    uint32_t a;
    uint16_t b, c;
    uint8_t d[8];
};
struct header {
    uint64_t signature;
    uint32_t revision, size, crc, reserved;
};
struct key {
    uint16_t scan, unicode;
};
struct input {
    void *reset;
    status (*read)(void *, struct key *);
    void *event;
};
struct console {
    void *reset;
    status (*print)(void *, const uint16_t *);
};
struct services {
    struct header hdr;
    void *fn[44];
};
struct config {
    struct guid guid;
    void *table;
};
struct system_table {
    struct header hdr;
    void *vendor;
    uint32_t revision;
    void *in_handle;
    struct input *in;
    void *out_handle;
    struct console *out;
    void *err_handle, *err, *runtime;
    struct services *services;
    size_t count;
    struct config *tables;
};
struct loaded_image {
    uint32_t revision;
    void *parent, *system, *device, *path, *reserved;
    uint32_t options_size;
    uint16_t *options;
    void *base;
    uint64_t size;
    uint32_t code_type, data_type;
    void *unload;
};
struct mode_info {
    uint32_t version, width, height, format, masks[4], pitch;
};
struct gop_mode {
    uint32_t maximum, current;
    struct mode_info *info;
    size_t info_size;
    uint64_t base;
    size_t size;
};
struct gop {
    status (*query)(void *, uint32_t, size_t *, struct mode_info **);
    status (*set)(void *, uint32_t);
    void *blt;
    struct gop_mode *mode;
};
struct memory_descriptor {
    uint32_t type, pad;
    uint64_t physical, virtual_address, pages, attributes;
};
static const struct guid loaded_guid = {
    0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const struct guid gop_guid = {
    0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
#define ERROR 0x8000000000000000ULL
#endif
