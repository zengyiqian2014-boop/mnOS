/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_BOOT_LAYOUT_H
#define MNOS_BOOT_LAYOUT_H
/* Loader validation and initial 4 KiB W^X mappings must cover the SAME range.
 * The bundled Debian filesystem requires space beyond the old 128 MiB end. */
#define MNOS_BOOT_LIMIT (1024ULL * 1024 * 1024)
#define MNOS_ENTROPY_TAG 0x4d4e5201u
#define MNOS_ENTROPY_TAG_SIZE 48u
#endif
