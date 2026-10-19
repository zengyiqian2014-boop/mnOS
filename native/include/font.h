/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_FONT_H
#define MNOS_FONT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FONT_MONO18 274u
/* Advance is in 1/64 pixel units, independent of the ink's bounding box. */
struct glyph {
    uint32_t code, offset;
    uint16_t width, height, advance, size;
    int16_t left, top;
};
extern const struct glyph font_glyphs[];
extern const unsigned font_count;
extern const unsigned char font_pixels[];
const struct glyph *font_find(uint32_t code, unsigned size);
uint32_t font_decode(const char *s, size_t length, size_t *position);
size_t text_previous(const char *s, size_t position);
int text_width(const char *s, unsigned size);
struct text_layout {
    const char *s;
    size_t length, position, row;
    int x, width;
    unsigned size;
    bool done;
};
struct text_cell {
    size_t byte, next, row;
    uint32_t code;
    int x, advance;
};
/* Drawing, wrapping, cursor navigation and mouse selection share this iterator. */
bool text_next(struct text_layout *layout, struct text_cell *cell);
struct text_cell text_caret(const char *s, size_t length, size_t byte, int width);
size_t text_hit(const char *s, size_t length, size_t row, int x, int width);
#endif
