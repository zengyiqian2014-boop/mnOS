/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_ICONS_H
#define MNOS_ICONS_H
#include <stdint.h>
/* Original vector artwork rasterized at each target size, never enlarged. */
struct icon_asset { uint32_t offset; uint16_t size, app; };
extern const struct icon_asset icon_assets[28];
extern const uint32_t icon_pixels[];
#endif
