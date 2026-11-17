// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The Gauges app, an iDrive app of its own under My Vehicle (libCarGauges.so, CarGauges.xml), next to Android Auto.
// Its entry has a car icon and a preview card with a car picture: the 800x480 iDrive shows no picture of its own there.
//   Gauges         the first page: a scrolling list of the pages below (the owner found OPTION menus too hidden)
//   Dashboard      3 pages of 8 cards (tile, name, value, bar), filled in order with the ticked gauges
//   Live values    the same gauges as a scrolling list, one row each
//   Choose gauges  every value of the catalogue (platform/gauge_catalogue.hpp): the iDrive's car data (CDS), standard
//                  OBD values and BMW engine values from the DME (gauge-collector, through the diagnostic gateway);
//                  OBD values the DME does not support (/var/local/data/AA/gauges-supported) are hidden
//   Shift light    rpm bar, speed and the chosen shift point (CDS)
//   Fault codes    the engine's stored codes, read only (gauge-collector --codes)
//   Gauge colour   8 colours or one per card
//   Car tests      a list of on-car tests; a press starts one and opens Test results
// Kept in /var/local/data/AA: gauges-choice (the ticks), gauges-colour, gauges-shift. DME values only once the owner
// created gauges-dme ("demo" in it: synthetic values) and the gateway's address is in gateway-ip.
//
// A port of ID6 apps' BoostDeck and ShiftDeck (github.com/inotbarnacle/ID6_apps_prerelease, MIT) from the NBT Evo's
// QNX to EntryNav2's Linux: the same RHMI framework and approach (bars pre-drawn as images, switched by id; a worker
// process for the diagnostics, its values over loopback UDP), cards laid out for 800x480 and fed from a catalogue of
// every value instead of a fixed set. MHD map switching and code clearing (writes to the DME) are not ported.
//
// The models are updated only while their page shows, and only on change. /var/local/data/AA/gauges-off keeps the app
// from being created (read at iDrive start).
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../platform/dme_gauges.hpp"
#include "../platform/service_contract.hpp"
#include "app_log.hpp"
#include "bmw_framework.hpp"
#include "gauge_ids.hpp"
#include "gauge_values.hpp"
#include "rhmi_ids.hpp"
#include "spawn_attr.hpp"

extern char** environ;

namespace evo {
namespace app {
namespace {

using namespace gauges;
using catalogue::kCds;

const char kGaugesOffFile[] = "/var/local/data/AA/gauges-off";
const char kDmeFile[] = "/var/local/data/AA/gauges-dme";
const char kGatewayFile[] = "/var/local/data/AA/gateway-ip";
const char kChoiceFile[] = "/var/local/data/AA/gauges-choice";
const char kSupportedFile[] = "/var/local/data/AA/gauges-supported";
const char kColourFile[] = "/var/local/data/AA/gauges-colour";
const char kShiftFile[] = "/var/local/data/AA/gauges-shift";
const char kCollector[] = "/opt/hmi/data/ro/Plugins/CarGauges/gauge-collector";
const char kCollectorLog[] = "/tmp/gauge-collector.log";
const char kCodesFile[] = "/tmp/gauge-codes.txt";
const unsigned kPollMs = 150;
const unsigned kModels = 1024;  // every text and image model id is below this

bmw::PluginApp* g_app = nullptr;
unsigned g_page = 0;  // the state shown, 0: none of ours
unsigned g_colour = 0;
unsigned g_shiftPoint = 2;

// Every catalogue entry's latest value: CDS from the iDrive, DME from the collector's frames.
bool g_on[kEntryCount];         // ticked
bool g_hidden[kEntryCount];     // an OBD value the DME does not support
bool g_valid[kEntryCount];
double g_value[kEntryCount];
uint64_t g_seenAt[kEntryCount];  // DME values: when the frame carrying it arrived (they go stale)
bool g_logged[kEntryCount];
unsigned g_cardEntry[kCards];  // the entry each card shows, kEntryCount: none

int g_udp = -1;
pid_t g_collector = -1;
pid_t g_codesPid = -1;
pid_t g_discoverPid = -1;   // gateway discovery started by the DME switch
char g_dmeWhy[48] = "";     // why the engine values are not read, "" when they are (cards show it)
char g_collectorWant[512];

char g_shownText[kModels][48];
unsigned g_shownImage[kModels];

// ---- models ----------------------------------------------------------------------------------------------------------
void setText(unsigned model, const char* text) {
    if (!g_app || model >= kModels || !strcmp(g_shownText[model], text)) {
        return;
    }
    g_app->updateModel(model, bmw::BasicString<char>(text));
    strncpy(g_shownText[model], text, sizeof(g_shownText[model]) - 1);
    g_shownText[model][sizeof(g_shownText[model]) - 1] = 0;
}

void setImage(unsigned model, unsigned image) {
    if (!g_app || model >= kModels || g_shownImage[model] == image) {
        return;
    }
    g_app->updateModel(model, bmw::ImageID(image));
    g_shownImage[model] = image;
}

int g_cardShown[kCards];  // a card's tile and bar visibility as last set: -1 unknown, 0 hidden, 1 shown
int g_liveShown[kCards];  // the same for the Live values rows
int g_menuShown[2];       // the Gauges list's Dashboard 2 and 3

void forgetShown() {
    memset(g_shownText, 0, sizeof(g_shownText));
    memset(g_shownImage, 0, sizeof(g_shownImage));
    for (unsigned c = 0; c < kCards; ++c) {
        g_cardShown[c] = -1;
        g_liveShown[c] = -1;
    }
    g_menuShown[0] = g_menuShown[1] = -1;
}

void setVisible(unsigned component, bool visible) {
    if (g_app) {
        g_app->setProperty(component, kPropVisible, bmw::Parcel(visible));
    }
}

unsigned barImage(unsigned card, unsigned level) {
    const unsigned colour = g_colour == kMulticolour ? card % kColours : g_colour;
    return kImgBar0 + colour * kBarColourStride + (level < kBarLevels ? level : kBarLevels - 1);
}

// ---- small files ------------------------------------------------------------------------------------------------------
bool readLine(const char* path, char* out, size_t cap) {
    FILE* f = fopen(path, "r");
    if (!f) {
        return false;
    }
    const bool ok = fgets(out, static_cast<int>(cap), f) != nullptr;
    fclose(f);
    if (ok) {
        out[strcspn(out, "\r\n")] = 0;
    }
    return ok;
}

unsigned readIndex(const char* path, unsigned count, unsigned def) {
    char text[16];
    if (!readLine(path, text, sizeof(text))) {
        return def;
    }
    const unsigned v = static_cast<unsigned>(atoi(text));
    return v < count ? v : def;
}

void writeLine(const char* path, const char* line) {
    FILE* f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

void loadChoice() {
    char line[kEntryCount + 8] = "";
    parseChoice(readLine(kChoiceFile, line, sizeof(line)) ? line : nullptr, g_on);
    // OBD PIDs the DME answered to "supported?" (gauge-collector --supported); without the file every one is offered.
    char supported[512] = "";
    const bool known = readLine(kSupportedFile, supported, sizeof(supported));
    for (unsigned i = 0; i < kEntryCount; ++i) {
        g_hidden[i] = false;
        if (known && kEntries[i].source == catalogue::kObd) {
            char pid[4];
            snprintf(pid, sizeof(pid), "%02X", kEntries[i].code);
            g_hidden[i] = strstr(supported, pid) == nullptr;
        }
    }
}

void saveChoice() {
    char line[kEntryCount + 8];
    formatChoice(g_on, line, sizeof(line));
    writeLine(kChoiceFile, line);
}

// The ticked, offered entries, in catalogue order, onto the cards.
void assignCards() {
    unsigned c = 0;
    for (unsigned i = 0; i < kEntryCount && c < kCards; ++i) {
        if (g_on[i] && !g_hidden[i]) {
            g_cardEntry[c++] = i;
        }
    }
    for (; c < kCards; ++c) {
        g_cardEntry[c] = kEntryCount;
    }
}

bool dashPage() {
    return g_page >= kDashState0 && g_page < kDashState0 + kPages;
}

// A page that shows gauge values: a dashboard page or Live values.
bool livePage() {
    return dashPage() || g_page == kLiveState;
}

// ---- the collector ------------------------------------------------------------------------------------------------------
pid_t spawnProgram(const char* program, const char* const* args, const char* outPath, bool truncate) {
    const int in = open("/dev/null", O_RDONLY | O_CLOEXEC);
    const int out = open(outPath, O_WRONLY | O_CREAT | O_CLOEXEC | (truncate ? O_TRUNC : O_APPEND), 0644);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (in >= 0) {
        posix_spawn_file_actions_adddup2(&fa, in, 0);
    }
    if (out >= 0) {
        posix_spawn_file_actions_adddup2(&fa, out, 1);
        posix_spawn_file_actions_adddup2(&fa, out, 2);
    }
    posix_spawnattr_t at;
    pid_t pid = -1;
    if (initServiceSpawnAttr(&at) == 0) {
        if (posix_spawn(&pid, program, &fa, &at, const_cast<char* const*>(args), environ) != 0) {
            pid = -1;
        }
        posix_spawnattr_destroy(&at);
    }
    posix_spawn_file_actions_destroy(&fa);
    if (in >= 0) {
        close(in);
    }
    if (out >= 0) {
        close(out);
    }
    return pid;
}

pid_t spawnCollector(const char* const* args) {
    return spawnProgram(kCollector, args, kCollectorLog, false);
}

void reap(pid_t* pid) {
    if (*pid > 0) {
        const pid_t r = waitpid(*pid, nullptr, WNOHANG);
        if (r == *pid || (r < 0 && errno == ECHILD)) {
            *pid = -1;
        }
    }
}

void stopCollector() {
    if (g_collector > 0) {
        kill(g_collector, SIGTERM);
        reap(&g_collector);
        alog("gauges: collector stopped");
    }
    g_collectorWant[0] = 0;
}

// Diagnostics allowed, and the gateway; false with the reason otherwise.
bool diagnosticsAllowed(char* host, bool* demo, const char** why) {
    char yes[32] = "";
    *demo = false;
    if (!readLine(kDmeFile, yes, sizeof(yes)) && access(kDmeFile, F_OK) != 0) {
        *why = "Engine values off";
        return false;
    }
    if (!strncmp(yes, "demo", 4)) {
        *demo = true;
        return true;
    }
    if (!readLine(kGatewayFile, host, 16) || !host[0]) {
        *why = g_discoverPid > 0 ? "Finding gateway" : "No gateway";
        return false;
    }
    return true;
}

// The collector for the DME entries on the shown page's cards; stopped when none is shown.
void updateCollector() {
    char want[512] = "";
    size_t used = 0;
    if (livePage()) {
        const bool all = g_page == kLiveState;  // the list shows every card
        const unsigned first = all ? 0 : (g_page - kDashState0) * kCardsPerPage;
        for (unsigned c = first; c < (all ? kCards : first + kCardsPerPage); ++c) {
            const unsigned e = g_cardEntry[c];
            if (e < kEntryCount && kEntries[e].source != kCds && used + 8 < sizeof(want)) {
                used += static_cast<size_t>(snprintf(want + used, sizeof(want) - used, "%s%u", used ? "," : "", e));
            }
        }
    }
    reap(&g_collector);
    if (!strcmp(want, g_collectorWant) && (g_collector > 0 || !want[0])) {
        return;
    }
    stopCollector();
    if (!want[0]) {
        return;
    }
    char host[16] = "";
    bool demo = false;
    const char* why = "";
    if (!diagnosticsAllowed(host, &demo, &why)) {
        if (strcmp(g_dmeWhy, why) != 0) {
            snprintf(g_dmeWhy, sizeof(g_dmeWhy), "%s", why);
            alog("gauges: engine values not read: %s (entries %s)", why, want);
        }
        return;  // the DME cards show the reason
    }
    g_dmeWhy[0] = 0;
    const char* args[] = {kCollector, demo ? "--demo" : "--session", "--want", want, demo ? nullptr : "--host", host,
                          nullptr};
    g_collector = spawnCollector(args);
    snprintf(g_collectorWant, sizeof(g_collectorWant), "%s", want);
    alog("gauges: collector %s pid %d for entries %s (%s)", g_collector > 0 ? "started" : "NOT started",
         static_cast<int>(g_collector), want, demo ? "demo" : host);
}

void readCodes() {
    char host[16] = "";
    bool demo = false;
    const char* why = "";
    for (unsigned i = 0; i < kCodeLines; ++i) {
        setText(kCodeLine0 + i, "");
    }
    if (!diagnosticsAllowed(host, &demo, &why) || demo) {
        setText(kCodeLine0, demo ? "Demo mode: no codes read" : why);
        return;
    }
    reap(&g_codesPid);
    if (g_codesPid > 0) {
        return;
    }
    unlink(kCodesFile);
    const char* args[] = {kCollector, "--codes", "--host", host, "--out", kCodesFile, nullptr};
    g_codesPid = spawnCollector(args);
    setText(kCodeLine0, g_codesPid > 0 ? "Reading the engine's codes..." : "Collector missing");
}

void showCodes() {
    FILE* f = fopen(kCodesFile, "r");
    if (!f) {
        return;
    }
    char line[64];
    unsigned i = 0;
    while (i < kCodeLines && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        setText(kCodeLine0 + i++, line);
    }
    fclose(f);
}

// ---- pages ------------------------------------------------------------------------------------------------------------
bool fresh(unsigned e) {
    if (!g_valid[e]) {
        return false;
    }
    return kEntries[e].source == kCds || dme::monoMs() - g_seenAt[e] < dme::kStaleMs;
}

void showCard(unsigned c) {
    const unsigned e = g_cardEntry[c];
    const int shown = e < kEntryCount ? 1 : 0;
    if (g_cardShown[c] != shown) {
        setVisible(kCardTile0 + c, shown != 0);
        setVisible(kCardBarComp0 + c, shown != 0);
        g_cardShown[c] = shown;
    }
    if (e >= kEntryCount) {
        setText(kCardTitle0 + c, "");
        setText(kCardValue0 + c, "");
        return;
    }
    char text[48];
    const bool ok = fresh(e);
    valueText(kEntries[e], ok, g_value[e], text, sizeof(text));
    if (!ok && kEntries[e].source != kCds && g_dmeWhy[0]) {
        snprintf(text, sizeof(text), "%s", g_dmeWhy);  // why there is no value
    }
    setText(kCardTitle0 + c, kEntries[e].title);
    setText(kCardValue0 + c, text);
    setImage(kCardBar0 + c, barImage(c, ok ? barLevel(kEntries[e], g_value[e]) : 0));
}

void showDash() {
    const unsigned first = (g_page - kDashState0) * kCardsPerPage;
    for (unsigned c = first; c < first + kCardsPerPage; ++c) {
        showCard(c);
    }
}

unsigned usedCards() {
    unsigned n = 0;
    while (n < kCards && g_cardEntry[n] < kEntryCount) {
        ++n;
    }
    return n;
}

void showLive() {
    for (unsigned c = 0; c < kCards; ++c) {
        const unsigned e = g_cardEntry[c];
        const int shown = e < kEntryCount ? 1 : 0;
        if (g_liveShown[c] != shown) {
            setVisible(kLiveRowComp0 + c, shown != 0);
            g_liveShown[c] = shown;
        }
        if (!shown) {
            continue;
        }
        char value[40], row[48];
        valueText(kEntries[e], fresh(e), g_value[e], value, sizeof(value));
        if (!fresh(e) && kEntries[e].source != kCds && g_dmeWhy[0]) {
            snprintf(value, sizeof(value), "%s", g_dmeWhy);
        }
        snprintf(row, sizeof(row), "%s   %s", kEntries[e].title, value);
        setText(kLiveRow0 + c, row);
    }
}

// The Gauges list: Dashboard 2 and 3 only while they have a card; the engine values switch and its line.
void showMenu() {
    const bool on = access(kDmeFile, F_OK) == 0;
    g_app->updateModel(kDmeModel, on);
    char host[16] = "";
    const bool gateway = readLine(kGatewayFile, host, sizeof(host)) && host[0];
    char note[64];
    if (!on) {
        snprintf(note, sizeof(note), "Engine values off (boost, OBD values)");
    } else if (g_discoverPid > 0) {
        snprintf(note, sizeof(note), "Finding the gateway...");
    } else if (!gateway) {
        snprintf(note, sizeof(note), "Gateway not found: is the car awake?");
    } else {
        snprintf(note, sizeof(note), "Engine values on, gateway %s", host);
    }
    setText(kDmeNote, note);
    const unsigned used = usedCards();
    const int want[2] = {used > kCardsPerPage ? 1 : 0, used > 2 * kCardsPerPage ? 1 : 0};
    for (unsigned i = 0; i < 2; ++i) {
        if (g_menuShown[i] != want[i]) {
            setVisible(i == 0 ? kMenuDash2Comp : kMenuDash3Comp, want[i] != 0);
            g_menuShown[i] = want[i];
        }
    }
}

void showShift() {
    const unsigned target = kShiftPoints[g_shiftPoint];
    unsigned rpmEntry = kEntryCount, speedEntry = kEntryCount;
    for (unsigned i = 0; i < kEntryCount; ++i) {
        if (kEntries[i].source == kCds && !strcmp(kEntries[i].key, "RPMSpeed")) {
            rpmEntry = i;
        } else if (kEntries[i].source == kCds && !strcmp(kEntries[i].key, "speedActual")) {
            speedEntry = i;
        }
    }
    const bool live = rpmEntry < kEntryCount && g_valid[rpmEntry];
    const double rpm = live ? g_value[rpmEntry] : 0;
    unsigned level = live ? static_cast<unsigned>(rpm + 125) / 250 : 0;
    if (level >= kShiftLevels) {
        level = kShiftLevels - 1;
    }
    setImage(kShiftBar, (live && rpm >= target ? kImgShiftWarn0 : kImgShift0) + level);
    char text[48];
    if (live) {
        snprintf(text, sizeof(text), "%s%ld rpm", rpm >= target ? "SHIFT  " : "", static_cast<long>(rpm / 50 + 0.5) * 50);
    } else {
        snprintf(text, sizeof(text), "-- rpm");
    }
    setText(kShiftRpm, text);
    if (speedEntry < kEntryCount && g_valid[speedEntry]) {
        snprintf(text, sizeof(text), "%.0f km/h", g_value[speedEntry]);
    } else {
        snprintf(text, sizeof(text), "-- km/h");
    }
    setText(kShiftSpeed, text);
    snprintf(text, sizeof(text), "Shift point %u rpm", target);
    setText(kShiftTarget, text);
}

void showChoose() {
    unsigned hidden = 0;
    for (unsigned i = 0; i < kEntryCount; ++i) {
        g_app->updateModel(kChoiceModel0 + i, g_on[i]);
        setVisible(kChoiceComp0 + i, !g_hidden[i]);
        hidden += g_hidden[i] ? 1 : 0;
    }
    char text[96];
    unsigned ticked = 0;
    for (unsigned i = 0; i < kEntryCount; ++i) {
        ticked += g_on[i] && !g_hidden[i] ? 1 : 0;
    }
    snprintf(text, sizeof(text), "%u ticked, %u cards%s", ticked, kCards,
             hidden ? " (OBD values this DME lacks are hidden)" : "");
    setText(kChooseNote, text);
}

// The preview card's trip lines, as BMW's own My Vehicle card shows them on larger screens. The card has no visibility
// notice: kept current whenever one of its values changes (setText sends only changes).
void showTrip() {
    static const char* const kTrip[3][2] = {
        {"Avg consumption", "Consumption"}, {"Range", "Range"}, {"Avg speed", "Average speed"}};
    for (unsigned t = 0; t < 3; ++t) {
        for (unsigned i = 0; i < kEntryCount; ++i) {
            if (kEntries[i].source != kCds || strcmp(kEntries[i].title, kTrip[t][0]) != 0) {
                continue;
            }
            char value[32], line[64];
            valueText(kEntries[i], g_valid[i], g_value[i], value, sizeof(value));
            snprintf(line, sizeof(line), "%s  %s", kTrip[t][1], value);
            setText(kTripLine0 + t, line);
        }
    }
}

void refresh() {
    if (dashPage()) {
        showDash();
    } else if (g_page == kLiveState) {
        showLive();
    } else if (g_page == kMenuState) {
        showMenu();
    } else if (g_page == kShiftState) {
        showShift();
    } else if (g_page == kChooseState) {
        showChoose();
    } else if (g_page == kColourState) {
        char text[48];
        snprintf(text, sizeof(text), "Selected: %s", kColourNames[g_colour]);
        setText(kColourSelected, text);
    } else if (g_page == kShiftPointState) {
        char text[48];
        snprintf(text, sizeof(text), "Selected: %u rpm", kShiftPoints[g_shiftPoint]);
        setText(kShiftPointSelected, text);
    }
}

// ---- Car tests ----------------------------------------------------------------------------------------------------------
// One test at a time, started by the owner's press; its output on the page's lines (photographed) and appended to
// /var/local/data/AA/car-tests.log. Cluster tests: car-test (tools/car_test.cpp); gateway tests: gauge-collector.
const char kCarTest[] = "/opt/hmi/data/ro/Plugins/CarGauges/car-test";
const char kTestOut[] = "/tmp/car-test.txt";
const char kTestLog[] = "/var/local/data/AA/car-tests.log";
enum TestKind { kTestCluster, kTestGateway, kTestFindGateway, kTestCarData };
struct CarTest {
    const char* name;  // as the page's button (make_gauges_xml.py CAR_TESTS)
    TestKind kind;
    const char* args[6];
};
const CarTest kCarTests[] = {
    // The cluster's own interface (0xE0): its content selection and the rest, read only (does it know a navigation?)
    {"Cluster: settings read (read only)", kTestCluster, {"--kombi-read", "auto"}},
    {"Cluster: function list (auto)", kTestCluster, {"--cluster-functions", "auto"}},
    {"Cluster: function list (0101)", kTestCluster, {"--cluster-functions", "0101"}},
    {"Cluster: size of area 5 (auto)", kTestCluster, {"--cluster-size", "auto", "5"}},
    {"Cluster: size of area 5 (0101)", kTestCluster, {"--cluster-size", "0101", "5"}},
    {"Cluster: draw test arrow (auto)", kTestCluster, {"--cluster-draw", "auto", "10"}},
    {"Cluster: draw test arrow (0101)", kTestCluster, {"--cluster-draw", "0101", "10"}},
    {"Cluster: draw, high channel (0101)", kTestCluster, {"--cluster-draw", "0101", "10", "--channel", "1"}},
    // NBT-AAD sends the HUDTerminal calls with tx 4 (docs/CLUSTER-HUDTERMINAL.md): the size call has a known answer
    {"Cluster: size of area 5, tx 4 (0101)", kTestCluster, {"--cluster-size", "0101", "5", "--channel", "4"}},
    {"Cluster: draw, tx 4 (0101)", kTestCluster, {"--cluster-draw", "0101", "10", "--channel", "4"}},
    {"Listen to cluster blocks 20 s", kTestCluster, {"--listen", "20"}},
    {"Gateway: find it", kTestFindGateway, {"--discover", "--write", "/var/local/data/AA/gateway-ip"}},
    {"Gateway: probe the engine", kTestGateway, {"--probe"}},
    {"Gateway: supported OBD values", kTestGateway, {"--supported", "--write", "/var/local/data/AA/gauges-supported"}},
    // The head unit's own UDS tunnel over MOST (06.F0), as ID6 apps fall back to it: DME reads only (22, OBD 01)
    {"Engine via MOST tunnel (read only)", kTestCluster, {"--diag-read", "auto"}},
    {"Listen to the diagnosis block 20 s", kTestCluster, {"--diag-listen", "20"}},
    {"Car data: show all values", kTestCarData, {}},
};
const unsigned kCarTestCount = sizeof(kCarTests) / sizeof(kCarTests[0]);

pid_t g_testPid = -1;
int g_testRunning = -1;  // the test whose output is shown
const char* g_testState = "";  // its state on the results page: running, finished, ...

void showTestStatus() {
    char text[80];
    snprintf(text, sizeof(text), "%s%s%s", g_testRunning >= 0 ? kCarTests[g_testRunning].name : "No test run yet",
             g_testState[0] ? ": " : "", g_testState);
    setText(kTestStatus, text);
}

// The last kTestLines lines of the test's output, on the page.
void showTestLines() {
    FILE* f = fopen(kTestOut, "r");
    char ring[kTestLines][64];
    unsigned n = 0;
    if (f) {
        char line[160];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            snprintf(ring[n % kTestLines], sizeof(ring[0]), "%s", line);
            ++n;
        }
        fclose(f);
    }
    const unsigned first = n > kTestLines ? n - kTestLines : 0;
    for (unsigned i = 0; i < kTestLines; ++i) {
        setText(kTestLine0 + i, first + i < n ? ring[(first + i) % kTestLines] : "");
    }
}

// The test's output appended to the log, with its name and the time.
void archiveTest(int test) {
    FILE* in = fopen(kTestOut, "r");
    FILE* out = fopen(kTestLog, "a");
    if (out) {
        const time_t now = time(nullptr);
        char when[32];
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(out, "=== %s  %s\n", when, test >= 0 ? kCarTests[test].name : "?");
        char line[256];
        while (in && fgets(line, sizeof(line), in)) {
            fputs(line, out);
        }
        fclose(out);
    }
    if (in) {
        fclose(in);
    }
}

void writeCarData() {
    FILE* f = fopen(kTestOut, "w");
    if (!f) {
        return;
    }
    unsigned valid = 0, total = 0;
    for (unsigned i = 0; i < kEntryCount; ++i) {
        if (kEntries[i].source == kCds) {
            ++total;
            valid += g_valid[i] ? 1 : 0;
        }
    }
    fprintf(f, "car data: %u of %u values received\n", valid, total);
    for (unsigned i = 0; i < kEntryCount; ++i) {
        if (kEntries[i].source == kCds && g_valid[i]) {
            char text[32];
            valueText(kEntries[i], true, g_value[i], text, sizeof(text));
            fprintf(f, "%s: %s\n", kEntries[i].title, text);
        }
    }
    for (unsigned i = 0; i < kEntryCount; ++i) {
        if (kEntries[i].source == kCds && !g_valid[i]) {
            fprintf(f, "%s: none (%s %s)\n", kEntries[i].title, kEntries[i].property, kEntries[i].key);
        }
    }
    fclose(f);
}

void writeTestNote(const char* a, const char* b) {
    FILE* f = fopen(kTestOut, "w");
    if (f) {
        fprintf(f, "%s\n%s\n", a, b);
        fclose(f);
    }
}

void runTestProgram(const CarTest& t);

void runTest(unsigned test) {
    reap(&g_testPid);
    if (g_testPid > 0) {
        g_app->triggerEvent(kEvTestResults);  // the running one's results
        alog("gauges: car test pressed while one runs: results shown");
        return;
    }
    const CarTest& t = kCarTests[test];
    alog("gauges: car test \"%s\"", t.name);
    g_testRunning = static_cast<int>(test);
    g_testState = "running";
    runTestProgram(t);
    showTestStatus();
    showTestLines();
    g_app->triggerEvent(kEvTestResults);  // Test results opens on the press
}

void runTestProgram(const CarTest& t) {
    if (t.kind == kTestCarData) {
        writeCarData();
        archiveTest(g_testRunning);
        g_testState = "done";
        return;
    }
    char host[16] = "";
    if (t.kind == kTestGateway && (!readLine(kGatewayFile, host, sizeof(host)) || !host[0])) {
        writeTestNote(t.name, "The gateway is not known yet: run \"Gateway: find it\" first");
        g_testState = "not run";
        return;
    }
    const char* argv[12];
    unsigned n = 0;
    argv[n++] = t.kind == kTestCluster ? kCarTest : kCollector;
    for (unsigned i = 0; i < 6 && t.args[i]; ++i) {
        argv[n++] = t.args[i];
    }
    if (t.kind == kTestGateway) {
        argv[n++] = "--host";
        argv[n++] = host;
    }
    argv[n] = nullptr;
    g_testPid = spawnProgram(argv[0], argv, kTestOut, true);
    if (g_testPid <= 0) {
        writeTestNote(t.name, "Could not start the test program");
        alog("gauges: car test program %s did not start (errno %d)", argv[0], errno);
        g_testState = "could not start";
        return;
    }
    alog("gauges: car test pid %d", static_cast<int>(g_testPid));
}

// ---- the poll, while a page needs the collector -----------------------------------------------------------------------
AppRunnable g_pollRunnable;
bool g_pollPosted = false;

bool needsPoll() {
    return (livePage() && g_collector > 0) ||
           (g_page == kCodesState && g_codesPid > 0) || g_testPid > 0 || g_discoverPid > 0;
}

void pollRun(void*) {
    g_pollPosted = false;
    if (!g_app) {
        return;
    }
    char data[dme::kMaxPacket];
    for (int i = 0; i < 32 && g_udp >= 0; ++i) {
        const ssize_t k = recv(g_udp, data, sizeof(data), 0);
        if (k <= 0) {
            break;
        }
        dme::Frame f;
        if (!dme::parseFrame(data, static_cast<size_t>(k), kEntryCount, &f)) {
            continue;
        }
        const uint64_t now = dme::monoMs();
        for (unsigned v = 0; v < f.count; ++v) {
            const unsigned e = f.entry[v];
            g_valid[e] = true;
            g_value[e] = f.value[v];
            g_seenAt[e] = now - f.ageMs[v];
            if (!g_logged[e]) {
                g_logged[e] = true;
                alog("gauges: %s first value %.3f (DME)", kEntries[e].title, f.value[v]);
            }
        }
    }
    if (livePage()) {
        updateCollector();  // started again if it ended (gateway gone), at most every poll
        refresh();
    } else if (g_page == kCodesState) {
        reap(&g_codesPid);
        if (g_codesPid <= 0) {
            showCodes();
        }
    }
    if (g_discoverPid > 0) {
        reap(&g_discoverPid);
        if (g_discoverPid <= 0) {
            char host[16] = "";
            alog("gauges: gateway discovery ended: %s", readLine(kGatewayFile, host, sizeof(host)) && host[0] ? host : "not found");
            g_dmeWhy[0] = 0;
            showMenu();  // the list's line, whichever page shows (iDrive keeps the model)
        }
    }
    if (g_testPid > 0) {
        reap(&g_testPid);
        if (g_testPid <= 0) {
            g_testState = "finished";
        }
        // Test results (971) never reports being shown on the car: its lines are kept current whatever shows
        showTestStatus();
        showTestLines();
        if (g_testPid <= 0) {
            archiveTest(g_testRunning);
            alog("gauges: car test ended, results in %s", kTestLog);
        }
    }
    if (needsPoll()) {
        g_pollPosted = true;
        postRunnable(g_app, g_pollRunnable, kPollMs);
    }
}

void startPoll() {
    if (!g_pollPosted && g_app && needsPoll()) {
        g_pollPosted = true;
        postRunnable(g_app, g_pollRunnable, kPollMs);
    }
}

// ---- iDrive's notifications ------------------------------------------------------------------------------------------
void pageVisible(unsigned state, bool visible) {
    alog("gauges: state %u %s", state, visible ? "shown" : "hidden");
    if (!visible) {
        if (g_page == state) {
            g_page = 0;
            updateCollector();  // nothing shown: stopped
        }
        return;
    }
    g_page = state;
    forgetShown();  // the HMI may have dropped the models while hidden: everything sent again
    if (livePage()) {
        assignCards();
        updateCollector();
    } else if (state == kCodesState) {
        readCodes();
    } else if (state == kChooseState) {
        loadChoice();  // gauges-supported may have been written meanwhile
    } else if (state == kTestResultState) {
        showTestStatus();
        showTestLines();
    } else if (state == kMenuState) {
        assignCards();  // the ticks may have changed
    }
    startPoll();
    refresh();
}

template <unsigned State>
bool onVisible(void*, const bmw::Parcel& value) {
    pageVisible(State, parcelBool(value));
    return true;
}

template <unsigned Colour>
bool onColour(void*, const bmw::Parcel&) {
    g_colour = Colour;
    char text[8];
    snprintf(text, sizeof(text), "%u", Colour);
    writeLine(kColourFile, text);
    alog("gauges: colour %s", kColourNames[Colour]);
    refresh();
    return true;
}

template <unsigned Point>
bool onShiftPoint(void*, const bmw::Parcel&) {
    g_shiftPoint = Point;
    char text[8];
    snprintf(text, sizeof(text), "%u", Point);
    writeLine(kShiftFile, text);
    refresh();
    return true;
}

template <unsigned Test>
bool onCarTest(void*, const bmw::Parcel&) {
    runTest(Test);
    startPoll();
    return true;
}

template <unsigned N>
struct TestActions {
    static void add(bmw::PluginApp* self) {
        TestActions<N - 1>::add(self);
        addAction(self, kActTest0 + N - 1, &onCarTest<N - 1>);
    }
};
template <>
struct TestActions<0> {
    static void add(bmw::PluginApp*) {}
};

// "Engine values (DME)": the owner's switch for reading the engine computer (read only, gauges-dme). On without a
// known gateway: the collector looks for it (the gateway's own UDP announcement) and writes gateway-ip.
bool onDmeToggle(void*, const bmw::Parcel&) {
    const bool on = access(kDmeFile, F_OK) != 0;
    if (on) {
        writeLine(kDmeFile, "1");
    } else {
        unlink(kDmeFile);
    }
    alog("gauges: engine values (DME) %s", on ? "on" : "off");
    char host[16] = "";
    reap(&g_discoverPid);
    if (on && (!readLine(kGatewayFile, host, sizeof(host)) || !host[0]) && g_discoverPid <= 0) {
        const char* args[] = {kCollector, "--discover", "--write", kGatewayFile, nullptr};
        g_discoverPid = spawnCollector(args);
        alog("gauges: gateway discovery %s (pid %d)", g_discoverPid > 0 ? "started" : "NOT started",
             static_cast<int>(g_discoverPid));
    }
    g_dmeWhy[0] = 0;
    g_collectorWant[0] = 0;  // judged again on the next dashboard
    showMenu();
    startPoll();
    return true;
}

bool onLiveRow(void*, const bmw::Parcel&) {
    return true;  // a row of Live values: nothing to do
}

bool onReadCodes(void*, const bmw::Parcel&) {
    readCodes();
    startPoll();
    return true;
}

template <unsigned Entry>
bool onChoice(void*, const bmw::Parcel&) {
    g_on[Entry] = !g_on[Entry];
    saveChoice();
    alog("gauges: %s %s", kEntries[Entry].title, g_on[Entry] ? "ticked" : "unticked");
    assignCards();
    if (g_page == kChooseState) {
        showChoose();
    }
    return true;
}

// The actions of the Choose gauges checkboxes, one template instance per entry.
template <unsigned N>
struct ChoiceActions {
    static void add(bmw::PluginApp* self) {
        ChoiceActions<N - 1>::add(self);
        addAction(self, kChoiceAction0 + N - 1, &onChoice<N - 1>);
    }
};
template <>
struct ChoiceActions<0> {
    static void add(bmw::PluginApp*) {}
};

// ---- CDS ----------------------------------------------------------------------------------------------------------------
// The distinct CDS properties of the catalogue, one stub each (a CdsStub carries no context).
const unsigned kMaxProperties = 32;
const char* g_properties[kMaxProperties];
unsigned g_propertyCount = 0;

bool onProperty(const char* property, const bmw::BasicString<unsigned short>& value) {
    char text[256];
    narrow(value, text, sizeof(text));
    bool changed = false;
    for (unsigned i = 0; i < kEntryCount; ++i) {
        const Entry& e = kEntries[i];
        if (e.source != kCds || strcmp(e.property, property) != 0) {
            continue;
        }
        double v = 0;
        const bool valid = readField(text, e.key, e.invalid, &v);
        changed = changed || valid != g_valid[i] || (valid && v != g_value[i]);
        g_valid[i] = valid;
        if (valid) {
            g_value[i] = v;
        }
        if (!g_logged[i]) {
            g_logged[i] = true;
            alog("gauges: %s first value '%s' -> %s", e.title, text, valid ? "valid" : "none");
        }
    }
    if (changed) {
        showTrip();
    }
    if (changed && livePage()) {
        refresh();
    } else if (changed && g_page == kShiftState) {
        showShift();
    }
    return true;
}

template <unsigned N>
bool onCds(void*, const bmw::BasicString<unsigned short>& value) {
    return N < g_propertyCount ? onProperty(g_properties[N], value) : true;
}

template <unsigned N>
struct CdsStubs {
    static CdsStub at(unsigned i) { return i == N - 1 ? &onCds<N - 1> : CdsStubs<N - 1>::at(i); }
};
template <>
struct CdsStubs<0> {
    static CdsStub at(unsigned) { return nullptr; }
};

AppRunnable g_subRunnable;
void subscribeRun(void*) {
    if (!g_app) {
        return;
    }
    for (unsigned i = 0; i < g_propertyCount; ++i) {
        addCdsHandler(g_app, g_properties[i], CdsStubs<kMaxProperties>::at(i));
    }
    alog("gauges: %u CDS properties subscribed", g_propertyCount);
}

void collectProperties() {
    g_propertyCount = 0;
    for (unsigned i = 0; i < kEntryCount; ++i) {
        if (kEntries[i].source != kCds) {
            continue;
        }
        bool known = false;
        for (unsigned k = 0; k < g_propertyCount; ++k) {
            known = known || !strcmp(g_properties[k], kEntries[i].property);
        }
        if (!known && g_propertyCount < kMaxProperties) {
            g_properties[g_propertyCount++] = kEntries[i].property;
        }
    }
}

void openUdp() {
    g_udp = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(dme::kUdpPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (g_udp >= 0 && bind(g_udp, reinterpret_cast<struct sockaddr*>(&a), sizeof(a)) < 0) {
        alog("gauges: UDP %u busy: no DME values", dme::kUdpPort);
        close(g_udp);
        g_udp = -1;
    }
}

// ---- the app object -------------------------------------------------------------------------------------------------
bool gaugesOnInit(bmw::PluginApp* self) {
    for (unsigned p = 0; p < kPages; ++p) {
        static const ActionStub kDash[kPages] = {&onVisible<kDashState0>, &onVisible<kDashState0 + 1>,
                                                 &onVisible<kDashState0 + 2>};
        addStateHandler(self, kDashState0 + p, kHandlerVisible, kDash[p]);
    }
    addStateHandler(self, kShiftState, kHandlerVisible, &onVisible<kShiftState>);
    addStateHandler(self, kCodesState, kHandlerVisible, &onVisible<kCodesState>);
    addStateHandler(self, kColourState, kHandlerVisible, &onVisible<kColourState>);
    addStateHandler(self, kShiftPointState, kHandlerVisible, &onVisible<kShiftPointState>);
    addStateHandler(self, kChooseState, kHandlerVisible, &onVisible<kChooseState>);
    static const ActionStub kColour[] = {&onColour<0>, &onColour<1>, &onColour<2>, &onColour<3>, &onColour<4>,
                                         &onColour<5>, &onColour<6>, &onColour<7>, &onColour<8>};
    for (unsigned i = 0; i < sizeof(kColour) / sizeof(kColour[0]); ++i) {
        addAction(self, kActColour0 + i, kColour[i]);
    }
    static const ActionStub kPoint[] = {&onShiftPoint<0>, &onShiftPoint<1>, &onShiftPoint<2>, &onShiftPoint<3>};
    for (unsigned i = 0; i < 4; ++i) {
        addAction(self, kActShiftPoint0 + i, kPoint[i]);
    }
    addAction(self, kActReadCodes, &onReadCodes);
    TestActions<kCarTestCount>::add(self);
    addStateHandler(self, kTestState, kHandlerVisible, &onVisible<kTestState>);
    addStateHandler(self, kTestResultState, kHandlerVisible, &onVisible<kTestResultState>);
    addStateHandler(self, kMenuState, kHandlerVisible, &onVisible<kMenuState>);
    addStateHandler(self, kLiveState, kHandlerVisible, &onVisible<kLiveState>);
    addAction(self, kActLiveRow, &onLiveRow);
    addAction(self, kActDme, &onDmeToggle);
    ChoiceActions<kEntryCount>::add(self);
    g_colour = readIndex(kColourFile, kColours + 1, 0);
    g_shiftPoint = readIndex(kShiftFile, sizeof(kShiftPoints) / sizeof(kShiftPoints[0]), 2);
    loadChoice();
    assignCards();
    collectProperties();
    openUdp();
    const bool ok = self->onInit();
    alog("gauges: onInit -> %d, %u catalogue entries, colour %s", static_cast<int>(ok), kEntryCount,
         kColourNames[g_colour]);
    makeRunnable(g_pollRunnable, &pollRun);
    makeRunnable(g_subRunnable, &subscribeRun);
    postRunnable(self, g_subRunnable, 1500);  // as the Android Auto app: CDS once the app runs
    return ok;
}

void gaugesOnError(bmw::PluginApp*, unsigned code, const bmw::BasicString<char>& text) {
    const char* s = text.raw();
    alog("gauges: onError %u: %s", code, s ? s : "(null)");
}

void gaugesOnFinalize(bmw::PluginApp* self) {
    alog("gauges: onFinalize");
    g_page = 0;
    stopCollector();
    self->onFinalize();
}

bmw::PluginApp* createGauges(bmw::IDispatcher& dispatcher, const char* name, const bmw::Bundle& params,
                             const bmw::Bundle& config) {
    mkdir(contract::kDataDir, 0755);
    alog("gauges: create(name=%s), HMI pid %d", name ? name : "(null)", static_cast<int>(getpid()));
    if (access(kGaugesOffFile, F_OK) == 0) {
        alog("gauges: %s exists: not created", kGaugesOffFile);
        return nullptr;
    }
    if (g_app) {
        return nullptr;
    }
    memset(g_valid, 0, sizeof(g_valid));
    memset(g_logged, 0, sizeof(g_logged));
    memset(g_seenAt, 0, sizeof(g_seenAt));
    forgetShown();
    const PluginAppHooks hooks = {&gaugesOnInit, &gaugesOnError, &gaugesOnFinalize};
    void* base = nullptr;
    g_app = newPluginApp(dispatcher, name, params, config, hooks, &base);
    return g_app;
}

void destroyGauges(void* app) {
    alog("gauges: destroy(%p)", app);
    if (!app) {
        return;
    }
    if (app == g_app) {
        g_app = nullptr;
        stopCollector();
        if (g_udp >= 0) {
            close(g_udp);
            g_udp = -1;
        }
    }
    deletePluginApp(app);
}

}  // namespace
}  // namespace app
}  // namespace evo

// What ManagedPluginApp::init reads with dlsym (as in entry_aa_app.cpp): create at +0x10, destroy at +0x14.
extern "C" {
struct GaugesPluginAppInfo {
    uint16_t versionMajor;
    uint16_t versionMinor;
    const char* name;
    const char* vendor;
    uint32_t rhmiInterfaceVersion;
    bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&);
    void (*destroy)(void*);
};
__attribute__((visibility("default"))) GaugesPluginAppInfo pluginAppInfo = {
    1, 0, "Gauges", "CarGauges", 1, &evo::app::createGauges, &evo::app::destroyGauges};
}
