// SPDX-License-Identifier: MIT
// gauge-collector: the engine values of the Gauges app, read from the DME through the car's diagnostic gateway (HSFZ,
// platform/hsfz.hpp), and the engine's fault codes. Reads: ReadDataByIdentifier (BMW values), OBD current data
// (service 01) and ReadDTCInformation (hsfz::readOnlyService). The one write is --clear, the DME's fault memory
// (hsfz::clearFaultMemory), started only from the Gauges app's confirmation page. Installed next to the Gauges
// app (opt/hmi/data/ro/Plugins/CarGauges) and started by it only while a dashboard page shows a DME value, with the
// gateway's address in /var/local/data/AA/gateway-ip (none while /var/local/data/AA/gauges-dme-off exists).
//
// Ported from ID6 apps' collector (github.com/inotbarnacle/ID6_apps_prerelease, source/native/src/collector.c), MIT,
// Copyright (c) 2026 ID6 Apps contributors (notice in platform/dme_gauges.hpp). Changes: Linux; any list of the
// catalogue's OBD and BMW values (platform/gauge_catalogue.hpp) instead of a fixed eight; the DME asked which OBD
// values it supports; a probe that prints the raw answers; the gateway found by the identification broadcast, as their
// desktop installer does; no MHD map switching. Code clearing as their Engine Fault Codes does it (14 FF FF FF, the
// extended session only after a refusal for the session).
//
//   gauge-collector --discover [--write FILE]          broadcast the vehicle identification, print/save the gateway
//   gauge-collector --supported --host IP [--write F]  the OBD PIDs this DME answers (01 00/20/40/60), hex, one line
//   gauge-collector --probe --host IP                  one read of every catalogue DME value, raw and decoded
//   gauge-collector --session --host IP --want LIST    the listed catalogue entries (comma-separated indexes) to the
//                                                      Gauges app (127.0.0.1:42860), until the app (its parent) goes
//   gauge-collector --codes --host IP [--out FILE]     the engine's stored fault codes
//   gauge-collector --clear --host IP [--out FILE]     clears the DME's fault memory, then reads the codes again
//   gauge-collector --demo --want LIST [--cycles N]    synthetic values, no car (page test)
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <math.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dme_gauges.hpp"
#include "gauge_catalogue.hpp"
#include "hsfz.hpp"

namespace {

using namespace evo;
using catalogue::Entry;
using catalogue::kEntries;
using catalogue::kEntryCount;

volatile sig_atomic_t g_running = 1;
void onSignal(int) {
    g_running = 0;
}

void pauseMs(int ms) {
    while (g_running && poll(nullptr, 0, ms) < 0 && errno == EINTR) {
    }
}

const unsigned kAmbientDid = 0x4201;  // ambient pressure: boost is the absolute pressure minus this
const unsigned kBoostDid = 0x4205;

// One DME value: 1 decoded, 0 refused or not plausible, -3 no answer in time (the connection is kept), -1 the
// connection failed. Raw answer printed when trace.
int readEntry(int fd, const Entry& e, double* value, bool trace) {
    unsigned char req[3];
    unsigned size;
    if (e.source == catalogue::kObd) {
        req[0] = 0x01;
        req[1] = static_cast<unsigned char>(e.code);
        size = 2;
    } else if (e.source == catalogue::kDid) {
        req[0] = 0x22;
        req[1] = static_cast<unsigned char>(e.code >> 8);
        req[2] = static_cast<unsigned char>(e.code);
        size = 3;
    } else {
        return 0;
    }
    unsigned char reply[64];
    unsigned nrc = 0;
    const int n = hsfz::exchange(fd, hsfz::kDme, req, size, reply, sizeof(reply), 500, &nrc, &g_running);
    if (n == -2) {
        if (trace) {
            printf("%-22s refused, NRC %02X\n", e.title, nrc);
        }
        return 0;
    }
    if (n < static_cast<int>(size)) {
        if (trace) {
            printf("%-22s no answer\n", e.title);
        }
        return n == -3 ? -3 : n < 0 ? -1 : 0;
    }
    const unsigned char* data = reply + size;  // after the positive service byte and the echoed PID / DID
    const unsigned dataLen = static_cast<unsigned>(n) - size;
    const bool ok = e.source == catalogue::kObd ? catalogue::obdValue(e, data, dataLen, value)
                                                : dme::decodeDid(e.code, data, dataLen, value);
    if (trace) {
        printf("%-22s", e.title);
        for (int i = 0; i < n; ++i) {
            printf(" %02X", reply[i]);
        }
        if (ok) {
            // boost comes as absolute kPa; the gauge shows it minus the ambient pressure, in bar
            const bool boost = e.source == catalogue::kDid && e.code == kBoostDid;
            printf(" -> %.3f %s\n", *value, boost ? "kPa absolute" : e.unit);
        } else {
            printf(" -> not decoded (length, sentinel or out of range)\n");
        }
    }
    return ok ? 1 : 0;
}

bool readAmbient(int fd, double* kpa) {
    const unsigned char req[3] = {0x22, kAmbientDid >> 8, kAmbientDid & 0xff};
    unsigned char reply[16];
    unsigned nrc = 0;
    const int n = hsfz::exchange(fd, hsfz::kDme, req, 3, reply, sizeof(reply), 500, &nrc, &g_running);
    return n >= 3 && dme::decodeDid(kAmbientDid, reply + 3, static_cast<size_t>(n - 3), kpa);
}

// The identification broadcast on every network of the unit (the 169.254 link and the car's internal VLANs), as
// E-Sys/ISTA send it: read only. Every answer is printed; the first gateway (DIAGADR10) is written.
int discover(const char* writeFile) {
    struct ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        puts("no network list");
        return 1;
    }
    char gateway[16] = "";
    int networks = 0, answers = 0;
    for (struct ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr) {
            continue;
        }
        char local[16], bcast[16];
        inet_ntop(AF_INET, &reinterpret_cast<const struct sockaddr_in*>(i->ifa_addr)->sin_addr, local, sizeof(local));
        inet_ntop(AF_INET, &reinterpret_cast<const struct sockaddr_in*>(i->ifa_broadaddr)->sin_addr, bcast,
                  sizeof(bcast));
        ++networks;
        hsfz::IdentAnswer got[8];
        const int n = hsfz::identify(local, bcast, 1500, got, 8);
        printf("%s (%s -> %s): %d answer(s)\n", i->ifa_name, local, bcast, n < 0 ? 0 : n);
        for (int k = 0; k < n; ++k) {
            answers += 1;
            printf("  %s DIAGADR%s%s\n", got[k].ip, got[k].diagAddr, got[k].gateway ? "  <- gateway" : "");
            if (got[k].gateway && !gateway[0]) {
                snprintf(gateway, sizeof(gateway), "%s", got[k].ip);
            }
        }
        fflush(stdout);
    }
    freeifaddrs(list);
    printf("%d network(s), %d answer(s), gateway %s\n", networks, answers, gateway[0] ? gateway : "not found");
    if (gateway[0] && writeFile) {
        FILE* f = fopen(writeFile, "w");
        if (f) {
            fprintf(f, "%s\n", gateway);
            fclose(f);
            printf("written to %s\n", writeFile);
        }
    }
    return gateway[0] ? 0 : 1;
}

// 01 00, 01 20, 01 40, 01 60: four bitmaps of 32 PIDs each, the last bit saying whether the next range exists.
int supported(const char* host, const char* writeFile) {
    const int fd = hsfz::connectGateway(host, hsfz::kPort, &g_running);
    if (fd < 0) {
        printf("gateway %s: no connection\n", host);
        return 1;
    }
    char line[512] = "";
    size_t used = 0;
    for (unsigned base = 0; base <= 0x60 && g_running; base += 0x20) {
        const unsigned char req[2] = {0x01, static_cast<unsigned char>(base)};
        unsigned char reply[16];
        unsigned nrc = 0;
        const int n = hsfz::exchange(fd, hsfz::kDme, req, 2, reply, sizeof(reply), 500, &nrc, &g_running);
        if (n < 6) {
            break;
        }
        const uint32_t bits = (static_cast<uint32_t>(reply[2]) << 24) | (static_cast<uint32_t>(reply[3]) << 16) |
                              (static_cast<uint32_t>(reply[4]) << 8) | reply[5];
        for (unsigned i = 0; i < 32; ++i) {
            if (bits & (0x80000000u >> i)) {
                used += static_cast<size_t>(snprintf(line + used, sizeof(line) - used, "%02X ", base + i + 1));
            }
        }
        if (!(bits & 1)) {
            break;  // no further range
        }
    }
    close(fd);
    printf("supported OBD PIDs: %s\n", line[0] ? line : "(none)");
    if (writeFile && line[0]) {
        FILE* f = fopen(writeFile, "w");
        if (f) {
            fprintf(f, "%s\n", line);
            fclose(f);
        }
    }
    return line[0] ? 0 : 1;
}

int probe(const char* host) {
    const int fd = hsfz::connectGateway(host, hsfz::kPort, &g_running);
    if (fd < 0) {
        printf("gateway %s:%u: no connection\n", host, hsfz::kPort);
        return 1;
    }
    printf("gateway %s:%u: connected\n", host, hsfz::kPort);
    double amb = 0;
    printf("%-22s %s\n", "Ambient pressure", readAmbient(fd, &amb) ? "read" : "not read");
    for (unsigned i = 0; i < kEntryCount && g_running; ++i) {
        double v = 0;
        if (kEntries[i].source != catalogue::kCds && readEntry(fd, kEntries[i], &v, true) == -1) {
            break;
        }
        pauseMs(30);
    }
    close(fd);
    return 0;
}

// The clear, before the codes are read again: false (the reason written) when it did not happen.
bool clearMemory(int fd, FILE* out, const char** after) {
    unsigned nrc = 0;
    bool session = false;
    const int rc = hsfz::clearFaultMemory(fd, hsfz::kDme, &nrc, &session, &g_running);
    if (rc == 1) {
        *after = session ? " after clear (extended session)" : " after clear";
        return true;
    }
    if (rc == -2 && (nrc == 0x22 || nrc == 0x83 || nrc == 0x88)) {
        fprintf(out, "Clear refused (NRC %02X): ignition on, engine off, then again\n", nrc);
    } else if (rc == -2) {
        fprintf(out, "Clear refused (NRC %02X)\n", nrc);
    } else {
        fprintf(out, "No answer to the clear: read the codes again\n");
    }
    return false;
}

// The DME's fault memory, cleared first when `clear`. The first line says what happened ("Engine: 2 codes", or why
// nothing was read), then one line per code, "XXXXXX  status SS", which the Gauges app describes.
int codes(const char* host, const char* outFile, bool clear) {
    FILE* out = outFile ? fopen(outFile, "w") : stdout;
    if (!out) {
        return 1;
    }
    const char* after = "";
    const int fd = hsfz::connectGateway(host, hsfz::kPort, &g_running);
    if (fd < 0) {
        fprintf(out, "Diagnostic gateway unavailable (%s:%u)\n", host, hsfz::kPort);
    } else if (clear && !clearMemory(fd, out, &after)) {
        close(fd);
    } else {
        // Fault, pending, confirmed, failed since clear or warning requested (BoostDeck's status mask).
        const unsigned kMask = 0xaf;
        const unsigned char req[3] = {0x19, 0x02, static_cast<unsigned char>(kMask)};
        unsigned char reply[4094];
        unsigned nrc = 0;
        const int n = hsfz::exchange(fd, hsfz::kDme, req, 3, reply, sizeof(reply), 1800, &nrc, &g_running);
        if (n == -2) {
            fprintf(out, "%sRead refused (NRC %02X)\n", clear ? "Cleared. " : "", nrc);
        } else if (n < 3 || (n - 3) % 4 != 0) {
            fprintf(out, clear ? "Cleared; the codes could not be read again\n" : "No valid answer\n");
        } else {
            const unsigned mask = reply[2] & kMask;
            int count = 0;
            for (int i = 3; i < n; i += 4) {
                count += (reply[i + 3] & mask) ? 1 : 0;
            }
            fprintf(out, "Engine: %d %s%s\n", count, count == 1 ? "code" : "codes", after);
            for (int i = 3, shown = 0; i < n && shown < 128; i += 4) {
                if (reply[i + 3] & mask) {
                    fprintf(out, "%02X%02X%02X  status %02X\n", reply[i], reply[i + 1], reply[i + 2], reply[i + 3]);
                    ++shown;
                }
            }
        }
        close(fd);
    }
    if (out != stdout) {
        fclose(out);
    }
    return 0;
}

// "3,25,40" -> indexes of DME entries (CDS ones are the app's own business and are skipped).
unsigned parseWant(const char* list, unsigned* out, unsigned max) {
    unsigned n = 0;
    while (list && *list && n < max) {
        char* end = nullptr;
        const unsigned long i = strtoul(list, &end, 10);
        if (end == list) {
            break;
        }
        if (i < kEntryCount && kEntries[i].source != catalogue::kCds) {
            out[n++] = static_cast<unsigned>(i);
        }
        list = *end == ',' ? end + 1 : end;
    }
    return n;
}

int session(const char* host, bool demo, const unsigned* want, unsigned count, unsigned cycles, unsigned udpPort) {
    const pid_t parent = getppid();
    const int sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (sock < 0) {
        return 1;
    }
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(static_cast<uint16_t>(udpPort));
    inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);  // local only
    bool wantsBoost = false;
    for (unsigned k = 0; k < count; ++k) {
        wantsBoost = wantsBoost || (kEntries[want[k]].source == catalogue::kDid && kEntries[want[k]].code == kBoostDid);
    }
    double value[dme::kMaxValues] = {0};
    uint64_t seen[dme::kMaxValues] = {0};
    double ambient = 0;
    uint64_t ambientAt = 0;
    int gw = -1, lock = -1;
    uint64_t retryAt = 0;
    // A value that does not answer is skipped, the connection kept; one that misses 3 times running rests 10 s, so
    // the others stay current (while driving, a slow answer used to drop the connection and every value went "--").
    unsigned misses[dme::kMaxValues] = {0};
    uint64_t restUntil[dme::kMaxValues] = {0};
    unsigned connects = 0, drops = 0, noAnswers = 0;
    // A gateway that closes the connection before answering anything (the car falling asleep, ignition off) is tried
    // again later each time, up to 10 s, instead of every second.
    unsigned quickDrops = 0, connectFails = 0;
    bool answered = false;  // an answer since the last connect
    uint64_t statusAt = dme::monoMs() + 30000, cycleMs = 0, stateAt = 0;
    const unsigned kTickMs = 150;
    unsigned cycle = 0;
    dme::Frame f;
    memset(&f, 0, sizeof(f));
    while (g_running && (!cycles || cycle < cycles) && (demo || getppid() == parent)) {
        const uint64_t start = dme::monoMs();
        if (demo) {
            const double t = cycle * (kTickMs / 1000.0);
            for (unsigned k = 0; k < count; ++k) {
                const Entry& e = kEntries[want[k]];
                value[k] = e.lo + (e.hi - e.lo) * (0.5 + 0.4 * sin(t + k));
                seen[k] = start;
            }
        } else {
            if (gw < 0 && start >= retryAt) {
                if (lock < 0) {
                    lock = hsfz::reserveGateway();
                }
                gw = lock >= 0 ? hsfz::connectGateway(host, hsfz::kPort, &g_running) : -1;
                answered = false;
                if (gw < 0) {
                    retryAt = dme::monoMs() + 2000;
                    connectFails += lock >= 0 ? 1 : 0;
                } else if (++connects <= 3 || connects % 20 == 0) {
                    printf("[%llu s] session: connected to %s (connection %u)\n",
                           static_cast<unsigned long long>(start / 1000), host, connects);
                    fflush(stdout);
                }
            }
            if (gw >= 0 && wantsBoost && start - ambientAt > 1000) {
                double a = 0;
                if (readAmbient(gw, &a)) {
                    ambient = a;
                    ambientAt = dme::monoMs();
                }
            }
            for (unsigned k = 0; gw >= 0 && k < count && g_running; ++k) {
                const Entry& e = kEntries[want[k]];
                if (restUntil[k] > dme::monoMs()) {
                    continue;
                }
                double v = 0;
                const int rc = readEntry(gw, e, &v, false);
                if (rc == -1) {
                    close(gw);
                    gw = -1;
                    quickDrops = answered ? 0 : quickDrops + 1;
                    const unsigned shift = quickDrops < 4 ? quickDrops : 4;
                    const unsigned waitMs = (1000u << shift) < 10000u ? (1000u << shift) : 10000u;
                    retryAt = dme::monoMs() + waitMs;
                    ++drops;
                    if (drops <= 3 || drops % 20 == 0) {
                        printf("[%llu s] session: connection lost at %s (%s; %u so far), again in %u s\n",
                               static_cast<unsigned long long>(dme::monoMs() / 1000), e.title,
                               answered ? "after answers" : "before any answer", drops, waitMs / 1000);
                        fflush(stdout);
                    }
                    break;
                }
                if (rc == -3) {
                    ++noAnswers;
                    if (++misses[k] >= 3) {
                        misses[k] = 0;
                        restUntil[k] = dme::monoMs() + 10000;
                        printf("[%llu s] session: %s did not answer 3 times: rests 10 s\n",
                               static_cast<unsigned long long>(dme::monoMs() / 1000), e.title);
                        fflush(stdout);
                    }
                    continue;
                }
                misses[k] = 0;
                answered = true;  // an answer, even a refusal: the connection works
                quickDrops = 0;
                if (rc != 1) {
                    continue;
                }
                if (e.source == catalogue::kDid && e.code == kBoostDid) {
                    if (!ambientAt || dme::monoMs() - ambientAt > 1800) {
                        continue;
                    }
                    v = (v - ambient) / 100.0;  // kPa absolute - measured ambient -> gauge bar
                } else if (e.source == catalogue::kDid && e.code == 0x5889) {
                    v *= 14.7;  // lambda -> gasoline-equivalent AFR
                }
                value[k] = v;
                seen[k] = dme::monoMs();
            }
        }
        f.seq++;
        f.count = 0;
        const uint64_t now = dme::monoMs();
        for (unsigned k = 0; k < count && f.count < dme::kMaxValues; ++k) {
            if (!seen[k] || now - seen[k] >= dme::kStaleMs) {
                continue;
            }
            f.entry[f.count] = want[k];
            f.value[f.count] = value[k];
            f.ageMs[f.count] = static_cast<unsigned>(now - seen[k]);
            ++f.count;
        }
        char packet[dme::kMaxPacket];
        const int len = dme::encodeFrame(f, packet, sizeof(packet));
        if (len <= 0 || sendto(sock, packet, static_cast<size_t>(len), 0, reinterpret_cast<const sockaddr*>(&dest),
                               sizeof(dest)) != len) {
            break;
        }
        ++cycle;
        const uint64_t elapsed = dme::monoMs() - start;
        cycleMs = elapsed;
        if (!demo && now >= stateAt) {  // about once a second: the state in words, for Gauges' Diagnostics
            char line[160];
            if (gw >= 0) {
                snprintf(line, sizeof(line), "connected to %s: %u of %u values current", host, f.count, count);
            } else if (lock < 0) {
                snprintf(line, sizeof(line), "waiting: another engine reader holds the gateway");
            } else if (drops) {
                snprintf(line, sizeof(line), "not connected to %s: it closed the connection %u times (car asleep?)",
                         host, drops);
            } else if (connectFails) {
                snprintf(line, sizeof(line), "not connected: %s does not accept (%u tries)", host, connectFails);
            } else {
                snprintf(line, sizeof(line), "connecting to %s", host);
            }
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%s.tmp", dme::kStatusFile);
            if (FILE* s = fopen(tmp, "w")) {
                fprintf(s, "%s\n", line);
                if (fclose(s) == 0) {
                    rename(tmp, dme::kStatusFile);
                }
            }
            stateAt = now + 1000;
        }
        if (!demo && now >= statusAt) {  // every 30 s: what the gauges get
            printf("[%llu s] session: %s, %u of %u values current, last pass %llu ms, %u no-answers, %u drops\n",
                   static_cast<unsigned long long>(now / 1000), gw >= 0 ? "connected" : "not connected", f.count, count,
                   static_cast<unsigned long long>(cycleMs), noAnswers, drops);
            fflush(stdout);
            statusAt = now + 30000;
        }
        pauseMs(elapsed < kTickMs ? static_cast<int>(kTickMs - elapsed) : 10);
    }
    if (gw >= 0) {
        close(gw);
    }
    if (lock >= 0) {
        close(lock);
    }
    close(sock);
    if (demo) {
        printf("demo: %u frames sent to 127.0.0.1:%u OK\n", cycle, udpPort);
    }
    return 0;
}

unsigned number(const char* s, unsigned lo, unsigned hi) {
    char* end = nullptr;
    const unsigned long n = strtoul(s, &end, 10);
    if (!*s || *end || n < lo || n > hi) {
        fprintf(stderr, "invalid number: %s\n", s);
        exit(2);
    }
    return static_cast<unsigned>(n);
}

}  // namespace

int main(int argc, char** argv) {
    const char *host = nullptr, *writeFile = nullptr, *outFile = nullptr, *wantList = nullptr;
    enum { kNone, kDiscover, kSupported, kProbe, kSession, kCodes, kClear, kDemo } mode = kNone;
    unsigned cycles = 0, udpPort = dme::kUdpPort;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const bool more = i + 1 < argc;
        if (!strcmp(a, "--discover") && mode == kNone) {
            mode = kDiscover;
        } else if (!strcmp(a, "--supported") && mode == kNone) {
            mode = kSupported;
        } else if (!strcmp(a, "--probe") && mode == kNone) {
            mode = kProbe;
        } else if (!strcmp(a, "--session") && mode == kNone) {
            mode = kSession;
        } else if (!strcmp(a, "--codes") && mode == kNone) {
            mode = kCodes;
        } else if (!strcmp(a, "--clear") && mode == kNone) {
            mode = kClear;
        } else if (!strcmp(a, "--demo") && mode == kNone) {
            mode = kDemo;
        } else if (!strcmp(a, "--host") && more) {
            host = argv[++i];
        } else if (!strcmp(a, "--write") && more) {
            writeFile = argv[++i];
        } else if (!strcmp(a, "--out") && more) {
            outFile = argv[++i];
        } else if (!strcmp(a, "--want") && more) {
            wantList = argv[++i];
        } else if (!strcmp(a, "--cycles") && more) {
            cycles = number(argv[++i], 1, 1000000);
        } else if (!strcmp(a, "--udp-port") && more) {
            udpPort = number(argv[++i], 1, 65535);
        } else {
            mode = kNone;
            break;
        }
    }
    const bool needsHost = mode == kSupported || mode == kProbe || mode == kSession || mode == kCodes || mode == kClear;
    if (mode == kNone || (needsHost && !host)) {
        fprintf(stderr,
                "Usage: gauge-collector --discover [--write FILE] | --supported --host IP [--write FILE] | "
                "--probe --host IP | --session --host IP --want LIST | --codes --host IP [--out FILE] | "
                "--clear --host IP [--out FILE] | --demo --want LIST [--cycles N]\n");
        return 2;
    }
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGPIPE, SIG_IGN);
    unsigned want[dme::kMaxValues];
    const unsigned count = parseWant(wantList, want, dme::kMaxValues);
    switch (mode) {
        case kDiscover:
            return discover(writeFile);
        case kSupported:
            return supported(host, writeFile);
        case kProbe:
            return probe(host);
        case kSession:
            return session(host, false, want, count, cycles, udpPort);
        case kCodes:
            return codes(host, outFile, false);
        case kClear:
            return codes(host, outFile, true);
        case kDemo:
            return session(nullptr, true, want, count, cycles, udpPort);
        default:
            return 2;
    }
}
