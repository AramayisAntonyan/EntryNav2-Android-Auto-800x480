// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The Gauges app's value handling for any catalogue entry (platform/gauge_catalogue.hpp): reading a CDS number,
// the bar level, the value's text, and the set of gauges the owner ticked. Pure logic (tests/test_gauge_values.cpp).
#ifndef ENTRY_AAD_HMIAPP_GAUGE_VALUES_HPP
#define ENTRY_AAD_HMIAPP_GAUGE_VALUES_HPP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/gauge_catalogue.hpp"

namespace evo {
namespace gauges {

using catalogue::Entry;
using catalogue::kEntries;
using catalogue::kEntryCount;

const unsigned kBarLevels = 25;  // images: 0 (empty) .. 24 (full)

// The number after "<key>": in a CDS text; false when the key, a number, or a valid value is missing.
inline bool readField(const char* text, const char* key, double invalid, double* value) {
    if (!text || !key) {
        return false;
    }
    char pat[40];
    const size_t n = strlen(key);
    if (n + 3 > sizeof(pat)) {
        return false;
    }
    pat[0] = '"';
    memcpy(pat + 1, key, n);
    pat[n + 1] = '"';
    pat[n + 2] = 0;
    const char* p = strstr(text, pat);
    if (!p) {
        return false;
    }
    p += n + 2;
    while (*p == ' ' || *p == ':') {
        ++p;
    }
    char* end = nullptr;
    const double v = strtod(p, &end);
    if (end == p) {
        return false;
    }
    if (invalid > 0 && v >= invalid) {
        return false;  // the car's "no value"
    }
    *value = v;
    return true;
}

// 0 .. kBarLevels - 1 over the entry's range.
inline unsigned barLevel(const Entry& e, double v) {
    if (!(v > e.lo)) {
        return 0;
    }
    if (v >= e.hi) {
        return kBarLevels - 1;
    }
    const unsigned level = static_cast<unsigned>((v - e.lo) / (e.hi - e.lo) * (kBarLevels - 1) + 0.5);
    return level > kBarLevels - 1 ? kBarLevels - 1 : level;
}

// "89 C", or "--" while there is no value. ASCII units, as BoostDeck writes them.
inline void valueText(const Entry& e, bool valid, double v, char* out, size_t cap) {
    if (!valid) {
        snprintf(out, cap, "--");
    } else if (e.unit[0]) {
        snprintf(out, cap, "%.*f %s", e.decimals, v, e.unit);
    } else {
        snprintf(out, cap, "%.*f", e.decimals, v);
    }
}

// The ticked gauges, as a line of 0/1 per catalogue entry ("1101..."); defaults (Entry::on) for a missing file or
// an entry past its end, so a longer catalogue keeps the owner's choice for the old entries.
inline void parseChoice(const char* text, bool* on) {
    const size_t n = text ? strlen(text) : 0;
    for (unsigned i = 0; i < kEntryCount; ++i) {
        on[i] = i < n && (text[i] == '0' || text[i] == '1') ? text[i] == '1' : kEntries[i].on;
    }
}
inline void formatChoice(const bool* on, char* out, size_t cap) {
    size_t i = 0;
    for (; i < kEntryCount && i + 1 < cap; ++i) {
        out[i] = on[i] ? '1' : '0';
    }
    out[i] = 0;
}

}  // namespace gauges
}  // namespace evo

#endif  // ENTRY_AAD_HMIAPP_GAUGE_VALUES_HPP
