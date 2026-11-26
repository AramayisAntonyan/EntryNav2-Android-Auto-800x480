// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The dial faces and bar tracks of a scale the owner sets by hand (Gauge ranges: min and max), drawn on the head unit:
// a port of hmiapp/gauge_art.py (Dial.face, Bar.track) that draws the same pictures as the PC does for the
// catalogue's scales (tests/test_gauge_draw.cpp compares them). The figures and icons come from
// CarGauges_Glyphs.bin (hmiapp/make_db.py write_glyphs). Pure logic and file output: no iDrive calls.
#ifndef ENTRY_AAD_HMIAPP_GAUGE_DRAW_HPP
#define ENTRY_AAD_HMIAPP_GAUGE_DRAW_HPP

#include <stddef.h>

namespace evo {
namespace gauges {
namespace draw {

// The pictures a scale has: gauge_ids.hpp's faces and tracks, in this order.
enum Kind { kFace, kWideFace, kWideTrack, kTallTrack, kArcFace, kWideArcFace, kKinds };

struct Scale {
    double lo, hi;
    bool red;        // a red end
    double redFrom;  // where it starts
    bool alert;      // the gauge is past its alert: what is white is drawn red (the flash's other half)
};

// An RGBA picture (straight alpha).
struct Picture {
    int w, h;
    unsigned char* rgba;  // w * h * 4, owned
    Picture() : w(0), h(0), rgba(nullptr) {}
    ~Picture();
    Picture(const Picture&) = delete;
    Picture& operator=(const Picture&) = delete;
};

// Loads CarGauges_Glyphs.bin; false when missing or malformed (then nothing can be drawn).
bool loadSheets(const char* path);
bool sheetsLoaded();

// Draws kind for the scale; the gauge's icon from the sheets (entry: catalogue index), its name only without an icon.
bool render(Kind kind, const Scale& scale, unsigned entry, const char* title, const char* unit, Picture* out);

// A PNG of the picture (stored, not compressed: no zlib on the unit's side needed).
bool writePng(const Picture& p, const char* path);

}  // namespace draw
}  // namespace gauges
}  // namespace evo

#endif
