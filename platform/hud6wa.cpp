// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Adapted from NBT-AAD's cluster/hud6wa_display.cpp, with its junction view and lanes (PR #162, tom-pryor), and the
// rules of its guidance state machine (cluster/nav_core.cpp): the time approach, the main-prompt reminder, a renewed
// step, no DEPART at 0 m, the distance clean-up.
#include "hud6wa.hpp"

#include <stdio.h>
#include <string.h>

#include "evo_log.hpp"
#include "kombi_display.hpp"

namespace evo {
namespace hudterm {

namespace {

const char* const kTag = "Hud6wa";
const double kMetresPerMile = 1609.344;
const double kStalePosS = 3.0;  // a new maneuver waits at most this long for a position of its own
const double kRenewM = 100.0;   // a jump up by more than this on a changed step body: a new step that looks the same

// Off-ramps and forks (Maneuver TYPE_OFF_RAMP_* 21..24, TYPE_FORK_* 25..26): also due by distance.
bool motorwayTurn(int type) {
    return type >= 21 && type <= 26;
}

// Maneuver TYPE_DEPART at 0 m: what Maps sends for about a second when guidance starts and after each reroute, before
// the first real turn. Nothing to show (a straight arrow at 0 m). One with no distance yet waits like any other.
bool departHere(const kombi::Maneuver& m) {
    double metres = 0;
    return m.type == 1 && kombi::metresToManeuver(m, metres) && metres < 1.0;
}

bool sameLanes(const std::vector<kombi::PhoneLane>& a, const std::vector<kombi::PhoneLane>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) return false;
        for (size_t k = 0; k < a[i].size(); ++k)
            if (a[i][k].shape != b[i][k].shape || a[i][k].highlighted != b[i][k].highlighted) return false;
    }
    return true;
}

long long secondsForLog(const kombi::Maneuver& m) {
    return m.hasSeconds ? static_cast<long long>(m.seconds) : -1;
}

// A picture's items as calls into buffer h: a fill colour change before each run of one colour.
void addItems(Batch& b, uint16_t h, const Picture& pic) {
    int fill = -1;
    for (size_t i = 0; i < pic.size(); ++i) {
        if (pic[i].colour != fill) {
            fill = pic[i].colour;
            b.add(kSetFillColor, setFillColor(h, pic[i].colour));
        }
        b.add(kAddPolygon, addPolygon(h, static_cast<uint16_t>(i), pic[i].poly.type, pic[i].poly.pts));
    }
}

// The maneuver's arrow (the picture before the junction view), in the profile's fill colour.
Picture arrowPicture(const ShapeSpec& spec, const HudProfile& p) {
    Shape design, fitted;
    drawShape(spec, design);
    fitShape(design, p.glyph, fitted);
    Picture pic;
    for (size_t i = 0; i < fitted.size(); ++i) {
        Item it;
        it.colour = p.fill;
        it.poly = fitted[i];
        pic.push_back(it);
    }
    return pic;
}

}  // namespace

HandleSet handlesOf(const HudProfile& p) {
    HandleSet h;
    h.allow(p.frame);
    h.allow(p.street);
    h.allow(p.distance);
    h.allow(p.unit);
    h.allow(p.bufA);
    h.allow(p.bufB);
    if (p.routeA && p.routeB) {
        h.allow(p.routeA);
        h.allow(p.routeB);
    }
    if (p.laneA && p.laneB) {
        h.allow(p.laneA);
        h.allow(p.laneB);
    }
    return h;
}

const char* approachName(HudApproach::Mode mode) {
    return mode == HudApproach::kTime ? "time" : mode == HudApproach::kDistance ? "distance" : "off";
}

bool parseApproach(const char* text, HudApproach& out) {
    char word[16] = "";
    double warn = -1, main = -1;
    const int n = text ? sscanf(text, "%15s %lf %lf", word, &warn, &main) : 0;
    if (n < 1) return false;
    HudApproach a = out;
    if (!strcmp(word, "off")) {
        a.mode = HudApproach::kOff;
    } else if (!strcmp(word, "distance")) {
        a.mode = HudApproach::kDistance;
    } else if (!strcmp(word, "time")) {
        a.mode = HudApproach::kTime;
        if (n >= 2) {
            if (warn <= 0 || warn > 600) return false;
            a.warnS = warn;
        }
        if (n >= 3) {
            if (main < 0 || main > 600) return false;
            a.mainS = main;
        }
        if (a.mainS >= a.warnS) a.mainS = 0;  // a main prompt not below its warning: none
    } else {
        return false;
    }
    out = a;
    return true;
}

bool ShadowSink::submit(const Batch& b) {
    for (size_t i = 0; i < b.calls.size(); ++i) {
        ELOG(kTag, "  [shadow] %s", describeCall(b.calls[i]).c_str());
    }
    return true;
}

PipeSink::PipeSink(const std::string& program, const std::string& logFile, unsigned channel)
    : program_(program), logFile_(logFile), channel_(channel), pipe_(nullptr) {}

PipeSink::~PipeSink() {
    if (pipe_) {
        pclose(pipe_);
    }
}

bool PipeSink::open() {
    if (pipe_) {
        return true;
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "LD_LIBRARY_PATH=/usr/local/lib:/opt/hmi/lib exec %s --serve 0101 --channel %u >>%s 2>&1",
             program_.c_str(), channel_, logFile_.c_str());
    pipe_ = popen(cmd, "w");
    ELOG(kTag, "cluster sender %s: %s", pipe_ ? "started" : "NOT started", cmd);
    return pipe_ != nullptr;
}

bool PipeSink::submit(const Batch& b) {
    if (!open()) {
        return false;
    }
    for (size_t i = 0; i < b.calls.size(); ++i) {
        const Call& c = b.calls[i];
        std::string line;
        char hex[8];  // a function number takes 3 hex digits, a payload byte 2
        snprintf(hex, sizeof(hex), "%03X", static_cast<unsigned>(c.fkt) & 0xFFFu);
        line = hex;
        line += ' ';
        for (size_t k = 0; k < c.payload.size(); ++k) {
            snprintf(hex, sizeof(hex), "%02X", c.payload[k]);
            line += hex;
        }
        if (c.payload.empty()) {
            line += '-';
        }
        line += '\n';
        ELOG(kTag, "  [sent] %s", describeCall(c).c_str());
        if (fputs(line.c_str(), pipe_) == EOF) {
            ELOG(kTag, "cluster sender gone: started again on the next batch");
            pclose(pipe_);
            pipe_ = nullptr;
            return false;
        }
    }
    fflush(pipe_);
    return true;
}

const char* hudUnitText(int displayUnit) {
    switch (displayUnit) {
        case kombi::kUnitMetres: return "m";
        case kombi::kUnitKilometres:
        case kombi::kUnitKilometresTenths: return "km";
        case kombi::kUnitMiles:
        case kombi::kUnitMilesTenths: return "mi";
        case kombi::kUnitFeet: return "ft";
        case kombi::kUnitYards: return "yd";
        default: return "";
    }
}

// No distance yet shows nothing, rather than a character the cluster's font may not have.
HudTexts hudTexts(const kombi::Maneuver& m, const HudProfile& p) {
    HudTexts t;
    t.street = toClusterText(m.road, p.streetPunct, p.streetChars, false);
    t.distance = toClusterText(m.distanceText, ".", p.distanceChars, true);
    if (!t.distance.empty()) {
        t.unit = hudUnitText(m.distanceUnit);
    }
    return t;
}

Hud6wa::Hud6wa(HudSink& sink, const HudProfile& profile, const kombi::GuideSettings& settings)
    : sink_(sink),
      p_(profile),
      s_(settings),
      allowed_(handlesOf(profile)),
      showing_(false),
      sentAny_(false),
      stopped_(false),
      fresh_(true),
      visible_(profile.bufB),
      distanceAt_(0),
      gap_(false),
      gapAt_(0),
      junction_(true),
      nearMetres_(300),
      hasLayout_(false),
      near_(false),
      nearShown_(false),
      nearAt_(0),
      stage_(0),
      stageShown_(0),
      routeVisible_(profile.routeB),
      routeDirty_(false),
      haveNext_(false),
      nextSince_(0),
      prompt_(kHidden),
      timed_(false),
      remind_(false),
      noSeconds_(false),
      drawnStepSeq_(0),
      hasMinM_(false),
      minM_(0),
      renew_(false),
      depart_(false),
      noDistance_(false),
      noDistanceAt_(0),
      lanes_(false),
      laneVisible_(profile.laneB),
      lanesDirty_(false) {
    if (settings.approachMiles <= 0) {
        approach_.mode = HudApproach::kOff;  // at once, as the distance rule's 0 always meant
    }
}

JunctionColours Hud6wa::colours() const {
    JunctionColours c;
    c.edge = p_.edge;
    c.road = p_.road;
    c.route = p_.route;
    return c;
}

Batch Hud6wa::showBatch(const HudTexts& t, const ShapeSpec& spec) const {
    return pictureBatch("maneuver", arrowPicture(spec, p_), t, false);
}

Batch Hud6wa::pictureBatch(const char* label, const Picture& pic, const HudTexts& t, bool emptyRoute) const {
    Batch b(label);
    const uint16_t hidden = visible_ == p_.bufA ? p_.bufB : p_.bufA;
    b.add(kRemoveAllItems, removeAllItems(hidden));
    addItems(b, hidden, pic);
    b.add(kSwitchVisibility, switchVisibility(visible_, hidden));
    b.add(kRemoveAllItems, removeAllItems(visible_));
    if (emptyRoute && routeInOverlay()) {
        b.add(kRemoveAllItems, removeAllItems(p_.routeA));
        b.add(kRemoveAllItems, removeAllItems(p_.routeB));
    }
    if (fresh_) {
        b.add(kSetVisibility, setVisibility(p_.street, true));
        b.add(kSetVisibility, setVisibility(p_.distance, true));
        b.add(kSetVisibility, setVisibility(p_.unit, true));
    }
    b.add(kTextareaSetText, setText(p_.street, t.street.c_str()));
    b.add(kTextareaSetText, setText(p_.distance, t.distance.c_str()));
    b.add(kTextareaSetText, setText(p_.unit, t.unit.c_str()));
    b.add(kSetVisibility, setVisibility(p_.frame, true));
    return b;
}

Batch Hud6wa::routeBatch(const Picture& pic) const {
    Batch b("route");
    const uint16_t hidden = routeVisible_ == p_.routeA ? p_.routeB : p_.routeA;
    b.add(kRemoveAllItems, removeAllItems(hidden));
    addItems(b, hidden, pic);
    b.add(kSwitchVisibility, switchVisibility(routeVisible_, hidden));
    b.add(kRemoveAllItems, removeAllItems(routeVisible_));
    return b;
}

Batch Hud6wa::clearBatch() const {
    Batch b("clear");
    b.add(kSetVisibility, setVisibility(p_.frame, false));
    b.add(kRemoveAllItems, removeAllItems(p_.bufA));
    b.add(kRemoveAllItems, removeAllItems(p_.bufB));
    // Always (NBT-AAD a648451): after a restart or lost work, which buffer shows is not known for sure.
    b.add(kSwitchVisibility, switchVisibility(p_.bufA, p_.bufB));
    if (junction_ && routeInOverlay()) {
        b.add(kRemoveAllItems, removeAllItems(p_.routeA));
        b.add(kRemoveAllItems, removeAllItems(p_.routeB));
        b.add(kSwitchVisibility, switchVisibility(p_.routeA, p_.routeB));
    }
    if (lanes_) {
        b.add(kRemoveAllItems, removeAllItems(p_.laneA));
        b.add(kRemoveAllItems, removeAllItems(p_.laneB));
        b.add(kSwitchVisibility, switchVisibility(p_.laneA, p_.laneB));
    }
    b.add(kTextareaSetText, setText(p_.street, ""));
    b.add(kTextareaSetText, setText(p_.distance, ""));
    b.add(kTextareaSetText, setText(p_.unit, ""));
    b.add(kSetVisibility, setVisibility(p_.street, false));
    return b;
}

// Every call checked first: one that breaks a limit stops the driver and nothing of the batch is handed on.
bool Hud6wa::deliver(const Batch& b) {
    for (size_t i = 0; i < b.calls.size(); ++i) {
        if (!callOk(b.calls[i], allowed_)) {
            ELOG(kTag, "internal error: call %u of the %s batch breaks a limit (%s): stopped", static_cast<unsigned>(i),
                 b.label.c_str(), describeCall(b.calls[i]).c_str());
            stopped_ = true;
            return false;
        }
    }
    sentAny_ = true;
    if (!sink_.submit(b)) {
        ELOG(kTag, "the %s batch was not taken", b.label.c_str());
        return false;
    }
    return true;
}

Batch Hud6wa::remindBatch() const {
    Batch b("remind");
    b.add(kSetVisibility, setVisibility(p_.frame, true));
    return b;
}

Batch Hud6wa::lanesBatch(const Picture& pic) const {
    if (pic.empty()) {
        Batch b("lanes off");
        b.add(kRemoveAllItems, removeAllItems(p_.laneA));
        b.add(kRemoveAllItems, removeAllItems(p_.laneB));
        return b;
    }
    Batch b("lanes");
    const uint16_t hidden = laneVisible_ == p_.laneA ? p_.laneB : p_.laneA;
    b.add(kRemoveAllItems, removeAllItems(hidden));
    addItems(b, hidden, pic);
    b.add(kSwitchVisibility, switchVisibility(laneVisible_, hidden));
    b.add(kRemoveAllItems, removeAllItems(laneVisible_));
    return b;
}

// True: m is held back (not shown; a different maneuver shown is cleared).
bool Hud6wa::approachHold(const kombi::Maneuver& m, double now) {
    if (approach_.mode == HudApproach::kOff) {
        return false;
    }
    if (approach_.mode == HudApproach::kDistance) {
        return distanceHold(m);
    }
    return timeHold(m, now);
}

bool Hud6wa::timeHold(const kombi::Maneuver& m, double now) {
    const bool same = isShown(m);
    if (!haveNext_ || !m.sameManeuver(next_)) {  // a new next maneuver
        haveNext_ = true;
        next_ = m;
        nextSince_ = now;
        prompt_ = same ? kWarned : kHidden;
        timed_ = same;
    }
    // Right after a turn the next step can come with the last step's position (a few metres, a few seconds): a new
    // maneuver is judged only on a position stored after its step, or after kStalePosS. Meanwhile nothing changes.
    if (!same && m.posSeq != 0 && m.posSeq == m.stepPosSeq && now - nextSince_ < kStalePosS) {
        return true;
    }
    double metres = 0;
    const bool hasMetres = kombi::metresToManeuver(m, metres);
    const bool mway = motorwayTurn(m.type) && hasMetres;
    if (!m.hasSeconds && !mway) {  // nothing to time it by
        // A position without seconds (not a step still waiting for one), away from the turn: at it the phone often
        // leaves them out, which says nothing about whether it sends them.
        if (m.posSeq != 0 && (!hasMetres || metres >= kombi::kNoSecondsM)) {
            noSeconds_ = true;
        }
        if (same && timed_) {
            return false;  // shown by time: one position without seconds doesn't hide it
        }
        return distanceHold(m);
    }
    const bool bySeconds = m.hasSeconds && m.seconds <= approach_.warnS;
    const bool warn = bySeconds || (mway && metres <= approach_.mwayWarnM);
    const bool main = approach_.mainS > 0 &&
                      ((m.hasSeconds && m.seconds <= approach_.mainS) || (mway && metres <= approach_.mwayMainM));
    if (same) {  // shown: it stays, whatever the traffic does to the seconds
        if (prompt_ < kMain && main) {
            prompt_ = kMain;
            remind_ = true;
        }
        return false;
    }
    if (!warn) {
        if (showing_) {
            ELOG(kTag, "APPROACH: next maneuver %s%lld s, %.0f m away: hidden", m.hasSeconds ? "" : "?",
                 secondsForLog(m), hasMetres ? metres : -1.0);
            clear("approach");
        }
        noDistance_ = false;
        return true;
    }
    prompt_ = main ? kMain : kWarned;
    timed_ = true;
    ELOG(kTag, "WARN type %d: %lld s, %.0f m (%s)%s", m.type, secondsForLog(m), hasMetres ? metres : -1.0,
         bySeconds ? "time" : "off-ramp or fork distance", main ? ", main prompt already due" : "");
    return false;
}

bool Hud6wa::distanceHold(const kombi::Maneuver& m) {
    if (s_.approachMiles <= 0) {
        return false;
    }
    double metres;
    if (!kombi::metresToManeuver(m, metres)) {
        return false;  // unknown distance: shown, as before (fail open)
    }
    const bool same = isShown(m);  // hysteresis: the same maneuver only
    if (metres <= s_.approachMiles * kMetresPerMile * (same ? s_.hysteresis : 1.0)) {
        return false;
    }
    if (showing_) {
        ELOG(kTag, "APPROACH: next maneuver %.1f mi away (> %g mi): hidden", metres / kMetresPerMile, s_.approachMiles);
        clear("approach");
    }
    noDistance_ = false;
    return true;
}

// The maneuver shown, with a step body that changed since it was drawn, and a distance more than kRenewM above the
// nearest it got: the turn was made and the next one looks the same (NBT-AAD 7bf6304). A same step only counts down,
// give or take GPS noise. The nearest, not the last, so the new position may come before or after the new step body.
bool Hud6wa::renewedStep(const kombi::Maneuver& m) {
    if (!showing_ || !m.sameManeuver(shownTurn_)) {
        return false;
    }
    double metres;
    if (!kombi::metresToManeuver(m, metres)) {
        return false;
    }
    if (hasMinM_ && m.stepSeq != drawnStepSeq_ && metres > minM_ + kRenewM) {
        ELOG(kTag, "NEW STEP type %d: looks like the one shown, but %.0f m -> %.0f m", m.type, minM_, metres);
        return true;
    }
    if (!hasMinM_ || metres < minM_) {
        minM_ = metres;
    }
    hasMinM_ = true;
    return false;
}

void Hud6wa::drawn(const kombi::Maneuver& m) {
    drawnStepSeq_ = m.stepSeq;
    hasMinM_ = kombi::metresToManeuver(m, minM_);
    if (!hasMinM_) {
        minM_ = 0;
    }
    renew_ = false;
    remind_ = false;  // drawn whole: the panel is on anyway
}

// The distance as the cluster shows it. A snapshot without one (NavigationState before its position) keeps the last
// one rather than flicker; a first maneuver waits firstDistanceS for one, then shows without. With an approach rule
// the text is kept short (shortDistance), as NBT-AAD's state machine does. False: wait.
bool Hud6wa::fitDistance(kombi::Maneuver& m, double now) {
    if (m.distanceText.empty() && showing_ && !shownTurn_.distanceText.empty() && shownTurn_.distanceText != "-") {
        m.distanceText = shownTurn_.distanceText;
        m.distanceUnit = shownTurn_.distanceUnit;
        m.hasMetres = shownTurn_.hasMetres;
        m.metres = shownTurn_.metres;
    }
    if (m.distanceText.empty()) {
        if (!noDistance_) {
            noDistance_ = true;
            noDistanceAt_ = now;
        }
        if (!showing_ && now - noDistanceAt_ < s_.firstDistanceS) {
            return false;
        }
        m.distanceText = "-";  // shown as nothing (hudTexts)
        return true;
    }
    noDistance_ = false;
    const bool shortText =
        approach_.mode == HudApproach::kTime || (approach_.mode == HudApproach::kDistance && s_.approachMiles > 0);
    if (shortText) {
        std::string text;
        int unit = m.distanceUnit;
        kombi::shortDistance(m, s_.distanceChars, text, unit);
        m.distanceText = text;
        m.distanceUnit = unit;
    }
    return true;
}

void Hud6wa::step(const kombi::Maneuver* next, double now, const char* whyNone) {
    if (stopped_) {
        return;
    }
    // A DEPART at 0 m counts as no guidance (NBT-AAD 46748eb): after a reroute the arrow shown stays through it (and
    // goes after graceS, as in any gap) instead of a straight arrow at 0 m for a second, drawn and then replaced.
    const bool depart = next && departHere(*next);
    if (depart) {
        if (!depart_) {
            ELOG(kTag, "DEPART at 0 m: not shown%s", showing_ ? ", the arrow shown stays" : "");
        }
        next = nullptr;
        whyNone = "depart only";
    }
    depart_ = depart;
    if (!next) {
        noDistance_ = false;
        if (!showing_) {
            return;
        }
        if (!gap_) {
            gap_ = true;
            gapAt_ = now;
        }
        if (now - gapAt_ >= s_.graceS) {
            clear(whyNone);
        }
        return;
    }
    gap_ = false;
    kombi::Maneuver m = *next;
    if (!renew_ && renewedStep(m)) {
        renew_ = true;      // until it is drawn or cleared
        haveNext_ = false;  // and the time rule judges it from now
    }
    if (approachHold(m, now) || !fitDistance(m, now)) {
        return;
    }
    const HudTexts t = hudTexts(m, p_);
    if (!isShown(m)) {
        // A new maneuver: its junction worked out now, the view due at once if it is already near.
        hasLayout_ = junction_ && junctionLayout(m.type, m.hasExitAngle, m.exitAngle, m.exitNumber >= 0, m.exitNumber,
                                                 p_.glyph, p_.canvasBottom, layout_);
        near_ = false;
        nearAt_ = 0;
        stage_ = 0;
        double metres = 0;
        if (nearDue(m, &metres)) {
            near_ = true;
            nearAt_ = metres;
            updateStage(m);
        }
        if (!showPicture(m, t, true)) {
            return;
        }
        drawn(m);
        distanceAt_ = now;
        sendRoute();
        sendLanes(m);
        return;
    }
    double metres = 0;
    if (!near_ && nearDue(m, &metres)) {
        near_ = true;
        nearAt_ = metres;
        ELOG(kTag, "NEAR type %d: %lld s, %.0f m: junction view", m.type, secondsForLog(m), metres);
        updateStage(m);
        if (!showPicture(m, t, false)) {  // the arrow becomes the junction view
            return;
        }
        drawn(m);
        distanceAt_ = now;
        sendRoute();
        sendLanes(m);
        return;
    }
    if (near_) {
        updateStage(m);
        sendRoute();
    }
    if (remind_) {
        remind_ = false;
        sendRemind(m);
    }
    sendLanes(m);
    shownTurn_ = m;  // the distance as last seen: a snapshot without one keeps it
    const char* const note = sink_.live() ? "" : " (shadow: not sent)";
    if ((t.distance == shown_.distance && t.unit == shown_.unit) || now - distanceAt_ < s_.distanceEveryS) {
        return;
    }
    Batch b("distance");
    if (t.distance != shown_.distance) {
        b.add(kTextareaSetText, setText(p_.distance, t.distance.c_str()));
    }
    if (t.unit != shown_.unit) {
        b.add(kTextareaSetText, setText(p_.unit, t.unit.c_str()));
    }
    ELOG(kTag, "DIST '%s' '%s'%s", t.distance.c_str(), t.unit.c_str(), note);
    if (!deliver(b)) {
        return;
    }
    shown_.distance = t.distance;
    shown_.unit = t.unit;
    distanceAt_ = now;
}

// At the main prompt (NBT-AAD 4df17a6): the driver can dismiss the cluster's nav panel with the stalk button, and the
// factory navigation shows its turn again at an announcement point; this brings a dismissed turn back the same way.
void Hud6wa::sendRemind(const kombi::Maneuver& m) {
    if (!showing_ || !sentAny_) {
        return;
    }
    const Batch b = remindBatch();
    ELOG(kTag, "MAIN type %d: %lld s: panel on again%s", m.type, secondsForLog(m),
         sink_.live() ? "" : " (shadow: not sent)");
    deliver(b);
}

// The phone's lanes, drawn when they change and emptied when they go (NBT-AAD #162, off by default there too).
void Hud6wa::sendLanes(const kombi::Maneuver& m) {
    if (!lanes_) {
        return;
    }
    Picture pic;
    const bool draw = !m.lanes.empty() && drawLanes(m.lanes, p_.lanes, p_.road, p_.route, pic);
    if (!draw) {
        if (!lanesDirty_) {
            return;
        }
        pic.clear();
    } else if (lanesDirty_ && sameLanes(m.lanes, lanesShown_)) {
        return;
    }
    const Batch b = lanesBatch(pic);
    ELOG(kTag, "LANES %u%s | %u calls%s", static_cast<unsigned>(draw ? m.lanes.size() : 0), draw ? "" : " (off)",
         static_cast<unsigned>(b.calls.size()), sink_.live() ? "" : " (shadow: not sent)");
    if (!deliver(b)) {
        return;
    }
    if (draw) {
        laneVisible_ = laneVisible_ == p_.laneA ? p_.laneB : p_.laneA;
        lanesDirty_ = true;
        lanesShown_ = m.lanes;
    } else {
        lanesDirty_ = false;
        lanesShown_.clear();
    }
}

// Due from the main prompt as the approach times it (the phone's seconds, or an off-ramp's or a fork's distance), or
// within nearMetres whatever the rule. A position stored before the step changed may be the last step's.
bool Hud6wa::nearDue(const kombi::Maneuver& m, double* metres) const {
    if (metres) {
        *metres = 0;
    }
    if (!junction_ || !hasLayout_) {
        return false;
    }
    const bool stale = m.posSeq != 0 && m.posSeq == m.stepPosSeq;
    double d = 0;
    const bool hasMetres = !stale && kombi::metresToManeuver(m, d);
    if (metres && hasMetres) {
        *metres = d;
    }
    if (hasMetres && d <= nearMetres_) {
        return true;
    }
    if (stale || approach_.mode != HudApproach::kTime || approach_.mainS <= 0) {
        return false;
    }
    if (m.hasSeconds && m.seconds <= approach_.mainS) {
        return true;
    }
    return hasMetres && motorwayTurn(m.type) && d <= approach_.mwayMainM;
}

// 1..kStages from the distance left of where the view became due; only ever goes down (the tail only shortens).
void Hud6wa::updateStage(const kombi::Maneuver& m) {
    const bool stale = m.posSeq != 0 && m.posSeq == m.stepPosSeq;
    double d = 0;
    const bool hasMetres = !stale && kombi::metresToManeuver(m, d);
    if (hasMetres && nearAt_ <= 0) {
        nearAt_ = d;  // due by time before a distance came: the stages divide the first one
    }
    int q = kStages;
    if (hasMetres && nearAt_ > 0) {
        const double x = kStages * d / nearAt_;
        q = static_cast<int>(x);
        if (q < x) {
            ++q;
        }
        q = q < 1 ? 1 : q > static_cast<int>(kStages) ? static_cast<int>(kStages) : q;
    }
    stage_ = stage_ == 0 || q < stage_ ? q : stage_;
}

bool Hud6wa::showPicture(const kombi::Maneuver& m, const HudTexts& t, bool isNew) {
    const ShapeSpec spec = shapeFor(m.type, m.hasExitAngle, m.exitAngle);
    const bool junction = near_ && hasLayout_;
    Picture pic;
    std::string what;
    if (junction) {
        drawRoads(layout_, colours(), kJunctionStyle, pic);
        if (!routeInOverlay()) {
            drawRoute(layout_, tailFor(layout_, stage_, kStages), colours(), kJunctionStyle, pic);
        }
        what = "junction view, " + describeSpec(spec);
        if (layout_.exitNumber) {
            char n[24];
            snprintf(n, sizeof(n), " exit %d", layout_.exitNumber);
            what += n;
        }
    } else {
        pic = arrowPicture(spec, p_);
        what = describeSpec(spec);
    }
    const bool emptyRoute = !junction && routeDirty_;
    const Batch b = pictureBatch(isNew ? "maneuver" : "junction", pic, t, emptyRoute);
    ELOG(kTag, "%s type %d -> %s | '%s' '%s' | road '%s' -> '%s' | %u calls%s", isNew ? "SHOW" : "NEAR", m.type,
         what.c_str(), t.distance.c_str(), t.unit.c_str(), m.road.c_str(), t.street.c_str(),
         static_cast<unsigned>(b.calls.size()), sink_.live() ? "" : " (shadow: not sent)");
    if (!deliver(b)) {
        return false;
    }
    visible_ = visible_ == p_.bufA ? p_.bufB : p_.bufA;
    fresh_ = false;
    showing_ = true;
    shownTurn_ = m;
    shown_ = t;
    nearShown_ = junction;
    if (emptyRoute) {
        routeDirty_ = false;
    }
    if (!junction) {
        stageShown_ = 0;
    } else if (!routeInOverlay()) {
        stageShown_ = stage_;
    }
    return true;
}

void Hud6wa::sendRoute() {
    if (!near_ || !nearShown_ || !routeInOverlay() || stage_ == 0 || stage_ == stageShown_) {
        return;
    }
    Picture pic;
    const int tail = tailFor(layout_, stage_, kStages);
    drawRoute(layout_, tail, colours(), kJunctionStyle, pic);
    const Batch b = routeBatch(pic);
    ELOG(kTag, "ROUTE stage %d/%d, tail %d | %u calls%s", stage_, static_cast<int>(kStages), tail,
         static_cast<unsigned>(b.calls.size()), sink_.live() ? "" : " (shadow: not sent)");
    if (!deliver(b)) {
        return;
    }
    routeVisible_ = routeVisible_ == p_.routeA ? p_.routeB : p_.routeA;
    routeDirty_ = true;
    stageShown_ = stage_;
}

void Hud6wa::clear(const char* why) {
    if (!showing_ && !sentAny_) {
        return;
    }
    const Batch b = clearBatch();
    ELOG(kTag, "CLEAR (%s): %u calls%s", why, static_cast<unsigned>(b.calls.size()),
         sink_.live() ? "" : " (shadow: not sent)");
    const bool wasStopped = stopped_;
    stopped_ = false;  // the clear goes even after a refused batch: it is built from fixed calls only
    deliver(b);
    stopped_ = stopped_ || wasStopped;
    forget();
}

void Hud6wa::yieldToFactory() {
    if (showing_ || sentAny_) {
        ELOG(kTag, "the BMW navigation guides: it owns the panel, ours is forgotten");
    }
    forget();
}

void Hud6wa::forget() {
    showing_ = false;
    sentAny_ = false;
    fresh_ = true;
    gap_ = false;
    visible_ = p_.bufB;  // where a clear or the navigation's end of route leaves it
    shown_ = HudTexts();
    shownTurn_ = kombi::Maneuver();
    near_ = nearShown_ = hasLayout_ = false;
    nearAt_ = 0;
    stage_ = stageShown_ = 0;
    routeVisible_ = p_.routeB;
    routeDirty_ = false;
    remind_ = false;
    renew_ = false;
    hasMinM_ = false;
    noDistance_ = false;
    laneVisible_ = p_.laneB;
    lanesDirty_ = false;
    lanesShown_.clear();
}

}  // namespace hudterm
}  // namespace evo
