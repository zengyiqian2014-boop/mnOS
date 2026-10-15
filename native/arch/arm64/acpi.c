/* SPDX-License-Identifier: GPL-3.0-or-later
 * ACPI 6.x table discovery only. No AML interpreter or borrowed OS drivers.
 */
#include "platform.h"
static const uint8_t *madt;
static uint64_t little(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i)
        v |= (uint64_t)p[i] << (i * 8);
    return v;
}
static bool checksum(const uint8_t *p, size_t n) {
    uint8_t sum = 0;
    while (n--)
        sum += *p++;
    return sum == 0;
}
static const uint8_t *table(uint64_t address, bool (*valid)(uint64_t, size_t)) {
    if (!valid(address, 36))
        return NULL;
    const uint8_t *p = (void *)(uintptr_t)address;
    uint64_t n = little(p + 4, 4);
    return n >= 36 && n <= 1u << 20 && valid(address, n) && checksum(p, n) ? p : NULL;
}
bool arm_acpi(const void *rsdp, bool (*valid)(uint64_t, size_t)) {
    const uint8_t *r = rsdp;
    if (!valid((uintptr_t)r, 36) || memcmp(r, "RSD PTR ", 8) || r[15] < 2 || !checksum(r, 20))
        return false;
    uint64_t length = little(r + 20, 4);
    if (length < 36 || length > 4096 || !valid((uintptr_t)r, length) || !checksum(r, length))
        return false;
    const uint8_t *root = table(little(r + 24, 8), valid);
    if (!root || memcmp(root, "XSDT", 4))
        return false;
    size_t bytes = little(root + 4, 4);
    if ((bytes - 36) % 8)
        return false;
    bool psci = false, timer = false;
    for (size_t at = 36; at < bytes; at += 8) {
        const uint8_t *p = table(little(root + at, 8), valid);
        if (!p)
            return false;
        size_t n = little(p + 4, 4);
        if (!memcmp(p, "APIC", 4))
            madt = p;
        if (!memcmp(p, "SPCR", 4) && n >= 60 && (p[36] == 3 || p[36] == 13) && p[40] == 0)
            platform.uart = little(p + 44, 8);
        if (!memcmp(p, "FACP", 4) && n >= 131) {
            unsigned flags = little(p + 129, 2);
            psci = flags & 1;
            platform.psci_hvc = flags & 2;
        }
        if (!memcmp(p, "GTDT", 4) && n >= 64) {
            platform.timer_irq = little(p + 56, 4);
            timer = platform.timer_irq >= 16 && platform.timer_irq < 32;
        }
    }
    if (!madt || !psci || !timer)
        return false;
    bytes = little(madt + 4, 4);
    if (bytes < 44)
        return false;
    for (size_t at = 44; at < bytes;) {
        if (bytes - at < 2)
            return false;
        const uint8_t *p = madt + at;
        unsigned n = p[1];
        if (n < 2 || n > bytes - at)
            return false;
        if (p[0] == 12 && n >= 24) {
            platform.gicd = little(p + 8, 8);
            platform.gic_version = p[20];
        }
        if (p[0] == 11 && n >= 76 && (little(p + 12, 4) & 1) && little(p + 32, 8))
            platform.gicc = little(p + 32, 8);
        if (p[0] == 14 && n >= 16) {
            if (platform.gicr)
                return false;
            platform.gicr = little(p + 4, 8);
            platform.gicr_size = little(p + 12, 4);
        }
        at += n;
    }
    return platform.uart && platform.gicd &&
           ((platform.gic_version == 2 && platform.gicc) ||
            (platform.gic_version == 3 && platform.gicr && platform.gicr_size));
}
void arm_acpi_cpus(void (*visit)(uint64_t)) {
    size_t bytes = little(madt + 4, 4);
    for (size_t at = 44; at < bytes; at += madt[at + 1]) {
        const uint8_t *p = madt + at;
        if (p[0] == 11 && p[1] >= 76 && (little(p + 12, 4) & 1))
            visit(little(p + 68, 8));
    }
}
