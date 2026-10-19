/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "font.h"
static const struct glyph *lookup(uint32_t code, unsigned size) {
    unsigned lo = 0, hi = font_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        const struct glyph *g = &font_glyphs[mid];
        if (g->size < size || (g->size == size && g->code < code)) lo = mid + 1;
        else hi = mid;
    }
    return lo < font_count && font_glyphs[lo].size == size && font_glyphs[lo].code == code
               ? &font_glyphs[lo] : NULL;
}
const struct glyph *font_find(uint32_t code, unsigned size) {
    const struct glyph *g = lookup(code, size);
    if (!g && size != FONT_MONO18) g = lookup(code, 18);
    if (!g) g = lookup(0xfffd, size);
    return g ? g : lookup(0xfffd, 18);
}
int font_scale(int value, const struct glyph *g, unsigned size) {
    return value * (int)(size & 255) / (int)(g->size & 255);
}
unsigned font_advance(const struct glyph *g, unsigned size) {
    return g ? (unsigned)font_scale(g->advance, g, size) : 0;
}
bool font_unpack(const struct glyph *g, unsigned char *out, size_t capacity) {
    if (!g || g->width > 64 || g->height > 64 ||
        g->offset > font_pixels_size || g->bytes > font_pixels_size - g->offset) return false;
    size_t total = (size_t)g->width * g->height, p = 0;
    if (total > capacity) return false;
    for (unsigned i = 0; i < g->bytes; i++) {
        unsigned b = font_pixels[g->offset + i], n = (b & 15) + 1;
        if (n > total - p) return false;
        while (n--) out[p++] = (b >> 4) * 17;
    }
    return p == total;
}
uint32_t font_decode(const char *s, size_t length, size_t *position) {
    if (*position >= length) return 0;
    size_t start = *position;
    unsigned char lead = (unsigned char)s[(*position)++];
    if (lead < 128) return lead;
    unsigned count;
    uint32_t code, minimum;
    if (lead >= 0xc2 && lead <= 0xdf) { count = 1; code = lead & 31; minimum = 0x80; }
    else if (lead >= 0xe0 && lead <= 0xef) { count = 2; code = lead & 15; minimum = 0x800; }
    else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; code = lead & 7; minimum = 0x10000; }
    else return 0xfffd;
    if (length - *position < count) return 0xfffd;
    for (unsigned i = 0; i < count; i++) {
        unsigned char c = (unsigned char)s[*position + i];
        if ((c & 0xc0) != 0x80) return 0xfffd;
        code = (code << 6) | (c & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0xfffd;
    *position = start + count + 1;
    return code;
}
size_t text_previous(const char *s, size_t position) {
    size_t before = 0, p = 0;
    while (p < position) { before = p; font_decode(s, position, &p); }
    return before;
}
int text_width(const char *s, unsigned size) {
    size_t length = 0, p = 0;
    while (s[length]) length++;
    int x = 0;
    while (p < length) {
        const struct glyph *g = font_find(font_decode(s, length, &p), size);
        if (g) x += font_advance(g, size);
    }
    return (x + 32) / 64;
}
bool text_next(struct text_layout *l, struct text_cell *c) {
    if (l->done) return false;
    c->byte = l->position;
    c->code = font_decode(l->s, l->length, &l->position);
    c->next = l->position;
    const struct glyph *g = font_find(c->code, l->size);
    int advance = c->code && c->code != '\n' && c->code != '\r' && g ? (int)font_advance(g, l->size) : 0;
    if (c->code == '\t') {
        const struct glyph *space = font_find(' ', l->size);
        int stop = space ? (int)font_advance(space, l->size) * 4 : 32 * 64;
        advance = stop - l->x % stop;
    }
    if (l->x && advance && l->x + advance > l->width * 64) {
        l->row++;
        l->x = 0;
        if (c->code == '\t') advance = (int)font_advance(font_find(' ', l->size), l->size) * 4;
    }
    c->x = l->x;
    c->row = l->row;
    c->advance = advance;
    if (c->byte == l->length) l->done = true;
    else if (c->code == '\n') { l->row++; l->x = 0; }
    else l->x += advance;
    return true;
}
struct text_cell text_caret(const char *s, size_t length, size_t byte, int width) {
    struct text_layout l = {.s=s, .length=length, .width=width, .size=18};
    struct text_cell c = {0};
    while (text_next(&l, &c)) if (c.byte >= byte) break;
    return c;
}
size_t text_hit(const char *s, size_t length, size_t row, int x, int width) {
    struct text_layout l = {.s=s, .length=length, .width=width, .size=18};
    struct text_cell c;
    size_t best = length;
    int distance = 0x7fffffff;
    while (text_next(&l, &c)) {
        if (c.row > row) break;
        if (c.row != row) continue;
        int d = c.x > x ? c.x - x : x - c.x;
        if (d < distance) { distance = d; best = c.byte; }
    }
    return best;
}
