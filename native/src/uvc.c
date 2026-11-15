/* SPDX-License-Identifier: GPL-3.0-or-later
 * USB Video Class 1.x descriptors and uncompressed YUY2 payloads. No OS ABI. */
#include "uvc.h"
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)le16(p) | (uint32_t)le16(p + 2) << 16;
}
bool uvc_describe(const uint8_t *p, size_t n, struct uvc_mode *out) {
    static const uint8_t yuy2[16] = {'Y',  'U', 'Y', '2',  0, 0,    0x10, 0,
                                     0x80, 0,   0,   0xaa, 0, 0x38, 0x9b, 0x71};
    struct uvc_mode best = {0};
    if (n < 9 || p[0] < 9 || p[1] != 2 || le16(p + 2) > n || le16(p + 2) < 9)
        return false;
    n = le16(p + 2);
    uint8_t iface = 255, alt = 0, format = 0;
    bool video = false, raw = false, control = false;
    uint16_t version = 0;
    for (size_t at = 0; at < n;) {
        if (n - at < 2 || p[at] < 2 || p[at] > n - at)
            return false;
        const uint8_t *d = p + at;
        unsigned len = d[0];
        if (d[1] == 4) {
            if (len < 9)
                return false;
            iface = d[2];
            alt = d[3];
            video = d[5] == 14 && d[6] == 2;
            control = d[5] == 14 && d[6] == 1;
            raw = false;
            format = 0;
        } else if (control && d[1] == 0x24 && len >= 5 && d[2] == 1) {
            version = le16(d + 3);
        } else if (video && !alt && d[1] == 0x24) {
            if (len < 3)
                return false;
            if (d[2] == 4) {
                if (len < 27)
                    return false;
                raw = !memcmp(d + 5, yuy2, 16) && d[21] == 16;
                format = d[3];
            } else if (d[2] == 6 || d[2] == 0x10)
                raw = false;
            else if (raw && d[2] == 5) {
                if (len < 26)
                    return false;
                unsigned w = le16(d + 5), h = le16(d + 7), count = d[25];
                uint32_t period = le32(d + 21);
                if ((count && len < 26 + 4 * count) || (!count && len < 38))
                    return false;
                if (!w || !h || (w & 1) || w > UVC_WIDTH || h > UVC_HEIGHT || !format || !d[3]) {
                    at += len;
                    continue;
                }
                if (count) {
                    uint32_t chosen = 0;
                    for (unsigned i = 0; i < count; ++i) {
                        uint32_t value = le32(d + 26 + 4 * i);
                        if (value >= 333333 && (!chosen || value < chosen))
                            chosen = value;
                    }
                    if (chosen)
                        period = chosen;
                } else {
                    uint32_t low = le32(d + 26), high = le32(d + 30), step = le32(d + 34);
                    if (!low || high < low || !step)
                        return false;
                    uint64_t target = low > 333333 ? low : 333333;
                    target = low + ((target - low + step - 1) / step) * step;
                    if (target > high)
                        target = high;
                    period = (uint32_t)target;
                }
                if (!period || period > 100000000) {
                    at += len;
                    continue;
                }
                if (!best.width || w * h > best.width * best.height)
                    best = (struct uvc_mode){.configuration = p[5],
                                             .interface = iface,
                                             .format = format,
                                             .frame = d[3],
                                             .width = w,
                                             .height = h,
                                             .period = period,
                                             .version = version};
            }
        }
        at += len;
    }
    if (!best.width || !best.configuration ||
        (best.version != 0x100 && best.version != 0x110 && best.version != 0x150))
        return false;
    /* Choose the largest advertised HS ISO service interval, or a bulk IN
     * endpoint. Negotiation checks the device's selected payload before DMA. */
    video = false;
    for (size_t at = 0; at < n; at += p[at]) {
        const uint8_t *d = p + at;
        if (d[1] == 4) {
            iface = d[2];
            alt = d[3];
            video = d[5] == 14 && d[6] == 2 && iface == best.interface;
        }
        if (video && d[1] == 5) {
            if (d[0] < 7)
                return false;
            unsigned kind = d[3] & 3, packet = le16(d + 4), size = packet & 2047,
                     mult = (packet >> 11) & 3;
            if (!(d[2] & 0x80) || !(d[2] & 15) || (d[2] & 0x70) || !size || size > 1024 ||
                mult == 3)
                continue;
            uint32_t payload = size * (mult + 1);
            if (kind != 1 && kind != 2)
                continue;
            if (kind == 1 && (!d[6] || d[6] > 16))
                continue;
            if (!best.endpoint || kind == 2 || (!best.iso ? false : payload > best.payload)) {
                best.endpoint = d[2];
                best.alternate = alt;
                best.packet = packet;
                best.interval = d[6];
                best.iso = kind == 1;
                best.payload = kind == 1 ? payload : 4096;
            }
        }
    }
    if (!best.endpoint)
        return false;
    *out = best;
    return true;
}
void uvc_frames_init(struct uvc_frames *f, uint8_t *a, uint8_t *b, size_t capacity,
                     size_t expected) {
    memset(f, 0, sizeof *f);
    f->writing = a;
    f->ready = b;
    f->capacity = capacity;
    f->expected = expected;
}
bool uvc_payload(struct uvc_frames *f, const uint8_t *p, size_t n) {
    if (!f || !f->writing || !f->ready || !f->expected || f->expected > f->capacity)
        return false;
    if (n < 2 || p[0] < 2 || p[0] > n ||
        p[0] < (2u + ((p[1] & 4) ? 4u : 0u) + ((p[1] & 8) ? 6u : 0u))) {
        f->invalid = true;
        ++f->dropped;
        return false;
    }
    unsigned fid = p[1] & 1;
    if (!f->started || fid != f->fid) {
        if (f->started && f->used)
            ++f->dropped;
        f->started = true;
        f->fid = fid;
        f->used = 0;
        f->invalid = false;
    }
    if (p[1] & 0x40)
        f->invalid = true;
    size_t bytes = n - p[0];
    if (bytes > f->expected - f->used)
        f->invalid = true;
    if (!f->invalid) {
        memcpy(f->writing + f->used, p + p[0], bytes);
        f->used += bytes;
    }
    if (!(p[1] & 2))
        return false;
    bool complete = !f->invalid && f->used == f->expected;
    if (complete) {
        uint8_t *old = f->ready;
        f->ready = f->writing;
        f->writing = old;
        ++f->sequence;
    } else
        ++f->dropped;
    f->used = 0;
    f->invalid = true; /* A second EOF with the same FID is never a new frame. */
    return complete;
}
static unsigned clamp(int n) {
    return n < 0 ? 0 : n > 255 ? 255 : (unsigned)n;
}
uint32_t uvc_yuy2_pixel(const uint8_t *p, unsigned x) {
    p += (x / 2) * 4;
    int y = (int)p[(x & 1) * 2] - 16, u = (int)p[1] - 128, v = (int)p[3] - 128;
    return clamp((298 * y + 409 * v + 128) / 256) << 16 |
           clamp((298 * y - 100 * u - 208 * v + 128) / 256) << 8 |
           clamp((298 * y + 516 * u + 128) / 256);
}
