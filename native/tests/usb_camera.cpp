/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host-side synthetic UVC camera. QEMU transports these packets through its
 * real emulated xHCI controller; no Linux guest is involved. Never shipped. */
#include "uvc_fixture.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <usbredirparser.h>
using Clock = std::chrono::steady_clock;
struct Camera {
    int fd;
    bool iso, streaming = false, connected = true;
    uint8_t configuration = 0, alt = 0, fid = 0;
    size_t offset = 0;
    usbredirparser *parser;
    std::array<uint8_t, 26> probe{};
    std::deque<std::pair<uint64_t, usb_redir_bulk_packet_header>> pending;
    explicit Camera(int socket, bool isochronous)
        : fd(socket), iso(isochronous), parser(usbredirparser_create()) {
        if (!parser)
            throw std::runtime_error("usbredirparser_create");
        probe[0] = 1;
        probe[2] = 1;
        probe[3] = 1;
        probe[4] = 0x15;
        probe[5] = 0x16;
        probe[6] = 5;
        probe[19] = 0x58;
        probe[20] = 2;
        probe[23] = 4;
        parser->priv = this;
        parser->log_func = [](void *, int, const char *message) {
            fprintf(stderr, "camera: %s\n", message);
        };
        parser->read_func = [](void *p, uint8_t *b, int n) {
            auto &c = *(Camera *)p;
            int r = recv(c.fd, b, n, MSG_DONTWAIT);
            if (r == 0)
                return -1;
            return r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : r;
        };
        parser->write_func = [](void *p, uint8_t *b, int n) {
            auto &c = *(Camera *)p;
            int r = send(c.fd, b, n, MSG_DONTWAIT | MSG_NOSIGNAL);
            return r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : r;
        };
        parser->hello_func = [](void *p, usb_redir_hello_header *) {
            auto &c = *(Camera *)p;
            usb_redir_interface_info_header i{};
            i.interface_count = 2;
            i.interface[0] = 0;
            i.interface[1] = 1;
            i.interface_class[0] = i.interface_class[1] = 14;
            i.interface_subclass[0] = 1;
            i.interface_subclass[1] = 2;
            usbredirparser_send_interface_info(c.parser, &i);
            c.endpoints();
            usb_redir_device_connect_header d{};
            d.speed = usb_redir_speed_high;
            d.device_class = 0xef;
            d.device_subclass = 2;
            d.device_protocol = 1;
            d.vendor_id = 0xcafe;
            d.product_id = 0x4001;
            d.device_version_bcd = 0x100;
            usbredirparser_send_device_connect(c.parser, &d);
        };
        parser->reset_func = [](void *p) {
            auto &c = *(Camera *)p;
            c.streaming = false;
            c.offset = 0;
            c.pending.clear();
        };
        parser->set_configuration_func = [](void *p, uint64_t id,
                                            usb_redir_set_configuration_header *h) {
            auto &c = *(Camera *)p;
            c.configuration = h->configuration;
            c.config_reply(id);
        };
        parser->get_configuration_func = [](void *p, uint64_t id) {
            ((Camera *)p)->config_reply(id);
        };
        parser->set_alt_setting_func = [](void *p, uint64_t id,
                                          usb_redir_set_alt_setting_header *h) {
            auto &c = *(Camera *)p;
            if (h->interface == 1)
                c.alt = h->alt;
            c.alt_reply(id, h->interface);
            c.endpoints();
        };
        parser->get_alt_setting_func = [](void *p, uint64_t id,
                                          usb_redir_get_alt_setting_header *h) {
            ((Camera *)p)->alt_reply(id, h->interface);
        };
        parser->control_packet_func = [](void *p, uint64_t id, usb_redir_control_packet_header *h,
                                         uint8_t *b,
                                         int n) { ((Camera *)p)->control(id, *h, b, n); };
        parser->bulk_packet_func = [](void *p, uint64_t id, usb_redir_bulk_packet_header *h,
                                      uint8_t *b, int) {
            auto &c = *(Camera *)p;
            c.pending.emplace_back(id, *h);
            usbredirparser_free_packet_data(c.parser, b);
        };
        parser->cancel_data_packet_func = [](void *p, uint64_t id) {
            auto &c = *(Camera *)p;
            auto it = std::find_if(c.pending.begin(), c.pending.end(),
                                   [id](auto &v) { return v.first == id; });
            if (it != c.pending.end()) {
                auto h = it->second;
                h.status = usb_redir_cancelled;
                h.length = h.length_high = 0;
                usbredirparser_send_bulk_packet(c.parser, id, &h, nullptr, 0);
                c.pending.erase(it);
            }
        };
        parser->start_iso_stream_func = [](void *p, uint64_t id,
                                           usb_redir_start_iso_stream_header *h) {
            auto &c = *(Camera *)p;
            c.streaming = true;
            usb_redir_iso_stream_status_header s{0, h->endpoint};
            usbredirparser_send_iso_stream_status(c.parser, id, &s);
        };
        parser->stop_iso_stream_func = [](void *p, uint64_t id,
                                          usb_redir_stop_iso_stream_header *h) {
            auto &c = *(Camera *)p;
            c.streaming = false;
            usb_redir_iso_stream_status_header s{0, h->endpoint};
            usbredirparser_send_iso_stream_status(c.parser, id, &s);
        };
        uint32_t caps[USB_REDIR_CAPS_SIZE]{};
        for (int cap : {usb_redir_cap_connect_device_version, usb_redir_cap_ep_info_max_packet_size,
                        usb_redir_cap_64bits_ids, usb_redir_cap_32bits_bulk_length})
            usbredirparser_caps_set_cap(caps, cap);
        usbredirparser_init(parser, "mnOS synthetic UVC test", caps, USB_REDIR_CAPS_SIZE,
                            usbredirparser_fl_usb_host);
    }
    ~Camera() {
        usbredirparser_destroy(parser);
        close(fd);
    }
    void endpoints() {
        usb_redir_ep_info_header e{};
        std::fill(std::begin(e.type), std::end(e.type), usb_redir_type_invalid);
        e.type[0] = e.type[16] = usb_redir_type_control;
        e.max_packet_size[0] = e.max_packet_size[16] = 64;
        e.type[17] = iso ? usb_redir_type_iso : usb_redir_type_bulk;
        e.interface[17] = 1;
        e.interval[17] = 1;
        e.max_packet_size[17] = iso ? 1024 : 512;
        usbredirparser_send_ep_info(parser, &e);
    }
    void config_reply(uint64_t id) {
        usb_redir_configuration_status_header h{0, configuration};
        usbredirparser_send_configuration_status(parser, id, &h);
    }
    void alt_reply(uint64_t id, uint8_t interface) {
        usb_redir_alt_setting_status_header h{0, interface,
                                              static_cast<uint8_t>(interface == 1 ? alt : 0)};
        usbredirparser_send_alt_setting_status(parser, id, &h);
    }
    void control(uint64_t id, usb_redir_control_packet_header h, uint8_t *data, int n) {
        std::vector<uint8_t> reply;
        h.status = usb_redir_success;
        if (h.requesttype == 0x80 && h.request == 6) {
            if (h.value == 0x100)
                reply = fixture::device();
            else if (h.value == 0x200)
                reply = fixture::configuration(iso);
            else
                h.status = usb_redir_stall;
        } else if (h.requesttype == 0x21 && h.request == 1 &&
                   (h.value == 0x100 || h.value == 0x200) && n == 26) {
            std::copy_n(data, 26, probe.begin());
            probe[22] = 0;
            probe[23] = 4;
            probe[24] = probe[25] = 0;
        } else if (h.requesttype == 0xa1 && h.request == 0x81 && h.value == 0x100)
            reply.assign(probe.begin(), probe.end());
        else
            h.status = usb_redir_stall;
        if (h.requesttype & 0x80) {
            if (reply.size() > h.length)
                reply.resize(h.length);
            h.length = reply.size();
        }
        usbredirparser_send_control_packet(parser, id, &h, reply.empty() ? nullptr : reply.data(),
                                           reply.size());
        usbredirparser_free_packet_data(parser, data);
    }
    std::vector<uint8_t> payload(size_t maximum) {
        size_t count = std::min(maximum - 2, 320u * 240u * 2u - offset);
        std::vector<uint8_t> b(count + 2);
        b[0] = 2;
        b[1] = 0x80 | fid;
        const uint8_t red[4] = {81, 90, 81, 240}, blue[4] = {41, 240, 41, 110};
        for (size_t i = 0; i < count; ++i) {
            size_t at = offset + i;
            b[2 + i] = ((at / 2) % 320 < 160 ? red : blue)[at % 4];
        }
        offset += count;
        if (offset == 320u * 240u * 2u) {
            b[1] |= 2;
            offset = 0;
            fid ^= 1;
        }
        return b;
    }
    void packets() {
        if (usbredirparser_get_bufferered_output_size(parser) > 1024 * 1024)
            return;
        for (unsigned i = 0; i < 32; ++i) {
            if (iso) {
                if (!streaming)
                    return;
                auto b = payload(1024);
                usb_redir_iso_packet_header h{0x81, 0, static_cast<uint16_t>(b.size())};
                usbredirparser_send_iso_packet(parser, 0, &h, b.data(), b.size());
            } else {
                if (pending.empty())
                    return;
                auto [id, h] = pending.front();
                pending.pop_front();
                unsigned length = h.length | uint32_t(h.length_high) << 16;
                if (length < 2) {
                    h.status = usb_redir_inval;
                    h.length = h.length_high = 0;
                    usbredirparser_send_bulk_packet(parser, id, &h, nullptr, 0);
                    continue;
                }
                auto b = payload(std::min<unsigned>(1024, length));
                h.status = 0;
                h.length = b.size();
                h.length_high = 0;
                usbredirparser_send_bulk_packet(parser, id, &h, b.data(), b.size());
            }
        }
    }
    void run() {
        auto next = Clock::now();
        for (;;) {
            pollfd p{fd, POLLIN, 0};
            if (usbredirparser_has_data_to_write(parser))
                p.events |= POLLOUT;
            if (poll(&p, 1, 2) < 0)
                break;
            if (p.revents & (POLLERR | POLLHUP))
                break;
            if (p.revents & POLLIN)
                if (usbredirparser_do_read(parser) < 0)
                    break;
            if (Clock::now() >= next) {
                packets();
                next = Clock::now() + std::chrono::milliseconds(4);
            }
            if (usbredirparser_do_write(parser) < 0)
                break;
        }
    }
};
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: usb-camera SOCKET bulk|iso\n");
        return 2;
    }
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    if (strlen(argv[1]) >= sizeof a.sun_path)
        return 2;
    strcpy(a.sun_path, argv[1]);
    if (bind(server, (sockaddr *)&a, sizeof a) || listen(server, 1)) {
        perror("camera socket");
        return 1;
    }
    int fd = accept(server, nullptr, nullptr);
    close(server);
    if (fd < 0)
        return 1;
    try {
        Camera(fd, !strcmp(argv[2], "iso")).run();
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
