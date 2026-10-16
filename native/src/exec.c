/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "exec.h"
#include <string.h>
struct ehdr {
    unsigned char ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct phdr {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
_Static_assert(sizeof(struct ehdr) == 64, "ELF header layout");
_Static_assert(sizeof(struct phdr) == 56, "ELF segment layout");
static int range(uint64_t off, uint64_t len, uint64_t size) {
    return off <= size && len <= size - off;
}
int elf_load(const void *image, size_t size, uint16_t machine, uint64_t base,
             void *code, void *data, uint64_t *entry) {
    struct ehdr h;
    struct phdr load[2];
    unsigned count = 0, seen = 0;
    int executable_entry = 0;
    if (!image || !code || !data || !entry || size < sizeof h ||
        (machine != 62 && machine != 183) || base > UINT64_MAX - 8192 || (base & 4095))
        return -1;
    memcpy(&h, image, sizeof h);
    if (memcmp(h.ident, "\177ELF", 4) || h.ident[4] != 2 || h.ident[5] != 1 ||
        h.ident[6] != 1 || h.ident[7] != 0 || h.ident[8] != 0 || h.type != 2 ||
        h.machine != machine || h.version != 1 || h.flags || h.ehsize != sizeof h ||
        h.phentsize != sizeof(struct phdr) || !h.phnum || h.phnum > 16 ||
        !range(h.phoff, (uint64_t)h.phnum * sizeof(struct phdr), size))
        return -1;
    for (unsigned i = 0; i < h.phnum; ++i) {
        struct phdr p;
        memcpy(&p, (const unsigned char *)image + h.phoff + i * sizeof p, sizeof p);
        if (!p.type) continue;
        /* Metadata and non-executable GNU_STACK need no runtime action. */
        if (p.type == 4 || p.type == 6) {
            if (!range(p.offset, p.filesz, size)) return -1;
            continue;
        }
        if (p.type == 0x6474e551 && !(p.flags & 1)) continue;
        if (p.type != 1 || count == 2 || !p.memsz || p.filesz > p.memsz ||
            !range(p.offset, p.filesz, size) ||
            (p.align > 1 && ((p.align & (p.align - 1)) ||
             (p.vaddr & (p.align - 1)) != (p.offset & (p.align - 1))))) return -1;
        unsigned kind;
        if (p.flags == 5 && range(p.vaddr - base, p.memsz, 4096) && p.vaddr >= base)
            kind = 1;
        else if (p.flags == 6 && p.vaddr >= base + 4096 &&
                 range(p.vaddr - base - 4096, p.memsz, 4096)) kind = 2;
        else return -1;
        if (seen & kind) return -1;
        seen |= kind;
        if (kind == 1 && h.entry >= p.vaddr && h.entry - p.vaddr < p.filesz &&
            (machine != 183 || !(h.entry & 3))) executable_entry = 1;
        load[count++] = p;
    }
    if (!executable_entry) return -1;
    memset(code, 0, 4096);
    memset(data, 0, 4096);
    for (unsigned i = 0; i < count; ++i) {
        struct phdr *p = &load[i];
        unsigned char *dst = p->flags == 5 ? code : data;
        uint64_t offset = p->vaddr - base - (p->flags == 6 ? 4096 : 0);
        memcpy(dst + offset, (const unsigned char *)image + p->offset, p->filesz);
    }
    *entry = h.entry;
    return 0;
}
