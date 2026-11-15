/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native polling xHCI host and UVC capture. USB/xHCI wire formats, no Linux ABI.
 * Direct root ports, control, bulk IN and high-speed isochronous IN. */
#include "kernel.h"
#include "usb.h"
#include "usb_platform.h"
#define HC_MAX 4
#define DEV_MAX 32
#define RING_SIZE 256u
#define RX_COUNT 240u
#define TYPE(n) ((uint32_t)(n) << 10)
struct trb {
    uint64_t parameter;
    uint32_t status, control;
};
struct dma {
    uint64_t address;
    void *pointer;
};
struct ring {
    struct dma dma;
    unsigned put, cycle;
};
struct host {
    volatile uint8_t *base;
    unsigned op, db, rt, ports, slots, context_size;
    bool wide, alive;
    struct dma dcbaa, event, erst, scratch;
    struct ring command;
    unsigned event_at, event_cycle;
    uint64_t waiting;
    unsigned command_code, command_slot;
};
struct device {
    struct host *host;
    unsigned slot, port, speed;
    uint16_t vendor, product;
    bool attached, video;
    struct dma input, output, buffer;
    struct ring control;
    uint8_t config[4096];
    size_t config_size;
    struct uvc_mode mode;
    uint64_t waiting, data_wait;
    unsigned code, residual;
    bool data_short;
};
struct stream {
    struct device *device;
    struct ring ring;
    struct dma buffers[RX_COUNT];
    unsigned dci, bytes;
    bool active, configured, kick;
    struct {
        unsigned buffer;
        bool pending;
    } td[RING_SIZE];
    struct uvc_frames frames;
    uint8_t images[2][UVC_FRAME_BYTES];
};
static struct host hosts[HC_MAX];
static struct device devices[DEV_MAX];
static unsigned host_count, device_count;
static struct stream stream;
static const char *capture_status = "Camera off";
static uint32_t get(struct host *h, unsigned r) {
    return *(volatile uint32_t *)(h->base + r);
}
static void put(struct host *h, unsigned r, uint32_t v) {
    *(volatile uint32_t *)(h->base + r) = v;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
static void put64(struct host *h, unsigned r, uint64_t v) {
    put(h, r, (uint32_t)v);
    put(h, r + 4, (uint32_t)(v >> 32));
}
static struct dma allocate(struct host *h) {
    uint64_t p = page_try_alloc();
    if (p && !h->wide && p >= 0x100000000ULL) {
        page_free(p);
        p = 0;
#ifndef __aarch64__
        p = page_alloc_between(PAGE, 0x100000000ULL);
#endif
    }
    return (struct dma){p, p ? phys_pointer(p) : NULL};
}
static bool ring_init(struct host *h, struct ring *r) {
    r->dma = allocate(h);
    r->cycle = 1;
    return r->dma.pointer != NULL;
}
static uint64_t enqueue(struct ring *r, uint64_t parameter, uint32_t status, uint32_t control) {
    struct trb *p = r->dma.pointer;
    unsigned at = r->put;
    p[at] = (struct trb){parameter, status, control | r->cycle};
    usb_dma_sync(p + at, sizeof *p);
    if (++r->put == RING_SIZE - 1) {
        p[r->put] = (struct trb){r->dma.address, 0, TYPE(6) | 2 | r->cycle};
        usb_dma_sync(p + r->put, sizeof *p);
        r->put = 0;
        r->cycle ^= 1;
    }
    return r->dma.address + at * sizeof *p;
}
static void receive(struct host *, const struct trb *);
static void events(struct host *h) {
    if (!h->alive)
        return;
    struct trb *p = h->event.pointer;
    unsigned count = 0;
    while (count++ < RING_SIZE) {
        usb_dma_sync(p + h->event_at, sizeof *p);
        struct trb e = p[h->event_at];
        if ((e.control & 1) != h->event_cycle)
            break;
        unsigned type = (e.control >> 10) & 63, code = e.status >> 24;
        if (type == 33 && e.parameter == h->waiting) {
            h->command_code = code;
            h->command_slot = e.control >> 24;
        } else if (type == 32)
            receive(h, &e);
        else if (type == 34) {
            unsigned port = (unsigned)(e.parameter >> 24) & 255;
            if (port && port <= h->ports) {
                uint32_t value = get(h, h->op + 0x400 + (port - 1) * 16);
                if (!(value & 1))
                    for (unsigned i = 0; i < device_count; ++i)
                        if (devices[i].host == h && devices[i].port == port) {
                            devices[i].attached = false;
                            if (stream.device == devices + i) {
                                stream.active = false;
                                capture_status = "Camera disconnected";
                                stream.frames.sequence = 0;
                            }
                        }
                put(h, h->op + 0x400 + (port - 1) * 16, (value & (1u << 9)) | (value & 0xfe0000));
            }
        }
        if (++h->event_at == RING_SIZE) {
            h->event_at = 0;
            h->event_cycle ^= 1;
        }
        put64(h, h->rt + 0x38, h->event.address + h->event_at * sizeof *p + 8);
    }
    if (stream.kick && stream.active && stream.device && stream.device->host == h) {
        stream.kick = false;
        put(h, h->db + stream.device->slot * 4, stream.dci);
    }
}
static bool wait_register(struct host *h, unsigned reg, uint32_t mask, uint32_t wanted,
                          unsigned ms) {
    uint64_t end = usb_millis() + ms;
    do {
        if ((get(h, reg) & mask) == wanted)
            return true;
        usb_delay(1);
    } while (usb_millis() < end);
    return false;
}
static unsigned command(struct host *h, uint64_t parameter, uint32_t control) {
    if (!h->alive)
        return 0;
    h->command_code = 0;
    h->waiting = enqueue(&h->command, parameter, 0, control);
    put(h, h->db, 0);
    uint64_t end = usb_millis() + 3000;
    while (!h->command_code && usb_millis() < end) {
        events(h);
        if (!h->command_code)
            usb_delay(1);
    }
    if (!h->command_code) {
        h->alive = false;
        put(h, h->op, 0);
        capture_status = "USB controller timed out; capture stopped";
    }
    return h->command_code == 1 ? h->command_slot : 0;
}
static uint32_t *context(struct device *d, bool input, unsigned index) {
    return (uint32_t *)((uint8_t *)(input ? d->input.pointer : d->output.pointer) +
                        index * d->host->context_size);
}
static void sync_context(struct device *d) {
    usb_dma_sync(d->input.pointer, PAGE);
    usb_dma_sync(d->output.pointer, PAGE);
}
static bool control(struct device *d, uint8_t request_type, uint8_t request, uint16_t value,
                    uint16_t index, void *data, unsigned length) {
    if (!d->attached || !d->host->alive || length > PAGE)
        return false;
    struct host *h = d->host;
    uint64_t setup = (uint64_t)request_type | (uint64_t)request << 8 | (uint64_t)value << 16 |
                     (uint64_t)index << 32 | (uint64_t)length << 48;
    bool in = (request_type & 0x80) != 0;
    d->code = 0;
    d->residual = 0;
    d->data_short = false;
    if (length && !in)
        memcpy(d->buffer.pointer, data, length);
    usb_dma_sync(d->buffer.pointer, PAGE);
    enqueue(&d->control, setup, 8, TYPE(2) | 64 | ((length ? (in ? 3u : 2u) : 0u) << 16));
    d->data_wait =
        length ? enqueue(&d->control, d->buffer.address, length, TYPE(3) | 4 | (in ? 1u << 16 : 0))
               : 0;
    d->waiting = enqueue(&d->control, 0, 0, TYPE(4) | 32 | ((!length || !in) ? 1u << 16 : 0));
    put(h, h->db + d->slot * 4, 1);
    uint64_t end = usb_millis() + 2000;
    while (!d->code && usb_millis() < end) {
        events(h);
        if (!d->code)
            usb_delay(1);
    }
    if (d->code != 1 || d->residual > length) {
        log_write("USB control error: ");
        log_dec(d->code);
        log_write("\n");
        /* Quiesce this device before its control buffer could be reused.
         * DMA pages remain allocated even if the controller fails to stop. */
        d->attached = false;
        if (h->alive)
            (void)command(h, 0, TYPE(10) | (d->slot << 24));
        return false;
    }
    if (length && in) {
        usb_dma_sync(d->buffer.pointer, PAGE);
        memcpy(data, d->buffer.pointer, length - d->residual);
    }
    return !d->residual;
}
static void submit(unsigned buffer) {
    struct device *d = stream.device;
    unsigned at = stream.ring.put;
    if (stream.td[at].pending) {
        stream.active = false;
        capture_status = "USB receive ring overflow";
        return;
    }
    unsigned flags = TYPE(d->mode.iso ? 5 : 1) | 32 | 4;
    if (d->mode.iso) {
        unsigned packets = (stream.bytes + (d->mode.packet & 2047) - 1) / (d->mode.packet & 2047);
        flags |= 1u << 31;
        flags |= (packets - 1) << 16;
    }
    stream.td[at].buffer = buffer;
    stream.td[at].pending = true;
    usb_dma_sync(stream.buffers[buffer].pointer, PAGE);
    enqueue(&stream.ring, stream.buffers[buffer].address, stream.bytes, flags);
}
static void receive(struct host *h, const struct trb *e) {
    unsigned slot = e->control >> 24, dci = (e->control >> 16) & 31, code = e->status >> 24,
             residual = e->status & 0xffffff;
    if (stream.device && stream.device->host == h && slot == stream.device->slot &&
        dci == stream.dci) {
        if (!stream.active)
            return;
        /* An empty ISO ring reports the NEXT dequeue position. It does not
         * complete or consume that TRB. Retiring it here corrupts ownership. */
        if ((code == 14 || code == 15) && stream.device->mode.iso) {
            ++stream.frames.dropped;
            return;
        }
        uint64_t offset = e->parameter - stream.ring.dma.address;
        if (offset >= ((RING_SIZE - 1) * sizeof(struct trb)) || offset % sizeof(struct trb)) {
            stream.active = false;
            capture_status = "Invalid USB transfer completion";
            return;
        }
        unsigned at = offset / sizeof(struct trb), buffer = stream.td[at].buffer;
        if (!stream.td[at].pending || buffer >= RX_COUNT) {
            stream.active = false;
            capture_status = "Unexpected USB transfer completion";
            return;
        }
        stream.td[at].pending = false;
        if (code == 1 || code == 13) {
            if (residual > stream.bytes) {
                stream.active = false;
                capture_status = "Invalid USB packet length";
                return;
            }
            usb_dma_sync(stream.buffers[buffer].pointer, PAGE);
            if (uvc_payload(&stream.frames, stream.buffers[buffer].pointer,
                            stream.bytes - residual) &&
                stream.frames.sequence == 1)
                log_write("UVC FRAME READY\n");
        } else if (code == 23 || code == 31) {
            ++stream.frames.dropped;
            stream.frames.invalid = true;
        } else {
            stream.active = false;
            capture_status = "Camera transfer failed";
            log_write("UVC transfer error: ");
            log_dec(code);
            log_write("\n");
            return;
        }
        submit(buffer);
        stream.kick = true;
        return;
    }
    for (unsigned i = 0; i < device_count; ++i) {
        struct device *d = devices + i;
        if (d->host != h || d->slot != slot || dci != 1)
            continue;
        if (e->parameter == d->data_wait && code == 13) {
            d->residual = residual;
            d->data_short = true;
        } else if (e->parameter == d->waiting || code != 1)
            d->code = code;
    }
}
static bool endpoint(struct device *d, bool drop) {
    struct host *h = d->host;
    unsigned dci = stream.dci;
    memset(d->input.pointer, 0, PAGE);
    usb_dma_sync(d->output.pointer, PAGE);
    uint32_t *flags = context(d, true, 0), *slot = context(d, true, 1),
             *ep = context(d, true, dci + 1);
    memcpy(slot, context(d, false, 0), h->context_size);
    if (drop) {
        flags[0] = 1u << dci;
        flags[1] = 1;
        slot[0] = (slot[0] & 0x7ffffff) | (1u << 27);
    } else {
        flags[1] = 1 | (1u << dci);
        slot[0] = (slot[0] & 0x7ffffff) | (dci << 27);
        unsigned interval = d->mode.iso ? d->mode.interval - 1 : 0,
                 burst = (d->mode.packet >> 11) & 3;
        ep[0] = interval << 16;
        ep[1] = (d->mode.iso ? 5u : 6u) << 3 | (d->mode.iso ? 0 : 3u << 1) | (burst << 8) |
                ((uint32_t)(d->mode.packet & 2047) << 16);
        ep[2] = (uint32_t)stream.ring.dma.address | 1;
        ep[3] = stream.ring.dma.address >> 32;
        ep[4] = stream.bytes | (d->mode.iso ? stream.bytes << 16 : 0);
    }
    sync_context(d);
    return command(h, d->input.address, TYPE(12) | (d->slot << 24)) != 0;
}
void camera_stop(void) {
    stream.active = false;
    if (stream.configured && stream.device) {
        struct device *d = stream.device;
        if (d->host->alive) {
            (void)command(d->host, 0, TYPE(15) | (stream.dci << 16) | (d->slot << 24));
            (void)endpoint(d, true);
            if (d->attached)
                (void)control(d, 1, 11, 0, d->mode.interface, NULL, 0);
        }
    }
    stream.configured = false;
    stream.kick = false;
    stream.frames.sequence = 0;
    stream.frames.used = 0;
    memset(stream.images, 0, sizeof stream.images);
    capture_status = "Camera off";
}
bool camera_start(void) {
    camera_stop();
    struct device *d = NULL;
    for (unsigned i = 0; i < device_count; ++i)
        if (devices[i].attached && devices[i].video && devices[i].host->alive) {
            d = devices + i;
            break;
        }
    if (!d) {
        capture_status = "No supported UVC YUY2 camera on an xHCI root port";
        return false;
    }
    /* This first implementation covers USB 2 high-speed video. SuperSpeed
     * requires its companion descriptor/ESIT policy, and is rejected explicitly. */
    if (d->speed != 3) {
        capture_status = "Camera requires a USB high-speed connection (480 Mb/s)";
        return false;
    }
    stream.device = d;
    stream.dci = (d->mode.endpoint & 15) * 2 + 1;
    uint8_t probe[48] = {1, 0};
    unsigned probe_size = d->mode.version >= 0x150 ? 48 : d->mode.version >= 0x110 ? 34 : 26;
    probe[2] = d->mode.format;
    probe[3] = d->mode.frame;
    for (unsigned i = 0; i < 4; ++i) {
        probe[4 + i] = (uint8_t)(d->mode.period >> (8 * i));
        probe[18 + i] = (uint8_t)((d->mode.width * d->mode.height * 2u) >> (8 * i));
        probe[22 + i] = (uint8_t)(d->mode.payload >> (8 * i));
    }
    if (!control(d, 1, 11, 0, d->mode.interface, NULL, 0) ||
        !control(d, 0x21, 1, 0x100, d->mode.interface, probe, probe_size) ||
        !control(d, 0xa1, 0x81, 0x100, d->mode.interface, probe, probe_size)) {
        capture_status = "UVC probe negotiation failed";
        return false;
    }
    uint32_t payload = 0, frame = 0;
    for (unsigned i = 0; i < 4; ++i) {
        payload |= (uint32_t)probe[22 + i] << (8 * i);
        frame |= (uint32_t)probe[18 + i] << (8 * i);
    }
    if (probe[2] != d->mode.format || probe[3] != d->mode.frame ||
        frame != d->mode.width * d->mode.height * 2u || !payload || payload > d->mode.payload) {
        capture_status = "Camera negotiated an unsupported payload or format";
        return false;
    }
    if (!control(d, 0x21, 1, 0x200, d->mode.interface, probe, probe_size)) {
        capture_status = "UVC commit failed";
        return false;
    }
    if (!stream.ring.dma.pointer && !ring_init(d->host, &stream.ring)) {
        capture_status = "Camera DMA allocation failed";
        return false;
    }
    memset(stream.ring.dma.pointer, 0, PAGE);
    stream.ring.put = 0;
    stream.ring.cycle = 1;
    memset(stream.td, 0, sizeof stream.td);
    for (unsigned i = 0; i < RX_COUNT; ++i) {
        if (!stream.buffers[i].pointer)
            stream.buffers[i] = allocate(d->host);
        if (!stream.buffers[i].pointer) {
            capture_status = "Camera DMA allocation failed";
            return false;
        }
    }
    stream.bytes = payload;
    if (!control(d, 1, 11, d->mode.alternate, d->mode.interface, NULL, 0) || !endpoint(d, false)) {
        capture_status = "Camera endpoint configuration failed";
        return false;
    }
    stream.configured = true;
    uvc_frames_init(&stream.frames, stream.images[0], stream.images[1], UVC_FRAME_BYTES, frame);
    stream.active = true;
    for (unsigned i = 0; i < RX_COUNT && stream.active; ++i)
        submit(i);
    put(d->host, d->host->db + d->slot * 4, stream.dci);
    capture_status = "Streaming UVC YUY2";
    log_write("UVC STREAM STARTED\n");
    return stream.active;
}
static bool enumerate(struct host *h, unsigned port, unsigned speed) {
    if (device_count == DEV_MAX)
        return false;
    unsigned slot = command(h, 0, TYPE(9));
    if (!slot)
        return false;
    struct device *d = devices + device_count++;
    d->host = h;
    d->slot = slot;
    d->port = port;
    d->speed = speed;
    d->attached = true;
    d->input = allocate(h);
    d->output = allocate(h);
    d->buffer = allocate(h);
    if (!d->input.pointer || !d->output.pointer || !d->buffer.pointer || !ring_init(h, &d->control))
        return false;
    ((uint64_t *)h->dcbaa.pointer)[slot] = d->output.address;
    usb_dma_sync(h->dcbaa.pointer, PAGE);
    uint32_t *flags = context(d, true, 0), *s = context(d, true, 1), *ep = context(d, true, 2);
    flags[1] = 3;
    s[0] = (speed << 20) | (1u << 27);
    s[1] = port << 16;
    unsigned packet = speed == 4 ? 512 : speed == 3 ? 64 : 8;
    ep[1] = 3u << 1 | 4u << 3 | packet << 16;
    ep[2] = (uint32_t)d->control.dma.address | 1;
    ep[3] = d->control.dma.address >> 32;
    ep[4] = 8;
    sync_context(d);
    if (!command(h, d->input.address, TYPE(11) | (slot << 24)))
        return false;
    uint8_t descriptor[18] = {0};
    if (!control(d, 0x80, 6, 0x100, 0, descriptor, 8) || descriptor[0] < 18)
        return false;
    if (speed == 4 && descriptor[7] > 9)
        return false;
    unsigned actual = speed == 4 ? (1u << descriptor[7]) : descriptor[7];
    if (!actual || actual > 512)
        return false;
    if (actual != packet) {
        memset(d->input.pointer, 0, PAGE);
        usb_dma_sync(d->output.pointer, PAGE);
        flags = context(d, true, 0);
        ep = context(d, true, 2);
        flags[1] = 2;
        memcpy(ep, context(d, false, 1), h->context_size);
        ep[1] = (ep[1] & 65535) | (actual << 16);
        sync_context(d);
        if (!command(h, d->input.address, TYPE(13) | (slot << 24)))
            return false;
    }
    if (!control(d, 0x80, 6, 0x100, 0, descriptor, 18))
        return false;
    d->vendor = (uint16_t)(descriptor[8] | descriptor[9] << 8);
    d->product = (uint16_t)(descriptor[10] | descriptor[11] << 8);
    if (!control(d, 0x80, 6, 0x200, 0, d->config, 9))
        return false;
    d->config_size = d->config[2] | d->config[3] << 8;
    if (d->config_size < 9 || d->config_size > sizeof d->config ||
        !control(d, 0x80, 6, 0x200, 0, d->config, d->config_size))
        return false;
    if (!control(d, 0, 9, d->config[5], 0, NULL, 0))
        return false;
    d->video = uvc_describe(d->config, d->config_size, &d->mode);
    log_write("USB DEVICE: ");
    log_hex(d->vendor);
    log_write(":");
    log_hex(d->product);
    log_write(d->video ? " UVC YUY2\n" : " enumerated\n");
    return true;
}
static bool initialize(struct host *h, uint64_t bar) {
    h->base = usb_map_mmio(bar, 65536);
    if (!h->base)
        return false;
    h->op = h->base[0];
    if (h->op < 0x20 || h->op > 0x100)
        return false;
    uint32_t p1 = get(h, 4), p2 = get(h, 8), c = get(h, 0x10);
    h->ports = p1 >> 24;
    h->slots = p1 & 255;
    if (!h->ports || !h->slots)
        return false;
    if (h->slots > DEV_MAX)
        h->slots = DEV_MAX;
    h->wide = (c & 1) != 0;
    h->context_size = c & 4 ? 64 : 32;
    h->db = get(h, 0x14) & ~3u;
    h->rt = get(h, 0x18) & ~31u;
    if (h->db > 65536 - 1024 || h->rt > 65536 - 64)
        return false;
    unsigned ext = (c >> 16) * 4, seen = 0;
    while (ext && seen++ < 128) {
        if (ext > 65528)
            return false;
        uint32_t cap = get(h, ext);
        if ((cap & 255) == 1) {
            put(h, ext, cap | (1u << 24));
            if (!wait_register(h, ext, 1u << 16, 0, 1000))
                return false;
            put(h, ext + 4, 0);
        }
        unsigned next = (cap >> 8) & 255;
        if (!next)
            break;
        ext += next * 4;
    }
    if (!wait_register(h, h->op + 4, 1u << 11, 0, 1000))
        return false;
    put(h, h->op, 0);
    if (!wait_register(h, h->op + 4, 1, 1, 1000))
        return false;
    put(h, h->op, 2);
    if (!wait_register(h, h->op, 2, 0, 1000) || !wait_register(h, h->op + 4, 1u << 11, 0, 1000))
        return false;
    if (!(get(h, h->op + 8) & 1))
        return false;
    h->dcbaa = allocate(h);
    h->event = allocate(h);
    h->erst = allocate(h);
    if (!h->dcbaa.pointer || !h->event.pointer || !h->erst.pointer || !ring_init(h, &h->command))
        return false;
    unsigned scratch = ((p2 >> 27) & 31) | (((p2 >> 21) & 31) << 5);
    if (scratch > 512)
        return false;
    if (scratch) {
        h->scratch = allocate(h);
        if (!h->scratch.pointer)
            return false;
        for (unsigned i = 0; i < scratch; ++i) {
            struct dma page = allocate(h);
            if (!page.pointer)
                return false;
            ((uint64_t *)h->scratch.pointer)[i] = page.address;
            usb_dma_sync(page.pointer, PAGE);
        }
        usb_dma_sync(h->scratch.pointer, PAGE);
        ((uint64_t *)h->dcbaa.pointer)[0] = h->scratch.address;
    }
    uint64_t *erst = h->erst.pointer;
    erst[0] = h->event.address;
    erst[1] = RING_SIZE;
    usb_dma_sync(h->erst.pointer, PAGE);
    usb_dma_sync(h->event.pointer, PAGE);
    usb_dma_sync(h->dcbaa.pointer, PAGE);
    usb_dma_sync(h->command.dma.pointer, PAGE);
    put64(h, h->op + 0x30, h->dcbaa.address);
    put(h, h->op + 0x38, h->slots);
    put64(h, h->op + 0x18, h->command.dma.address | 1);
    put(h, h->rt + 0x28, 1);
    put64(h, h->rt + 0x30, h->erst.address);
    put64(h, h->rt + 0x38, h->event.address);
    put(h, h->rt + 0x20, 0);
    h->event_cycle = 1;
    h->alive = true;
    put(h, h->op, 1);
    if (!wait_register(h, h->op + 4, 1, 0, 1000)) {
        h->alive = false;
        return false;
    }
    log_write("XHCI READY\n");
    for (unsigned port = 1; port <= h->ports && h->alive; ++port) {
        unsigned reg = h->op + 0x400 + (port - 1) * 16;
        uint32_t value = get(h, reg);
        if (!(value & 1))
            continue;
        if (!(value & 2)) {
            put(h, reg, (value & (1u << 9)) | (1u << 4));
            if (!wait_register(h, reg, 1u << 4, 0, 1000))
                continue;
            usb_delay(20);
            value = get(h, reg);
        }
        if (!(value & 2))
            continue;
        put(h, reg, (value & (1u << 9)) | (value & 0xfe0000));
        if (!enumerate(h, port, (value >> 10) & 15))
            log_write("USB port enumeration failed\n");
    }
    return h->alive;
}
void usb_init(void) {
    usb_platform_init();
    unsigned first, last;
    if (!usb_pci_window(&first, &last))
        return;
    for (unsigned b = first; b <= last; ++b)
        for (unsigned s = 0; s < 32; ++s) {
            uint32_t id = usb_pci_read(b, s, 0, 0);
            if ((id & 65535) == 65535)
                continue;
            unsigned functions = usb_pci_read(b, s, 0, 12) & 0x800000 ? 8 : 1;
            for (unsigned f = 0; f < functions && host_count < HC_MAX; ++f) {
                if ((usb_pci_read(b, s, f, 8) & 0xffffff00) != 0x0c033000)
                    continue;
                uint32_t low = usb_pci_read(b, s, f, 0x10);
                if ((low & 1) || ((low & 6) != 0 && (low & 6) != 4))
                    continue;
                uint64_t bar = low & ~15u;
                if ((low & 6) == 4)
                    bar |= (uint64_t)usb_pci_read(b, s, f, 0x14) << 32;
                if (!bar)
                    continue;
                uint32_t config = usb_pci_read(b, s, f, 4) & 65535;
                usb_pci_write(b, s, f, 4, config | 6);
                struct host *h = hosts + host_count++;
                if (!initialize(h, bar)) {
                    if (h->alive)
                        put(h, h->op, 0);
                    h->alive = false;
                    log_write("xHCI initialization failed\n");
                }
            }
        }
}
void usb_poll(void) {
    for (unsigned i = 0; i < host_count; ++i)
        events(hosts + i);
}
unsigned usb_controller_count(void) {
    return host_count;
}
unsigned usb_device_count(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < device_count; ++i)
        n += devices[i].attached;
    return n;
}
const char *camera_status(void) {
    return capture_status;
}
bool camera_active(void) {
    return stream.active || stream.configured;
}
const struct uvc_mode *camera_mode(void) {
    return stream.device ? &stream.device->mode : NULL;
}
const struct uvc_frames *camera_frames(void) {
    return &stream.frames;
}
void usb_list(void (*out)(const char *)) {
    char line[64], n[24];
    number(n, host_count);
    size_t k = 0;
    const char *p = "Native xHCI controllers: ";
    while (*p)
        line[k++] = *p++;
    p = n;
    while (*p)
        line[k++] = *p++;
    line[k] = 0;
    out(line);
    for (unsigned i = 0; i < device_count; ++i)
        if (devices[i].attached) {
            const struct device *d = devices + i;
            const char *hex = "0123456789abcdef";
            unsigned at = 0;
            for (unsigned j = 0; j < 4; ++j)
                line[at++] = hex[(d->vendor >> (12 - j * 4)) & 15];
            line[at++] = ':';
            for (unsigned j = 0; j < 4; ++j)
                line[at++] = hex[(d->product >> (12 - j * 4)) & 15];
            line[at++] = ' ';
            p = d->video ? "UVC YUY2 camera" : "USB device";
            while (*p)
                line[at++] = *p++;
            line[at] = 0;
            out(line);
        }
    out(capture_status);
}
