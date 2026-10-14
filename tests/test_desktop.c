/* Check framebuffer bounds and color formats without touching display devices.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define main desktop_main
#include "../desktop/desktop.c"
#undef main
#include <assert.h>

int main(void) {
    var = (struct fb_var_screeninfo){.xres = 640, .yres = 480,
        .xres_virtual = 648, .yres_virtual = 482, .xoffset = 8, .yoffset = 2,
        .bits_per_pixel = 32, .red = {16, 8, 0}, .green = {8, 8, 0}, .blue = {0, 8, 0}};
    fix = (struct fb_fix_screeninfo){.type = FB_TYPE_PACKED_PIXELS,
        .visual = FB_VISUAL_TRUECOLOR, .line_length = 648 * 4, .smem_len = 648 * 4 * 482};
    assert(framebuffer_ok());
    fix.smem_len--;
    assert(!framebuffer_ok());
    fix.smem_len++;
    var.xoffset++;
    assert(!framebuffer_ok());
    var.xoffset--;
    var.red.length = 32;
    assert(!framebuffer_ok());
    var.red.length = 8;
    var.red.msb_right = 1;
    assert(!framebuffer_ok());
    var.red.msb_right = 0;
    var.red.offset = UINT32_MAX;
    assert(!framebuffer_ok());
    var.red.offset = 16;

    width = 640; height = 480;
    fb = malloc(fix.smem_len);
    canvas = calloc((size_t)width * height, sizeof(*canvas));
    assert(fb && canvas);
    memset(fb, 0xab, fix.smem_len);
    box(-5, -5, 10, 10, 0x123456);
    box(638, 478, 100, 100, 0xfedcba);
    present();
    assert(fb[0] == 0xab); /* Virtual-screen offsets are preserved. */
    uint32_t value;
    memcpy(&value, fb + 2 * fix.line_length + 8 * 4, 4);
    assert(value == 0x123456);
    assert(canvas[5] == 0); /* Clipped drawing did not spill into the next column. */
    assert(canvas[479 * 640 + 639] == 0xfedcba);
    free(fb); free(canvas);

    var.bits_per_pixel = 16;
    var.red = (struct fb_bitfield){11, 5, 0};
    var.green = (struct fb_bitfield){5, 6, 0};
    var.blue = (struct fb_bitfield){0, 5, 0};
    assert(framebuffer_ok());
    assert(channel(255, var.red) == 0xf800);
    assert(channel(255, var.green) == 0x07e0);
    assert(channel(255, var.blue) == 0x001f);
    puts("framebuffer bounds, clipping and RGB565/RGB888 formats pass");
    return 0;
}
