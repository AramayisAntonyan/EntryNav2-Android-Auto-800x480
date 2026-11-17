// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tests platform/dme_gauges.hpp and platform/hsfz.cpp: DID decoding, the frame of catalogue values to the Gauges app, the gateway's
// identification answer, and one exchange with a fake gateway (alive check, acknowledgement, "pending", answer).
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dme_gauges.hpp"
#include "hsfz.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

using namespace evo;

static void decoding() {
    double v = 0;
    const unsigned char amb[2] = {0x65, 0x00};  // 0x6500 / 256 = 101 kPa
    CHECK(dme::decodeDid(0x4201, amb, 2, &v) && v == 101);
    const unsigned char boost[2] = {0x5a, 0x00};  // 0x5a00 / 128 = 180 kPa
    CHECK(dme::decodeDid(0x4205, boost, 2, &v) && v == 180);
    const unsigned char cool[2] = {0x0e, 0x31};  // 3633 / 10 - 273.15 = 90.15 C
    CHECK(dme::decodeDid(0x4300, cool, 2, &v) && v > 90.1 && v < 90.2);
    const unsigned char none[2] = {0xff, 0xff};
    CHECK(!dme::decodeDid(0x4300, none, 2, &v) && !dme::decodeDid(0x4a49, none, 2, &v));
    const unsigned char neg[2] = {0xff, 0xfe};  // -0.2 degrees
    CHECK(dme::decodeDid(0x4a49, neg, 2, &v) && v < -0.19 && v > -0.21);
    const unsigned char iat[1] = {0x5c};  // 92 * .75 - 48 = 21 C
    CHECK(dme::decodeDid(0x580f, iat, 1, &v) && v == 21);
    CHECK(!dme::decodeDid(0x4300, cool, 1, &v));  // wrong length
    CHECK(!dme::decodeDid(0x1234, cool, 2, &v));  // unknown
}

static void frames() {
    dme::Frame f;
    memset(&f, 0, sizeof(f));
    f.seq = 7;
    f.count = 2;
    f.entry[0] = 52;
    f.value[0] = 0.85;
    f.ageMs[0] = 20;
    f.entry[1] = 58;
    f.value[1] = 90;
    f.ageMs[1] = 300;
    char packet[dme::kMaxPacket];
    const int n = dme::encodeFrame(f, packet, sizeof(packet));
    CHECK(n > 0);
    dme::Frame g;
    CHECK(dme::parseFrame(packet, static_cast<size_t>(n), 59, &g));
    CHECK(g.seq == 7 && g.count == 2 && g.entry[0] == 52 && g.value[0] == 0.85 && g.ageMs[1] == 300);
    CHECK(!dme::parseFrame(packet, static_cast<size_t>(n), 55, &g));  // an entry past the catalogue
    CHECK(dme::parseFrame("G2 1\n", 5, 59, &g) && g.count == 0);  // nothing fresh: an empty frame
    CHECK(!dme::parseFrame("G2 1 3:x:5", 10, 59, &g));
    CHECK(!dme::parseFrame("G2 1 3:1.0:-5", 13, 59, &g));
    CHECK(!dme::parseFrame("NBG3 1 0 0 0", 12, 59, &g));
}

static void ident() {
    const unsigned char gw[] = {0, 0, 0, 0x20, 0, 0x11, 'D', 'I', 'A', 'G', 'A', 'D', 'R', '1', '0', 'B', 'M', 'W'};
    const unsigned char hu[] = {0, 0, 0, 0x20, 0, 0x11, 'D', 'I', 'A', 'G', 'A', 'D', 'R', '6', '3', 'B', 'M', 'W'};
    CHECK(hsfz::identIsGateway(gw, sizeof(gw)));
    CHECK(!hsfz::identIsGateway(hu, sizeof(hu)));
    CHECK(!hsfz::identIsGateway(gw, 5));
    char diag[8];
    hsfz::identDiagAddr(gw, sizeof(gw), diag, sizeof(diag));
    CHECK(!strcmp(diag, "10"));
    hsfz::identDiagAddr(gw, 5, diag, sizeof(diag));
    CHECK(!strcmp(diag, "?"));
}

// The fake gateway: reads one request, answers with an alive check, an acknowledgement, "response pending" and
// the positive answer for DID 4300 (90.15 C).
static void* fakeGateway(void* arg) {
    const int fd = *static_cast<int*>(arg);
    unsigned char req[11];
    size_t got = 0;
    while (got < sizeof(req)) {
        const ssize_t k = read(fd, req + got, sizeof(req) - got);
        if (k <= 0) {
            return nullptr;
        }
        got += static_cast<size_t>(k);
    }
    const unsigned char alive[] = {0, 0, 0, 0, 0, 0x12};
    const unsigned char ack[] = {0, 0, 0, 5, 0, 2, 0xf4, 0x12, 0x22, 0x43, 0x00};
    const unsigned char pending[] = {0, 0, 0, 5, 0, 1, 0x12, 0xf4, 0x7f, 0x22, 0x78};
    const unsigned char answer[] = {0, 0, 0, 7, 0, 1, 0x12, 0xf4, 0x62, 0x43, 0x00, 0x0e, 0x31};
    if (write(fd, alive, sizeof(alive)) < 0 || write(fd, ack, sizeof(ack)) < 0 ||
        write(fd, pending, sizeof(pending)) < 0 || write(fd, answer, sizeof(answer)) < 0) {
        return nullptr;
    }
    unsigned char back[8];
    if (read(fd, back, sizeof(back)) < 0) {  // the tester's answer to the alive check
        return nullptr;
    }
    return nullptr;
}

static void exchange() {
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    pthread_t t;
    pthread_create(&t, nullptr, fakeGateway, &sv[1]);
    const unsigned char req[3] = {0x22, 0x43, 0x00};
    unsigned char reply[32];
    unsigned nrc = 99;
    const int n = hsfz::exchange(sv[0], hsfz::kDme, req, 3, reply, sizeof(reply), 2000, &nrc, nullptr);
    CHECK(n == 5 && reply[0] == 0x62 && reply[1] == 0x43 && reply[3] == 0x0e && nrc == 0);
    double v = 0;
    CHECK(dme::decodeDid(0x4300, reply + 3, 2, &v) && v > 90.1 && v < 90.2);
    pthread_join(t, nullptr);
    // Writing services are refused before anything is sent.
    const unsigned char write[4] = {0x2e, 0x43, 0x00, 0x01};
    CHECK(hsfz::exchange(sv[0], hsfz::kDme, write, 4, reply, sizeof(reply), 100, &nrc, nullptr) == -1);
    const unsigned char clear[4] = {0x14, 0xff, 0xff, 0xff};
    CHECK(hsfz::exchange(sv[0], hsfz::kDme, clear, 4, reply, sizeof(reply), 100, &nrc, nullptr) == -1);
    CHECK(hsfz::readOnlyService(0x22) && hsfz::readOnlyService(0x19) && !hsfz::readOnlyService(0x2f));
    close(sv[0]);
    close(sv[1]);
}

int main() {
    decoding();
    frames();
    ident();
    exchange();
    if (g_failures) {
        printf("FAIL gauge collector: %d check(s)\n", g_failures);
        return 1;
    }
    puts("PASS gauge collector: DID decoding, frames, gateway identification, HSFZ exchange, writes refused");
    return 0;
}
