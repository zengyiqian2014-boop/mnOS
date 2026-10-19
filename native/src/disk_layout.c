/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original, bounded GPT/FAT32 boot-volume writer. No installed filesystem is
 * inferred to be disposable: only the installer calls the write entry point.
 */
#include "disk_layout.h"
#include <string.h>
static const uint8_t data_type[16] = {0x4d, 0x4e, 0x4f, 0x53, 0x44, 0x41, 0x54, 0x41,
                                      0x81, 0x54, 0x6d, 0x6e, 0x6f, 0x73, 0,    1};
static const uint8_t esp_type[16] = {0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
                                     0xba, 0x4b, 0,    0xa0, 0xc9, 0x3e, 0xc9, 0x3b};
static uint32_t crc(const void *data, size_t n) {
    const uint8_t *p = data;
    uint32_t c = ~0u;
    while (n--) {
        c ^= *p++;
        for (unsigned i = 0; i < 8; i++)
            c = (c >> 1) ^ ((0u - (c & 1)) & 0xedb88320u);
    }
    return ~c;
}
static uint32_t u32(const void *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
static uint64_t u64(const void *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}
static void w16(void *p, uint16_t v) {
    memcpy(p, &v, 2);
}
static void w32(void *p, uint32_t v) {
    memcpy(p, &v, 4);
}
static void w64(void *p, uint64_t v) {
    memcpy(p, &v, 8);
}
bool disk_data_partition(const struct disk_io *io, uint64_t *start, uint64_t *length) {
    uint8_t header[512], entries[16384];
    if (io->sectors < MN_DATA_START + 2048 || !io->read(io->context, 1, 1, header) ||
        memcmp(header, "EFI PART", 8))
        return false;
    uint32_t size = u32(header + 12), sum = u32(header + 16);
    w32(header + 16, 0);
    if (size != 92 || crc(header, size) != sum || u64(header + 24) != 1 ||
        u64(header + 32) != io->sectors - 1 || u32(header + 80) != 128 || u32(header + 84) != 128 ||
        u64(header + 72) != 2)
        return false;
    if (!io->read(io->context, 2, 32, entries) || crc(entries, sizeof entries) != u32(header + 88))
        return false;
    unsigned found = 0;
    uint64_t a = 0, b = 0;
    for (unsigned i = 0; i < 128; i++)
        if (!memcmp(entries + i * 128, data_type, 16)) {
            a = u64(entries + i * 128 + 32);
            b = u64(entries + i * 128 + 40);
            ++found;
        }
    if (found != 1 || a < MN_DATA_START || a % 8 || b < a || b >= io->sectors - 33 ||
        u64(header + 40) > a || u64(header + 48) < b)
        return false;
    *start = a;
    *length = b - a + 1;
    return true;
}
static bool write(const struct disk_io *io, uint64_t lba, unsigned n, const void *p) {
    return lba < io->sectors && n <= io->sectors - lba && io->write(io->context, lba, n, p);
}
static void directory(uint8_t *p, const char name[11], uint8_t attr, uint32_t cluster,
                      uint32_t size) {
    memset(p, 0, 32);
    memcpy(p, name, 11);
    p[11] = attr;
    w16(p + 20, cluster >> 16);
    w16(p + 26, cluster);
    w32(p + 28, size);
}
bool disk_install_boot(const struct disk_io *io, const void *payload, size_t bytes, bool arm64,
                       const uint8_t guid[16], void (*progress)(unsigned)) {
    uint8_t sector[512], entries[16384], fat[4096];
    if (!payload || !bytes || bytes > (32u << 20) || io->sectors < MN_DATA_START + 32768 ||
        io->sectors > UINT64_MAX / 512)
        return false;
    /* Write payload and FAT first. Publish GPT only after all data is flushed. */
    const uint64_t first = MN_ESP_START + 32 + 2048;
    unsigned clusters = (unsigned)((bytes + 511) / 512);
    for (unsigned i = 0; i < clusters; i += 8) {
        size_t n = bytes - (size_t)i * 512;
        if (n > sizeof fat)
            n = sizeof fat;
        memset(fat, 0, sizeof fat);
        memcpy(fat, (const uint8_t *)payload + (size_t)i * 512, n);
        if (!write(io, first + 3 + i, (unsigned)((n + 511) / 512), fat))
            return false;
        if (progress && i % 256 == 0)
            progress(5 + i * 65 / clusters);
    }
    for (unsigned s = 0; s < 1024; s += 8) {
        memset(fat, 0, sizeof fat);
        for (unsigned i = 0; i < 1024; i++) {
            unsigned c = s * 128 + i;
            uint32_t v = 0;
            if (c == 0)
                v = 0x0ffffff8;
            else if (c <= 4)
                v = 0x0fffffff;
            else if (c < 5 + clusters)
                v = c == 4 + clusters ? 0x0fffffff : c + 1;
            w32(fat + i * 4, v);
        }
        if (!write(io, MN_ESP_START + 32 + s, 8, fat) ||
            !write(io, MN_ESP_START + 1056 + s, 8, fat))
            return false;
    }
    memset(sector, 0, 512);
    directory(sector, "EFI        ", 16, 3, 0);
    if (!write(io, first, 1, sector))
        return false;
    memset(sector, 0, 512);
    directory(sector, ".          ", 16, 3, 0);
    directory(sector + 32, "..         ", 16, 0, 0);
    directory(sector + 64, "BOOT       ", 16, 4, 0);
    if (!write(io, first + 1, 1, sector))
        return false;
    memset(sector, 0, 512);
    directory(sector, ".          ", 16, 4, 0);
    directory(sector + 32, "..         ", 16, 3, 0);
    directory(sector + 64, arm64 ? "BOOTAA64EFI" : "BOOTX64 EFI", 32, 5, (uint32_t)bytes);
    if (!write(io, first + 2, 1, sector))
        return false;
    /* Reserved sectors, VBR and duplicate VBR/FSInfo. */
    memset(fat, 0, sizeof fat);
    for (unsigned s = 0; s < 32; s += 8)
        if (!write(io, MN_ESP_START + s, 8, fat))
            return false;
    memset(sector, 0, 512);
    sector[0] = 0xeb;
    sector[1] = 0x58;
    sector[2] = 0x90;
    memcpy(sector + 3, "MNOS    ", 8);
    w16(sector + 11, 512);
    sector[13] = 1;
    w16(sector + 14, 32);
    sector[16] = 2;
    sector[21] = 0xf8;
    w16(sector + 24, 63);
    w16(sector + 26, 255);
    w32(sector + 28, MN_ESP_START);
    w32(sector + 32, MN_ESP_SECTORS);
    w32(sector + 36, 1024);
    w32(sector + 44, 2);
    w16(sector + 48, 1);
    w16(sector + 50, 6);
    sector[64] = 0x80;
    sector[66] = 0x29;
    memcpy(sector + 67, guid, 4);
    memcpy(sector + 71, "MNOS BOOT  ", 11);
    memcpy(sector + 82, "FAT32   ", 8);
    sector[510] = 0x55;
    sector[511] = 0xaa;
    if (!write(io, MN_ESP_START, 1, sector) || !write(io, MN_ESP_START + 6, 1, sector))
        return false;
    memset(sector, 0, 512);
    w32(sector, 0x41615252);
    w32(sector + 484, 0x61417272);
    w32(sector + 488, 0xffffffff);
    w32(sector + 492, 0xffffffff);
    w32(sector + 508, 0xaa550000);
    if (!write(io, MN_ESP_START + 1, 1, sector) || !write(io, MN_ESP_START + 7, 1, sector))
        return false;
    memset(entries, 0, sizeof entries);
    for (unsigned i = 0; i < 2; i++) {
        uint8_t *e = entries + i * 128;
        memcpy(e, i ? data_type : esp_type, 16);
        memcpy(e + 16, guid, 16);
        e[31] ^= (uint8_t)(i + 1);
        w64(e + 32, i ? MN_DATA_START : MN_ESP_START);
        w64(e + 40, i ? io->sectors - 34 : MN_DATA_START - 1);
        const char *label = i ? "mnOS data" : "mnOS boot";
        for (unsigned n = 0; label[n]; n++)
            w16(e + 56 + 2 * n, (uint16_t)label[n]);
    }
    uint32_t esum = crc(entries, sizeof entries);
    if (!write(io, 2, 32, entries) || !write(io, io->sectors - 33, 32, entries) ||
        !io->flush(io->context))
        return false;
    for (unsigned i = 0; i < 2; i++) {
        memset(sector, 0, 512);
        memcpy(sector, "EFI PART", 8);
        w32(sector + 8, 0x10000);
        w32(sector + 12, 92);
        w64(sector + 24, i ? io->sectors - 1 : 1);
        w64(sector + 32, i ? 1 : io->sectors - 1);
        w64(sector + 40, 34);
        w64(sector + 48, io->sectors - 34);
        memcpy(sector + 56, guid, 16);
        w64(sector + 72, i ? io->sectors - 33 : 2);
        w32(sector + 80, 128);
        w32(sector + 84, 128);
        w32(sector + 88, esum);
        w32(sector + 16, crc(sector, 92));
        if (!write(io, i ? io->sectors - 1 : 1, 1, sector))
            return false;
    }
    memset(sector, 0, 512);
    sector[450] = 0xee;
    sector[447] = 0;
    sector[448] = 2;
    sector[449] = 0;
    memset(sector + 451, 255, 3);
    w32(sector + 454, 1);
    w32(sector + 458, io->sectors - 1 > 0xffffffff ? 0xffffffff : (uint32_t)(io->sectors - 1));
    sector[510] = 0x55;
    sector[511] = 0xaa;
    if (!write(io, 0, 1, sector) || !io->flush(io->context))
        return false;
    if (progress)
        progress(80);
    return true;
}
