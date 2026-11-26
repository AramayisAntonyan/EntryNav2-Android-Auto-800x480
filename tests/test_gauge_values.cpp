// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tests hmiapp/gauge_values.hpp and platform/gauge_catalogue.hpp: CDS values read as AAIdrive reads them, OBD
// formulas, bar levels, value texts, the owner's choice of gauges and their looks, and the dashboard layout
// (hmiapp/gauge_layout.hpp).
#include <stdio.h>
#include <string.h>

#include "gauge_layout.hpp"
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
    CHECK(water && oil && rpm && water->on && !rpm->on);  // the first dashboard: Water, not RPM
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
    CHECK(on[0] && !on[1]);  // defaults: Water yes, Oil no
    for (unsigned i = 0; i < kEntryCount; ++i) {
        CHECK(on[i] == kEntries[i].on);
    }
    parseChoice("01", on);
    CHECK(!on[0] && on[1] && on[2] == kEntries[2].on);  // the file's choice, defaults after its end
    char line[128];
    formatChoice(on, line, sizeof(line));
    CHECK(strlen(line) == kEntryCount && line[0] == '0' && line[1] == '1');

    // The fuel quality as Armenia's grades.
    const Entry* fuel = find("Fuel quality");
    CHECK(fuel != nullptr);
    if (fuel) {
        char t[32];
        valueText(*fuel, true, 92.3, t, sizeof(t));
        CHECK(!strcmp(t, "Regular 92.3"));
        valueText(*fuel, true, 95.0, t, sizeof(t));
        CHECK(!strcmp(t, "Premium 95.0"));
        valueText(*fuel, true, 97.1, t, sizeof(t));
        CHECK(!strcmp(t, "Super 97.1"));
        valueText(*fuel, true, 98.0, t, sizeof(t));
        CHECK(!strcmp(t, "Super+ 98"));
    }

    // Every entry has a usable range and a title short enough for a card.
    for (unsigned i = 0; i < kEntryCount; ++i) {
        CHECK(kEntries[i].hi > kEntries[i].lo && strlen(kEntries[i].title) <= 22);
    }
    // Looks: the file's digits, the entry's default past its end or for a digit that is no look.
    unsigned looks[kEntryCount];
    parseLooks(nullptr, looks);
    CHECK(looks[2] == kEntries[2].look && looks[0] == kEntries[0].look);
    parseLooks("49", looks);
    CHECK(looks[0] == kLookTall && looks[1] == kEntries[1].look && looks[3] == kEntries[3].look);
    formatLooks(looks, line, sizeof(line));
    CHECK(strlen(line) == kEntryCount && line[0] == '4');
    CHECK(!strcmp(find("RPM") ? kLookNames[find("RPM")->look] : "", "Wide dial"));
    parseLooks("6", looks);
    CHECK(looks[0] == kLookWideArc && lookWidth(kLookWideArc) == 2 && lookHeight(kLookArc) == 1);
    {
        unsigned look = kLookCard, seen = 0;  // a press at a time: every look once, then back to the card
        do {
            look = nextLook(look);
            ++seen;
        } while (look != kLookCard && seen < 20);
        CHECK(seen == kLooks && nextLook(kLookCard) == kLookDial && nextLook(kLookDial) == kLookArc);
    }
    for (unsigned i = 0; i < kEntryCount; ++i) {
        CHECK(kEntries[i].look < kLooks);
    }
    CHECK(levelOf(*find("Water"), 40, 41) == 0 && levelOf(*find("Water"), 130, 41) == 40);
    CHECK(levelOf(*find("Water"), 85, 41) == 20 && levelOf(*find("Water"), 500, 41) == 40);

    // Layout: 4 x 2 cells a page, first free place in order.
    {
        Layout<3> l;
        const unsigned e[] = {0, 1, 2, 3};
        const unsigned k[] = {kLookDial, kLookDial, kLookWideDial, kLookCard};  // the default four
        layOut(e, k, 4, &l);
        CHECK(l.entry[0] == 0 && l.entry[1] == 1 && l.entry[2] == 2 && l.look[2] == kLookWideDial);
        CHECK(l.entry[3] == kEntryCount && l.entry[4] == 3 && l.pages == 1 && l.dropped == 0);
    }
    {
        Layout<3> l;  // a wide gauge after three cards skips to the next row; a later card fills the gap
        const unsigned e[] = {0, 1, 2, 3, 4};
        const unsigned k[] = {kLookCard, kLookCard, kLookCard, kLookWide, kLookCard};
        layOut(e, k, 5, &l);
        CHECK(l.entry[3] == 4 && l.entry[4] == 3 && l.look[4] == kLookWide && l.entry[6] == kEntryCount);
    }
    {
        Layout<3> l;  // a tall gauge takes both rows of its column; past row 0 it goes to the next page
        const unsigned e[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        const unsigned k[] = {kLookTall, kLookCard, kLookCard, kLookCard, kLookCard,
                              kLookCard, kLookCard, kLookTall, kLookCard};
        layOut(e, k, 9, &l);
        CHECK(l.entry[0] == 0 && l.entry[4] == kEntryCount && l.entry[5] == 4 && l.entry[7] == 6);
        CHECK(l.entry[8] == 7 && l.look[8] == kLookTall && l.entry[9] == 8 && l.pages == 2);
    }
    {
        Layout<1> l;  // no room: counted, not placed
        const unsigned e[] = {0, 1, 2, 3, 4};
        const unsigned k[] = {kLookWideDial, kLookWideDial, kLookWideDial, kLookWideDial, kLookCard};
        layOut(e, k, 5, &l);
        CHECK(l.dropped == 1 && l.entry[0] == 0 && l.entry[2] == 1 && l.entry[4] == 2 && l.entry[6] == 3);
    }

    {
        Layout<3> l;  // a chosen place first; the others fill around it; a place that does not fit is ignored
        const unsigned e[] = {0, 1, 2, 3};
        const unsigned k[] = {kLookCard, kLookCard, kLookWide, kLookTall};
        const unsigned p[] = {kNoPlace, 9, 3, 2};  // the wide one cannot start in the last column
        layOut(e, k, 4, &l, p);
        // the tall bar takes cells 2 and 6; the wide one, its place ignored, finds the bottom row's first two
        CHECK(l.entry[9] == 1 && l.entry[2] == 3 && l.look[2] == kLookTall && l.entry[0] == 0);
        CHECK(l.entry[4] == 2 && l.look[4] == kLookWide && l.dropped == 0 && l.pages == 2);
    }
    {
        Layout<1> l;  // two gauges want one place: the first in order gets it
        const unsigned e[] = {0, 1};
        const unsigned k[] = {kLookCard, kLookCard};
        const unsigned p[] = {5, 5};
        layOut(e, k, 2, &l, p);
        CHECK(l.entry[5] == 0 && l.entry[0] == 1);
    }
    {
        unsigned places[kEntryCount];
        parsePlaces("-C-", places);
        CHECK(places[0] == kNoPlace && places[1] == 2 && places[2] == kNoPlace && places[3] == kNoPlace);
        char line[kEntryCount + 1];
        formatPlaces(places, line, sizeof(line));
        CHECK(line[0] == '-' && line[1] == 'C' && strlen(line) == kEntryCount);
        char name[48];
        placeName(kLookWide, 12, name, sizeof(name));
        CHECK(!strcmp(name, "Dashboard 2, bottom 1-2"));
        placeName(kLookTall, 3, name, sizeof(name));
        CHECK(!strcmp(name, "Dashboard 1, column 4"));
        CHECK(startFits(kLookWide, 2) && !startFits(kLookWide, 3) && !startFits(kLookTall, 4));
    }
    {  // no settings files: the owner's first dashboard, all eight cells of page 1, nothing else ticked
        bool on[kEntryCount];
        unsigned looks[kEntryCount], places[kEntryCount], entries[kEntryCount], lks[kEntryCount], pl[kEntryCount];
        parseChoice(nullptr, on);
        parseLooks(nullptr, looks);
        parsePlaces(nullptr, places);
        unsigned n = 0;
        for (unsigned i = 0; i < kEntryCount; ++i) {
            if (on[i]) {
                entries[n] = i;
                lks[n] = looks[i];
                pl[n] = places[i];
                ++n;
            }
        }
        CHECK(n == 7);
        Layout<3> l;
        layOut(entries, lks, n, &l, pl);
        const char* const kWant[8] = {"Boost",          "Boost",        "Water",      "Intake air (OBD)",
                                      "Module voltage", "Fuel quality", "Fuel level", "Oil pressure"};
        for (unsigned c = 0; c < 8; ++c) {
            const unsigned e = l.entry[c] < kEntryCount ? l.entry[c] : entries[0];
            CHECK(c == 1 || (l.entry[c] < kEntryCount && !strcmp(kEntries[e].title, kWant[c])));
        }
        CHECK(l.look[0] == kLookWideArc && l.look[5] == kLookArc && l.look[2] == kLookCard && l.pages == 1);
        // A saved layout is kept as it is: a file that places nothing places nothing.
        parsePlaces("", places);
        for (unsigned i = 0; i < kEntryCount; ++i) {
            CHECK(places[i] == kNoPlace);
        }
    }

    if (g_failures) {
        printf("FAIL gauge values: %d check(s)\n", g_failures);
        return 1;
    }
    printf(
        "PASS gauge values: %u catalogue entries, CDS fields, OBD formulas, bars, texts, choice, looks, layout, "
        "places\n",
        kEntryCount);
    return 0;
}
