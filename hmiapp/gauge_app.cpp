// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The Gauges app, an iDrive app of its own under My Vehicle (libCarGauges.so, CarGauges.xml), next to Android Auto.
// Its entry has a car icon and a preview card with a car picture: the 800x480 iDrive shows no picture of its own there.
// The entry opens the dashboard straight away (the owner: the gauges were too far to reach); OPTION holds the rest.
//   Dashboard      up to 3 pages of 4 x 2 cells, filled in order with the ticked gauges, each in its look
//                  (hmiapp/gauge_layout.hpp): card (name, value, bar), dial (a half circle as BMW's clusters), wide
//                  bar or wide dial (two cells across), tall bar (two cells high, as ID6 apps' BoostDeck)
//   OPTION         Dashboard 2 / 3 (while they have gauges), Live values, Shift light, Gauge settings, Diagnostics
//   Live values    the same gauges as a scrolling list, one row each
//   Shift light    rpm bar, speed and the chosen shift point (CDS)
//   Gauge settings:
//     Choose gauges  every value of the catalogue (platform/gauge_catalogue.hpp): the iDrive's car data (CDS), standard
//                    OBD values and BMW engine values from the DME (gauge-collector, through the diagnostic gateway);
//                    OBD values the DME does not support (/var/local/data/AA/gauges-supported) are hidden
//     Gauge styles   one row per ticked gauge: a press gives it the next look
//     Shift point
//   Diagnostics:
//     Engine values (DME) switch and its state line
//     Fault codes    the engine's stored codes with a description (gauge-collector --codes); "Clear the codes..."
//                    opens a page that says what clearing does, whose one button clears the DME's fault memory
//                    (gauge-collector --clear, the owner's choice 2026-11-19), then shows the codes left
//     Car tests      a list of on-car tests; a press starts one and opens Test results
// Kept in /var/local/data/AA: gauges-choice (the ticks), gauges-style (the looks), gauges-shift. Red needles and white
// bars (no colour setting). The gauge pictures come from CarGauges_Art.pack (see setArt). DME
// values only once the owner created gauges-dme ("demo" in it: synthetic values) and the gateway's address is in
// gateway-ip.
//
// A port of ID6 apps' BoostDeck and ShiftDeck (github.com/inotbarnacle/ID6_apps_prerelease, MIT) from the NBT Evo's
// QNX to EntryNav2's Linux: the same RHMI framework and approach (bars and needles pre-drawn as images, switched by
// id; a worker process for the diagnostics, its values over loopback UDP), laid out for 800x480 and fed from a
// catalogue of every value instead of a fixed set. MHD map switching is not ported (stock DME); code clearing follows
// their Engine Fault Codes.
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

#include "../platform/dme_codes.hpp"
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
const char kArtPack[] = "/opt/hmi/data/ro/Plugins/CarGauges/CarGauges_Art.pack";
const char kArtDir[] = "/tmp/gauge-art";  // pictures unpacked on first use (RAM), sent by path
const char kShiftFile[] = "/var/local/data/AA/gauges-shift";
const char kStyleFile[] = "/var/local/data/AA/gauges-style";
const char kCollector[] = "/opt/hmi/data/ro/Plugins/CarGauges/gauge-collector";
const char kCollectorLog[] = "/tmp/gauge-collector.log";
const char kCodesFile[] = "/tmp/gauge-codes.txt";
const unsigned kPollMs = 150;
const unsigned kModels = 1024;  // every text and image model id is below this

bmw::PluginApp* g_app = nullptr;
unsigned g_page = 0;  // the state shown, 0: none of ours
unsigned g_shiftPoint = 2;

// Every catalogue entry's latest value: CDS from the iDrive, DME from the collector's frames.
bool g_on[kEntryCount];      // ticked
bool g_hidden[kEntryCount];  // an OBD value the DME does not support
bool g_valid[kEntryCount];
double g_value[kEntryCount];
uint64_t g_seenAt[kEntryCount];  // DME values: when the frame carrying it arrived (they go stale)
bool g_logged[kEntryCount];
unsigned g_look[kEntryCount];  // each entry's look (gauges-style)
unsigned g_list[kCards];       // the ticked, offered entries in order (Live values, Gauge styles)
unsigned g_listCount = 0;
Layout<kPages> g_layout;  // where they are on the dashboard pages

int g_udp = -1;
pid_t g_collector = -1;
pid_t g_codesPid = -1;
pid_t g_discoverPid = -1;  // gateway discovery started by the DME switch
char g_dmeWhy[48] = "";    // why the engine values are not read, "" when they are (cards show it)
char g_collectorWant[512];

char g_shownText[kModels][96];
unsigned g_shownImage[kModels];

// ---- models
// ----------------------------------------------------------------------------------------------------------
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

// ---- the pictures of CarGauges_Art.pack
// ------------------------------------------------------------------------------- iDrive refuses an image archive
// of 1.5 MB or more (the app then never opens), so every gauge picture is in a pack of our own: "GART", a count, then
// (id, offset, length) per picture, then the PNGs (hmiapp/make_db.py write_art). A picture is written to
// /tmp/gauge-art/<id>.png the first time it is needed and sent through an raImageModel by its path, as Android Auto's
// album cover is.
int g_packFd = -1;
struct ArtPlace {
    uint32_t offset, length;
};
ArtPlace g_art[kArtIds];  // length 0: not in the pack
bool g_artWritten[kArtIds];

void openArt() {
    g_packFd = open(kArtPack, O_RDONLY | O_CLOEXEC);
    unsigned char head[8];
    if (g_packFd < 0 || pread(g_packFd, head, 8, 0) != 8 || memcmp(head, "GART", 4) != 0) {
        alog("gauges: no picture pack %s: the gauges show no pictures", kArtPack);
        return;
    }
    uint32_t count = 0;
    memcpy(&count, head + 4, 4);
    unsigned found = 0;
    for (uint32_t i = 0; i < count && i < 65536; ++i) {
        uint32_t entry[3];
        if (pread(g_packFd, entry, sizeof(entry), 8 + 12 * static_cast<off_t>(i)) != sizeof(entry)) {
            break;
        }
        if (entry[0] >= kArtFirst && entry[0] < kArtFirst + kArtIds) {
            g_art[entry[0] - kArtFirst] = ArtPlace{entry[1], entry[2]};
            ++found;
        }
    }
    mkdir(kArtDir, 0755);
    alog("gauges: picture pack: %u pictures", found);
}

// The picture's file, unpacked if needed; false when the pack lacks it or the write failed.
bool artPath(unsigned id, char* path, size_t cap) {
    if (id < kArtFirst || id >= kArtFirst + kArtIds || g_packFd < 0) {
        return false;
    }
    const ArtPlace& a = g_art[id - kArtFirst];
    if (a.length == 0 || a.length > (1u << 20)) {
        return false;
    }
    snprintf(path, cap, "%s/%u.png", kArtDir, id);
    if (g_artWritten[id - kArtFirst]) {
        return true;
    }
    char* data = static_cast<char*>(malloc(a.length));
    bool ok = data && pread(g_packFd, data, a.length, a.offset) == static_cast<ssize_t>(a.length);
    if (ok) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s/%u.tmp", kArtDir, id);
        FILE* f = fopen(tmp, "wb");
        ok = f && fwrite(data, 1, a.length, f) == a.length;
        if (f) {
            ok = fclose(f) == 0 && ok;
        }
        ok = ok && rename(tmp, path) == 0;
    }
    free(data);
    g_artWritten[id - kArtFirst] = ok;
    return ok;
}

// An raImageModel shows the pack's picture `id` (sent only when it changes).
void setArt(unsigned model, unsigned id) {
    if (!g_app || model >= kModels || g_shownImage[model] == id) {
        return;
    }
    char path[64];
    if (!artPath(id, path, sizeof(path))) {
        return;
    }
    bmw::Image image{bmw::BasicString<char>(path)};
    if (image.isLoaded()) {
        g_app->updateModel(model, image);
        g_shownImage[model] = id;
    }
}

// Visibility as last set: -1 unknown, 0 hidden, 1 shown.
int g_lookShown[kLooks][kCards];  // a look's pictures at a cell
int g_liveShown[kCards];          // the Live values rows
int g_lookRowShown[kLookRows];    // the Gauge styles rows
int g_optionShown[kViews][2];     // each view's OPTION Dashboard 2 and 3

void forgetShown() {
    memset(g_shownText, 0, sizeof(g_shownText));
    memset(g_shownImage, 0, sizeof(g_shownImage));
    memset(g_lookShown, 0xff, sizeof(g_lookShown));  // -1
    memset(g_liveShown, 0xff, sizeof(g_liveShown));
    memset(g_lookRowShown, 0xff, sizeof(g_lookRowShown));
    memset(g_optionShown, 0xff, sizeof(g_optionShown));
}

void setVisible(unsigned component, bool visible) {
    if (g_app) {
        g_app->setProperty(component, kPropVisible, bmw::Parcel(visible));
    }
}

// Sets a component's visibility when it differs from *shown.
void showComponent(unsigned component, bool visible, int* shown) {
    if (*shown != (visible ? 1 : 0)) {
        setVisible(component, visible);
        *shown = visible ? 1 : 0;
    }
}

unsigned barImage(unsigned level) {
    return kImgBar0 + (level < kBarLevels ? level : kBarLevels - 1);
}

// ---- small files
// ------------------------------------------------------------------------------------------------------
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

void loadLooks() {
    char line[kEntryCount + 8] = "";
    parseLooks(readLine(kStyleFile, line, sizeof(line)) ? line : nullptr, g_look);
}

void saveLooks() {
    char line[kEntryCount + 8];
    formatLooks(g_look, line, sizeof(line));
    writeLine(kStyleFile, line);
}

// The ticked, offered entries, in catalogue order, laid out on the dashboard pages in their looks.
void assignCards() {
    g_listCount = 0;
    unsigned looks[kCards];
    for (unsigned i = 0; i < kEntryCount && g_listCount < kCards; ++i) {
        if (g_on[i] && !g_hidden[i]) {
            looks[g_listCount] = g_look[i];
            g_list[g_listCount++] = i;
        }
    }
    layOut(g_list, looks, g_listCount, &g_layout);
}

bool onDashboard(unsigned entry) {
    for (unsigned c = 0; c < kCards; ++c) {
        if (g_layout.entry[c] == entry) {
            return true;
        }
    }
    return false;
}

bool dashPage() {
    return g_page >= kDashState0 && g_page < kDashState0 + kPages;
}

// A page that shows gauge values: a dashboard page or Live values.
bool livePage() {
    return dashPage() || g_page == kLiveState;
}

// ---- the collector
// ------------------------------------------------------------------------------------------------------
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
        const bool all = g_page == kLiveState;  // the list shows every gauge
        const unsigned first = all ? 0 : (g_page - kDashState0) * kCardsPerPage;
        for (unsigned c = first; c < (all ? g_listCount : first + kCardsPerPage); ++c) {
            const unsigned e = all ? g_list[c] : g_layout.entry[c];
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

uint64_t g_clearResultUntil = 0;  // until then the codes page shows the clear's result instead of reading again

// Reads (clear false) or clears then reads (clear true) the DME's fault memory into kCodesFile.
void startCodes(bool clear) {
    reap(&g_codesPid);
    if (g_codesPid > 0) {
        return;  // one at a time: the page shows the running one's result
    }
    char host[16] = "";
    bool demo = false;
    const char* why = "";
    for (unsigned i = 0; i < kCodeLines; ++i) {
        setText(kCodeLine0 + i, "");
    }
    if (!diagnosticsAllowed(host, &demo, &why) || demo) {
        setText(kCodeLine0, demo ? "Demo mode: the engine is not asked" : why);
        if (clear) {
            alog("gauges: clear of the engine's codes not started: %s", demo ? "demo mode" : why);
        }
        return;
    }
    unlink(kCodesFile);
    const char* args[] = {kCollector, clear ? "--clear" : "--codes", "--host", host, "--out", kCodesFile, nullptr};
    g_codesPid = spawnCollector(args);
    if (clear) {
        alog("gauges: clear of the engine's codes %s (pid %d)", g_codesPid > 0 ? "started" : "NOT started",
             static_cast<int>(g_codesPid));
    }
    setText(kCodeLine0, g_codesPid <= 0 ? "Collector missing"
                        : clear         ? "Clearing the engine's codes..."
                                        : "Reading the engine's codes...");
}

void readCodes() {
    startCodes(false);
}

// One collector line "XXXXXX  status SS" as "XXXXXX Active: description"; other lines as they are.
void codeText(const char* line, char* out, size_t cap) {
    unsigned code = 0, status = 0;
    int used = 0;
    if (sscanf(line, "%6x  status %2x%n", &code, &status, &used) != 2 || line[used] != 0) {
        snprintf(out, cap, "%s", line);
        return;
    }
    // Status bits (ISO 14229): 0 test failed now, 2 pending, 3 confirmed (stored).
    const char* state = (status & 0x01) ? "Active" : (status & 0x04) ? "Pending" : "Stored";
    const char* text = dme::describeCode(code);
    snprintf(out, cap, "%06X %s: %s", code, state, text ? text : "no description (code not in the list)");
}

void showCodes() {
    FILE* f = fopen(kCodesFile, "r");
    if (!f) {
        return;
    }
    char line[96], text[96];
    unsigned i = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (i < kCodeLines) {
            codeText(line, text, sizeof(text));
            setText(kCodeLine0 + i, text);
            alog("gauges: codes: %s", text);
        }
        ++i;
    }
    fclose(f);
    if (i > kCodeLines) {  // the last row says how many more there are
        snprintf(text, sizeof(text), "... and %u more (all in %s)", i - kCodeLines + 1, kCodesFile);
        setText(kCodeLine0 + kCodeLines - 1, text);
    }
}

// The confirmation page's text (its models kept by iDrive: set at start and again when it shows).
void showClearNotes() {
    setText(kClearNote0, "Clears the engine computer's (DME) stored fault codes and their freeze-frame data.");
    setText(kClearNote0 + 1,
            "Ignition on, engine off. A fault still present comes back; emissions readiness restarts.");
}

bool onClearCodes(void*, const bmw::Parcel&) {
    alog("gauges: \"Yes, clear the engine's codes\" pressed");
    startCodes(true);
    g_clearResultUntil = dme::monoMs() + 120000;
    g_app->triggerEvent(kEvCodes);  // the codes page shows the clear's progress and result
    return true;
}

// ---- pages
// ------------------------------------------------------------------------------------------------------------
bool fresh(unsigned e) {
    if (!g_valid[e]) {
        return false;
    }
    return kEntries[e].source == kCds || dme::monoMs() - g_seenAt[e] < dme::kStaleMs;
}

// Cell c: the gauge that starts there, in its look; every other look's pictures hidden and labels empty there.
void showCell(unsigned c) {
    const unsigned e = g_layout.entry[c];
    const unsigned look = e < kEntryCount ? g_layout.look[c] : static_cast<unsigned>(kLooks);
    for (unsigned l = 0; l < kLooks; ++l) {
        if (!lookFits(l, c)) {
            continue;  // the look has no components at this cell
        }
        for (unsigned p = 0; p < kLookImages; ++p) {
            if (kLookImage0[l][p]) {
                showComponent(kLookImage0[l][p] + c, l == look, &g_lookShown[l][c]);
            }
        }
        if (l != look) {
            setText(kLookTitle0[l] + c, "");
            setText(kLookValue0[l] + c, "");
        }
    }
    if (look >= kLooks) {
        return;
    }
    const Entry& entry = kEntries[e];
    char text[48];
    const bool ok = fresh(e);
    valueText(entry, ok, g_value[e], text, sizeof(text));
    if (!ok && entry.source != kCds && g_dmeWhy[0]) {
        snprintf(text, sizeof(text), "%s", g_dmeWhy);  // why there is no value
    }
    setText(kLookTitle0[look] + c, entry.title);
    setText(kLookValue0[look] + c, text);
    const unsigned fine = ok ? levelOf(entry, g_value[e], kFineLevels) : 0;
    switch (look) {
        case kLookDial:
            setArt(kCardBack0 + c, kImgFace0 + e);
            setArt(kCardBar0 + c, kImgNeedle0 + fine);
            break;
        case kLookWide:
            setArt(kCardBack0 + c, kImgWideTrack0 + e);
            setArt(kCardBar0 + c, kImgWideBar0 + fine);
            break;
        case kLookWideDial:
            setArt(kCardBack0 + c, kImgWideFace0 + e);
            setArt(kCardBar0 + c, kImgWideNeedle0 + fine);
            break;
        case kLookArc:
            setArt(kCardBack0 + c, kImgArcFace0 + e);
            setArt(kCardBar0 + c, kImgArcNeedle0 + fine);
            break;
        case kLookWideArc:
            setArt(kCardBack0 + c, kImgWideArcFace0 + e);
            setArt(kCardBar0 + c, kImgWideArcNeedle0 + fine);
            break;
        case kLookTall:
            setArt(kCardBack0 + c, kImgTallTrack0 + e);
            setArt(kCardBar0 + c, kImgTallBar0 + fine);
            break;
        default:
            setArt(kCardBack0 + c, kImgCardIcon0 + e);
            setArt(kCardBar0 + c, barImage(ok ? barLevel(entry, g_value[e]) : 0));
            break;
    }
}

void showDash() {
    const unsigned first = (g_page - kDashState0) * kCardsPerPage;
    for (unsigned c = first; c < first + kCardsPerPage; ++c) {
        showCell(c);
    }
}

// The views' OPTION menus: Dashboard 2 and 3 only while they have a gauge. A view has no button for itself.
void showOptions() {
    for (unsigned v = 0; v < kViews; ++v) {
        for (unsigned i = 0; i < 2; ++i) {
            const unsigned item = i == 0 ? kOptionDash2 : kOptionDash3;
            if (v != item) {  // views 1 and 2 are Dashboard 2 and 3 themselves
                showComponent(kOptionComp0 + v * 10 + item, g_layout.pages > item, &g_optionShown[v][i]);
            }
        }
    }
}

void showLive() {
    for (unsigned c = 0; c < kCards; ++c) {
        const unsigned e = c < g_listCount ? g_list[c] : kEntryCount;
        const bool shown = e < kEntryCount;
        showComponent(kLiveRowComp0 + c, shown, &g_liveShown[c]);
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

// Diagnostics: the engine values switch and its line.
void showDiag() {
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
}

// Gauge styles: each ticked gauge with its look; a press gives it the next one.
void showLooks() {
    unsigned placed = 0;
    for (unsigned r = 0; r < kLookRows; ++r) {
        const bool shown = r < g_listCount;
        showComponent(kLookRowComp0 + r, shown, &g_lookRowShown[r]);
        if (!shown) {
            continue;
        }
        const unsigned e = g_list[r];
        const bool there = onDashboard(e);
        placed += there ? 1 : 0;
        char row[64];
        snprintf(row, sizeof(row), "%s: %s%s", kEntries[e].title, kLookNames[g_look[e] < kLooks ? g_look[e] : 0],
                 there ? "" : " (no room on the dashboard)");
        setText(kLookRow0 + r, row);
    }
    char note[96];
    snprintf(note, sizeof(note), "%u gauges on %u dashboard page%s. Press one to change its look.", placed,
             g_layout.pages, g_layout.pages == 1 ? "" : "s");
    setText(kLookNote, g_listCount ? note : "No gauge ticked: Choose gauges first");
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
        snprintf(text, sizeof(text), "%s%ld rpm", rpm >= target ? "SHIFT  " : "",
                 static_cast<long>(rpm / 50 + 0.5) * 50);
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
    const unsigned shown = g_listCount - g_layout.dropped;
    snprintf(text, sizeof(text), "%u ticked, %u on the dashboard%s", ticked, shown,
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
    } else if (g_page == kDiagState) {
        showDiag();
    } else if (g_page == kLookState) {
        showLooks();
    } else if (g_page == kShiftState) {
        showShift();
    } else if (g_page == kChooseState) {
        showChoose();
    } else if (g_page == kShiftPointState) {
        char text[48];
        snprintf(text, sizeof(text), "Selected: %u rpm", kShiftPoints[g_shiftPoint]);
        setText(kShiftPointSelected, text);
    }
}

// ---- Car tests
// ---------------------------------------------------------------------------------------------------------- One test
// at a time, started by the owner's press; its output on the page's lines (photographed) and appended to
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
int g_testRunning = -1;        // the test whose output is shown
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

// ---- the poll, while a page needs the collector
// -----------------------------------------------------------------------
AppRunnable g_pollRunnable;
bool g_pollPosted = false;

bool needsPoll() {
    return (livePage() && g_collector > 0) || g_codesPid > 0 || g_testPid > 0 || g_discoverPid > 0;
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
    }
    if (g_codesPid > 0) {
        reap(&g_codesPid);
        if (g_codesPid <= 0) {
            showCodes();  // whichever page shows: the models stay for the codes page
        }
    }
    if (g_discoverPid > 0) {
        reap(&g_discoverPid);
        if (g_discoverPid <= 0) {
            char host[16] = "";
            alog("gauges: gateway discovery ended: %s",
                 readLine(kGatewayFile, host, sizeof(host)) && host[0] ? host : "not found");
            g_dmeWhy[0] = 0;
            showDiag();  // the Diagnostics line, whichever page shows (iDrive keeps the model)
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

// ---- iDrive's notifications
// ------------------------------------------------------------------------------------------
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
    if (livePage() || state == kShiftState) {
        assignCards();
        updateCollector();
        showOptions();  // the view's OPTION: Dashboard 2 and 3 while they have gauges
    } else if (state == kCodesState) {
        reap(&g_codesPid);
        if (g_codesPid > 0) {
            // a read or the clear runs: its "...ing the engine's codes" line stays until its result replaces it
        } else if (dme::monoMs() < g_clearResultUntil) {
            showCodes();  // back from the clear: its result, not a new read
        } else {
            readCodes();
        }
    } else if (state == kClearState) {
        showClearNotes();
    } else if (state == kChooseState) {
        loadChoice();  // gauges-supported may have been written meanwhile
    } else if (state == kTestResultState) {
        showTestStatus();
        showTestLines();
    } else if (state == kLookState) {
        assignCards();  // the ticks may have changed
    }
    startPoll();
    refresh();
}

// The entry: the combinedAction's raAction, its hmiAction opens the dashboard. The dashboard's own visibility notice
// may never come (the old first page, opened by the entry, never sent one): it is treated as shown from here.
bool onEntry(void*, const bmw::Parcel&) {
    alog("gauges: entry pressed: dashboard");
    pageVisible(kDashState0, true);
    return true;
}

template <unsigned Row>
bool onLookRow(void*, const bmw::Parcel&) {
    if (Row >= g_listCount) {
        return true;
    }
    const unsigned e = g_list[Row];
    g_look[e] = nextLook(g_look[e]);
    saveLooks();
    assignCards();
    alog("gauges: %s look %s%s", kEntries[e].title, kLookNames[g_look[e]], onDashboard(e) ? "" : " (no room)");
    showLooks();
    return true;
}

template <unsigned N>
struct LookActions {
    static void add(bmw::PluginApp* self) {
        LookActions<N - 1>::add(self);
        addAction(self, kActLookRow0 + N - 1, &onLookRow<N - 1>);
    }
};
template <>
struct LookActions<0> {
    static void add(bmw::PluginApp*) {}
};

template <unsigned State>
bool onVisible(void*, const bmw::Parcel& value) {
    pageVisible(State, parcelBool(value));
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
    showDiag();
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

// ---- CDS
// ---------------------------------------------------------------------------------------------------------------- The
// distinct CDS properties of the catalogue, one stub each (a CdsStub carries no context).
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
    addStateHandler(self, kShiftPointState, kHandlerVisible, &onVisible<kShiftPointState>);
    addStateHandler(self, kChooseState, kHandlerVisible, &onVisible<kChooseState>);
    static const ActionStub kPoint[] = {&onShiftPoint<0>, &onShiftPoint<1>, &onShiftPoint<2>, &onShiftPoint<3>};
    for (unsigned i = 0; i < 4; ++i) {
        addAction(self, kActShiftPoint0 + i, kPoint[i]);
    }
    addAction(self, kActReadCodes, &onReadCodes);
    addAction(self, kActClearCodes, &onClearCodes);
    addStateHandler(self, kClearState, kHandlerVisible, &onVisible<kClearState>);
    TestActions<kCarTestCount>::add(self);
    addStateHandler(self, kTestState, kHandlerVisible, &onVisible<kTestState>);
    addStateHandler(self, kTestResultState, kHandlerVisible, &onVisible<kTestResultState>);
    addStateHandler(self, kSettingsState, kHandlerVisible, &onVisible<kSettingsState>);
    addStateHandler(self, kDiagState, kHandlerVisible, &onVisible<kDiagState>);
    addStateHandler(self, kLookState, kHandlerVisible, &onVisible<kLookState>);
    addStateHandler(self, kLiveState, kHandlerVisible, &onVisible<kLiveState>);
    addAction(self, kActEntry, &onEntry);
    addAction(self, kActLiveRow, &onLiveRow);
    addAction(self, kActDme, &onDmeToggle);
    ChoiceActions<kEntryCount>::add(self);
    LookActions<kLookRows>::add(self);
    g_shiftPoint = readIndex(kShiftFile, sizeof(kShiftPoints) / sizeof(kShiftPoints[0]), 2);
    loadChoice();
    loadLooks();
    assignCards();
    collectProperties();
    openUdp();
    openArt();
    const bool ok = self->onInit();
    showClearNotes();
    alog("gauges: onInit -> %d, %u catalogue entries", static_cast<int>(ok), kEntryCount);
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
