// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// OPTION on the iDrive controller while Android Auto is on screen. Google Maps hides its buttons a few seconds into a
// route, and a car without a touch screen has no tap to bring them back: OPTION does it. What it sends is chosen by
// /var/local/data/AA/option-key (contract::kOptionKeyFile), read when the service starts. Pure logic
// (tests/test_option_key.cpp).
//   (no file), "nudge"  one knob step forward and one back: Android Auto shows its focus and the hidden controls
//                       without opening anything
//   "tap"               a touch at the centre of the shown picture (on the map: its buttons; elsewhere it may press
//                       what is there)
//   a number            that Android key code, if the input channel declares it (core/include/dhu_sdp.h)
//   "menu"              as before: OPTION is left to iDrive and opens the Android Auto menu of the iDrive app
#ifndef ENTRY_AAD_PLATFORM_OPTION_KEY_HPP
#define ENTRY_AAD_PLATFORM_OPTION_KEY_HPP

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace evo {

struct OptionKey {
    enum Mode { kNudge, kTap, kKey, kMenu };
    Mode mode;
    uint32_t keycode;  // kKey only
    OptionKey() : mode(kNudge), keycode(0) {}
    bool toAndroidAuto() const { return mode != kMenu; }
    const char* name() const { return mode == kNudge ? "nudge" : mode == kTap ? "tap" : mode == kKey ? "key" : "menu"; }
};

// The key codes the input channel declares (core/include/dhu_sdp.h, input service 7): the phone ignores the others.
inline bool declaredKeycode(uint32_t k) {
    return (k >= 1 && k <= 23) || k == 81 || k == 84 || k == 85 || k == 87 || k == 88 || k == 126 || k == 127 ||
           (k >= 268 && k <= 271) || (k >= 65536 && k <= 65543 && k != 65539);
}

// The file's first word; anything unknown, or a code the phone would ignore, gives the default (nudge).
inline OptionKey parseOptionKey(const char* text) {
    OptionKey o;
    if (!text) {
        return o;
    }
    while (*text && isspace(static_cast<unsigned char>(*text))) {
        ++text;
    }
    char word[16];
    size_t n = 0;
    while (text[n] && !isspace(static_cast<unsigned char>(text[n])) && n + 1 < sizeof(word)) {
        word[n] = static_cast<char>(tolower(static_cast<unsigned char>(text[n])));
        ++n;
    }
    word[n] = 0;
    if (!strcmp(word, "tap")) {
        o.mode = OptionKey::kTap;
    } else if (!strcmp(word, "menu")) {
        o.mode = OptionKey::kMenu;
    } else if (n && isdigit(static_cast<unsigned char>(word[0]))) {
        char* end = nullptr;
        const unsigned long k = strtoul(word, &end, 10);
        if (end && !*end && k <= 0xFFFFFFFFul && declaredKeycode(static_cast<uint32_t>(k)) &&
            k != 65536) {  // the knob is relative steps, not a key
            o.mode = OptionKey::kKey;
            o.keycode = static_cast<uint32_t>(k);
        }
    }
    return o;
}

}  // namespace evo

#endif  // ENTRY_AAD_PLATFORM_OPTION_KEY_HPP
