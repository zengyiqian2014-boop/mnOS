/* SPDX-License-Identifier: GPL-3.0-or-later
 * MNFS v1: original checksummed, copy-on-write B-tree filesystem.
 * The disk format is documented in native/FILESYSTEM.md. It is not APFS.
 * Serialized little-endian x86_64 structures; no libc or operating-system ABI.
 */
#include "mnfs.h"
#include <string.h>
#define NODE_MAGIC 0x45444f4eu
#define INODE_MAGIC 0x444f4e49u
#define MAX_KEYS 63u
#define MAX_LEVEL 20u
enum { INODE = 1, DIRECTORY = 2, DATA = 3 };
struct key { uint64_t owner, type, index; };
struct value { uint64_t block, size; uint32_t checksum, flags; };
struct entry { struct key key; struct value value; };
struct node {
    uint32_t checksum, magic, level, count;
    struct entry entries[64];
    uint64_t children[65];
    uint8_t padding[488];
};
struct inode {
    uint32_t checksum, magic;
    uint64_t id, parent;
    char name[256];
    uint8_t padding[3816];
};
struct checkpoint {
    uint32_t checksum, magic;
    char signature[8];
    uint64_t version, generation, blocks, root, next_inode, snapshot;
    uint8_t padding[4032];
};
_Static_assert(sizeof(struct node) == MNFS_BLOCK, "node format");
_Static_assert(sizeof(struct inode) == MNFS_BLOCK, "inode format");
_Static_assert(sizeof(struct checkpoint) == MNFS_BLOCK, "checkpoint format");
struct change { uint64_t left, right; struct entry middle; };
struct transaction { uint64_t root, next_inode, snapshot; };
static const uint32_t crc_table[256] = {
    0x00000000u, 0xf26b8303u, 0xe13b70f7u, 0x1350f3f4u, 0xc79a971fu, 0x35f1141cu, 0x26a1e7e8u, 0xd4ca64ebu,
    0x8ad958cfu, 0x78b2dbccu, 0x6be22838u, 0x9989ab3bu, 0x4d43cfd0u, 0xbf284cd3u, 0xac78bf27u, 0x5e133c24u,
    0x105ec76fu, 0xe235446cu, 0xf165b798u, 0x030e349bu, 0xd7c45070u, 0x25afd373u, 0x36ff2087u, 0xc494a384u,
    0x9a879fa0u, 0x68ec1ca3u, 0x7bbcef57u, 0x89d76c54u, 0x5d1d08bfu, 0xaf768bbcu, 0xbc267848u, 0x4e4dfb4bu,
    0x20bd8edeu, 0xd2d60dddu, 0xc186fe29u, 0x33ed7d2au, 0xe72719c1u, 0x154c9ac2u, 0x061c6936u, 0xf477ea35u,
    0xaa64d611u, 0x580f5512u, 0x4b5fa6e6u, 0xb93425e5u, 0x6dfe410eu, 0x9f95c20du, 0x8cc531f9u, 0x7eaeb2fau,
    0x30e349b1u, 0xc288cab2u, 0xd1d83946u, 0x23b3ba45u, 0xf779deaeu, 0x05125dadu, 0x1642ae59u, 0xe4292d5au,
    0xba3a117eu, 0x4851927du, 0x5b016189u, 0xa96ae28au, 0x7da08661u, 0x8fcb0562u, 0x9c9bf696u, 0x6ef07595u,
    0x417b1dbcu, 0xb3109ebfu, 0xa0406d4bu, 0x522bee48u, 0x86e18aa3u, 0x748a09a0u, 0x67dafa54u, 0x95b17957u,
    0xcba24573u, 0x39c9c670u, 0x2a993584u, 0xd8f2b687u, 0x0c38d26cu, 0xfe53516fu, 0xed03a29bu, 0x1f682198u,
    0x5125dad3u, 0xa34e59d0u, 0xb01eaa24u, 0x42752927u, 0x96bf4dccu, 0x64d4cecfu, 0x77843d3bu, 0x85efbe38u,
    0xdbfc821cu, 0x2997011fu, 0x3ac7f2ebu, 0xc8ac71e8u, 0x1c661503u, 0xee0d9600u, 0xfd5d65f4u, 0x0f36e6f7u,
    0x61c69362u, 0x93ad1061u, 0x80fde395u, 0x72966096u, 0xa65c047du, 0x5437877eu, 0x4767748au, 0xb50cf789u,
    0xeb1fcbadu, 0x197448aeu, 0x0a24bb5au, 0xf84f3859u, 0x2c855cb2u, 0xdeeedfb1u, 0xcdbe2c45u, 0x3fd5af46u,
    0x7198540du, 0x83f3d70eu, 0x90a324fau, 0x62c8a7f9u, 0xb602c312u, 0x44694011u, 0x5739b3e5u, 0xa55230e6u,
    0xfb410cc2u, 0x092a8fc1u, 0x1a7a7c35u, 0xe811ff36u, 0x3cdb9bddu, 0xceb018deu, 0xdde0eb2au, 0x2f8b6829u,
    0x82f63b78u, 0x709db87bu, 0x63cd4b8fu, 0x91a6c88cu, 0x456cac67u, 0xb7072f64u, 0xa457dc90u, 0x563c5f93u,
    0x082f63b7u, 0xfa44e0b4u, 0xe9141340u, 0x1b7f9043u, 0xcfb5f4a8u, 0x3dde77abu, 0x2e8e845fu, 0xdce5075cu,
    0x92a8fc17u, 0x60c37f14u, 0x73938ce0u, 0x81f80fe3u, 0x55326b08u, 0xa759e80bu, 0xb4091bffu, 0x466298fcu,
    0x1871a4d8u, 0xea1a27dbu, 0xf94ad42fu, 0x0b21572cu, 0xdfeb33c7u, 0x2d80b0c4u, 0x3ed04330u, 0xccbbc033u,
    0xa24bb5a6u, 0x502036a5u, 0x4370c551u, 0xb11b4652u, 0x65d122b9u, 0x97baa1bau, 0x84ea524eu, 0x7681d14du,
    0x2892ed69u, 0xdaf96e6au, 0xc9a99d9eu, 0x3bc21e9du, 0xef087a76u, 0x1d63f975u, 0x0e330a81u, 0xfc588982u,
    0xb21572c9u, 0x407ef1cau, 0x532e023eu, 0xa145813du, 0x758fe5d6u, 0x87e466d5u, 0x94b49521u, 0x66df1622u,
    0x38cc2a06u, 0xcaa7a905u, 0xd9f75af1u, 0x2b9cd9f2u, 0xff56bd19u, 0x0d3d3e1au, 0x1e6dcdeeu, 0xec064eedu,
    0xc38d26c4u, 0x31e6a5c7u, 0x22b65633u, 0xd0ddd530u, 0x0417b1dbu, 0xf67c32d8u, 0xe52cc12cu, 0x1747422fu,
    0x49547e0bu, 0xbb3ffd08u, 0xa86f0efcu, 0x5a048dffu, 0x8ecee914u, 0x7ca56a17u, 0x6ff599e3u, 0x9d9e1ae0u,
    0xd3d3e1abu, 0x21b862a8u, 0x32e8915cu, 0xc083125fu, 0x144976b4u, 0xe622f5b7u, 0xf5720643u, 0x07198540u,
    0x590ab964u, 0xab613a67u, 0xb831c993u, 0x4a5a4a90u, 0x9e902e7bu, 0x6cfbad78u, 0x7fab5e8cu, 0x8dc0dd8fu,
    0xe330a81au, 0x115b2b19u, 0x020bd8edu, 0xf0605beeu, 0x24aa3f05u, 0xd6c1bc06u, 0xc5914ff2u, 0x37faccf1u,
    0x69e9f0d5u, 0x9b8273d6u, 0x88d28022u, 0x7ab90321u, 0xae7367cau, 0x5c18e4c9u, 0x4f48173du, 0xbd23943eu,
    0xf36e6f75u, 0x0105ec76u, 0x12551f82u, 0xe03e9c81u, 0x34f4f86au, 0xc69f7b69u, 0xd5cf889du, 0x27a40b9eu,
    0x79b737bau, 0x8bdcb4b9u, 0x988c474du, 0x6ae7c44eu, 0xbe2da0a5u, 0x4c4623a6u, 0x5f16d052u, 0xad7d5351u,
};
static uint32_t crc(const void *buffer, size_t count) {
    const uint8_t *p = buffer;
    uint32_t value = ~0u;
    while (count--) value = (value >> 8) ^ crc_table[(value ^ *p++) & 255];
    return ~value;
}
static uint32_t metadata_crc(const void *p) { return crc((const uint8_t *)p + 4, MNFS_BLOCK - 4); }
static int compare(struct key a, struct key b) {
    if (a.owner != b.owner) return a.owner < b.owner ? -1 : 1;
    if (a.type != b.type) return a.type < b.type ? -1 : 1;
    return a.index == b.index ? 0 : a.index < b.index ? -1 : 1;
}
static bool valid_block(struct mnfs *f, uint64_t p) { return p >= 2 && p < f->io.blocks; }
static bool marked(struct mnfs *f, uint64_t p) { return (f->used[p / 8] >> (p % 8)) & 1; }
static void mark(struct mnfs *f, uint64_t p) {
    if (!marked(f, p)) { f->used[p / 8] |= 1u << (p % 8); --f->free_blocks; }
}
static int allocate_block(struct mnfs *f, uint64_t *p) {
    if (!f->free_blocks) return MN_NOSPC;
    for (uint64_t n = 0; n < f->io.blocks; ++n) {
        uint64_t at = f->cursor++;
        if (f->cursor == f->io.blocks) f->cursor = 2;
        if (!marked(f, at)) { mark(f, at); *p = at; return MN_OK; }
    }
    return MN_NOSPC;
}
static int write_block(struct mnfs *f, uint64_t p, const void *buffer) {
    if (p >= f->io.blocks || !f->io.write(f->io.context, p, buffer)) {
        f->poisoned = true; return MN_IO;
    }
    return MN_OK;
}
static int save_metadata(struct mnfs *f, void *buffer, uint64_t *p) {
    int e = allocate_block(f, p);
    if (e) return e;
    *(uint32_t *)buffer = metadata_crc(buffer);
    return write_block(f, *p, buffer);
}
static int load_node(struct mnfs *f, uint64_t p, struct node **result) {
    if (!valid_block(f, p)) return MN_CORRUPT;
    struct node *n = f->io.allocate(sizeof *n);
    if (!n) return MN_NOMEM;
    int e = MN_CORRUPT;
    if (!f->io.read(f->io.context, p, n)) { e = MN_IO; goto bad; }
    if (n->magic != NODE_MAGIC || n->checksum != metadata_crc(n) ||
        !n->count || n->count > MAX_KEYS || n->level > MAX_LEVEL) goto bad;
    for (unsigned i = 0; i < n->count; ++i) {
        struct key key = n->entries[i].key;
        if (!key.owner || key.type < INODE || key.type > DATA ||
            (key.type == INODE && key.index) || (i && compare(n->entries[i - 1].key, key) >= 0)) goto bad;
    }
    for (unsigned i = 0; i <= n->count; ++i)
        if (n->level ? !valid_block(f, n->children[i]) : n->children[i] != 0) goto bad;
    *result = n; return MN_OK;
bad:
    f->io.release(n); return e;
}
static unsigned position(struct node *n, struct key key) {
    unsigned lo = 0, hi = n->count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (compare(n->entries[mid].key, key) < 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}
static int lookup(struct mnfs *f, uint64_t root, struct key key, struct value *v) {
    unsigned expected = MAX_LEVEL + 1;
    while (root) {
        struct node *n;
        int e = load_node(f, root, &n);
        if (e) return e;
        if (n->level >= expected) { f->io.release(n); return MN_CORRUPT; }
        expected = n->level;
        unsigned at = position(n, key);
        if (at < n->count && !compare(n->entries[at].key, key)) {
            *v = n->entries[at].value; f->io.release(n); return MN_OK;
        }
        root = n->level ? n->children[at] : 0;
        f->io.release(n);
    }
    return MN_NOENT;
}
static int insert(struct mnfs *f, uint64_t root, struct entry value, unsigned expected,
                  struct change *out) {
    struct node *n;
    int e;
    if (root) { e = load_node(f, root, &n); if (e) return e; }
    else {
        n = f->io.allocate(sizeof *n);
        if (!n) return MN_NOMEM;
        memset(n, 0, sizeof *n); n->magic = NODE_MAGIC;
    }
    if (n->level >= expected) { e = MN_CORRUPT; goto done; }
    unsigned at = position(n, value.key);
    if (at < n->count && !compare(n->entries[at].key, value.key)) n->entries[at] = value;
    else {
        struct change child = {0};
        if (n->level) {
            e = insert(f, n->children[at], value, n->level, &child);
            if (e) goto done;
            n->children[at] = child.left;
            if (!child.right) goto store;
            value = child.middle;
        }
        for (unsigned i = n->count; i > at; --i) {
            n->entries[i] = n->entries[i - 1]; n->children[i + 1] = n->children[i];
        }
        n->entries[at] = value; n->children[at + 1] = child.right; ++n->count;
    }
store:
    out->right = 0;
    if (n->count > MAX_KEYS) {
        struct node *right = f->io.allocate(sizeof *right);
        if (!right) { e = MN_NOMEM; goto done; }
        memset(right, 0, sizeof *right);
        right->magic = NODE_MAGIC; right->level = n->level; right->count = 31;
        memcpy(right->entries, n->entries + 33, 31 * sizeof(struct entry));
        memcpy(right->children, n->children + 33, 32 * sizeof(uint64_t));
        out->middle = n->entries[32]; n->count = 32;
        memset(n->entries + 32, 0, 32 * sizeof(struct entry));
        memset(n->children + 33, 0, 32 * sizeof(uint64_t));
        e = save_metadata(f, right, &out->right); f->io.release(right);
        if (e) goto done;
    }
    e = save_metadata(f, n, &out->left);
done:
    f->io.release(n); return e;
}
static int set(struct mnfs *f, struct key key, struct value value) {
    struct change out;
    int e = insert(f, f->root, (struct entry){key, value}, MAX_LEVEL + 1, &out);
    if (e) return e;
    if (!out.right) { f->root = out.left; return MN_OK; }
    struct node *n;
    e = load_node(f, out.left, &n);
    if (e) return e;
    unsigned level = n->level + 1;
    memset(n, 0, sizeof *n);
    n->magic = NODE_MAGIC; n->level = level; n->count = 1;
    n->entries[0] = out.middle; n->children[0] = out.left; n->children[1] = out.right;
    e = level > MAX_LEVEL ? MN_NOSPC : save_metadata(f, n, &f->root);
    f->io.release(n); return e;
}
static bool valid_name(const char *name) {
    size_t n = 0;
    while (n <= MNFS_NAME && name[n]) {
        if ((uint8_t)name[n] < 32 || name[n] == '/' || name[n] == 127) return false;
        ++n;
    }
    return n && n <= MNFS_NAME && !(n == 1 && name[0] == '.') &&
           !(n == 2 && name[0] == '.' && name[1] == '.');
}
static int inode_read(struct mnfs *f, uint64_t root, uint64_t id, struct value *v,
                      uint64_t *parent, char *name) {
    int e = lookup(f, root, (struct key){id, INODE, 0}, v);
    if (e) return e;
    if (!v->block) return MN_NOENT;
    if (!valid_block(f, v->block) || v->size > MNFS_MAX_FILE || v->flags > 1) return MN_CORRUPT;
    struct inode *n = f->io.allocate(sizeof *n);
    if (!n) return MN_NOMEM;
    if (!f->io.read(f->io.context, v->block, n)) e = MN_IO;
    else if (n->magic != INODE_MAGIC || n->checksum != metadata_crc(n) || n->id != id ||
             !n->parent || !valid_name(n->name)) e = MN_CORRUPT;
    else { if (parent) *parent = n->parent; if (name) memcpy(name, n->name, 256); }
    f->io.release(n); return e;
}
static int inode_write(struct mnfs *f, uint64_t id, uint64_t parent, const char *name, struct value *v) {
    struct inode *n = f->io.allocate(sizeof *n);
    if (!n) return MN_NOMEM;
    memset(n, 0, sizeof *n); n->magic = INODE_MAGIC; n->id = id; n->parent = parent;
    memcpy(n->name, name, strlen(name) + 1);
    int e = save_metadata(f, n, &v->block);
    f->io.release(n);
    return e ? e : set(f, (struct key){id, INODE, 0}, *v);
}
static uint64_t name_hash(const char *s) {
    uint64_t hash = 14695981039346656037ULL;
    while (*s) { hash ^= (uint8_t)*s++; hash *= 1099511628211ULL; }
    return hash;
}
static int directory_find(struct mnfs *f, uint64_t parent, const char *name,
                          uint64_t *id, struct key *slot) {
    struct key k = {parent, DIRECTORY, name_hash(name)}, first = k;
    bool hole = false;
    /* A valid tree cannot contain more live entries than physical blocks. */
    for (uint64_t attempts = 0; attempts < f->io.blocks; ++attempts, ++k.index) {
        struct value v;
        int e = lookup(f, f->root, k, &v);
        if (e == MN_NOENT) { *slot = hole ? first : k; return MN_NOENT; }
        if (e) return e;
        if (!v.block) { if (!hole) { first = k; hole = true; } continue; }
        char stored[256]; uint64_t up;
        struct value ino;
        e = inode_read(f, f->root, v.block, &ino, &up, stored);
        if (e || up != parent) return e ? e : MN_CORRUPT;
        if (!strcmp(stored, name)) { *id = v.block; *slot = k; return MN_OK; }
    }
    return MN_CORRUPT;
}
static int path(struct mnfs *f, const char *p, bool parent_only, uint64_t *result, char *last) {
    size_t total = 0;
    while (total <= MNFS_PATH && p[total]) ++total;
    if (!total || total > MNFS_PATH) return MN_INVALID;
    uint64_t id = 1;
    for (;;) {
        while (*p == '/') ++p;
        if (!*p) { if (parent_only) return MN_INVALID; *result = id; return MN_OK; }
        char name[256]; size_t count = 0;
        while (*p && *p != '/') { if (count == MNFS_NAME) return MN_INVALID; name[count++] = *p++; }
        name[count] = 0;
        const char *next = p; while (*next == '/') ++next;
        struct value v; uint64_t up;
        int e = inode_read(f, f->root, id, &v, &up, NULL);
        if (e) return e;
        if (!v.flags) return MN_NOTDIR;
        if (parent_only && !*next) {
            if (!valid_name(name)) return MN_INVALID;
            memcpy(last, name, count + 1); *result = id; return MN_OK;
        }
        if (!strcmp(name, ".")) continue;
        if (!strcmp(name, "..")) { id = up; continue; }
        if (!valid_name(name)) return MN_INVALID;
        struct key slot;
        e = directory_find(f, id, name, &id, &slot);
        if (e) return e;
    }
}
static int next_entry(struct mnfs *f, uint64_t root, struct key after, bool inclusive,
                      unsigned expected, struct entry *result, bool *found) {
    if (!root) return MN_OK;
    struct node *n;
    int e = load_node(f, root, &n);
    if (e) return e;
    if (n->level >= expected) { e = MN_CORRUPT; goto done; }
    unsigned at = position(n, after);
    if (!inclusive && at < n->count && !compare(n->entries[at].key, after)) ++at;
    if (n->level) {
        e = next_entry(f, n->children[at], after, inclusive, n->level, result, found);
        if (e || *found) goto done;
    }
    if (at < n->count) { *result = n->entries[at]; *found = true; }
done:
    f->io.release(n); return e;
}
static int walk_mark(struct mnfs *f, uint64_t root, uint64_t block, unsigned expected,
                     const struct key *lower, const struct key *upper) {
    if (!block) return MN_OK;
    struct node *n;
    int e = load_node(f, block, &n);
    if (e) return e;
    if (n->level >= expected || (expected != MAX_LEVEL + 1 && n->level + 1 != expected) ||
        (lower && compare(n->entries[0].key, *lower) <= 0) ||
        (upper && compare(n->entries[n->count - 1].key, *upper) >= 0)) { e = MN_CORRUPT; goto done; }
    /* Shared nodes must be visited once per root: an inode deleted in the current
       root can still own these data blocks in the previous root or snapshot. */
    if ((f->seen[block / 8] >> (block % 8)) & 1) { e = MN_CORRUPT; goto done; }
    f->seen[block / 8] |= 1u << (block % 8);
    mark(f, block);
    for (unsigned i = 0; i < n->count; ++i) {
        struct entry entry = n->entries[i];
        if (!entry.value.block || entry.key.type == DIRECTORY) continue;
        struct value ino;
        e = inode_read(f, root, entry.key.owner, &ino, NULL, NULL);
        if (e == MN_NOENT && entry.key.type == DATA) { e = MN_OK; continue; }
        if (e) goto done;
        if (entry.key.type == DATA) {
            if (ino.flags || entry.key.index >= (MNFS_MAX_FILE / MNFS_BLOCK + 1)) { e = MN_CORRUPT; goto done; }
            if (entry.key.index >= (ino.size + MNFS_BLOCK - 1) / MNFS_BLOCK) continue;
        }
        if (!valid_block(f, entry.value.block)) { e = MN_CORRUPT; goto done; }
        mark(f, entry.value.block);
    }
    if (n->level) for (unsigned i = 0; i <= n->count; ++i) {
        e = walk_mark(f, root, n->children[i], n->level,
                      i ? &n->entries[i - 1].key : lower,
                      i < n->count ? &n->entries[i].key : upper);
        if (e) goto done;
    }
done:
    f->io.release(n); return e;
}
int mnfs_collect(struct mnfs *f) {
    memset(f->used, 0, (size_t)((f->io.blocks + 7) / 8));
    f->free_blocks = f->io.blocks; mark(f, 0); mark(f, 1);
    uint64_t roots[] = {f->root, f->snapshot, f->previous, f->previous_snapshot};
    for (unsigned i = 0; i < 4; ++i) {
        memset(f->seen, 0, (size_t)((f->io.blocks + 7) / 8));
        int e = walk_mark(f, roots[i], roots[i], MAX_LEVEL + 1, NULL, NULL);
        if (e) { f->poisoned = true; f->collect = true; return e; }
    }
    f->cursor = 2; f->collect = false; return MN_OK;
}
static int begin(struct mnfs *f, struct transaction *t) {
    if (!f->writable) return MN_RO;
    if (f->poisoned) return MN_IO;
    if (f->collect || f->free_blocks < f->io.blocks / 4) {
        int e = mnfs_collect(f); if (e) return e;
    }
    *t = (struct transaction){f->root, f->next_inode, f->snapshot}; return MN_OK;
}
static int finish(struct mnfs *f, struct transaction t, int e) {
    if (!e && f->generation == UINT64_MAX) e = MN_NOSPC;
    if (!e) {
        struct checkpoint *c = f->io.allocate(sizeof *c);
        if (!c) e = MN_NOMEM;
        else {
            memset(c, 0, sizeof *c); memcpy(c->signature, "MNFSv1\0", 8);
            c->magic = 0x53464e4d; c->version = 1; c->generation = f->generation + 1;
            c->blocks = f->io.blocks; c->root = f->root; c->next_inode = f->next_inode; c->snapshot = f->snapshot;
            c->checksum = metadata_crc(c);
            if (!f->io.flush(f->io.context)) { f->poisoned = true; e = MN_IO; }
            if (!e) e = write_block(f, f->slot ^ 1, c);
            if (!e && !f->io.flush(f->io.context)) { f->poisoned = true; e = MN_IO; }
            f->io.release(c);
        }
    }
    if (e) {
        f->root = t.root; f->next_inode = t.next_inode; f->snapshot = t.snapshot; f->collect = true;
    } else {
        f->previous = t.root; f->previous_snapshot = t.snapshot; f->slot ^= 1; ++f->generation;
    }
    return e;
}
static int init(struct mnfs *f, const struct mnfs_io *io, bool writable) {
    memset(f, 0, sizeof *f);
    if (!io || !io->read || !io->allocate || !io->release || io->blocks < 32 ||
        io->blocks > UINT64_MAX - 7 || (io->blocks + 7) / 8 > SIZE_MAX ||
        (writable && (!io->write || !io->flush))) return MN_INVALID;
    f->io = *io; f->writable = writable;
    f->used = io->allocate((size_t)((io->blocks + 7) / 8));
    if (!f->used) return MN_NOMEM;
    f->seen = io->allocate((size_t)((io->blocks + 7) / 8));
    if (!f->seen) { io->release(f->used); f->used = NULL; return MN_NOMEM; }
    f->cursor = 2; return mnfs_collect(f);
}
void mnfs_close(struct mnfs *f) {
    if (f->used) f->io.release(f->used);
    if (f->seen) f->io.release(f->seen);
    memset(f, 0, sizeof *f);
}
int mnfs_format(struct mnfs *f, const struct mnfs_io *io) {
    int e = init(f, io, true); if (e) return e;
    /* Caller must supply a new disposable device; the kernel never formats disks. */
    uint8_t *zero = f->io.allocate(MNFS_BLOCK);
    if (!zero) { mnfs_close(f); return MN_NOMEM; }
    memset(zero, 0, MNFS_BLOCK);
    e = write_block(f, 0, zero); if (!e) e = write_block(f, 1, zero);
    f->io.release(zero);
    struct value root = {.flags = 1}; f->next_inode = 2;
    if (!e) e = inode_write(f, 1, 1, "root", &root);
    if (!e) e = finish(f, (struct transaction){0, 2, 0}, MN_OK);
    if (e) mnfs_close(f);
    return e;
}
static bool checkpoint_valid(struct mnfs *f, struct checkpoint *c) {
    return c->magic == 0x53464e4d && !memcmp(c->signature, "MNFSv1\0", 8) &&
           c->version == 1 && c->generation && c->blocks == f->io.blocks &&
           c->checksum == metadata_crc(c) && valid_block(f, c->root) && c->next_inode >= 2 &&
           (!c->snapshot || valid_block(f, c->snapshot));
}
int mnfs_mount(struct mnfs *f, const struct mnfs_io *io, bool writable) {
    int e = init(f, io, writable); if (e) return e;
    struct checkpoint *c[2] = {io->allocate(MNFS_BLOCK), io->allocate(MNFS_BLOCK)};
    bool valid[2] = {false, false};
    if (!c[0] || !c[1]) { e = MN_NOMEM; goto done; }
    for (unsigned i = 0; i < 2; ++i)
        valid[i] = io->read(io->context, i, c[i]) && checkpoint_valid(f, c[i]);
    unsigned first = valid[1] && (!valid[0] || c[1]->generation > c[0]->generation) ? 1 : 0;
    e = MN_CORRUPT;
    for (unsigned trial = 0; trial < 2; ++trial) {
        unsigned at = first ^ trial;
        if (!valid[at]) continue;
        f->root = c[at]->root; f->snapshot = c[at]->snapshot; f->next_inode = c[at]->next_inode;
        f->generation = c[at]->generation; f->slot = at;
        f->poisoned = false;
        f->previous = f->previous_snapshot = 0;
        e = mnfs_collect(f);
        struct value root; uint64_t parent;
        if (!e) e = inode_read(f, f->root, 1, &root, &parent, NULL);
        if (!e && (!root.flags || parent != 1)) e = MN_CORRUPT;
        if (!e) {
            if (valid[at ^ 1] && c[at ^ 1]->generation < c[at]->generation) {
                uint64_t old = c[at ^ 1]->root, snap = c[at ^ 1]->snapshot;
                memset(f->seen, 0, (size_t)((f->io.blocks + 7) / 8));
                int old_error = walk_mark(f, old, old, MAX_LEVEL + 1, NULL, NULL);
                memset(f->seen, 0, (size_t)((f->io.blocks + 7) / 8));
                if (!old_error && !walk_mark(f, snap, snap, MAX_LEVEL + 1, NULL, NULL)) {
                    f->previous = old; f->previous_snapshot = snap;
                }
            }
            break;
        }
    }
done:
    if (c[0]) io->release(c[0]); if (c[1]) io->release(c[1]);
    if (e) mnfs_close(f);
    return e;
}
int mnfs_stat(struct mnfs *f, const char *name, struct mnfs_stat *st) {
    uint64_t id;
    int e = path(f, name, false, &id, NULL); if (e) return e;
    struct value v; e = inode_read(f, f->root, id, &v, NULL, NULL);
    if (!e) *st = (struct mnfs_stat){id, v.size, v.flags != 0};
    return e;
}
int mnfs_create(struct mnfs *f, const char *name, bool directory) {
    char leaf[256]; uint64_t parent, exists; struct key slot;
    int e = path(f, name, true, &parent, leaf); if (e) return e;
    e = directory_find(f, parent, leaf, &exists, &slot);
    if (e != MN_NOENT) return e ? e : MN_EXISTS;
    struct transaction t; e = begin(f, &t); if (e) return e;
    if (f->next_inode == UINT64_MAX) return finish(f, t, MN_NOSPC);
    uint64_t id = f->next_inode++;
    struct value v = {.flags = directory};
    e = inode_write(f, id, parent, leaf, &v);
    if (!e) e = set(f, slot, (struct value){.block = id});
    return finish(f, t, e);
}
int mnfs_list(struct mnfs *f, const char *name, mnfs_visit visit, void *context) {
    struct mnfs_stat st; int e = mnfs_stat(f, name, &st);
    if (e) return e; if (!st.directory) return MN_NOTDIR;
    struct key after = {st.inode, DIRECTORY, 0}; bool inclusive = true;
    for (;;) {
        struct entry entry; bool found = false;
        e = next_entry(f, f->root, after, inclusive, MAX_LEVEL + 1, &entry, &found);
        if (e || !found || entry.key.owner != st.inode || entry.key.type != DIRECTORY) return e;
        after = entry.key; inclusive = false;
        if (!entry.value.block) continue;
        struct value v; char leaf[256]; uint64_t parent;
        e = inode_read(f, f->root, entry.value.block, &v, &parent, leaf);
        if (e || parent != st.inode) return e ? e : MN_CORRUPT;
        struct mnfs_stat child = {entry.value.block, v.size, v.flags != 0};
        if (!visit(context, leaf, &child)) return MN_OK;
    }
}
static int data_read(struct mnfs *f, uint64_t id, uint64_t index, uint8_t *buffer) {
    struct value v;
    int e = lookup(f, f->root, (struct key){id, DATA, index}, &v);
    if (e == MN_NOENT || (!e && !v.block)) { memset(buffer, 0, MNFS_BLOCK); return MN_OK; }
    if (e) return e;
    if (!valid_block(f, v.block)) return MN_CORRUPT;
    if (!f->io.read(f->io.context, v.block, buffer)) return MN_IO;
    return crc(buffer, MNFS_BLOCK) == v.checksum ? MN_OK : MN_CORRUPT;
}
static int data_write(struct mnfs *f, uint64_t id, uint64_t index, const uint8_t *buffer) {
    struct value v = {.checksum = crc(buffer, MNFS_BLOCK)};
    int e = allocate_block(f, &v.block);
    if (!e) e = write_block(f, v.block, buffer);
    return e ? e : set(f, (struct key){id, DATA, index}, v);
}
int mnfs_read(struct mnfs *f, const char *name, uint64_t offset, void *output, size_t count, size_t *done) {
    *done = 0;
    struct mnfs_stat st; int e = mnfs_stat(f, name, &st);
    if (e) return e; if (st.directory) return MN_ISDIR;
    if (offset >= st.size || !count) return MN_OK;
    if (count > st.size - offset) count = (size_t)(st.size - offset);
    uint8_t *buffer = f->io.allocate(MNFS_BLOCK);
    if (!buffer) return MN_NOMEM;
    while (*done < count) {
        size_t in = offset % MNFS_BLOCK, n = MNFS_BLOCK - in;
        if (n > count - *done) n = count - *done;
        e = data_read(f, st.inode, offset / MNFS_BLOCK, buffer); if (e) break;
        memcpy((uint8_t *)output + *done, buffer + in, n); *done += n; offset += n;
    }
    f->io.release(buffer); return e;
}
int mnfs_write(struct mnfs *f, const char *name, uint64_t offset, const void *input, size_t count) {
    if (offset > MNFS_MAX_FILE || count > (uint64_t)MNFS_MAX_FILE - offset) return MN_INVALID;
    struct mnfs_stat st; int e = mnfs_stat(f, name, &st);
    if (e) return e; if (st.directory) return MN_ISDIR;
    if (!count) return MN_OK;
    struct transaction t; e = begin(f, &t); if (e) return e;
    uint8_t *buffer = f->io.allocate(MNFS_BLOCK);
    if (!buffer) return finish(f, t, MN_NOMEM);
    uint64_t end = offset + count;
    while (count) {
        size_t in = offset % MNFS_BLOCK, n = MNFS_BLOCK - in;
        if (n > count) n = count;
        e = data_read(f, st.inode, offset / MNFS_BLOCK, buffer); if (e) break;
        memcpy(buffer + in, input, n);
        e = data_write(f, st.inode, offset / MNFS_BLOCK, buffer); if (e) break;
        input = (const uint8_t *)input + n; offset += n; count -= n;
    }
    if (!e) {
        struct value v; e = lookup(f, f->root, (struct key){st.inode, INODE, 0}, &v);
        if (!e) { if (v.size < end) v.size = end; e = set(f, (struct key){st.inode, INODE, 0}, v); }
    }
    f->io.release(buffer); return finish(f, t, e);
}
int mnfs_truncate(struct mnfs *f, const char *name, uint64_t size) {
    if (size > MNFS_MAX_FILE) return MN_INVALID;
    struct mnfs_stat st; int e = mnfs_stat(f, name, &st);
    if (e) return e; if (st.directory) return MN_ISDIR;
    struct transaction t; e = begin(f, &t); if (e) return e;
    if (size < st.size) {
        struct key after = {st.inode, DATA, (size + MNFS_BLOCK - 1) / MNFS_BLOCK};
        bool inclusive = true;
        for (;;) {
            struct entry entry; bool found = false;
            e = next_entry(f, f->root, after, inclusive, MAX_LEVEL + 1, &entry, &found);
            if (e || !found || entry.key.owner != st.inode || entry.key.type != DATA) break;
            after = entry.key; inclusive = false;
            if (entry.value.block) { e = set(f, entry.key, (struct value){0}); if (e) break; }
        }
        if (!e && size % MNFS_BLOCK) {
            uint8_t *buffer = f->io.allocate(MNFS_BLOCK);
            if (!buffer) e = MN_NOMEM;
            else {
                e = data_read(f, st.inode, size / MNFS_BLOCK, buffer);
                if (!e) {
                    memset(buffer + size % MNFS_BLOCK, 0, MNFS_BLOCK - size % MNFS_BLOCK);
                    e = data_write(f, st.inode, size / MNFS_BLOCK, buffer);
                }
                f->io.release(buffer);
            }
        }
    }
    if (!e) {
        struct value v; e = lookup(f, f->root, (struct key){st.inode, INODE, 0}, &v);
        if (!e) { v.size = size; e = set(f, (struct key){st.inode, INODE, 0}, v); }
    }
    return finish(f, t, e);
}
static bool nonempty(void *context, const char *name, const struct mnfs_stat *st) {
    (void)name; (void)st; *(bool *)context = true; return false;
}
int mnfs_remove(struct mnfs *f, const char *name) {
    char leaf[256]; uint64_t parent, id; struct key slot;
    int e = path(f, name, true, &parent, leaf); if (e) return e;
    e = directory_find(f, parent, leaf, &id, &slot); if (e) return e;
    struct value v; e = inode_read(f, f->root, id, &v, NULL, NULL); if (e) return e;
    if (v.flags) {
        bool found = false; e = mnfs_list(f, name, nonempty, &found);
        if (e || found) return e ? e : MN_NOTEMPTY;
    }
    struct transaction t; e = begin(f, &t); if (e) return e;
    e = set(f, slot, (struct value){0});
    if (!e) e = set(f, (struct key){id, INODE, 0}, (struct value){0});
    /* Orphan data records are skipped by collection. IDs are never reused. */
    return finish(f, t, e);
}
int mnfs_rename(struct mnfs *f, const char *from, const char *to) {
    char old[256], name[256]; uint64_t parent, dest, id, exists; struct key source, target;
    int e = path(f, from, true, &parent, old); if (e) return e;
    e = directory_find(f, parent, old, &id, &source); if (e) return e;
    e = path(f, to, true, &dest, name); if (e) return e;
    e = directory_find(f, dest, name, &exists, &target);
    if (!e) return exists == id ? MN_OK : MN_EXISTS;
    if (e != MN_NOENT) return e;
    struct value v; e = inode_read(f, f->root, id, &v, NULL, NULL); if (e) return e;
    uint64_t ancestor = dest;
    for (uint64_t n = 0; ancestor != 1 && n < f->io.blocks; ++n) {
        if (ancestor == id) return MN_INVALID;
        struct value up; uint64_t next;
        e = inode_read(f, f->root, ancestor, &up, &next, NULL); if (e) return e;
        if (next == ancestor) return MN_CORRUPT;
        ancestor = next;
    }
    if (ancestor != 1 || id == 1) return MN_INVALID;
    struct transaction t; e = begin(f, &t); if (e) return e;
    e = inode_write(f, id, dest, name, &v);
    if (!e) e = set(f, source, (struct value){0});
    if (!e) e = set(f, target, (struct value){.block = id});
    return finish(f, t, e);
}
int mnfs_snapshot(struct mnfs *f, unsigned action) {
    if (action > 2) return MN_INVALID;
    if (action == 1 && !f->snapshot) return MN_NOENT;
    struct transaction t; int e = begin(f, &t); if (e) return e;
    if (action == 0) f->snapshot = f->root;
    if (action == 1) f->root = f->snapshot;
    if (action == 2) f->snapshot = 0;
    return finish(f, t, MN_OK);
}
const char *mnfs_error(int e) {
    static const char *errors[] = {"OK", "I/O failed; remount required", "checksum or metadata invalid",
        "out of memory", "not enough free transaction space", "not found", "already exists",
        "not a directory", "is a directory", "directory not empty", "invalid path or range", "read-only"};
    return e >= 0 && (size_t)e < sizeof errors / sizeof *errors ? errors[e] : "unknown error";
}
