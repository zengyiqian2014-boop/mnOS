/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform.h"
static uint32_t be32(const void *p) {
    const uint8_t *b = p;
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
static uint64_t cells(const void *p, unsigned n) {
    const uint8_t *b = p;
    uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i)
        v = (v << 32) | be32(b + 4 * i);
    return v;
}
static bool compat(const uint8_t *p, size_t n, const char *name) {
    size_t at = 0, want = strlen(name);
    while (at < n) {
        size_t k = at;
        while (k < n && p[k])
            ++k;
        if (k == n)
            return false;
        if (k - at == want && !memcmp(p + at, name, want))
            return true;
        at = k + 1;
    }
    return false;
}
struct node {
    unsigned ac, sc, pac, psc;
    const uint8_t *reg;
    uint32_t reglen;
    bool memory, cpu, reserved, disabled, uart, gic2, gic3, virtio, psci, hvc;
};
static bool walk(const void *data, bool collect, void (*cpu)(uint64_t)) {
    const uint8_t *b = data;
    if (!b || be32(b) != 0xd00dfeed)
        return false;
    uint32_t total = be32(b + 4), off = be32(b + 8), strings = be32(b + 12), sn = be32(b + 32),
             len = be32(b + 36);
    if (total < 40 || total > (16u << 20) || off > total || len > total - off || strings > total ||
        sn > total - strings)
        return false;
    const uint8_t *p = b + off, *end = p + len;
    struct node stack[32];
    unsigned depth = 0;
    if (collect)
        platform.fdt_size = total;
    while (p + 4 <= end) {
        uint32_t tag = be32(p);
        p += 4;
        if (tag == 1) {
            if (depth == ARRAY_SIZE(stack))
                return false;
            const char *name = (const char *)p;
            while (p < end && *p)
                ++p;
            if (p == end)
                return false;
            ++p;
            p = b + (((size_t)(p - b) + 3) & ~(size_t)3);
            if (p > end)
                return false;
            struct node parent = depth ? stack[depth - 1] : (struct node){.ac = 2, .sc = 1};
            stack[depth++] =
                (struct node){.ac = parent.ac,
                              .sc = parent.sc,
                              .pac = parent.ac,
                              .psc = parent.sc,
                              .memory = !strncmp(name, "memory@", 7),
                              .cpu = !strncmp(name, "cpu@", 4),
                              .reserved = parent.reserved || streq(name, "reserved-memory")};
        } else if (tag == 3) {
            if (!depth || p + 8 > end)
                return false;
            uint32_t n = be32(p), no = be32(p + 4);
            p += 8;
            if (n > (size_t)(end - p) || no >= sn)
                return false;
            const char *name = (const char *)b + strings + no;
            size_t nz = 0;
            while (nz < sn - no && name[nz])
                ++nz;
            if (nz == sn - no)
                return false;
            struct node *s = &stack[depth - 1];
            if (streq(name, "#address-cells") && n == 4)
                s->ac = be32(p);
            if (streq(name, "#size-cells") && n == 4)
                s->sc = be32(p);
            if (streq(name, "reg")) {
                s->reg = p;
                s->reglen = n;
            }
            if (streq(name, "status") && compat(p, n, "disabled"))
                s->disabled = true;
            if (streq(name, "device_type") && compat(p, n, "memory"))
                s->memory = true;
            if (streq(name, "compatible")) {
                s->uart = compat(p, n, "arm,pl011");
                s->gic2 = compat(p, n, "arm,cortex-a15-gic") || compat(p, n, "arm,gic-400");
                s->gic3 = compat(p, n, "arm,gic-v3");
                s->virtio = compat(p, n, "virtio,mmio");
                s->psci = compat(p, n, "arm,psci-0.2") || compat(p, n, "arm,psci-1.0");
            }
            if (streq(name, "method"))
                s->hvc = compat(p, n, "hvc");
            if (collect && streq(name, "bootargs") && n) {
                for (size_t i = 0; i + 15 < n; ++i)
                    if (!memcmp(p + i, "mnos.selftest=1", 15))
                        boot.selftest = true;
            }
            p = b + (((size_t)(p - b) + n + 3) & ~(size_t)3);
            if (p > end)
                return false;
        } else if (tag == 2) {
            if (!depth)
                return false;
            struct node *s = &stack[--depth];
            if (collect && s->psci)
                platform.psci_hvc = s->hvc;
            if (s->disabled || !s->reg || s->pac > 2 || s->psc > 2 || !s->pac)
                continue;
            unsigned stride = (s->pac + s->psc) * 4;
            if (!stride || s->reglen % stride)
                return false;
            for (unsigned at = 0; at < s->reglen; at += stride) {
                uint64_t base = cells(s->reg + at, s->pac),
                         size = cells(s->reg + at + s->pac * 4, s->psc);
                if (size > UINT64_MAX - base)
                    return false;
                if (cpu && s->cpu)
                    cpu(base);
                if (!collect)
                    continue;
                if (s->memory) {
                    if (platform.ram_count == ARM_RANGES)
                        return false;
                    platform.ram[platform.ram_count++] = (struct arm_range){base, size};
                }
                if (s->reserved) {
                    if (platform.reserved_count == ARM_RANGES)
                        return false;
                    platform.reserved[platform.reserved_count++] = (struct arm_range){base, size};
                }
                if (!at && s->uart)
                    platform.uart = base;
                if (!at && (s->gic2 || s->gic3)) {
                    platform.gicd = base;
                    platform.gic_version = s->gic3 ? 3 : 2;
                }
                if (at == stride && s->gic2)
                    platform.gicc = base;
                if (at == stride && s->gic3) {
                    platform.gicr = base;
                    platform.gicr_size = size;
                }
                if (!at && s->virtio) {
                    if (platform.virtio_count == ARM_VIRTIO)
                        return false;
                    platform.virtio[platform.virtio_count++] = base;
                }
            }
        } else if (tag == 9)
            return depth == 0;
        else if (tag != 4)
            return false;
    }
    return false;
}
bool arm_fdt(const void *p) {
    platform.fdt = p;
    platform.timer_irq = 30;
    if (!walk(p, true, NULL))
        return false;
    const uint8_t *b = p;
    uint32_t off = be32(b + 16);
    if (off > platform.fdt_size)
        return false;
    while (off + 16 <= platform.fdt_size) {
        uint64_t base = cells(b + off, 2), size = cells(b + off + 8, 2);
        off += 16;
        if (!base && !size)
            return true;
        if (size > UINT64_MAX - base || platform.reserved_count == ARM_RANGES)
            return false;
        platform.reserved[platform.reserved_count++] = (struct arm_range){base, size};
    }
    return false;
}
void arm_fdt_cpus(void (*visit)(uint64_t)) {
    if (!walk(platform.fdt, false, visit))
        panic("CPU device tree");
}
