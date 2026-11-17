// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Adapted from NBT-AAD's cluster/hud6wa_profile.hpp and cluster/hud6wa_display.cpp: the 6WA's layout and the batches
// of calls that draw a maneuver, here driven by this project's phone messages (platform/kombi_phone.hpp).
//
// Android Auto's next maneuver on an F-series cluster's MOST HUDTerminal (platform/hudterm.hpp). Each event is one
// batch of calls, sent back to back:
//   maneuver    RemoveAllItems(hidden buffer), SetFillColor(hidden), the picture's polygons into it,
//               SwitchVisibility(shown -> hidden), RemoveAllItems(old buffer), [the three texts switched on, the first
//               time], street, distance and unit texts, the panel (frame) on
//   distance    the distance text, and the unit text when it changed
//   remind      the panel (frame) on again at the maneuver's main prompt (the driver may have dismissed it)
//   route       the junction view's route, double-buffered in 7/8
//   lanes       the phone's lanes, double-buffered in 9/10 (off unless kombi-6wa-lanes exists)
//   clear       panel off, both buffers emptied, the navigation's buffer left visible, texts emptied, street off: the
//               state the factory navigation leaves at the end of a route
// Every call is checked against the profile's handles and its exact shape before it leaves (hudterm::callOk): a batch
// with one bad call is not handed on at all, and the driver stops until the next Android Auto session.
//
// Where the batches go is a HudSink. For now there is only ShadowSink: each call is written to the service log and
// nothing reaches the car. A sink that sends over vMOST comes once the cluster's HUDTerminal is seen answering on the
// car (docs/ORIGINS.md).
#ifndef ENTRY_AAD_PLATFORM_HUD6WA_HPP
#define ENTRY_AAD_PLATFORM_HUD6WA_HPP

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "hudterm.hpp"
#include "hudterm_junction.hpp"
#include "hudterm_shapes.hpp"
#include "kombi_guide.hpp"
#include "kombi_phone.hpp"

namespace evo {
namespace hudterm {

// One cluster's HUDTerminal layout: which instance does what, which colour, where to draw.
struct HudProfile {
    const char* name;
    uint16_t frame;                   // the panel: on while we show guidance
    uint16_t street, distance, unit;  // texts
    uint16_t bufA, bufB;              // the double-buffered picture; bufB is the one the navigation leaves visible
    uint8_t fill;                     // fill colour index for the picture (the cluster's colour scheme picks the RGB)
    Rect glyph;                       // where the picture goes
    size_t streetChars;
    size_t distanceChars;
    const char* streetPunct;  // punctuation kept in a street name (toClusterText); anything else becomes a space
    // The junction view (NBT-AAD #162): the route overlay, double-buffered over the picture on the same coordinates
    // (routeB visible at rest; 0: none, the route then goes into the picture), its colours, and the canvas's bottom
    // edge (the roads run past it and the cluster clips them).
    uint16_t routeA, routeB;
    uint8_t edge, road, route;  // dark road edges, grey roads, the route (and the lanes to take)
    int canvasBottom;
    // The lanes (NBT-AAD #162): 9/10 are named LaneGuidance in the cluster's firmware, double-buffered like 5/6 (laneB
    // visible at rest). The factory navigation was never seen drawing in them, so the box is NBT-AAD's guess: the
    // free strip left of the picture. 0: no lanes.
    uint16_t laneA, laneB;
    Rect lanes;
};

// The 6WA, as NBT-AAD read it from the factory navigation's own traffic on an F20 and drew on it: 1 the guidance panel;
// 2 street, 3 distance, 4 unit; 5 and 6 the double-buffered picture, swapped with SwitchVisibility. Fill 4 is the
// factory navigation's colour for its picture. The glyph box is where their test arrow showed whole. Texts up to 32
// ASCII characters showed whole; a comma made a text show nothing.
// The junction view, from NBT-AAD #162: the factory's near view puts its roads in 6 and the route over them in 7
// ("DecreasingArrow"), so 7/8 is the route overlay; colours from their car's colour sweep (7 black, 3 light grey,
// 4 white); the canvas is clipped about 120..140 down (photos).
const HudProfile k6wa = {"6wa", 1, 2, 3, 4, 5,   6, 4,  {70, 20, 170, 120}, 32, 8, "-./",
                         7,     8, 7, 3, 4, 129, 9, 10, {4, 22, 68, 48}};

// The handles a profile lets the driver use: nothing else can be sent.
HandleSet handlesOf(const HudProfile& p);

struct Batch {
    std::string label;  // for the log
    std::vector<Call> calls;
    Batch() {}
    explicit Batch(const char* l) : label(l) {}
    void add(uint16_t fkt, const std::vector<uint8_t>& p) { calls.push_back(Call(fkt, p)); }
};

// Where a checked batch goes.
class HudSink {
public:
    virtual ~HudSink() {}
    // True when the batch was taken.
    virtual bool submit(const Batch& b) = 0;
    // Whether calls reach the car (the log says "not sent" otherwise).
    virtual bool live() const = 0;
};

// Writes each call to the service log; nothing is sent.
class ShadowSink : public HudSink {
public:
    bool submit(const Batch& b) override;
    bool live() const override { return false; }
};

// Sends each call to the cluster through a helper process (car-test --serve), one line per call: "FKT HEX".
// Started on the first batch; a helper that died is started again on the next one.
class PipeSink : public HudSink {
public:
    PipeSink(const std::string& program, const std::string& logFile, unsigned channel);
    ~PipeSink();
    bool submit(const Batch& b) override;
    bool live() const override { return true; }

private:
    bool open();
    std::string program_, logFile_;
    unsigned channel_;
    FILE* pipe_;
};

// The three texts the cluster shows for a maneuver, as the profile's font can take them.
struct HudTexts {
    std::string street, distance, unit;
    bool operator==(const HudTexts& o) const { return street == o.street && distance == o.distance && unit == o.unit; }
};
HudTexts hudTexts(const kombi::Maneuver& m, const HudProfile& p);
const char* hudUnitText(int displayUnit);  // "m", "km", "mi", "ft", "yd"; "" when unknown

// When a maneuver shows (NBT-AAD #157, tom-pryor), from the phone's seconds to it as the factory navigation does at
// its announcements:
//   kTime      shown once the seconds are <= warnS; at <= mainS (the main prompt) the panel is switched on again
//              (remind) and the junction view comes in. Off-ramps and forks are also due by distance (mwayWarnM,
//              mwayMainM), so an exit in a queue is still warned in time to change lanes. Once shown, a maneuver
//              stays until the next one, whatever traffic does to the seconds. A phone that sends no seconds gets
//              the distance rule.
//   kDistance  shown within GuideSettings::approachMiles, hidden again past approachMiles * hysteresis
//   kOff       shown at once (the menu's Directions at any distance)
struct HudApproach {
    enum Mode { kOff = 0, kDistance, kTime };
    Mode mode;
    double warnS, mainS;          // seconds; mainS 0: no main prompt
    double mwayWarnM, mwayMainM;  // metres, off-ramps and forks
    HudApproach() : mode(kTime), warnS(50), mainS(20), mwayWarnM(2000), mwayMainM(1000) {}
};
const char* approachName(HudApproach::Mode mode);
// kombi-6wa-approach's content: "time [warnS [mainS]]", "distance" or "off". False (out unchanged) for anything
// else. A main prompt not below its warning is turned off.
bool parseApproach(const char* text, HudApproach& out);

// The guidance state machine for a HUDTerminal cluster. Single-threaded; now in seconds of a monotonic clock.
//   new maneuver (type, road, exit angle or number), or a new step that only looks like the one shown
//                                            -> the maneuver batch
//   distance or unit text changes            -> a distance batch, at most every distanceEveryS
//   not due yet (HudApproach)                -> nothing; a different maneuver shown is cleared
//   main prompt of the maneuver shown        -> the remind batch, or the junction view
//   no guidance for graceS, or a DEPART at 0 m only
//                                            -> the clear batch
class Hud6wa {
public:
    Hud6wa(HudSink& sink, const HudProfile& profile, const kombi::GuideSettings& settings);

    // One tick. next: the phone's next maneuver; null when there is no guidance (whyNone says why, for the log).
    void step(const kombi::Maneuver* next, double now, const char* whyNone = "guidance ended");
    // Clears what we drew, when anything was handed on.
    void clear(const char* why);
    // The BMW navigation guides: forget what we drew without a clear, the factory navigation owns the panel.
    void yieldToFactory();

    bool showing() const { return showing_; }
    bool sentAny() const { return sentAny_; }
    // A batch broke a limit (our own data is wrong): nothing more is handed on until resume().
    bool stopped() const { return stopped_; }
    void resume() { stopped_ = false; }

    // The junction view (NBT-AAD #162): from nearMetres to the maneuver the arrow becomes the maneuver's roads with
    // the route over them, the route's tail shorter at each of kStages stages. On by default.
    enum { kStages = 4 };
    void setJunction(bool on, double nearMetres) {
        junction_ = on;
        nearMetres_ = nearMetres;
    }
    bool nearShown() const { return nearShown_; }
    int routeStage() const { return stageShown_; }

    void setApproach(const HudApproach& a) { approach_ = a; }
    const HudApproach& approach() const { return approach_; }
    // The phone's lanes in 9/10 (NBT-AAD: off until their place on the cluster is known).
    void setLanes(bool on) { lanes_ = on && p_.laneA && p_.laneB; }
    bool lanesOn() const { return lanes_; }
    // The time rule had to use the distance rule for lack of seconds since the last call: the caller logs it once per
    // Android Auto session, the sign of a phone that does not send them.
    bool takeNoSeconds() {
        const bool f = noSeconds_;
        noSeconds_ = false;
        return f;
    }

    // The batches, built without handing them on (tests).
    Batch showBatch(const HudTexts& t, const ShapeSpec& spec) const;
    Batch pictureBatch(const char* label, const Picture& pic, const HudTexts& t, bool emptyRoute) const;
    Batch routeBatch(const Picture& pic) const;
    Batch clearBatch() const;
    Batch remindBatch() const;
    Batch lanesBatch(const Picture& pic) const;
    uint16_t visibleBuffer() const { return visible_; }
    uint16_t visibleRoute() const { return routeVisible_; }

private:
    enum Prompt { kHidden = 0, kWarned, kMain };
    // m is the maneuver shown (same type, road and exits), and not a new step that only looks like it.
    bool isShown(const kombi::Maneuver& m) const { return showing_ && !renew_ && m.sameManeuver(shownTurn_); }
    bool approachHold(const kombi::Maneuver& m, double now);
    bool timeHold(const kombi::Maneuver& m, double now);
    bool distanceHold(const kombi::Maneuver& m);
    bool renewedStep(const kombi::Maneuver& m);
    void drawn(const kombi::Maneuver& m);
    bool fitDistance(kombi::Maneuver& m, double now);
    void sendRemind(const kombi::Maneuver& m);
    void sendLanes(const kombi::Maneuver& m);
    bool deliver(const Batch& b);
    void forget();
    bool routeInOverlay() const { return p_.routeA && p_.routeB; }
    JunctionColours colours() const;
    bool nearDue(const kombi::Maneuver& m, double* metres) const;
    void updateStage(const kombi::Maneuver& m);
    bool showPicture(const kombi::Maneuver& m, const HudTexts& t, bool isNew);
    void sendRoute();

    HudSink& sink_;
    const HudProfile p_;
    const kombi::GuideSettings s_;
    const HandleSet allowed_;

    bool showing_;
    bool sentAny_;
    bool stopped_;
    bool fresh_;        // the texts' visibility is not known: the next picture switches them on
    uint16_t visible_;  // the buffer on show
    kombi::Maneuver shownTurn_;
    HudTexts shown_;
    double distanceAt_;
    bool gap_;
    double gapAt_;
    // the junction view of the maneuver shown
    bool junction_;
    double nearMetres_;
    Layout layout_;
    bool hasLayout_;
    bool near_;       // due: from nearMetres until the next maneuver
    bool nearShown_;  // the picture on show is the junction view
    double nearAt_;   // metres to the maneuver when it became due: the stages divide it
    int stage_;       // the route stage due (0: none)
    int stageShown_;  // the route stage drawn (0: none)
    uint16_t routeVisible_;
    bool routeDirty_;  // something of ours may be in the route overlay
    // the approach
    HudApproach approach_;
    bool haveNext_;  // kTime: the next maneuver as last seen, shown or not, and how far it got
    kombi::Maneuver next_;
    double nextSince_;
    int prompt_;   // Prompt
    bool timed_;   // shown by the time rule (not its distance fallback): it stays until the next one
    bool remind_;  // the main prompt is due on the maneuver shown
    bool noSeconds_;
    // A next step that looks like the one shown (two right turns onto roads of the same name): its step body changed
    // since the draw and the distance jumped up from the nearest it got. renew_ holds until it is drawn or cleared.
    uint32_t drawnStepSeq_;
    bool hasMinM_;
    double minM_;
    bool renew_;
    bool depart_;      // the last step was a DEPART at 0 m (logged once per run of them)
    bool noDistance_;  // noDistanceAt_ valid: a maneuver without a distance yet
    double noDistanceAt_;
    // the lanes
    bool lanes_;
    uint16_t laneVisible_;
    bool lanesDirty_;  // something of ours may be in the lane buffers
    std::vector<kombi::PhoneLane> lanesShown_;
};

}  // namespace hudterm
}  // namespace evo

#endif
