/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "uvc_fixture.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
extern "C" {
#include "uvc.h"
}
int main() {
    uvc_mode mode{};
    for (bool iso : {false, true}) {
        auto d = fixture::configuration(iso);
        assert(uvc_describe(d.data(), d.size(), &mode));
        assert(mode.width == 320 && mode.height == 240 && mode.period == 333333);
        assert(mode.version == 0x100);
        assert(mode.iso == iso && mode.endpoint == 0x81 && mode.interface == 1 &&
               mode.alternate == unsigned(iso));
        // Every truncated descriptor stream must be rejected, with no out-of-bounds access.
        for (size_t n = 0; n < d.size(); ++n)
            assert(!uvc_describe(d.data(), n, &mode));
        for (size_t at = 0; at < d.size(); at += d[at]) {
            auto malformed = d;
            malformed[at] = 0;
            assert(!uvc_describe(malformed.data(), malformed.size(), &mode));
            malformed[at] = 1;
            assert(!uvc_describe(malformed.data(), malformed.size(), &mode));
        }
        auto bad = d;
        bad[bad.size() - 1] = 17;
        if (iso)
            assert(!uvc_describe(bad.data(), bad.size(), &mode));
        bad = d;
        bad[bad.size() - 5] = 0;
        assert(!uvc_describe(bad.data(), bad.size(), &mode));
        for (size_t at = 0; at < d.size(); at += d[at])
            if (d[at + 1] == 0x24 && d[at + 2] == 4) {
                bad = d;
                bad[at + 5] = 'X';
                assert(!uvc_describe(bad.data(), bad.size(), &mode));
            }
        for (size_t at = 0; at < d.size(); at += d[at])
            if (d[at + 1] == 0x24 && d[at + 2] == 1 && d[at] == 13) {
                for (unsigned version : {0x110, 0x150}) {
                    bad = d;
                    bad[at + 3] = version & 255;
                    bad[at + 4] = version >> 8;
                    assert(uvc_describe(bad.data(), bad.size(), &mode) && mode.version == version);
                }
                bad = d;
                bad[at + 3] = 0xff;
                assert(!uvc_describe(bad.data(), bad.size(), &mode));
            }
    }
    uint8_t a[8]{}, b[8]{}, first[] = {2, 0, 1, 2, 3, 4}, last[] = {2, 2, 5, 6, 7, 8};
    uvc_frames f{};
    uvc_frames_init(&f, a, b, 8, 8);
    assert(!uvc_payload(&f, first, sizeof first));
    assert(uvc_payload(&f, last, sizeof last));
    assert(f.sequence == 1 && !std::memcmp(f.ready, "\1\2\3\4\5\6\7\10", 8));
    assert(!uvc_payload(&f, last, sizeof last) && f.sequence == 1); // repeated EOF
    uint8_t overflow[] = {2, 3, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    assert(!uvc_payload(&f, overflow, sizeof overflow) && f.sequence == 1);
    uint8_t error[] = {2, 0x42, 1, 2, 3, 4, 5, 6, 7, 8};
    assert(!uvc_payload(&f, error, sizeof error));
    uint8_t partial[] = {2, 3, 1, 2};
    assert(!uvc_payload(&f, partial, sizeof partial));
    uint8_t pts[] = {2, 4};
    assert(!uvc_payload(&f, pts, sizeof pts));
    uint8_t invalid[] = {1, 0};
    assert(!uvc_payload(&f, invalid, sizeof invalid));
    uint8_t good[] = {2, 2, 8, 7, 6, 5, 4, 3, 2, 1};
    assert(uvc_payload(&f, good, sizeof good));
    assert(f.sequence == 2 && !std::memcmp(f.ready, good + 2, 8));
    uint8_t gray[] = {16, 128, 235, 128};
    assert(uvc_yuy2_pixel(gray, 0) == 0 && uvc_yuy2_pixel(gray, 1) == 0xffffff);
    uint8_t red[] = {81, 90, 81, 240};
    assert((uvc_yuy2_pixel(red, 0) & 0xff0000) > 0xf00000);
    std::cout << "UVC descriptor, bounds, frame recovery and YUY2 tests PASS\n";
}
