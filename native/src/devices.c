/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
static bool shift, extended;
static int mouse_x = 640, mouse_y = 360;
static uint8_t packet[3];
static unsigned mouse_bytes;
static bool wait_write(void) {
    for (unsigned i = 0; i < 100000; ++i)
        if (!(inb(0x64) & 2))
            return true;
    return false;
}
static bool wait_read(void) {
    for (unsigned i = 0; i < 100000; ++i)
        if (inb(0x64) & 1)
            return true;
    return false;
}
static void mouse_command(uint8_t c) {
    if (!wait_write())
        return;
    outb(0x64, 0xd4);
    if (!wait_write())
        return;
    outb(0x60, c);
    if (wait_read())
        (void)inb(0x60);
}
void input_init(void) {
    for (unsigned i = 0; i < 64 && (inb(0x64) & 1); ++i)
        (void)inb(0x60);
    if (!wait_write())
        return;
    outb(0x64, 0xa8);
    if (!wait_write())
        return;
    outb(0x64, 0x20);
    if (!wait_read())
        return;
    uint8_t config = inb(0x60);
    if (!wait_write())
        return;
    outb(0x64, 0x60);
    if (!wait_write())
        return;
    outb(0x60, (config | 3) & ~0x30);
    mouse_command(0xf6);
    mouse_command(0xf4);
}
void keyboard_event(uint8_t code) {
    if (code == 0xe0) {
        extended = true;
        return;
    }
    if (code == 42 || code == 54) {
        shift = true;
        return;
    }
    if (code == 170 || code == 182) {
        shift = false;
        return;
    }
    if (code & 0x80) {
        extended = false;
        return;
    }
    if (extended) {
        extended = false;
        return;
    }
    if (code >= 59 && code <= 64) {
        ui_key((char)(0x81 + code - 59));
        return;
    }
    static const char normal[128] = {
        [1] = 27,    [2] = '1',  [3] = '2',  [4] = '3',   [5] = '4',  [6] = '5',   [7] = '6',
        [8] = '7',   [9] = '8',  [10] = '9', [11] = '0',  [12] = '-', [13] = '=',  [14] = '\b',
        [15] = '\t', [16] = 'q', [17] = 'w', [18] = 'e',  [19] = 'r', [20] = 't',  [21] = 'y',
        [22] = 'u',  [23] = 'i', [24] = 'o', [25] = 'p',  [26] = '[', [27] = ']',  [28] = '\n',
        [30] = 'a',  [31] = 's', [32] = 'd', [33] = 'f',  [34] = 'g', [35] = 'h',  [36] = 'j',
        [37] = 'k',  [38] = 'l', [39] = ';', [40] = '\'', [41] = '`', [43] = '\\', [44] = 'z',
        [45] = 'x',  [46] = 'c', [47] = 'v', [48] = 'b',  [49] = 'n', [50] = 'm',  [51] = ',',
        [52] = '.',  [53] = '/', [57] = ' '};
    char c = normal[code];
    if (shift && c >= 'a' && c <= 'z')
        c -= 32;
    if (c)
        ui_key(c);
}
void mouse_event(uint8_t b) {
    if (mouse_bytes == 0 && !(b & 8))
        return;
    packet[mouse_bytes++] = b;
    if (mouse_bytes < 3)
        return;
    mouse_bytes = 0;
    if (packet[0] & 0xc0)
        return;
    mouse_x += (int8_t)packet[1];
    mouse_y -= (int8_t)packet[2];
    if (mouse_x < 0)
        mouse_x = 0;
    if (mouse_y < 0)
        mouse_y = 0;
    if (mouse_x >= (int)boot.fb.width)
        mouse_x = (int)boot.fb.width - 1;
    if (mouse_y >= (int)boot.fb.height)
        mouse_y = (int)boot.fb.height - 1;
    ui_pointer(mouse_x, mouse_y, packet[0] & 1);
}
struct pci_device {
    uint8_t bus, slot, fn, class_id, subclass;
    uint16_t vendor, device;
};
static struct pci_device devices[128];
unsigned pci_count;
static uint32_t pci_read(unsigned bus, unsigned slot, unsigned fn, unsigned reg) {
    outl(0xcf8, 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8) | (reg & 0xfc));
    return inl(0xcfc);
}
void pci_scan(void) {
    for (unsigned b = 0; b < 256; ++b)
        for (unsigned s = 0; s < 32; ++s) {
            uint32_t id = pci_read(b, s, 0, 0);
            if ((id & 0xffff) == 0xffff)
                continue;
            unsigned functions = (pci_read(b, s, 0, 12) & 0x800000) ? 8 : 1;
            for (unsigned f = 0; f < functions; ++f) {
                id = pci_read(b, s, f, 0);
                if ((id & 0xffff) == 0xffff)
                    continue;
                uint32_t class = pci_read(b, s, f, 8);
                if (pci_count < ARRAY_SIZE(devices))
                    devices[pci_count++] = (struct pci_device){
                        b, s, f, class >> 24, (class >> 16) & 255, id & 0xffff, id >> 16};
            }
        }
    log_write("PCI: ");
    log_dec(pci_count);
    log_write(" devices enumerated (enumeration is not driver support)\n");
}
void pci_read_line(unsigned i, char *out, size_t size) {
    if (size < 64 || i >= pci_count) {
        if (size)
            out[0] = 0;
        return;
    }
    struct pci_device *d = &devices[i];
    const char *hex = "0123456789abcdef";
    const char *name = d->class_id == 1    ? "Storage"
                       : d->class_id == 2  ? "Network"
                       : d->class_id == 3  ? "Display"
                       : d->class_id == 6  ? "Bridge"
                       : d->class_id == 12 ? "Serial bus"
                                           : "Device";
    unsigned p = 0;
    for (unsigned n = 0; n < 4; ++n)
        out[p++] = hex[(d->vendor >> (12 - n * 4)) & 15];
    out[p++] = ':';
    for (unsigned n = 0; n < 4; ++n)
        out[p++] = hex[(d->device >> (12 - n * 4)) & 15];
    out[p++] = ' ';
    out[p++] = ' ';
    while (*name)
        out[p++] = *name++;
    out[p] = 0;
}
static uint16_t pm1a, pm1b, sleep_a, sleep_b;
static bool checksum(const void *data, size_t n) {
    const uint8_t *p = data;
    uint8_t sum = 0;
    while (n--)
        sum += *p++;
    return sum == 0;
}
struct acpi_header {
    char sig[4];
    uint32_t length;
    uint8_t rest[28];
} __attribute__((packed));
static struct acpi_header *acpi_table(uint64_t addr) {
    if (addr < 4096 || addr >= 0x100000000ULL - sizeof(struct acpi_header))
        return NULL;
    struct acpi_header *h = (void *)addr;
    if (h->length < 36 || h->length > 1024 * 1024 || h->length > 0x100000000ULL - addr ||
        !checksum(h, h->length))
        return NULL;
    return h;
}
void power_init(void) {
    if (!boot.rsdp || !checksum(boot.rsdp, 20))
        return;
    const uint8_t *r = boot.rsdp;
    uint32_t root;
    memcpy(&root, r + 16, 4);
    struct acpi_header *table = acpi_table(root);
    if (!table)
        return;
    for (unsigned off = 36; off + 4 <= table->length; off += 4) {
        uint32_t addr;
        memcpy(&addr, (uint8_t *)table + off, 4);
        struct acpi_header *fadt = acpi_table(addr);
        if (!fadt || memcmp(fadt->sig, "FACP", 4) || fadt->length < 90)
            continue;
        uint32_t dsdt, pa, pb;
        memcpy(&dsdt, (uint8_t *)fadt + 40, 4);
        memcpy(&pa, (uint8_t *)fadt + 64, 4);
        memcpy(&pb, (uint8_t *)fadt + 68, 4);
        if (pa > 0xffff || pb > 0xffff)
            continue;
        struct acpi_header *aml = acpi_table(dsdt);
        if (!aml)
            continue;
        const uint8_t *p = (void *)aml;
        for (unsigned i = 36; i + 16 < aml->length; ++i)
            if (!memcmp(p + i, "_S5_", 4) && p[i + 4] == 0x12) {
                unsigned j = i + 5;
                unsigned lenbytes = p[j] >> 6;
                j += lenbytes + 1;
                if (j + 6 >= aml->length || p[j++] < 2)
                    continue;
                if (p[j] == 0x0a)
                    ++j;
                unsigned a = p[j++];
                if (p[j] == 0x0a)
                    ++j;
                unsigned b = p[j];
                if (a <= 7 && b <= 7) {
                    pm1a = (uint16_t)pa;
                    pm1b = (uint16_t)pb;
                    sleep_a = a << 10;
                    sleep_b = b << 10;
                    return;
                }
            }
    }
}
static void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0,%1" ::"a"(value), "Nd"(port));
}
void power_off(void) {
    log_write("POWER: shutdown requested\n");
    if (pm1a) {
        outw(pm1a, sleep_a | 0x2000);
        if (pm1b)
            outw(pm1b, sleep_b | 0x2000);
        /* Firmware/QEMU may process the power request asynchronously. */
        log_write("POWER: ACPI S5 issued\n");
        for (;;)
            __asm__ volatile("cli; hlt");
    }
    log_write("POWER: ACPI S5 unavailable or not accepted\n");
}
void reboot(void) {
    __asm__ volatile("cli");
    if (wait_write())
        outb(0x64, 0xfe);
    struct {
        uint16_t l;
        uint64_t b;
    } __attribute__((packed)) empty = {0, 0};
    __asm__ volatile("lidt %0; int $3" ::"m"(empty));
    for (;;)
        __asm__ volatile("hlt");
}
