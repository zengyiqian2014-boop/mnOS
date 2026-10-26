/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_BOOT_LAYOUT_H
#define MNOS_BOOT_LAYOUT_H
/* Loader validation and initial 4 KiB W^X mappings must cover the SAME range.
 * The bundled guest pushes the UEFI-linked image beyond the old 64 MiB end. */
#define MNOS_BOOT_LIMIT (128ULL * 1024 * 1024)
#endif
