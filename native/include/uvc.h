/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_UVC_H
#define MNOS_UVC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UVC_WIDTH 640u
#define UVC_HEIGHT 480u
#define UVC_FRAME_BYTES (UVC_WIDTH * UVC_HEIGHT * 2u)
struct uvc_mode {
    uint8_t configuration, interface, format, frame, endpoint, alternate, interval;
    uint16_t width, height, packet, version;
    uint32_t period, payload;
    bool iso;
};
bool uvc_describe(const uint8_t *, size_t, struct uvc_mode *);
struct uvc_frames {
    uint8_t *writing, *ready;
    size_t capacity, expected, used;
    uint64_t sequence, dropped;
    unsigned fid;
    bool started, invalid;
};
void uvc_frames_init(struct uvc_frames *, uint8_t *, uint8_t *, size_t, size_t);
bool uvc_payload(struct uvc_frames *, const uint8_t *, size_t);
uint32_t uvc_yuy2_pixel(const uint8_t *, unsigned);
#endif
