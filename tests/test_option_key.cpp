// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tests platform/option_key.hpp: what OPTION sends to Android Auto, from /var/local/data/AA/option-key.
#include <stdio.h>

#include "option_key.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

using evo::OptionKey;
using evo::parseOptionKey;

int main() {
    CHECK(parseOptionKey(nullptr).mode == OptionKey::kNudge);  // no file
    CHECK(parseOptionKey("").mode == OptionKey::kNudge);
    CHECK(parseOptionKey("nudge\n").mode == OptionKey::kNudge);
    CHECK(parseOptionKey("  Tap \r\n").mode == OptionKey::kTap);
    CHECK(parseOptionKey("menu").mode == OptionKey::kMenu && !parseOptionKey("menu").toAndroidAuto());
    CHECK(parseOptionKey("tap").toAndroidAuto() && parseOptionKey("").toAndroidAuto());
    const OptionKey search = parseOptionKey("84\n");
    CHECK(search.mode == OptionKey::kKey && search.keycode == 84);
    CHECK(parseOptionKey("65541").keycode == 65541);
    // Codes the input channel does not declare, the knob's relative code, and junk: the default.
    CHECK(parseOptionKey("82").mode == OptionKey::kNudge);  // MENU: not declared, the phone would ignore it
    CHECK(parseOptionKey("65536").mode == OptionKey::kNudge);
    CHECK(parseOptionKey("65539").mode == OptionKey::kNudge);
    CHECK(parseOptionKey("12x").mode == OptionKey::kNudge);
    CHECK(parseOptionKey("99999999999999999999").mode == OptionKey::kNudge);
    CHECK(parseOptionKey("something").mode == OptionKey::kNudge);
    CHECK(!strcmp(parseOptionKey("tap").name(), "tap"));
    if (g_failures) {
        printf("FAIL option key: %d check(s)\n", g_failures);
        return 1;
    }
    puts("PASS option key: nudge by default, tap, declared key codes only, menu as before");
    return 0;
}
