// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Draws faces and tracks with hmiapp/gauge_draw.cpp for tests/test_gauge_draw.py, which compares them with what
// hmiapp/gauge_art.py draws for the same scales.
//   gauge_draw_dump <CarGauges_Glyphs.bin> <out dir> <kind> <entry> <lo> <hi> <red 0/1> <redFrom> <title> <unit> <name>
//                   [alert 0/1]  (1: the gauge past its alert, every white stroke red)
#include <stdio.h>
#include <stdlib.h>

#include "../hmiapp/gauge_draw.hpp"

using namespace evo::gauges::draw;

int main(int argc, char** argv) {
    if (argc != 12 && argc != 13) {
        fprintf(stderr, "usage: see the file's head\n");
        return 2;
    }
    if (!loadSheets(argv[1])) {
        fprintf(stderr, "sheets not loaded: %s\n", argv[1]);
        return 1;
    }
    Scale s = {atof(argv[5]), atof(argv[6]), atoi(argv[7]) != 0, atof(argv[8]), argc == 13 && atoi(argv[12]) != 0};
    Picture p;
    if (!render(static_cast<Kind>(atoi(argv[3])), s, static_cast<unsigned>(atoi(argv[4])), argv[9], argv[10], &p)) {
        fprintf(stderr, "not drawn\n");
        return 1;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", argv[2], argv[11]);
    return writePng(p, path) ? 0 : 1;
}
