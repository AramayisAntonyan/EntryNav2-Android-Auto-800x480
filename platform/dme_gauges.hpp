// SPDX-License-Identifier: MIT
// Ported from ID6 apps (github.com/inotbarnacle/ID6_apps_prerelease, source/native/src/gauge_core.c), MIT licence:
//
//   Copyright (c) 2026 ID6 Apps contributors
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
//   documentation files (the "Software"), to deal in the Software without restriction, including without limitation
//   the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
//   to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all copies or substantial portions
//   of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
//   THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
//   CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
//   IN THE SOFTWARE.
//
// Changes here (Jiraiya43 fork for the F22 / EntryNav2): C++ header for Linux; the frame carries any list of
// catalogue entries (platform/gauge_catalogue.hpp) instead of BoostDeck's fixed eight. The DIDs and formulas are BoostDeck's, measured on a Gen 1
// B58 (340i 2016): this car's B46/B48 DME may answer differently, which `gauge-collector --probe` shows before any
// value is trusted. Readings that fail a check stay blank. Tests: tests/test_gauge_collector.cpp.
#ifndef ENTRY_AAD_PLATFORM_DME_GAUGES_HPP
#define ENTRY_AAD_PLATFORM_DME_GAUGES_HPP

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace evo {
namespace dme {

enum { kMaxPacket = 1400, kMaxValues = 64, kStaleMs = 2500, kUdpPort = 42860 };

// The values read in one cycle, from the collector to the iDrive app over loopback UDP, as text:
// "G2 <seq> <entry>:<value>:<age ms> ..." where entry is the index in platform/gauge_catalogue.hpp.
struct Frame {
    uint32_t seq;
    unsigned count;
    unsigned entry[kMaxValues];
    double value[kMaxValues];
    unsigned ageMs[kMaxValues];
};

inline uint64_t monoMs() {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) {
        return 0;
    }
    return static_cast<uint64_t>(t.tv_sec) * 1000 + static_cast<uint64_t>(t.tv_nsec) / 1000000;
}

// Payload bytes after the 62 <DID> prefix. False when the length, a sentinel or the plausible range says no.
inline bool decodeDid(unsigned did, const unsigned char* p, size_t n, double* out) {
    if (!p || !out || n != (did == 0x580f ? 1u : 2u)) {
        return false;
    }
    const unsigned r = did == 0x580f ? p[0] : (static_cast<unsigned>(p[0]) << 8) | p[1];
    double x, lo, hi;
    switch (did) {
        case 0x4201:  // ambient pressure, kPa
            x = r / 256.0;
            lo = 40;
            hi = 115;
            break;
        case 0x4205:  // absolute boost pressure, kPa
            x = r / 128.0;
            lo = 20;
            hi = 450;
            break;
        case 0x5889:  // lambda
            x = r * 16.0 / 65535.0;
            lo = .5;
            hi = 2;
            break;
        case 0x4a49:  // ignition timing; FFFF was ambiguous on the reference car: blank
            if (r == 65535) {
                return false;
            }
            x = (r & 0x8000 ? static_cast<int>(r) - 65536 : static_cast<int>(r)) / 10.0;
            lo = -64;
            hi = 64;
            break;
        case 0x580f:  // intake air, C
            if (r == 255) {
                return false;
            }
            x = r * .75 - 48;
            lo = -48;
            hi = 143.25;
            break;
        case 0xda12:  // transmission, C (EGS)
            x = static_cast<double>(r) - 48;
            lo = -48;
            hi = 180;
            break;
        case 0x56d7:  // rail pressure, bar
            if (r == 65535) {
                return false;
            }
            x = r / 50.0;
            lo = 0;
            hi = 500;
            break;
        case 0x586f:  // oil pressure, bar
            if (r == 65535) {
                return false;
            }
            x = r / 1000.0;
            lo = 0;
            hi = 20;
            break;
        case 0x4300:  // coolant, C
            if (r == 65535) {
                return false;
            }
            x = r / 10.0 - 273.15;
            lo = -48;
            hi = 150;
            break;
        default:
            return false;
    }
    if (!isfinite(x) || x < lo || x > hi) {
        return false;
    }
    *out = x;
    return true;
}

inline int encodeFrame(const Frame& f, char* out, size_t len) {
    if (!out || !len) {
        return -1;
    }
    int n = snprintf(out, len, "G2 %lu", static_cast<unsigned long>(f.seq));
    if (n < 0 || static_cast<size_t>(n) >= len) {
        return -1;
    }
    size_t used = static_cast<size_t>(n);
    for (unsigned i = 0; i < f.count && i < kMaxValues; ++i) {
        n = snprintf(out + used, len - used, " %u:%.4f:%u", f.entry[i], f.value[i], f.ageMs[i]);
        if (n < 0 || static_cast<size_t>(n) >= len - used) {
            return -1;
        }
        used += static_cast<size_t>(n);
    }
    if (len - used < 2) {
        return -1;
    }
    out[used++] = '\n';
    out[used] = 0;
    return static_cast<int>(used);
}

// Strict: any malformed part rejects the whole frame. maxEntry bounds the entry indexes.
inline bool parseFrame(const char* data, size_t len, unsigned maxEntry, Frame* out) {
    char b[kMaxPacket];
    if (!data || !out || len < 4 || len >= sizeof(b) || memchr(data, 0, len)) {
        return false;
    }
    memcpy(b, data, len);
    b[len] = 0;
    if (memcmp(b, "G2 ", 3) != 0) {
        return false;
    }
    Frame f;
    memset(&f, 0, sizeof(f));
    char* p = b + 3;
    char* end = nullptr;
    if (!isdigit(static_cast<unsigned char>(*p))) {
        return false;
    }
    f.seq = static_cast<uint32_t>(strtoul(p, &end, 10));
    p = end;
    while (*p) {
        while (*p == ' ' || *p == '\n' || *p == '\r') {
            ++p;
        }
        if (!*p) {
            break;
        }
        if (f.count >= kMaxValues || !isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }
        const unsigned long e = strtoul(p, &end, 10);
        if (end == p || *end != ':' || e >= maxEntry) {
            return false;
        }
        p = end + 1;
        errno = 0;
        const double v = strtod(p, &end);
        if (end == p || *end != ':' || errno == ERANGE || !isfinite(v) || fabs(v) > 1e7) {
            return false;
        }
        p = end + 1;
        if (!isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }
        const unsigned long age = strtoul(p, &end, 10);
        if (end == p || age > 999999 || (*end && *end != ' ' && *end != '\n' && *end != '\r')) {
            return false;
        }
        p = end;
        f.entry[f.count] = static_cast<unsigned>(e);
        f.value[f.count] = v;
        f.ageMs[f.count] = static_cast<unsigned>(age);
        ++f.count;
    }
    *out = f;
    return true;
}

}  // namespace dme
}  // namespace evo

#endif  // ENTRY_AAD_PLATFORM_DME_GAUGES_HPP
