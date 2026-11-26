// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// hmiapp/popup_peek.hpp: when Android Auto steps aside for an iDrive popup.
#include <assert.h>
#include <stdio.h>

#include "../hmiapp/popup_peek.hpp"

int main() {
    {  // a popup: one start, no repeat, one end
        PopupPeek p;
        assert(p.step(false) == PopupPeek::kNone && !p.active());
        assert(p.step(true) == PopupPeek::kStart && p.active());
        assert(p.step(true) == PopupPeek::kNone);
        assert(p.step(false) == PopupPeek::kEnd && !p.active());
        assert(p.step(false) == PopupPeek::kNone);
    }
    {  // a long popup does not take the picture again once its time is over; a new span does
        PopupPeek p;
        assert(p.step(true) == PopupPeek::kStart);
        for (long t = 301; t < 370; ++t) {
            assert(p.step(true) == PopupPeek::kNone);
        }
        assert(p.step(false) == PopupPeek::kEnd);
        assert(p.step(true) == PopupPeek::kStart);
    }
    {  // the driving mode popup: only its setting read off, with no coding forcing it, means none (#28)
        assert(!driveModePopupExpected(0, 0));
        assert(driveModePopupExpected(1, 0) && driveModePopupExpected(1, 1) && driveModePopupExpected(0, 1));
        assert(driveModePopupExpected(-1, 0) && driveModePopupExpected(-1, -1) && driveModePopupExpected(0, -1));
        assert(driveModePopupExpected(1, -1) && driveModePopupExpected(-1, 1));
    }
    puts(
        "PASS iDrive popups: one step aside per popup, driving mode display left out, no repeat, driving mode popup "
        "turned off in iDrive");
    return 0;
}
