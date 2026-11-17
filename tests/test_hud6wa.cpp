// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Tests platform/hudterm.hpp, platform/hudterm_shapes.hpp and platform/hud6wa.cpp: the HUDTerminal payloads, every
// maneuver picture within the cluster's limits, and the 6WA driver's batches tick by tick.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "hud6wa.hpp"
#include "hudterm.hpp"
#include "hudterm_shapes.hpp"
#include "kombi_test_util.hpp"

namespace {

using namespace evo::hudterm;
using evo::kombi::GuideSettings;
using evo::kombi::Maneuver;

typedef std::vector<uint8_t> Bytes;

Bytes bytes(const char* hex) {
    Bytes b;
    for (const char* p = hex; p[0] && p[1];) {
        if (*p == ' ') {
            ++p;
            continue;
        }
        unsigned v;
        sscanf(p, "%2x", &v);
        b.push_back(static_cast<uint8_t>(v));
        p += 2;
    }
    return b;
}

void payloads() {
    CHECK(setText(3, "250") == bytes("0003 00 0032 0035 0030 0000"));
    CHECK(setText(2, "") == bytes("0002 00 0000"));
    CHECK(setText(2, "M\xc3\xbc").empty());                     // not plain ASCII: refused, not mangled
    CHECK(setText(2, std::string(33, 'a').c_str()).empty());     // longer than 32
    CHECK(setVisibility(1, true) == bytes("0001 01"));
    CHECK(switchVisibility(6, 5) == bytes("0006 0005"));
    CHECK(removeAllItems(5) == bytes("0005"));
    CHECK(setFillColor(5, 4) == bytes("0005 04"));
    std::vector<Point> pts(3);
    pts[0].x = 1, pts[0].y = 2, pts[1].x = -1, pts[1].y = 300, pts[2].x = 0, pts[2].y = 0;
    CHECK(addPolygon(5, 7, kPolygon, pts) == bytes("0005 0007 03 03 0001 0002 FFFF 012C 0000 0000"));
    CHECK(addPolygon(5, 0, kPolygon, std::vector<Point>(kMaxPoints + 1)).empty());
    CHECK(addPolygon(5, 0, kPolygon, std::vector<Point>()).empty());
    CHECK(decodeText(&setText(3, "0.3")[0], setText(3, "0.3").size()) == "0.3");

    // Shape checks: only what we send, exactly.
    CHECK(payloadShapeOk(kTextareaSetText, setText(3, "abc")));
    CHECK(!payloadShapeOk(kTextareaSetText, bytes("0003 00 0061")));  // no terminator
    CHECK(!payloadShapeOk(kSetVisibility, bytes("0001 02")));
    CHECK(!payloadShapeOk(kIsVisible, bytes("0001")));
    CHECK(!payloadShapeOk(kAddCircle, Bytes(19)));
    CHECK(!payloadShapeOk(0x300, bytes("0001")));

    HandleSet allowed = handlesOf(k6wa);
    CHECK(callOk(Call(kSetVisibility, setVisibility(1, true)), allowed));
    CHECK(callOk(Call(kRemoveAllItems, removeAllItems(9)), allowed));           // the lanes
    CHECK(!callOk(Call(kSetVisibility, setVisibility(11, true)), allowed));     // a layer we never touch
    CHECK(!callOk(Call(kSetVisibility, setVisibility(22, false)), allowed));
    CHECK(!callOk(Call(kSwitchVisibility, switchVisibility(6, 11)), allowed));  // the second handle too
    CHECK(!callOk(Call(kRemoveAllItems, removeAllItems(0)), allowed));
    CHECK(describeCall(Call(kTextareaSetText, setText(3, "250"))) == "210 TextareaSetText h3 '250'");
}

void texts() {
    // Umlauts the German way, the comma and anything else a space, punctuation from the profile kept.
    CHECK(toClusterText("M\xc3\xbcnchner Stra\xc3\x9f" "e, 5", "-./", 32, false) == "Muenchner Strasse 5");
    CHECK(toClusterText("A9 / E45 \xe2\x86\x92 N\xc3\xbcrnberg", "-./", 32, false) == "A9 / E45 Nuernberg");
    CHECK(toClusterText("0,3", ".", 8, true) == "0.3");
    CHECK(toClusterText(std::string(40, 'x'), "", 32, false).size() == 32);
    CHECK(toClusterText("   ", "", 32, false).empty());

    Maneuver m;
    m.road = "Rue de l'\xc3\x89glise";
    m.distanceText = "1,2";
    m.distanceUnit = evo::kombi::kUnitKilometresTenths;
    HudTexts t = hudTexts(m, k6wa);
    CHECK(t.street == "Rue de l Eglise" && t.distance == "1.2" && t.unit == "km");
    m.distanceText = "";
    t = hudTexts(m, k6wa);
    CHECK(t.distance.empty() && t.unit.empty());  // no unit without a distance
    CHECK(std::string(hudUnitText(evo::kombi::kUnitFeet)) == "ft" && std::string(hudUnitText(99)).empty());
}

// Every Android Auto maneuver type, with and without exit angles: each item keeps the cluster's limits and lands in
// the glyph box.
void everyShapeFits() {
    const Rect box = k6wa.glyph;
    const HandleSet allowed = handlesOf(k6wa);
    int pictures = 0;
    for (int type = -1; type <= 50; ++type) {
        for (int angle = -1; angle <= 360; angle += (angle < 0 ? 1 : 45)) {
            const ShapeSpec spec = shapeFor(type, angle >= 0, angle);
            Shape design, fitted;
            drawShape(spec, design);
            fitShape(design, box, fitted);
            CHECK(!fitted.empty() && fitted.size() <= 8);
            for (size_t i = 0; i < fitted.size(); ++i) {
                CHECK(fitted[i].pts.size() >= 3 && fitted[i].pts.size() <= static_cast<size_t>(kMaxPoints));
                for (size_t k = 0; k < fitted[i].pts.size(); ++k) {
                    const Point& p = fitted[i].pts[k];
                    CHECK(p.x >= box.x0 && p.x <= box.x1 && p.y >= box.y0 && p.y <= box.y1);
                }
                const Call c(kAddPolygon, addPolygon(5, static_cast<uint16_t>(i), fitted[i].type, fitted[i].pts));
                CHECK(callOk(c, allowed));
            }
            ++pictures;
        }
    }
    CHECK(pictures > 400);
    // The families.
    CHECK(shapeFor(7, false, 0).angle == -90 && shapeFor(8, false, 0).angle == 90);  // normal left, right
    CHECK(shapeFor(11, false, 0).kind == kShapeUTurn);
    CHECK(shapeFor(39, false, 0).kind == kShapeFlag);
    const ShapeSpec r = shapeFor(31, true, 90);
    CHECK(r.kind == kShapeRoundabout && !r.clockwise && r.exit == 90);
    CHECK(shapeFor(31, true, 0).exit == 180);  // no usable angle: straight across
    CHECK(shapeFor(22, false, 0).stub != kNoStub);  // off ramp right: the highway goes on
    CHECK(describeSpec(shapeFor(8, false, 0)) == "arrow +90");
}

// Records every batch the driver hands on.
class Recorder : public HudSink {
public:
    bool submit(const Batch& b) override {
        batches.push_back(b);
        return true;
    }
    bool live() const override { return false; }
    std::vector<Batch> batches;
};

size_t count(const Batch& b, uint16_t fkt) {
    size_t n = 0;
    for (size_t i = 0; i < b.calls.size(); ++i) n += b.calls[i].fkt == fkt;
    return n;
}

const Call* find(const Batch& b, uint16_t fkt, uint16_t handle) {
    for (size_t i = 0; i < b.calls.size(); ++i)
        if (b.calls[i].fkt == fkt && b.calls[i].handle() == handle) return &b.calls[i];
    return NULL;
}

std::string textOf(const Batch& b, uint16_t handle) {
    const Call* c = find(b, kTextareaSetText, handle);
    return c ? decodeText(&c->payload[0], c->payload.size()) : "(none)";
}

Maneuver turn(int type, const char* road, const char* dist, int metres) {
    Maneuver m;
    m.type = type;
    m.road = road;
    m.distanceText = dist;
    m.distanceUnit = evo::kombi::kUnitMetres;
    m.hasMetres = true;
    m.metres = metres;
    return m;
}

void driver() {
    Recorder rec;
    GuideSettings s;
    Hud6wa hud(rec, k6wa, s);
    hud.setJunction(false, 300);  // the arrow only: the junction view has its own test
    const HandleSet allowed = handlesOf(k6wa);

    // Too far ahead: nothing.
    Maneuver far = turn(8, "Main St", "3000", 3000);
    hud.step(&far, 0.0);
    CHECK(rec.batches.empty() && !hud.showing());

    // The first maneuver: drawn into buffer 5, swapped in, the texts switched on, the panel on.
    Maneuver m = turn(8, "Main St", "300", 300);
    hud.step(&m, 1.0);
    CHECK(rec.batches.size() == 1 && hud.showing() && hud.visibleBuffer() == 5);
    const Batch& first = rec.batches[0];
    CHECK(first.calls[0].fkt == kRemoveAllItems && first.calls[0].handle() == 5);
    CHECK(count(first, kAddPolygon) >= 3 && find(first, kSwitchVisibility, 6) != NULL);
    CHECK(count(first, kSetVisibility) == 4);  // street, distance, unit, then the frame
    CHECK(first.calls.back().fkt == kSetVisibility && first.calls.back().handle() == 1);
    CHECK(textOf(first, 2) == "Main St" && textOf(first, 3) == "300" && textOf(first, 4) == "m");
    for (size_t i = 0; i < first.calls.size(); ++i) CHECK(callOk(first.calls[i], allowed));

    // The same turn, same distance: nothing. A new distance: only after distanceEveryS, and only the text.
    hud.step(&m, 1.1);
    m.distanceText = "250";
    m.metres = 250;
    hud.step(&m, 1.2);
    CHECK(rec.batches.size() == 1);
    hud.step(&m, 1.6);
    CHECK(rec.batches.size() == 2 && rec.batches[1].calls.size() == 1 && textOf(rec.batches[1], 3) == "250");

    // A new maneuver: drawn into the other buffer, no texts switched on again.
    Maneuver next = turn(7, "Elm St", "800", 800);
    hud.step(&next, 2.0);
    CHECK(rec.batches.size() == 3 && hud.visibleBuffer() == 6);
    const Batch& second = rec.batches[2];
    CHECK(second.calls[0].handle() == 6 && count(second, kSetVisibility) == 1);
    CHECK(find(second, kSwitchVisibility, 5) != NULL && textOf(second, 2) == "Elm St");

    // Guidance gone: kept through the grace period, then cleared, buffer 6 left visible as the navigation leaves it.
    hud.step(NULL, 3.0);
    hud.step(NULL, 3.0 + s.graceS - 0.1);
    CHECK(rec.batches.size() == 3 && hud.showing());
    hud.step(NULL, 3.0 + s.graceS);
    CHECK(rec.batches.size() == 4 && !hud.showing() && !hud.sentAny());
    const Batch& clear = rec.batches[3];
    CHECK(clear.label == "clear" && clear.calls[0].fkt == kSetVisibility && clear.calls[0].handle() == 1);
    CHECK(find(clear, kSwitchVisibility, 5) != NULL);  // always 5 -> 6, even with 6 already visible (a648451)
    CHECK(textOf(clear, 2).empty() && textOf(clear, 3).empty() && textOf(clear, 4).empty());

    // After a clear, the next picture switches the texts on again; a clear from buffer 5 swaps 6 back.
    hud.step(&m, 20.0);
    CHECK(count(rec.batches[4], kSetVisibility) == 4 && hud.visibleBuffer() == 5);
    hud.clear("test");
    CHECK(find(rec.batches[5], kSwitchVisibility, 5) != NULL);
    hud.clear("again");
    CHECK(rec.batches.size() == 6);  // nothing drawn: nothing to clear

    // The BMW navigation takes over: forgotten without a clear.
    hud.step(&m, 30.0);
    const size_t before = rec.batches.size();
    hud.yieldToFactory();
    CHECK(!hud.showing() && rec.batches.size() == before);

    // A shown maneuver hides again only past approach * hysteresis.
    Hud6wa h2(rec, k6wa, s);
    Maneuver a = turn(8, "A", "1500", 1500);
    h2.step(&a, 0.0);
    CHECK(h2.showing());
    a.metres = 1700;  // over 1 mi, under 1.15 mi
    h2.step(&a, 1.0);
    CHECK(h2.showing());
    a.metres = 2000;
    h2.step(&a, 2.0);
    CHECK(!h2.showing());
}

}  // namespace

// The junction view (NBT-AAD #162): the arrow first; from 300 m the maneuver's roads in the picture buffers and the
// route over them in 7/8, its tail shorter at each stage; a new maneuver's arrow empties the route; the clear too.
void junctionView() {
    Recorder rec;
    GuideSettings s;
    s.approachMiles = 0;
    Hud6wa hud(rec, k6wa, s);
    const HandleSet allowed = handlesOf(k6wa);
    CHECK(allowed.has(7) && allowed.has(8) && allowed.has(9) && allowed.has(10) && !allowed.has(11));

    Maneuver m = turn(7, "Elm St", "800", 800);
    hud.step(&m, 1.0);
    CHECK(rec.batches.size() == 1 && rec.batches[0].label == "maneuver" && !hud.nearShown());
    CHECK(find(rec.batches[0], kRemoveAllItems, 7) == NULL);  // nothing of ours in the route overlay yet

    m.distanceText = "280";
    m.metres = 280;
    hud.step(&m, 2.0);  // near: the junction view, then the route at its longest
    CHECK(rec.batches.size() == 3 && rec.batches[1].label == "junction" && rec.batches[2].label == "route");
    CHECK(hud.nearShown() && hud.routeStage() == Hud6wa::kStages && hud.visibleRoute() == 7);
    const Batch& view = rec.batches[1];
    CHECK(count(view, kSetFillColor) == 2 && find(view, kSetFillColor, 6) != NULL);  // edges, then roads
    CHECK(count(view, kAddPolygon) == 2);  // the road in and out (one path): its edge, then its core
    const Batch& route = rec.batches[2];
    CHECK(route.calls[0].fkt == kRemoveAllItems && route.calls[0].handle() == 7);
    CHECK(count(route, kAddPolygon) == 2 && find(route, kSwitchVisibility, 8) != NULL);
    for (size_t b = 0; b < rec.batches.size(); ++b)
        for (size_t i = 0; i < rec.batches[b].calls.size(); ++i) CHECK(callOk(rec.batches[b].calls[i], allowed));

    m.distanceText = "200";
    m.metres = 200;
    size_t mark = rec.batches.size();
    hud.step(&m, 3.0);  // stage 3 of 4 (200/280 * 4 = 2.9 -> 3): a shorter tail, then the new distance
    CHECK(rec.batches.size() == mark + 2 && rec.batches[mark].label == "route" && hud.routeStage() == 3);
    CHECK(rec.batches[mark + 1].label == "distance");
    const size_t before = rec.batches.size();
    m.metres = 190;
    hud.step(&m, 3.1);  // the same stage: no route batch (the distance text did not change either)
    CHECK(rec.batches.size() == before);
    m.metres = 300;
    hud.step(&m, 3.2);  // farther again (a GPS jump): the stage never grows back
    CHECK(hud.routeStage() == 3);

    // The next maneuver: its arrow, the route overlay emptied.
    Maneuver next = turn(13, "Oak Rd", "900", 900);
    hud.step(&next, 4.0);
    const Batch& arrow = rec.batches.back();
    CHECK(arrow.label == "maneuver" && !hud.nearShown());
    CHECK(find(arrow, kRemoveAllItems, 7) != NULL && find(arrow, kRemoveAllItems, 8) != NULL);

    // A roundabout near at once: the junction view with its exit number in the ring (more polygons, white).
    Maneuver ring = turn(31, "Ring", "150", 150);
    ring.hasExitAngle = true;
    ring.exitAngle = 90;
    ring.exitNumber = 2;
    hud.step(&ring, 5.0);
    const Batch& rb = rec.batches[rec.batches.size() - 2];
    CHECK(rb.label == "maneuver" && hud.nearShown() && count(rb, kAddPolygon) >= 8);
    CHECK(count(rb, kSetFillColor) == 3);  // edges, roads, the number in the route's colour

    // The clear empties the route overlay and leaves 8 showing.
    hud.clear("test");
    const Batch& clear = rec.batches.back();
    CHECK(clear.label == "clear" && find(clear, kRemoveAllItems, 7) != NULL && find(clear, kRemoveAllItems, 8) != NULL);
    CHECK(!hud.stopped());

    // Off: the arrow all the way.
    Recorder off;
    Hud6wa plain(off, k6wa, s);
    plain.setJunction(false, 300);
    Maneuver near = turn(7, "Elm St", "100", 100);
    plain.step(&near, 1.0);
    CHECK(off.batches.size() == 1 && !plain.nearShown());

    // A left and a right turn's junctions mirror each other about the picture's centre, to the unit.
    Layout left, right;
    CHECK(junctionLayout(7, false, 0, false, 0, k6wa.glyph, k6wa.canvasBottom, left));
    CHECK(junctionLayout(8, false, 0, false, 0, k6wa.glyph, k6wa.canvasBottom, right));
    const int cx = (k6wa.glyph.x0 + k6wa.glyph.x1) / 2;
    CHECK(left.roads.size() == right.roads.size() && left.ex + right.ex == 2 * cx && left.ey == right.ey);
    // The destination keeps its flag.
    Layout flag;
    CHECK(!junctionLayout(39, false, 0, false, 0, k6wa.glyph, k6wa.canvasBottom, flag));
}

// A maneuver with the phone's seconds, as a position stored after its step.
Maneuver timed(int type, const char* road, int metres, int seconds, uint32_t stepSeq = 1) {
    Maneuver m = turn(type, road, "", metres);
    char text[16];
    snprintf(text, sizeof(text), "%d", metres);
    m.distanceText = text;
    m.hasSeconds = seconds >= 0;
    m.seconds = seconds < 0 ? 0 : seconds;
    m.posSeq = 5;
    m.stepPosSeq = 4;
    m.stepSeq = stepSeq;
    return m;
}

// The time approach (NBT-AAD cc84e49, 4df17a6, 73cd1a0): warned at 50 s, reminded at the main prompt (20 s), kept
// whatever the seconds do once shown, a farther next maneuver hidden; off-ramps and forks by distance too.
void timeApproach() {
    Recorder rec;
    GuideSettings s;
    Hud6wa hud(rec, k6wa, s);
    hud.setJunction(false, 300);
    CHECK(hud.approach().mode == HudApproach::kTime && hud.approach().warnS == 50 && hud.approach().mainS == 20);

    Maneuver m = timed(8, "Main St", 1500, 90);
    hud.step(&m, 0.0);
    CHECK(rec.batches.empty() && !hud.showing());  // 1500 m is within 1 mi, but 90 s is not yet due
    m = timed(8, "Main St", 800, 45);
    hud.step(&m, 1.0);
    CHECK(rec.batches.size() == 1 && rec.batches[0].label == "maneuver" && hud.showing());
    m = timed(8, "Main St", 700, 70);  // traffic: the seconds go up, the arrow stays
    hud.step(&m, 2.0);
    CHECK(hud.showing() && rec.batches.size() == 2 && rec.batches[1].label == "distance");
    m = timed(8, "Main St", 200, 18);  // the main prompt: the panel on again, once
    hud.step(&m, 3.0);
    CHECK(rec.batches.size() == 4 && rec.batches[2].label == "remind" && rec.batches[3].label == "distance");
    CHECK(rec.batches[2].calls.size() == 1 && rec.batches[2].calls[0].fkt == kSetVisibility &&
          rec.batches[2].calls[0].handle() == 1);
    m = timed(8, "Main St", 150, 12);
    hud.step(&m, 4.0);
    CHECK(rec.batches.back().label == "distance");  // no second reminder

    // The next maneuver, 2 min away: hidden (the arrow shown is cleared) until it is due.
    Maneuver n = timed(7, "Elm St", 2500, 120, 2);
    hud.step(&n, 10.0);
    CHECK(!hud.showing() && rec.batches.back().label == "clear");
    // Its step came after the last position: held, without a clear, until a position of its own (or 3 s).
    const size_t mark = rec.batches.size();
    Maneuver stale = timed(13, "Oak Rd", 30, 2, 3);
    stale.stepPosSeq = stale.posSeq;
    hud.step(&stale, 11.0);
    CHECK(rec.batches.size() == mark && !hud.showing());

    // An off-ramp in a queue: due by distance (2 km) with the seconds far off.
    Maneuver ramp = timed(22, "A9", 1800, 300, 4);
    hud.step(&ramp, 20.0);
    CHECK(hud.showing() && rec.batches.back().label == "maneuver");

    // A phone without seconds: the distance rule, and the note for the log (not at the turn itself).
    Recorder r2;
    Hud6wa h2(r2, k6wa, s);
    Maneuver q = timed(8, "Main St", 1000, -1);
    h2.step(&q, 0.0);
    CHECK(h2.showing() && h2.takeNoSeconds() && !h2.takeNoSeconds());
    q = timed(8, "Main St", 20, -1);
    h2.step(&q, 1.0);
    CHECK(!h2.takeNoSeconds());

    // With the main prompt and the junction view on, the junction view is the reminder.
    Recorder r3;
    Hud6wa h3(r3, k6wa, s);
    Maneuver j = timed(7, "Elm St", 900, 40);
    h3.step(&j, 0.0);
    j = timed(7, "Elm St", 600, 19);
    h3.step(&j, 1.0);
    CHECK(h3.nearShown() && r3.batches[1].label == "junction");
    for (size_t i = 0; i < r3.batches.size(); ++i) CHECK(r3.batches[i].label != "remind");

    // The settings file.
    HudApproach a;
    CHECK(parseApproach("off\n", a) && a.mode == HudApproach::kOff);
    CHECK(parseApproach("time 40 15", a) && a.mode == HudApproach::kTime && a.warnS == 40 && a.mainS == 15);
    CHECK(parseApproach("time 30 30", a) && a.mainS == 0);  // not below the warning: no main prompt
    CHECK(parseApproach("distance", a) && a.mode == HudApproach::kDistance);
    CHECK(!parseApproach("soon", a) && !parseApproach("", a) && !parseApproach("time -5", a));
    CHECK(a.mode == HudApproach::kDistance);  // unchanged by what was refused
}

// No straight arrow at 0 m for a DEPART (NBT-AAD 46748eb); a next step that looks like the one shown (7bf6304).
void departAndRenewedStep() {
    Recorder rec;
    GuideSettings s;
    s.approachMiles = 0;  // at once: these rules don't depend on the approach
    Hud6wa hud(rec, k6wa, s);
    hud.setJunction(false, 300);
    Maneuver depart = turn(1, "", "0", 0);
    hud.step(&depart, 0.0);
    CHECK(rec.batches.empty());
    Maneuver m = turn(8, "Main St", "300", 300);
    m.stepSeq = 1;
    hud.step(&m, 1.0);
    CHECK(rec.batches.size() == 1);
    hud.step(&depart, 2.0);  // a reroute: the arrow stays through the grace time, then goes
    CHECK(hud.showing() && rec.batches.size() == 1);
    hud.step(&depart, 2.0 + s.graceS);
    CHECK(!hud.showing() && rec.batches.back().label == "clear");

    hud.step(&m, 20.0);
    m.distanceText = "10";
    m.metres = 10;
    hud.step(&m, 21.0);
    const size_t mark = rec.batches.size();
    // The turn is made; the next one is also a right onto Main St: its step body is new and its distance jumped up.
    Maneuver again = turn(8, "Main St", "900", 900);
    again.stepSeq = 2;
    hud.step(&again, 22.0);
    CHECK(rec.batches.size() > mark && rec.batches[mark].label == "maneuver");
    // The same step body with a GPS jump is not a new step.
    const size_t mark2 = rec.batches.size();
    again.metres = 700;
    again.distanceText = "700";
    hud.step(&again, 23.0);
    again.metres = 950;
    again.distanceText = "950";
    hud.step(&again, 24.0);
    for (size_t i = mark2; i < rec.batches.size(); ++i) CHECK(rec.batches[i].label != "maneuver");

    // A first maneuver without a distance waits firstDistanceS, then shows without one.
    Recorder r2;
    Hud6wa h2(r2, k6wa, s);
    Maneuver nd = turn(7, "Elm St", "", 0);
    nd.hasMetres = false;
    h2.step(&nd, 0.0);
    CHECK(r2.batches.empty());
    h2.step(&nd, s.firstDistanceS);
    CHECK(r2.batches.size() == 1 && textOf(r2.batches[0], 3).empty());
}

// The phone's lanes in 9/10 (NBT-AAD #162): drawn when they change, emptied when they go, cleared with the rest.
void lanes() {
    Recorder rec;
    GuideSettings s;
    s.approachMiles = 0;
    Hud6wa hud(rec, k6wa, s);
    hud.setJunction(false, 300);
    CHECK(!hud.lanesOn());
    hud.setLanes(true);
    const HandleSet allowed = handlesOf(k6wa);
    Maneuver m = turn(7, "Elm St", "200", 200);
    std::vector<evo::kombi::PhoneLane> row(3);
    row[0].push_back(evo::kombi::LaneShape(4, true));
    row[1].push_back(evo::kombi::LaneShape(1, false));
    row[1].push_back(evo::kombi::LaneShape(4, true));
    row[2].push_back(evo::kombi::LaneShape(1, false));
    m.lanes = row;
    hud.step(&m, 0.0);
    CHECK(rec.batches.size() == 2 && rec.batches[1].label == "lanes");
    const Batch& l = rec.batches[1];
    CHECK(l.calls[0].handle() == 9 && count(l, kAddPolygon) == 4 && find(l, kSwitchVisibility, 10) != NULL);
    CHECK(count(l, kSetFillColor) == 2);  // the others, then the lanes to take
    for (size_t i = 0; i < l.calls.size(); ++i) CHECK(callOk(l.calls[i], allowed));
    hud.step(&m, 1.0);
    CHECK(rec.batches.size() == 2);  // the same lanes: nothing
    m.lanes.clear();
    hud.step(&m, 2.0);
    CHECK(rec.batches.back().label == "lanes off");
    hud.clear("test");
    CHECK(find(rec.batches.back(), kRemoveAllItems, 9) != NULL && find(rec.batches.back(), kSwitchVisibility, 9) != NULL);
    // Every lane shape fits its box.
    for (int shape = 0; shape <= 9; ++shape) {
        Picture pic;
        std::vector<evo::kombi::PhoneLane> one(1);
        one[0].push_back(evo::kombi::LaneShape(shape, true));
        CHECK(drawLanes(one, k6wa.lanes, 3, 4, pic) && pic.size() == 1);
        CHECK(callOk(Call(kAddPolygon, addPolygon(9, 0, pic[0].poly.type, pic[0].poly.pts)), allowed));
        for (size_t k = 0; k < pic[0].poly.pts.size(); ++k) {
            const Point& p = pic[0].poly.pts[k];
            CHECK(p.x >= 0 && p.x < k6wa.glyph.x0 && p.y >= 0 && p.y <= 129);  // left of the picture, on the canvas
        }
    }
}

int main() {
    payloads();
    texts();
    everyShapeFits();
    driver();
    junctionView();
    timeApproach();
    departAndRenewedStep();
    lanes();
    puts(
        "PASS hud6wa: HUDTerminal payloads, every maneuver picture in the 6WA's limits, show/distance/clear batches, "
        "junction view with route stages and exit number, time approach and reminder, DEPART, renewed step, lanes");
    return 0;
}
