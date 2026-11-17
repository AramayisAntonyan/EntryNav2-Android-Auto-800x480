// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Adapted from NBT-AAD's cluster/hudterm.hpp: the protocol part only, without its MOST library types (the transport
// here will be platform/vmost_client.hpp). Host-tested in tests/test_hud6wa.cpp.
//
// The MOST HUDTerminal protocol (FBlock 82, instance 00), which F-series instrument clusters host. The factory
// navigation draws its guidance there as vector items. As NBT-AAD found it on a 6WA (F20):
//   - every call is a MOST method sent as StartResultAck (op 6). The first parameter is a u16 handle, the instance
//     id; the rest are the method's parameters, big-endian. The cluster answers with ResultAck (op D), echoing the
//     payload, to the caller and a copy to the factory navigation's address.
//   - TextareaSetText: handle, byte 00 (encoding), UTF-16BE characters, then 00 00.
//   - ImageAddPolygon: handle, u16 item index, u8 type (2 polyline, 3 filled), u8 count (1..99), then count x (s16 x,
//     s16 y). The cluster refuses more than 99 points.
//   - line colour, line width, fill colour and fill mode: handle and one byte, held per image. Fill colour is a colour
//     index, not RGB.
// Hard limits, checked when a call is built rather than trusted to the caller: only the functions in payloadShapeOk(),
// each with its exact payload shape, and only the handles the cluster's profile allows.
#ifndef ENTRY_AAD_PLATFORM_HUDTERM_HPP
#define ENTRY_AAD_PLATFORM_HUDTERM_HPP

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace evo {
namespace hudterm {

const uint8_t kFBlock = 0x82;
const uint8_t kInst = 0x00;

enum Fkt {
    kSetVisibility = 0x204,
    kIsVisible = 0x205,
    kSwitchVisibility = 0x20E,
    kTextareaSetText = 0x210,
    kSetLineColor = 0x218,
    kSetLineWidth = 0x219,
    kSetFillColor = 0x21B,
    kSetFillMode = 0x21C,
    kAddPolygon = 0x21D,
    kAddCircle = 0x21E,
    kRemoveAllItems = 0x220,
    kGetSize = 0xC04,
};
enum PolyType { kPolyline = 2, kPolygon = 3 };
enum { kMaxPoints = 99, kMaxTextChars = 32, kMaxHandle = 255 };

inline const char* fktName(uint16_t f) {
    switch (f) {
        case kSetVisibility: return "SetVisibility";
        case kIsVisible: return "IsVisible";
        case kSwitchVisibility: return "SwitchVisibility";
        case kTextareaSetText: return "TextareaSetText";
        case kSetLineColor: return "ImageSetLineColor";
        case kSetLineWidth: return "ImageSetLineWidth";
        case kSetFillColor: return "ImageSetFillColor";
        case kSetFillMode: return "ImageSetFillMode";
        case kAddPolygon: return "ImageAddPolygon";
        case kAddCircle: return "ImageAddCircle";
        case kRemoveAllItems: return "ImageRemoveAllItems";
        case kGetSize: return "ImageGetSize";
    }
    return "?";
}

inline uint16_t rdBe16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline void putBe16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

// The handles (instances) a cluster's profile lets us use. Handle 0 is never allowed.
class HandleSet {
public:
    HandleSet() { memset(on_, 0, sizeof(on_)); }
    void allow(int h) {
        if (h > 0 && h <= kMaxHandle) on_[h] = true;
    }
    bool has(int h) const { return h > 0 && h <= kMaxHandle && on_[h]; }

private:
    bool on_[kMaxHandle + 1];
};

// ---- payloads ----

struct Point {
    int16_t x, y;
};

inline std::vector<uint8_t> handleAnd(uint16_t h, uint8_t b) {
    std::vector<uint8_t> p;
    putBe16(p, h);
    p.push_back(b);
    return p;
}
inline std::vector<uint8_t> setVisibility(uint16_t h, bool on) { return handleAnd(h, on ? 1 : 0); }
inline std::vector<uint8_t> switchVisibility(uint16_t hide, uint16_t show) {
    std::vector<uint8_t> p;
    putBe16(p, hide);
    putBe16(p, show);
    return p;
}
inline std::vector<uint8_t> removeAllItems(uint16_t h) {
    std::vector<uint8_t> p;
    putBe16(p, h);
    return p;
}
inline std::vector<uint8_t> getSize(uint16_t h) { return removeAllItems(h); }
inline std::vector<uint8_t> setFillColor(uint16_t h, uint8_t colour) { return handleAnd(h, colour); }

// Empty when there are no points or more than kMaxPoints.
inline std::vector<uint8_t> addPolygon(uint16_t h, uint16_t index, PolyType type, const std::vector<Point>& pts) {
    std::vector<uint8_t> p;
    if (pts.empty() || pts.size() > kMaxPoints) return p;
    putBe16(p, h);
    putBe16(p, index);
    p.push_back(static_cast<uint8_t>(type));
    p.push_back(static_cast<uint8_t>(pts.size()));
    for (size_t i = 0; i < pts.size(); ++i) {
        putBe16(p, static_cast<uint16_t>(pts[i].x));
        putBe16(p, static_cast<uint16_t>(pts[i].y));
    }
    return p;
}

// The firmware's text format: handle, 00, UTF-16BE characters, 00 00. Empty when the text is longer than kMaxTextChars
// or not plain printable ASCII. An empty string is a valid call: it clears the text.
inline std::vector<uint8_t> setText(uint16_t h, const char* s) {
    std::vector<uint8_t> p;
    const size_t n = strlen(s);
    if (n > kMaxTextChars) return p;
    for (size_t i = 0; i < n; ++i)
        if (s[i] < 0x20 || s[i] >= 0x7f) return p;
    putBe16(p, h);
    p.push_back(0x00);
    for (size_t i = 0; i < n; ++i) {
        p.push_back(0);
        p.push_back(static_cast<uint8_t>(s[i]));
    }
    p.push_back(0);
    p.push_back(0);
    return p;
}

// UTF-8 -> text setText() takes and the cluster's font can show. Letters, digits and spaces are kept; Latin-1 and
// Latin Extended-A letters become their plain letters (German umlauts and sharp s the German way: ae, oe, ue, ss);
// the characters in `punct` are kept; a comma becomes a dot when commaToDot (a decimal comma in a distance); anything
// else becomes a space. Runs of spaces become one, the ends are trimmed, and the result is cut to maxChars.
// A comma made the 6WA show nothing at all.
inline std::string toClusterText(const std::string& utf8, const char* punct, size_t maxChars, bool commaToDot) {
    // U+00C0..U+017F, one entry per code point.
    static const char* const kLatin =
        "A|A|A|A|Ae|A|AE|C|E|E|E|E|I|I|I|I|D|N|O|O|O|O|Oe| |O|U|U|U|Ue|Y|Th|ss|"
        "a|a|a|a|ae|a|ae|c|e|e|e|e|i|i|i|i|d|n|o|o|o|o|oe| |o|u|u|u|ue|y|th|y|"
        "A|a|A|a|A|a|C|c|C|c|C|c|C|c|D|d|D|d|E|e|E|e|E|e|E|e|E|e|G|g|G|g|"
        "G|g|G|g|H|h|H|h|I|i|I|i|I|i|I|i|I|i|IJ|ij|J|j|K|k|k|L|l|L|l|L|l|L|"
        "l|L|l|N|n|N|n|N|n|n|N|n|O|o|O|o|O|o|OE|oe|R|r|R|r|R|r|S|s|S|s|S|s|"
        "S|s|T|t|T|t|T|t|U|u|U|u|U|u|U|u|U|u|U|u|W|w|Y|y|Y|Z|z|Z|z|Z|z|s";
    std::string raw;
    size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        size_t k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (i + k > utf8.size()) k = 1;
        uint32_t cp = k == 1 ? c : k == 2 ? (c & 0x1Fu) : k == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (size_t j = 1; j < k; ++j) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + j]) & 0x3Fu);
        if (k == 1 && c >= 0x80) cp = 0xFFFD;  // a stray continuation or bad lead byte
        i += k;
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9') || cp == ' ') {
            raw.push_back(static_cast<char>(cp));
        } else if (cp == ',' && commaToDot) {
            raw.push_back('.');
        } else if (cp > ' ' && cp < 0x7F && punct && strchr(punct, static_cast<int>(cp))) {
            raw.push_back(static_cast<char>(cp));
        } else if (cp >= 0xC0 && cp <= 0x17F) {
            const char* p = kLatin;
            for (uint32_t n = cp - 0xC0; n; --n) p = strchr(p, '|') + 1;
            while (*p && *p != '|') raw.push_back(*p++);
        } else {
            raw.push_back(' ');
        }
    }
    std::string out;
    for (size_t j = 0; j < raw.size(); ++j) {
        if (raw[j] == ' ' && (out.empty() || out[out.size() - 1] == ' ')) continue;
        out.push_back(raw[j]);
    }
    if (out.size() > maxChars) out.resize(maxChars);
    while (!out.empty() && out[out.size() - 1] == ' ') out.resize(out.size() - 1);
    return out;
}

// The ASCII text of a TextareaSetText payload (others as '?'); "" if it is not one.
inline std::string decodeText(const uint8_t* p, size_t n) {
    std::string s;
    if (n < 3 || p[2] != 0x00 || (n - 3) % 2) return s;
    for (size_t i = 3; i + 1 < n; i += 2) {
        const uint16_t ch = static_cast<uint16_t>((p[i] << 8) | p[i + 1]);
        if (ch == 0) break;
        s.push_back(ch >= 0x20 && ch < 0x7f ? static_cast<char>(ch) : '?');
    }
    return s;
}

// The shape each function we send must have. Anything else, IsVisible and circles included, is refused.
inline bool payloadShapeOk(uint16_t fkt, const std::vector<uint8_t>& p) {
    const size_t n = p.size();
    switch (fkt) {
        case kSetVisibility: return n == 3 && p[2] <= 1;
        case kRemoveAllItems:
        case kGetSize: return n == 2;
        case kSwitchVisibility: return n == 4;
        case kSetFillColor:
        case kSetLineColor:
        case kSetLineWidth:
        case kSetFillMode: return n == 3;
        case kTextareaSetText:
            return n >= 5 && (n - 3) % 2 == 0 && n <= 3 + 2 * (kMaxTextChars + 1) && p[2] == 0x00 && p[n - 1] == 0 &&
                   p[n - 2] == 0;
        case kAddPolygon:
            return n >= 10 && (p[4] == kPolyline || p[4] == kPolygon) && p[5] >= 1 && p[5] <= kMaxPoints &&
                   n == 6 + 4 * static_cast<size_t>(p[5]);
    }
    return false;
}

// ---- calls ----

struct Call {
    uint16_t fkt;
    std::vector<uint8_t> payload;
    Call() : fkt(0) {}
    Call(uint16_t f, const std::vector<uint8_t>& p) : fkt(f), payload(p) {}
    uint16_t handle() const { return payload.size() >= 2 ? rdBe16(&payload[0]) : 0; }
};

// Whether a call keeps every limit: the shape, its handle, and for SwitchVisibility the second handle too.
inline bool callOk(const Call& c, const HandleSet& allowed) {
    if (!payloadShapeOk(c.fkt, c.payload) || !allowed.has(c.handle())) return false;
    if (c.fkt == kSwitchVisibility && !allowed.has(rdBe16(&c.payload[2]))) return false;
    return true;
}

// One call for the log: "21D ImageAddPolygon h5 #0 polygon 7 pts x 70..130 y 20..120", "210 TextareaSetText h3 '250'".
inline std::string describeCall(const Call& c) {
    char b[160];
    const std::vector<uint8_t>& p = c.payload;
    const size_t n = p.size();
    const int k = snprintf(b, sizeof(b), "%03X %s h%u", c.fkt, fktName(c.fkt), c.handle());
    if (k < 0 || k >= static_cast<int>(sizeof(b))) return b;
    char* e = b + k;
    const size_t room = sizeof(b) - static_cast<size_t>(k);
    if (c.fkt == kSetVisibility && n == 3) {
        snprintf(e, room, " %s", p[2] ? "on" : "off");
    } else if (c.fkt == kSwitchVisibility && n == 4) {
        snprintf(e, room, " hidden, h%u shown", rdBe16(&p[2]));
    } else if (c.fkt == kTextareaSetText) {
        snprintf(e, room, " '%s'", decodeText(&p[0], n).c_str());
    } else if ((c.fkt == kSetFillColor || c.fkt == kSetLineColor || c.fkt == kSetLineWidth || c.fkt == kSetFillMode) &&
               n == 3) {
        snprintf(e, room, " %u", p[2]);
    } else if (c.fkt == kAddPolygon && n >= 6 && n == 6 + 4 * static_cast<size_t>(p[5]) && p[5]) {
        int x0 = 32767, y0 = 32767, x1 = -32768, y1 = -32768;
        for (size_t i = 6; i + 3 < n; i += 4) {
            const int x = static_cast<int16_t>(rdBe16(&p[i])), y = static_cast<int16_t>(rdBe16(&p[i + 2]));
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
        snprintf(e, room, " #%u %s %u pts x %d..%d y %d..%d", rdBe16(&p[2]), p[4] == kPolygon ? "polygon" : "polyline",
                 p[5], x0, x1, y0, y1);
    } else if (c.fkt != kRemoveAllItems && c.fkt != kGetSize) {
        snprintf(e, room, " %u bytes", static_cast<unsigned>(n));
    }
    return b;
}

}  // namespace hudterm
}  // namespace evo

#endif
