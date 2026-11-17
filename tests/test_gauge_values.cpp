// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tests hmiapp/gauge_values.hpp and platform/gauge_catalogue.hpp: CDS values read as AAIdrive reads them, OBD
// formulas, bar levels, value texts and the owner's choice of gauges.
#include <stdio.h>
#include <string.h>

#include "gauge_values.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

using namespace evo::gauges;
using evo::catalogue::obdValue;

static const Entry* find(const char* title) {
    for (unsigned i = 0; i < kEntryCount; ++i) {
        if (!strcmp(kEntries[i].title, title)) {
            return &kEntries[i];
        }
    }
    return nullptr;
}

int main() {
    const Entry* water = find("Water");
    const Entry* oil = find("Oil");
    const Entry* rpm = find("RPM");
    CHECK(water && oil && rpm && water->on && rpm->on);
    double v = 0;
    const char* temps = "{\"temperature\":{\"engine\":64,\"oil\":85}}";  // as logged on the car, 2026-11-14
    CHECK(readField(temps, water->key, water->invalid, &v) && v == 64);
    CHECK(readField(temps, oil->key, oil->invalid, &v) && v == 85);
    CHECK(!readField("{\"temperature\":{\"engine\":255,\"oil\":255}}", "engine", 255, &v));
    CHECK(!readField("{\"engine\":\"x\"}", "engine", 255, &v));
    CHECK(!readField(nullptr, "engine", 255, &v));
    CHECK(readField("{\"RPMSpeed\":812}", rpm->key, rpm->invalid, &v) && v == 812);

    // OBD formulas
    const Entry* coolant = find("Coolant (OBD)");
    const Entry* rpmObd = find("RPM (OBD)");
    const Entry* trim = find("Short fuel trim");
    const Entry* volts = find("Module voltage");
    const unsigned char c[1] = {130}, r[2] = {0x0c, 0x80}, t[1] = {128}, mv[2] = {0x36, 0xb0};
    CHECK(coolant && obdValue(*coolant, c, 1, &v) && v == 90);
    CHECK(rpmObd && obdValue(*rpmObd, r, 2, &v) && v == 800);
    CHECK(trim && obdValue(*trim, t, 1, &v) && v == 0);
    CHECK(volts && obdValue(*volts, mv, 2, &v) && v > 13.99 && v < 14.01);
    CHECK(!obdValue(*rpmObd, r, 1, &v));  // too short
    CHECK(!obdValue(*water, r, 2, &v));   // not OBD

    CHECK(barLevel(*water, 20) == 0 && barLevel(*water, 130) == kBarLevels - 1 && barLevel(*water, 85) == 12);

    char text[32];
    valueText(*water, true, 89, text, sizeof(text));
    CHECK(!strcmp(text, "89 C"));
    valueText(*oil, false, 0, text, sizeof(text));
    CHECK(!strcmp(text, "--"));

    bool on[kEntryCount];
    parseChoice(nullptr, on);
    CHECK(on[0] && on[1] && !on[kEntryCount - 1]);  // defaults
    parseChoice("01", on);
    CHECK(!on[0] && on[1] && on[2] == kEntries[2].on);  // the file's choice, defaults after its end
    char line[128];
    formatChoice(on, line, sizeof(line));
    CHECK(strlen(line) == kEntryCount && line[0] == '0' && line[1] == '1');

    // Every entry has a usable range and a title short enough for a card.
    for (unsigned i = 0; i < kEntryCount; ++i) {
        CHECK(kEntries[i].hi > kEntries[i].lo && strlen(kEntries[i].title) <= 22);
    }
    if (g_failures) {
        printf("FAIL gauge values: %d check(s)\n", g_failures);
        return 1;
    }
    printf("PASS gauge values: %u catalogue entries, CDS fields, OBD formulas, bars, texts, the owner's choice\n",
           kEntryCount);
    return 0;
}
