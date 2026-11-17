// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// car-test: the on-car tests of the Gauges app's "Car tests" page, one per run, each started by the owner's press.
// Everything sent and received is printed (the page shows the last lines, the app appends all of it to
// /var/local/data/AA/car-tests.log).
//
// The cluster's HUDTerminal (FBlock 0x82, instance 00; JayJay3071/nbt-android-auto docs/CLUSTER-HUDTERMINAL.md, NBT-AAD
// #134): whether this EntryNav2 can reach it, and whether a 6WA shows anything drawn there.
//   --cluster-functions ADDR          Get of function 0x000 (the list of the block's functions): read only
//   --cluster-size ADDR HANDLE        ImageGetSize (0xC04) of a drawing area, StartResultAck as the NBT sends it
//   --cluster-draw ADDR SECONDS       the ported 6WA driver's test arrow ("TEST", 200 m), then its clear batch
//   --listen SECONDS                  listen-only shadows on 82.00, E0.00, E0.01 and 52.00; prints what comes
//   --kombi-read ADDR                 the cluster's own interface (FBlock 0xE0, CPxyKombiInterface): Get of its
//                                     function list, then a Get of each of its 66 functions: read only. The
//                                     content selection (KombiSelection2 0xCA7) and the rest, summed up at the end
//   --nav-draw ADDR SECONDS           the cluster's link to a navigation: registers the navigation block 0x52.00
//                                     (this car has none), answers the cluster's subscription with guidance
//                                     active and an arrival time, then the test drawing to 82.00 at ADDR, the
//                                     clear, guidance off, and 0x52 deregistered. Sends: only with the owner's OK
//   --serve ADDR                      the Android Auto service's cluster sender (kombi-6wa-live): reads "FKT HEX"
//                                     lines on stdin, sends each as StartResultAck, prints every answer
//   --raw ADDR FKT OP HEX             one call to 82.00 as given (hex payload, "-" for none), then 3 s of answers:
//                                     to try payload layouts (a StartResultAck's sender handle) on ImageGetSize
//   --diag-listen SECONDS             listen-only shadows on the diagnosis block 06.F0 and 06.63; nothing sent
//   --diag-read ADDR [REQ...]         the head unit's UDS tunnel over MOST (ID6 apps' MOST fallback, MIT): each
//                                     request wrapped as [63][F0] 31 01 0F 0B 12 <len:2> <req> to 06.F0 function
//                                     0x050 (op Start), the DME's reply unwrapped from 71 01 0F 0B. Only reads go
//                                     out: 22 xx xx (one identifier) and OBD 01 xx, DME only. Default: 22 4300
//                                     (coolant, to compare with iDrive's engine.temperature), 01 05, 01 0C
// ADDR: "auto" (0xFFFF, vmostd resolves the owner) or a MOST device address in hex (the cluster: 0101).
// --channel N picks the vMOST channel field (0 as vmostctl; 1 as its --high option).
// Every call goes through the 6WA driver's own checks (hudterm::callOk: allowed functions, payload shapes, handles).
#include <ctype.h>
#include <dlfcn.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <map>
#include <vector>

#include "hud6wa.hpp"
#include "hudterm.hpp"
#include "kombi_guide.hpp"
#include "kombi_phone.hpp"
#include "vmost_client.hpp"

namespace {

using namespace evo;

const uint8_t kHudFblock = 0x82, kHudInstance = 0x00;
const uint32_t kOpGet = 0x1, kOpStartResultAck = 0x6, kOpStatus = 0xC, kOpErrorAck = 0xF;
const uint8_t kKombiFblock = 0xE0, kNavFblock = 0x52;
const uint16_t kGuidanceActive = 0x276, kArrival = 0xD10;  // RG_GuidanceActive, DTI_DistanceArrivalTimeToDestination

// The cluster interface's functions, from the HMI's CPxyKombiInterface proxies (research/re/kombi-0xE0-functions.txt).
struct KombiFunction {
    uint16_t fkt;
    const char* name;
};
const KombiFunction kKombiFunctions[] = {
    {0x001, "?"}, {0x002, "?"}, {0x200, "NewCCMessage"}, {0x203, "RequestCCMsg"}, {0x204, "CCRolle"},
    {0x205, "CCRolleID"}, {0x300, "CBSDataServiceList"}, {0x302, "RequestCBSServiceHint"},
    {0x303, "CBSServiceHint"}, {0x305, "ConsumptionValueECOa"}, {0x306, "ConsumptionValueECOb"},
    {0x307, "ConsumptionValueECOc"}, {0x308, "ConsumptionValueRegularA"}, {0x309, "ConsumptionValueRegularB"},
    {0x30A, "ConsumptionValueRegularC"}, {0x3AB, "ConsumptionAverageElectricVehicle"},
    {0x500, "ActiveCCMessage"}, {0x501, "ExtendedCCMessage"}, {0x502, "MenuCCMessage"},
    {0x503, "StandardCCMessage"}, {0x504, "KombiExternalDisplay"}, {0x505, "OperationBordcomputer"},
    {0x506, "Timer"}, {0x507, "BordcomputerArrival"}, {0x508, "TimeDate"}, {0x509, "BordcomputerAverage"},
    {0x510, "BordcomputerTravelStart"}, {0x511, "BordcomputerTravelData"}, {0x512, "RangeTankStatus"},
    {0x514, "Stopwatch"}, {0x515, "ShowTimeDate"}, {0x516, "UnitSpeedSpeedometer"},
    {0x517, "CalculatedNaviKI"}, {0x518, "ECORange"}, {0x543, "BordcomputerStatus2"}, {0x550, "?"},
    {0x601, "?"}, {0x602, "AutomaticServiceCallTriggerExt"}, {0x700, "?"}, {0x701, "?"}, {0x702, "?"},
    {0x722, "?"}, {0x723, "?"}, {0xC10, "?"}, {0xC11, "VehicleDynamicHistoryTableConfig"}, {0xC37, "?"},
    {0xC39, "DisplayElectricalPowerDistribution"}, {0xC3A, "?"}, {0xC3D, "RouteDestinationElectricHybrid"},
    {0xC3E, "?"}, {0xC42, "?"}, {0xC95, "?"}, {0xC96, "?"}, {0xC97, "?"}, {0xC98, "?"}, {0xC99, "?"},
    {0xC9A, "?"}, {0xCA0, "?"}, {0xCA1, "?"}, {0xCA2, "EfficiencyValueElectricVehicle3"},
    {0xCA3, "EfficiencyValueHybrid1"}, {0xCA4, "EfficiencyValueHybrid2"}, {0xCA5, "EfficiencyValueHybrid3"},
    {0xCA6, "HUDSelection2"}, {0xCA7, "KombiSelection2"}, {0xCA8, "?"},
};
const unsigned kKombiFunctionCount = sizeof(kKombiFunctions) / sizeof(kKombiFunctions[0]);

// What the cluster interface answered, by function: the operation and the bytes.
struct Answer {
    unsigned op;
    std::vector<uint8_t> data;
};
std::map<uint16_t, Answer> g_kombiAnswers;

// --nav-draw: who subscribed to the navigation block, and what it asked (handled outside the callback).
uint16_t g_navSubscriber = 0;
bool g_navSubscribed = false;
unsigned g_navGets = 0, g_navMessages = 0;

// The head unit's UDS tunnel over MOST (ID6_apps_prerelease source/native/src/most/most_link.c, MIT): diagnosis
// FBlock 0x06, instance 0xF0, function 0x050, routine 0x0F0B, diagnostic source 0x63 (the head unit).
const uint8_t kDiagFblock = 0x06, kTunnelInst = 0xF0, kDiagSource = 0x63, kDme = 0x12;
const uint16_t kTunnelFkt = 0x050, kTunnelRoutine = 0x0F0B;

// [63][F0] 31 01 0F 0B <ecu> <len:2> <request>
std::vector<uint8_t> tunnelFrame(uint8_t ecu, const std::vector<uint8_t>& request) {
    std::vector<uint8_t> f = {kDiagSource, kTunnelInst, 0x31, 0x01, kTunnelRoutine >> 8, kTunnelRoutine & 0xFF, ecu,
                              static_cast<uint8_t>(request.size() >> 8), static_cast<uint8_t>(request.size())};
    f.insert(f.end(), request.begin(), request.end());
    return f;
}

// Only reads leave: 22 with one identifier, or OBD 01 with one PID.
bool readOnlyRequest(const std::vector<uint8_t>& r) {
    return (r.size() == 3 && r[0] == 0x22) || (r.size() == 2 && r[0] == 0x01);
}

// The last tunnel answer seen: the inner UDS reply (after [src][tgt] 71 01 0F 0B <ecu> <len:2>), or a refusal
// of the routine itself (7F 31 nrc).
std::vector<uint8_t> g_tunnelInner;
int g_tunnelRefused = -1;
unsigned g_tunnelReports = 0;

void noteTunnelReply(const uint8_t* p, unsigned n) {
    ++g_tunnelReports;
    if (n >= 5 && p[2] == 0x7F && p[3] == 0x31) {
        g_tunnelRefused = p[4];
        return;
    }
    if (n < 9 || p[2] != 0x71 || p[3] != 0x01 || p[4] != (kTunnelRoutine >> 8) || p[5] != (kTunnelRoutine & 0xFF)) {
        return;
    }
    const unsigned len = (static_cast<unsigned>(p[7]) << 8) | p[8];
    if (len <= n - 9) {
        g_tunnelInner.assign(p + 9, p + 9 + len);
    }
}

VmostClient g_vmost;
unsigned g_received = 0;

double nowS() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

void printHex(const uint8_t* d, unsigned n) {
    for (unsigned i = 0; i < n && i < 80; ++i) {
        printf(" %02X", d[i]);
    }
    if (n > 80) {
        printf(" ...");
    }
}

void onMessage(void* msg, void*) {
    VmostMsgView v;
    if (!parseVmostMsg(msg, v)) {
        return;
    }
    if (v.fblock != 0x82 && v.fblock != 0xE0 && v.fblock != 0x52 && v.fblock != kDiagFblock) {
        return;
    }
    if (v.fblock == kDiagFblock && v.fkt == kTunnelFkt && v.data) {
        noteTunnelReply(v.data, v.len);
    }
    ++g_received;
    if (v.fblock == kKombiFblock) {
        Answer& a = g_kombiAnswers[v.fkt];
        a.op = v.op;
        a.data.assign(v.data, v.data ? v.data + v.len : v.data);
    }
    if (v.fblock == kNavFblock) {
        ++g_navMessages;
        if (v.fkt == 0x001 && v.op == 0x0 && v.addr != 0x100) {  // Notification Set from a remote device
            g_navSubscriber = v.addr;
            g_navSubscribed = true;
        }
        if (v.op == kOpGet) {
            ++g_navGets;
        }
    }
    printf("rx %04X %02X.%02X.%03X op %X len %u:", v.addr, v.fblock, v.inst, v.fkt, v.op, v.len);
    if (v.data) {
        printHex(v.data, v.len);
    }
    if (v.op == 0xF && v.len >= 3 && v.data) {
        printf("  (ErrorAck %02X)", v.data[2]);
    }
    printf("\n");
    fflush(stdout);
}

// Messages for this long, printed by onMessage.
void pump(double seconds) {
    int fd = -1;
    if (g_vmost.fileDescriptorGet(&fd) != 0 || fd < 0) {
        printf("no vMOST descriptor\n");
        return;
    }
    const double end = nowS() + seconds;
    while (nowS() < end) {
        struct pollfd p;
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        if (poll(&p, 1, 100) > 0) {
            g_vmost.processMessages();
        }
    }
}

bool open(const uint8_t (*shadows)[2], unsigned count) {
    if (g_vmost.load() != VmostClient::kLoaded) {
        printf("vMOST not available: %s\n", g_vmost.error().c_str());
        return false;
    }
    if (g_vmost.connect(onMessage, nullptr, 0) != 0) {
        printf("vmostConnect failed\n");
        return false;
    }
    for (unsigned i = 0; i < count; ++i) {
        const int rc = g_vmost.shadowRegister(shadows[i][0], shadows[i][1]);
        printf("listen %02X.%02X: %s\n", shadows[i][0], shadows[i][1], rc == 0 ? "ok" : "refused");
    }
    return true;
}

uint16_t g_addr = 0xFFFF;
uint32_t g_channel = 0;
uint16_t g_reserved = 0, g_reserved2 = 0;  // --r1/--r2: the message's two reserved fields (a sender handle?)

int sendTo(uint8_t fblock, uint8_t inst, uint16_t addr, uint32_t op, uint16_t fkt,
           const std::vector<uint8_t>& payload) {
    VmostMsgOut m;
    memset(&m, 0, sizeof(m));
    m.addr = addr;
    m.fblock = fblock;
    m.inst = inst;
    m.fkt = fkt;
    m.op = op;
    m.len = static_cast<uint16_t>(payload.size());
    m.data = payload.empty() ? nullptr : &payload[0];
    m.channel = g_channel;
    m.reserved = g_reserved;
    m.reserved2 = g_reserved2;
    const int rc = g_vmost.send(&m);
    printf("tx %04X %02X.%02X.%03X op %X ch %u len %u:", addr, fblock, inst, fkt, op, g_channel, m.len);
    if (!payload.empty()) {
        printHex(&payload[0], static_cast<unsigned>(payload.size()));
    }
    printf("  -> %s\n", rc == 0 ? "sent" : "SEND FAILED");
    fflush(stdout);
    return rc;
}

int sendCall(uint32_t op, uint16_t fkt, const std::vector<uint8_t>& payload) {
    return sendTo(kHudFblock, kHudInstance, g_addr, op, fkt, payload);
}

// The 6WA driver's batches, sent for real (StartResultAck to 82.00), a pause between calls as the NBT paces them.
class VmostSink : public hudterm::HudSink {
public:
    bool submit(const hudterm::Batch& b) override {
        printf("batch: %s, %u calls\n", b.label.c_str(), static_cast<unsigned>(b.calls.size()));
        for (size_t i = 0; i < b.calls.size(); ++i) {
            sendCall(kOpStartResultAck, b.calls[i].fkt, b.calls[i].payload);
            pump(0.02);
        }
        return true;
    }
    bool live() const override { return true; }
};

const uint8_t kClusterShadows[][2] = {{0x82, 0x00}};
const uint8_t kListenShadows[][2] = {{0x82, 0x00}, {0xE0, 0x00}, {0xE0, 0x01}, {0x52, 0x00}};

int clusterFunctions() {
    if (!open(kClusterShadows, 1)) {
        return 1;
    }
    sendCall(kOpGet, 0x000, std::vector<uint8_t>());
    pump(3);
    printf("done: %u answer(s) from 82/E0/52\n", g_received);
    return 0;
}

int clusterSize(uint16_t handle) {
    if (!open(kClusterShadows, 1)) {
        return 1;
    }
    sendCall(kOpStartResultAck, hudterm::kGetSize, hudterm::getSize(handle));
    pump(3);
    printf("done: %u answer(s)\n", g_received);
    return 0;
}

// The 6WA driver's test drawing: the arrow 600 m before a left turn ("TEST"), then at 200 m its junction view (roads,
// route, NBT-AAD #162), each for half the time, then the clear. What the cluster shows is the result to photograph.
kombi::Maneuver testTurn(int metres) {
    kombi::Maneuver m;
    m.type = 7;  // left: the driver's "arrow -90"
    m.road = "TEST";
    char text[16];
    snprintf(text, sizeof(text), "%d", metres);
    m.distanceText = text;
    m.distanceUnit = kombi::kUnitMetres;
    m.hasMetres = true;
    m.metres = metres;
    return m;
}

int clusterDraw(double seconds) {
    if (!open(kClusterShadows, 1)) {
        return 1;
    }
    VmostSink sink;
    kombi::GuideSettings settings;
    settings.approachMiles = 0;  // at once
    hudterm::Hud6wa hud(sink, hudterm::k6wa, settings);
    kombi::Maneuver m = testTurn(600);
    hud.step(&m, nowS());
    if (hud.stopped()) {
        printf("the driver refused its own batch: nothing more sent\n");
        return 1;
    }
    printf("1/2 arrow sent (left, 600 m): look at the cluster (%.0f s)\n", seconds / 2);
    pump(seconds / 2);
    m = testTurn(200);
    hud.step(&m, nowS());
    printf("2/2 junction view sent (200 m): look at the cluster (%.0f s)\n", seconds / 2);
    pump(seconds / 2);
    hud.clear("test end");
    pump(1);
    printf("done: %u answer(s), test drawing cleared\n", g_received);
    return 0;
}

// One call as given: FKT and OP in hex, the payload as hex digits ("-": none).
int raw(const char* fktText, const char* opText, const char* hex) {
    char* end = nullptr;
    const unsigned long fkt = strtoul(fktText, &end, 16);
    if (*end || fkt > 0xFFF) {
        printf("bad function %s\n", fktText);
        return 2;
    }
    const unsigned long op = strtoul(opText, &end, 16);
    if (*end || op > 0xF) {
        printf("bad op %s\n", opText);
        return 2;
    }
    std::vector<uint8_t> payload;
    for (size_t i = 0; strcmp(hex, "-") != 0 && hex[i] && hex[i + 1]; i += 2) {
        char b[3] = {hex[i], hex[i + 1], 0};
        payload.push_back(static_cast<uint8_t>(strtoul(b, nullptr, 16)));
    }
    if (!open(kClusterShadows, 1)) {
        return 1;
    }
    sendCall(static_cast<uint32_t>(op), static_cast<uint16_t>(fkt), payload);
    pump(3);
    printf("done: %u answer(s)\n", g_received);
    return 0;
}

// The service's sender: one call per stdin line, "FKT HEX" ("-" for no payload), until stdin closes.
int serve() {
    if (!open(kClusterShadows, 1)) {
        return 1;
    }
    printf("serving: calls from stdin to %04X channel %u\n", g_addr, g_channel);
    fflush(stdout);
    char line[2048];
    while (fgets(line, sizeof(line), stdin)) {
        char fktText[8] = "", hex[1800] = "";
        if (sscanf(line, "%7s %1799s", fktText, hex) != 2) {
            continue;
        }
        const unsigned long fkt = strtoul(fktText, nullptr, 16);
        std::vector<uint8_t> payload;
        for (size_t i = 0; strcmp(hex, "-") != 0 && hex[i] && hex[i + 1]; i += 2) {
            char b[3] = {hex[i], hex[i + 1], 0};
            payload.push_back(static_cast<uint8_t>(strtoul(b, nullptr, 16)));
        }
        sendCall(kOpStartResultAck, static_cast<uint16_t>(fkt & 0xFFF), payload);
        pump(0.02);
    }
    pump(0.5);
    printf("stdin closed: %u answer(s)\n", g_received);
    return 0;
}

// The cluster's own interface, read only: its function list, then a Get of each function, one at a time.
int kombiRead() {
    const uint8_t shadows[][2] = {{kKombiFblock, 0x00}, {kKombiFblock, 0x01}};
    if (!open(shadows, 2)) {
        return 1;
    }
    sendTo(kKombiFblock, 0x00, g_addr, kOpGet, 0x000, std::vector<uint8_t>());
    pump(1.5);
    for (unsigned i = 0; i < kKombiFunctionCount; ++i) {
        printf("-- %03X %s\n", kKombiFunctions[i].fkt, kKombiFunctions[i].name);
        sendTo(kKombiFblock, 0x00, g_addr, kOpGet, kKombiFunctions[i].fkt, std::vector<uint8_t>());
        pump(0.2);
    }
    pump(1.0);
    unsigned values = 0, errors = 0;
    for (std::map<uint16_t, Answer>::const_iterator it = g_kombiAnswers.begin(); it != g_kombiAnswers.end(); ++it) {
        (it->second.op == kOpErrorAck ? errors : values) += 1;
    }
    const unsigned asked = kKombiFunctionCount + 1, got = values + errors;
    printf("summary: %u value(s), %u ErrorAck(s), %u silent of %u functions\n", values, errors,
           asked > got ? asked - got : 0, asked);
    const uint16_t key[] = {0x000, 0xCA7, 0xCA8, 0xCA6, 0x517, 0x504};
    const char* const keyName[] = {"functions", "KombiSelection2", "CA8 (availability?)", "HUDSelection2",
                                   "CalculatedNaviKI", "KombiExternalDisplay"};
    for (unsigned k = 0; k < sizeof(key) / sizeof(key[0]); ++k) {
        std::map<uint16_t, Answer>::const_iterator it = g_kombiAnswers.find(key[k]);
        printf("%03X %s:", key[k], keyName[k]);
        if (it == g_kombiAnswers.end()) {
            printf(" no answer");
        } else {
            printf(" op %X", it->second.op);
            if (!it->second.data.empty()) {
                printHex(&it->second.data[0], static_cast<unsigned>(it->second.data.size()));
            }
        }
        printf("\n");
    }
    return 0;
}

// libvmostclient's own FBlock registration (vmostctl -f), resolved here only: no other program needs it.
typedef int (*FblockFn)(uint8_t, uint8_t);
FblockFn fblockFunction(const char* name) {
    void* lib = dlopen(kVmostLibrary, RTLD_NOW | RTLD_NOLOAD);
    void* p = lib ? dlsym(lib, name) : nullptr;
    FblockFn f = nullptr;
    memcpy(&f, &p, sizeof(f));
    return f;
}

void sendGuidance(uint16_t to, bool active) {
    sendTo(kNavFblock, 0x00, to, kOpStatus, kGuidanceActive, std::vector<uint8_t>(1, active ? 1 : 0));
    if (active) {
        // distance 12345, unit 0, 19:30, AM/PM 0, next day 0: what the November test sent
        const uint8_t eta[] = {0x00, 0x00, 0x30, 0x39, 0x00, 0x13, 0x1E, 0x00, 0x00};
        sendTo(kNavFblock, 0x00, to, kOpStatus, kArrival, std::vector<uint8_t>(eta, eta + sizeof(eta)));
    }
}

// As the navigation this car lacks: registered on 0x52, guidance active to whoever subscribes, then the test drawing.
int navDraw(double seconds) {
    const uint8_t shadows[][2] = {{kHudFblock, kHudInstance}, {kKombiFblock, 0x00}};
    if (!open(shadows, 2)) {
        return 1;
    }
    FblockFn reg = fblockFunction("vmostFblockRegister"), dereg = fblockFunction("vmostFblockDeregister");
    if (!reg || !dereg) {
        printf("vmostFblockRegister not found in %s: nothing sent\n", kVmostLibrary);
        return 1;
    }
    const int rc = reg(kNavFblock, 0x00);
    printf("register 52.00 (navigation): %s\n", rc == 0 ? "ok" : "REFUSED");
    if (rc != 0) {
        return 1;
    }
    for (int i = 0; i < 100 && !g_navSubscribed; ++i) {
        pump(0.1);  // the cluster side subscribes within a few seconds (November: 0x101, to 0x276 and 0xD10)
    }
    const uint16_t to = g_navSubscribed ? g_navSubscriber : 0x101;
    printf("%s: guidance active to %04X\n", g_navSubscribed ? "subscribed" : "no subscription in 10 s", to);
    sendGuidance(to, true);
    pump(2);
    VmostSink sink;
    kombi::GuideSettings settings;
    settings.approachMiles = 0;
    hudterm::Hud6wa hud(sink, hudterm::k6wa, settings);
    kombi::Maneuver m = testTurn(600);
    hud.step(&m, nowS());
    printf("1/2 arrow sent as the navigation (left, 600 m): look at the cluster (%.0f s)\n", seconds / 2);
    sendGuidance(to, true);
    pump(seconds / 2);
    m = testTurn(200);
    hud.step(&m, nowS());
    printf("2/2 junction view sent (200 m): look at the cluster (%.0f s)\n", seconds / 2);
    pump(seconds / 2);
    hud.clear("test end");
    pump(1);
    sendGuidance(to, false);
    pump(0.5);
    printf("deregister 52.00: %s\n", dereg(kNavFblock, 0x00) == 0 ? "ok" : "failed");
    printf("done: %u answer(s); navigation block: %u message(s), %u Get(s)\n", g_received, g_navMessages, g_navGets);
    return 0;
}

int listen(double seconds) {
    if (!open(kListenShadows, sizeof(kListenShadows) / sizeof(kListenShadows[0]))) {
        return 1;
    }
    printf("listening %.0f s, nothing sent\n", seconds);
    fflush(stdout);
    pump(seconds);
    printf("done: %u message(s)\n", g_received);
    return 0;
}

const uint8_t kDiagShadows[][2] = {{kDiagFblock, kTunnelInst}, {kDiagFblock, kDiagSource}};

// "224300" -> 22 43 00; false on an odd length or a character that is not hex.
bool parseHex(const char* s, std::vector<uint8_t>& out) {
    out.clear();
    const size_t n = strlen(s);
    if (n == 0 || n % 2) {
        return false;
    }
    for (size_t i = 0; i < n; i += 2) {
        char b[3] = {s[i], s[i + 1], 0};
        char* end = nullptr;
        const unsigned long v = strtoul(b, &end, 16);
        if (*end || !isxdigit(static_cast<unsigned char>(b[0]))) {
            return false;
        }
        out.push_back(static_cast<uint8_t>(v));
    }
    return true;
}

int diagListen(double seconds) {
    if (!open(kDiagShadows, sizeof(kDiagShadows) / sizeof(kDiagShadows[0]))) {
        return 1;
    }
    printf("listening %.0f s on the diagnosis block, nothing sent\n", seconds);
    pump(seconds);
    printf("done: %u message(s), %u tunnel report(s)\n", g_received, g_tunnelReports);
    return 0;
}

// What a known reply means, for the page.
void describe(const std::vector<uint8_t>& q, const std::vector<uint8_t>& a) {
    if (a.size() >= 3 && a[0] == 0x7F) {
        printf("  = refused by the DME, code %02X\n", a[2]);
        return;
    }
    if (q[0] == 0x22 && q[1] == 0x43 && q[2] == 0x00 && a.size() >= 5 && a[0] == 0x62) {
        printf("  = coolant %.1f C\n", ((a[3] << 8) | a[4]) / 10.0 - 273.15);
    } else if (q[0] == 0x01 && q[1] == 0x05 && a.size() >= 3 && a[0] == 0x41) {
        printf("  = coolant %d C (OBD)\n", a[2] - 40);
    } else if (q[0] == 0x01 && q[1] == 0x0C && a.size() >= 4 && a[0] == 0x41) {
        printf("  = %u rpm\n", ((a[2] << 8) | a[3]) / 4);
    }
}

int diagRead(int count, char** texts) {
    std::vector<std::vector<uint8_t>> requests;
    for (int i = 0; i < count; ++i) {
        std::vector<uint8_t> r;
        if (!parseHex(texts[i], r) || !readOnlyRequest(r)) {
            printf("not a read request: %s (only 22 xxxx and 01 xx): nothing sent\n", texts[i]);
            return 2;
        }
        requests.push_back(r);
    }
    if (requests.empty()) {
        requests = {{0x22, 0x43, 0x00}, {0x01, 0x05}, {0x01, 0x0C}};
    }
    if (!open(kDiagShadows, 1)) {
        return 1;
    }
    unsigned answered = 0;
    for (size_t i = 0; i < requests.size(); ++i) {
        g_tunnelInner.clear();
        g_tunnelRefused = -1;
        printf("DME read");
        printHex(&requests[i][0], static_cast<unsigned>(requests[i].size()));
        printf("\n");
        sendTo(kDiagFblock, kTunnelInst, g_addr, 0x0, kTunnelFkt, tunnelFrame(kDme, requests[i]));
        const double end = nowS() + 3;
        while (nowS() < end && g_tunnelInner.empty() && g_tunnelRefused < 0) {
            pump(0.1);
        }
        if (!g_tunnelInner.empty()) {
            ++answered;
            printf("DME answer:");
            printHex(&g_tunnelInner[0], static_cast<unsigned>(g_tunnelInner.size()));
            printf("\n");
            describe(requests[i], g_tunnelInner);
        } else if (g_tunnelRefused >= 0) {
            printf("tunnel refused the request: code %02X\n", g_tunnelRefused);
        } else {
            printf("no answer in 3 s\n");
        }
        pump(0.3);
    }
    printf("done: %u of %u answered through the MOST tunnel; %u message(s) seen\n", answered,
           static_cast<unsigned>(requests.size()), g_received);
    return 0;
}

// No car: the test arrow's batch built by the real 6WA driver into a counting sink; every call checked, none sent.
class CountingSink : public hudterm::HudSink {
public:
    unsigned calls = 0, batches = 0;
    bool submit(const hudterm::Batch& b) override {
        ++batches;
        calls += static_cast<unsigned>(b.calls.size());
        return true;
    }
    bool live() const override { return false; }
};

int selftest() {
    CountingSink sink;
    kombi::GuideSettings settings;
    settings.approachMiles = 0;
    hudterm::Hud6wa hud(sink, hudterm::k6wa, settings);
    kombi::Maneuver m = testTurn(600);
    hud.step(&m, 1.0);
    m = testTurn(200);
    hud.step(&m, 2.0);
    hud.clear("selftest");
    // the arrow, the junction view, its route, the clear
    const bool ok = !hud.stopped() && sink.batches == 4 && sink.calls > 0;
    printf("selftest: test drawing %u batches, %u calls, %s\n", sink.batches, sink.calls, ok ? "OK" : "FAILED");

    // The tunnel frame as ID6's most_tunnel_build makes it, its reply parsed, and the read-only gate.
    const std::vector<uint8_t> frame = tunnelFrame(kDme, {0x22, 0x43, 0x00});
    const std::vector<uint8_t> want = {0x63, 0xF0, 0x31, 0x01, 0x0F, 0x0B, 0x12, 0x00, 0x03, 0x22, 0x43, 0x00};
    const uint8_t reply[] = {0xF0, 0x63, 0x71, 0x01, 0x0F, 0x0B, 0x12, 0x00, 0x05, 0x62, 0x43, 0x00, 0x0B, 0x9A};
    noteTunnelReply(reply, sizeof(reply));
    std::vector<uint8_t> hex;
    const bool tunnelOk = frame == want && g_tunnelInner == std::vector<uint8_t>({0x62, 0x43, 0x00, 0x0B, 0x9A}) &&
                          readOnlyRequest({0x22, 0x43, 0x00}) && readOnlyRequest({0x01, 0x0C}) &&
                          !readOnlyRequest({0x2E, 0x43, 0x00}) && !readOnlyRequest({0x31, 0x01, 0x00}) &&
                          !readOnlyRequest({0x22, 0x43}) && !readOnlyRequest({0x10, 0x03}) &&
                          parseHex("224300", hex) && hex.size() == 3 && !parseHex("22430", hex) &&
                          !parseHex("22x300", hex);
    printf("selftest: MOST tunnel frame and reply %s\n", tunnelOk ? "OK" : "FAILED");
    return ok && tunnelOk ? 0 : 1;
}

bool parseAddr(const char* s, uint16_t* out) {
    if (!strcmp(s, "auto")) {
        *out = 0xFFFF;
        return true;
    }
    char* end = nullptr;
    const unsigned long v = strtoul(s, &end, 16);
    if (!*s || *end || v > 0xFFFF) {
        return false;
    }
    *out = static_cast<uint16_t>(v);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    for (int i = 1; i + 1 < argc; ++i) {
        if (!strcmp(argv[i], "--r1")) {
            g_reserved = static_cast<uint16_t>(strtoul(argv[i + 1], nullptr, 16));
        }
        if (!strcmp(argv[i], "--r2")) {
            g_reserved2 = static_cast<uint16_t>(strtoul(argv[i + 1], nullptr, 16));
        }
        if (!strcmp(argv[i], "--channel")) {
            g_channel = static_cast<uint32_t>(atoi(argv[i + 1]));
        }
    }
    const char* mode = argc > 1 ? argv[1] : "";
    if (!strcmp(mode, "--selftest")) {
        return selftest();
    }
    if (!strcmp(mode, "--listen") && argc > 2) {
        return listen(atof(argv[2]));
    }
    if (!strcmp(mode, "--diag-listen") && argc > 2) {
        return diagListen(atof(argv[2]));
    }
    if (argc > 2 && !parseAddr(argv[2], &g_addr)) {
        printf("bad address %s\n", argv[2]);
        return 2;
    }
    if (!strcmp(mode, "--kombi-read") && argc > 2) {
        return kombiRead();
    }
    if (!strcmp(mode, "--nav-draw") && argc > 3) {
        return navDraw(atof(argv[3]));
    }
    if (!strcmp(mode, "--diag-read") && argc > 2) {
        int n = 0;  // the requests: the arguments after ADDR up to the first option
        while (3 + n < argc && strncmp(argv[3 + n], "--", 2) != 0) {
            ++n;
        }
        return diagRead(n, argv + 3);
    }
    if (!strcmp(mode, "--cluster-functions") && argc > 2) {
        return clusterFunctions();
    }
    if (!strcmp(mode, "--cluster-size") && argc > 3) {
        return clusterSize(static_cast<uint16_t>(atoi(argv[3])));
    }
    if (!strcmp(mode, "--serve") && argc > 2) {
        return serve();
    }
    if (!strcmp(mode, "--raw") && argc > 5) {
        return raw(argv[3], argv[4], argv[5]);
    }
    if (!strcmp(mode, "--cluster-draw") && argc > 3) {
        return clusterDraw(atof(argv[3]));
    }
    fprintf(stderr,
            "Usage: car-test --cluster-functions ADDR | --cluster-size ADDR HANDLE | --cluster-draw ADDR SECONDS | "
            "--listen SECONDS | --kombi-read ADDR | --nav-draw ADDR SECONDS | --diag-listen SECONDS | "
            "--diag-read ADDR [22xxxx|01xx ...]   [--channel N]   (ADDR: auto or hex)\n");
    return 2;
}
