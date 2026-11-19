// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Where the ticked gauges go on the dashboard pages, each with its look: a card, a dial (a half circle over its pivot,
// as BMW's clusters draw their gauges), an arc (the F-series fuel gauge's shallow arc under its pivot), a wide bar, a
// wide dial or a wide arc over two cells across, a tall bar over two cells high (as ID6 apps' BoostDeck shows its
// vertical bar). A page is 4 x 2 cells; the gauges fill it in order, each in the first
// free place it fits (a later small gauge may fill a gap a big one left). Pure logic (tests/test_gauge_values.cpp).
#ifndef ENTRY_AAD_HMIAPP_GAUGE_LAYOUT_HPP
#define ENTRY_AAD_HMIAPP_GAUGE_LAYOUT_HPP

#include "../platform/gauge_catalogue.hpp"

namespace evo {
namespace gauges {

// The numbers are what gauges-style stores: new looks only at the end.
enum Look { kLookCard, kLookDial, kLookWide, kLookWideDial, kLookTall, kLookArc, kLookWideArc, kLooks };
const char* const kLookNames[kLooks] = {"Card", "Dial", "Wide bar", "Wide dial", "Tall bar", "Arc", "Wide arc"};

// The order Gauge styles goes through them, a press at a time: one cell, then two across, then two high.
inline unsigned nextLook(unsigned look) {
    static const unsigned kOrder[kLooks] = {kLookCard,     kLookDial,    kLookArc, kLookWide,
                                            kLookWideDial, kLookWideArc, kLookTall};
    for (unsigned i = 0; i < kLooks; ++i) {
        if (kOrder[i] == look) {
            return kOrder[(i + 1) % kLooks];
        }
    }
    return kLookCard;
}

const unsigned kCols = 4;
const unsigned kRows = 2;
const unsigned kCellsPerPage = kCols * kRows;

inline unsigned lookWidth(unsigned look) {
    return look == kLookWide || look == kLookWideDial || look == kLookWideArc ? 2 : 1;
}
inline unsigned lookHeight(unsigned look) {
    return look == kLookTall ? 2 : 1;
}

// A gauge's cell: page * kCellsPerPage + row * kCols + column. Each gauge is kept at its first (top-left) cell.
template <unsigned Pages>
struct Layout {
    static const unsigned kCells = Pages * kCellsPerPage;
    unsigned entry[kCells];  // the entry whose gauge starts in this cell; catalogue::kEntryCount: none
    unsigned look[kCells];   // its look
    unsigned dropped;        // gauges that found no room
    unsigned pages;          // pages with at least one gauge
};

// Lays out n gauges (entries[i] with looks[i]) in order.
template <unsigned Pages>
void layOut(const unsigned* entries, const unsigned* looks, unsigned n, Layout<Pages>* out) {
    const unsigned cells = Layout<Pages>::kCells;
    bool used[cells];
    for (unsigned c = 0; c < cells; ++c) {
        used[c] = false;
        out->entry[c] = catalogue::kEntryCount;
        out->look[c] = kLookCard;
    }
    out->dropped = 0;
    out->pages = 0;
    for (unsigned i = 0; i < n; ++i) {
        const unsigned look = looks[i] < kLooks ? looks[i] : static_cast<unsigned>(kLookCard);
        const unsigned w = lookWidth(look), h = lookHeight(look);
        bool placed = false;
        for (unsigned c = 0; c < cells && !placed; ++c) {
            const unsigned page = c / kCellsPerPage, row = c % kCellsPerPage / kCols, col = c % kCols;
            if (col + w > kCols || row + h > kRows) {
                continue;
            }
            bool free = true;
            for (unsigned dy = 0; dy < h; ++dy) {
                for (unsigned dx = 0; dx < w; ++dx) {
                    free = free && !used[c + dy * kCols + dx];
                }
            }
            if (!free) {
                continue;
            }
            for (unsigned dy = 0; dy < h; ++dy) {
                for (unsigned dx = 0; dx < w; ++dx) {
                    used[c + dy * kCols + dx] = true;
                }
            }
            out->entry[c] = entries[i];
            out->look[c] = look;
            out->pages = page + 1 > out->pages ? page + 1 : out->pages;
            placed = true;
        }
        out->dropped += placed ? 0 : 1;
    }
}

// The looks the owner picked, one digit per catalogue entry ("0130..."); the entry's default (Entry::look) for a
// missing file, an entry past its end or a digit that is no look.
inline void parseLooks(const char* text, unsigned* looks) {
    unsigned n = 0;
    while (text && text[n]) {
        ++n;
    }
    for (unsigned i = 0; i < catalogue::kEntryCount; ++i) {
        const bool given = i < n && text[i] >= '0' && text[i] < static_cast<char>('0' + kLooks);
        looks[i] = given ? static_cast<unsigned>(text[i] - '0') : catalogue::kEntries[i].look;
    }
}
inline void formatLooks(const unsigned* looks, char* out, unsigned cap) {
    unsigned i = 0;
    for (; i < catalogue::kEntryCount && i + 1 < cap; ++i) {
        out[i] = static_cast<char>('0' + (looks[i] < kLooks ? looks[i] : 0));
    }
    out[i] = 0;
}

// 0 .. levels - 1 over the entry's range (a dial's needle, a big bar).
inline unsigned levelOf(const catalogue::Entry& e, double v, unsigned levels) {
    if (!(v > e.lo)) {
        return 0;
    }
    if (v >= e.hi) {
        return levels - 1;
    }
    const unsigned level = static_cast<unsigned>((v - e.lo) / (e.hi - e.lo) * (levels - 1) + 0.5);
    return level > levels - 1 ? levels - 1 : level;
}

}  // namespace gauges
}  // namespace evo

#endif  // ENTRY_AAD_HMIAPP_GAUGE_LAYOUT_HPP
