# MNFS v1

MNFS is original C code shared by the native kernel and the `mnfs` Linux/host
image utility. It borrows general filesystem techniques—copy-on-write,
checksums and snapshots—without using Apple's APFS disk format or source code.
This is an experimental filesystem, not a replacement for a mature Linux root
filesystem. It supports files larger than FAT32's 4 GiB limit.

## Format

Blocks are 4096 bytes. Integer fields are little-endian; the current code targets
x86_64 and validates compile-time structure sizes. Block numbers and inode IDs
are 64-bit. File size is limited to `INT64_MAX` bytes by the API; available disk
space, metadata overhead and RAM impose much smaller practical bounds. Tests
cover sparse offsets beyond 32 TiB, not an exabyte-sized populated volume.

| Block type | Contents |
| --- | --- |
| 0 and 1: alternating checkpoints | CRC32C, magic/signature, version, generation, volume block count, tree root, next inode, saved snapshot root |
| B-tree node | CRC32C, magic, level, count, sorted entries and child pointers |
| Inode | CRC32C, magic, inode ID, parent inode ID, name (up to 255 bytes) |
| File data | 4096 bytes; its CRC32C is stored in the tree entry |

The tree key is `(owner inode, record type, index)`. Type 1 stores an inode
pointer, file size and directory flag. Type 2 maps a directory/name hash to an
inode ID, using bounded linear probing for collisions. Type 3 maps a file's
logical block index to a data block and checksum. Missing data records read as
zeroes. Names are opaque non-control bytes; there is no Unicode normalization
or case folding. Paths support `/`, `.` and `..`; total path bound is 1024 bytes.

Nodes contain up to 63 committed keys; the extra in-memory entry/child slot is
split scratch space. Maximum tree level is 20. Mount validates checksums,
block ranges, key ordering, parent/child key bounds, level transitions and
repeated child nodes. File data checksums are verified when data is read.
CRC32C detects accidental corruption; it is not cryptographic authentication.

## Transactions and recovery

Each modifying API call allocates new data and tree blocks. It then flushes all
data/metadata, writes the other checkpoint slot, and flushes again. The previous
checkpoint's reachable blocks remain allocated. Mount chooses the newest valid
checkpoint and validates its metadata; a damaged latest tree can fall back to
the older valid checkpoint. It rejects a volume when neither root is usable.
This assumes the storage device honors completed flushes.

Write or flush uncertainty poisons the writable mount. Later writes fail until
remount; the implementation does not continue using an uncertain checkpoint.
Out-of-space failures roll back the in-memory transaction root. A **console
command can contain multiple API transactions**: for example, `write` creates a
missing file, writes it and truncates it separately. Whole console commands are
not advertised as atomic. `rename` is one metadata transaction and refuses to
replace an existing different destination.

One saved snapshot root can be saved, restored or dropped. It is not a named,
unbounded snapshot history. Inode IDs are never reused. A mark-and-sweep pass
protects the current root, previous checkpoint and their saved snapshots before
reusing unreachable blocks. Two allocation/traversal bitmaps use roughly one
byte per 16 KiB of volume capacity (64 MiB per 1 TiB), plus tree working memory.
Native allocation is limited by available RAM. New-name churn leaves tombstone
metadata; metadata compaction is not yet implemented. Collection walks metadata
and is not optimized for very large populated disks.

## Access and drivers

The native kernel creates an 8 MiB temporary `/ram` volume. Its optional `/disk`
volume uses original ATA PIO code on legacy primary/secondary ports, LBA28 or
LBA48, 512-byte sectors and cache flushes. It recognizes a whole-device MNFS
image, not a GPT/MBR partition containing MNFS. The first valid MNFS device is
opened read-only. Login enables permitted home-directory writes. Root may use
`disk lock` to disable volume writes and `disk unlock` to enable them again.
Neither boot nor either command formats a disk.

`mnfs format NEW_IMAGE SIZE_MiB` uses exclusive creation and accepts regular files
only. Existing images are opened without following symlinks; device nodes are
rejected. The host utility and native kernel share the same filesystem code.
Do not open one image for writes from the host and a VM simultaneously.

No on-disk POSIX permissions/ACLs, encryption, compression, hard links, symlinks, multiple
writers, repair tool, Linux mount driver, native AHCI/NVMe or filesystem system
calls are implemented. The native console enforces authenticated user/root
sessions and home-directory permissions at its filesystem entry points. Keep important
files on a backed-up mature filesystem while this implementation is developed.

## Evidence

`tests/mnfs.c` runs with ASan/UBSan and checks >4 GiB writes, >32 TiB sparse
offsets, zero holes, overflow rejection, tree splits, directories, rename,
snapshot/collection interaction, remount and read-only behavior. Fault tests
interrupt each write/flush boundary, including torn writes and partially
reordered volatile caches, and require the old or new committed file. Additional
tests cover ENOSPC, data CRC failure and metadata/checkpoint fallback.
`tests/persistence.py` uses the actual native ATA driver, cuts VM power, reboots,
reads the files/settings again and checks the same image with the host utility.
These tests do not establish production durability or a security certification.
