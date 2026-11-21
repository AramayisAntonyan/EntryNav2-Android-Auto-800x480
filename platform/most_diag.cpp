// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// See most_diag.hpp.
#include "most_diag.hpp"

#include <poll.h>
#include <string.h>
#include <time.h>

namespace evo {
namespace mostdiag {

namespace {
uint64_t nowMs() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000 + static_cast<uint64_t>(t.tv_nsec) / 1000000;
}
}  // namespace

bool readOnly(const uint8_t* req, unsigned n) {
    return req && ((n == 3 && req[0] == 0x22) || (n == 2 && req[0] == 0x01) || (n >= 2 && req[0] == 0x19));
}

unsigned frame(uint8_t ecu, const uint8_t* req, unsigned n, uint8_t* out, unsigned cap) {
    if (!req || 9 + n > cap || n > 0xFFFF) {
        return 0;
    }
    const uint8_t head[9] = {kSource,
                             kInst,
                             0x31,
                             0x01,
                             static_cast<uint8_t>(kRoutine >> 8),
                             static_cast<uint8_t>(kRoutine & 0xFF),
                             ecu,
                             static_cast<uint8_t>(n >> 8),
                             static_cast<uint8_t>(n)};
    memcpy(out, head, 9);
    memcpy(out + 9, req, n);
    return 9 + n;
}

int parse(const uint8_t* p, unsigned n, const uint8_t** inner, unsigned* innerLen, unsigned* nrc) {
    if (!p) {
        return 0;
    }
    if (n >= 5 && p[2] == 0x7F && p[3] == 0x31) {
        if (nrc) {
            *nrc = p[4];
        }
        return -2;
    }
    if (n < 9 || p[2] != 0x71 || p[3] != 0x01 || p[4] != (kRoutine >> 8) || p[5] != (kRoutine & 0xFF)) {
        return 0;
    }
    const unsigned len = (static_cast<unsigned>(p[7]) << 8) | p[8];
    if (len > n - 9) {
        return 0;
    }
    *inner = p + 9;
    *innerLen = len;
    return 1;
}

Tunnel::Tunnel() : open_(false), want_(nullptr), wantLen_(0), gotLen_(0), gotNrc_(0) {}

bool Tunnel::open(const char** why) {
    if (open_) {
        return true;
    }
    if (vmost_.load() != VmostClient::kLoaded) {
        *why = "vMOST not available";
        return false;
    }
    if (vmost_.connect(&Tunnel::onMessage, this, 0) != 0) {
        *why = "vMOST connection refused";
        return false;
    }
    if (vmost_.shadowRegister(kFblock, kInst) != 0) {
        *why = "the diagnosis block 06.F0 not available";
        return false;
    }
    open_ = true;
    return true;
}

void Tunnel::onMessage(void* msg, void* self) {
    Tunnel* t = static_cast<Tunnel*>(self);
    VmostMsgView v;
    if (!t || !t->want_ || !parseVmostMsg(msg, v) || v.fblock != kFblock || v.fkt != kFkt || !v.data) {
        return;
    }
    const uint8_t* inner = nullptr;
    unsigned len = 0, nrc = 0;
    const int r = parse(v.data, v.len, &inner, &len, &nrc);
    if (r == -2) {
        t->gotNrc_ = nrc;
        t->gotLen_ = -2;
        return;
    }
    if (r != 1 || len < 1) {
        return;
    }
    const uint8_t* q = t->want_;
    if (len >= 3 && inner[0] == 0x7F && inner[1] == q[0]) {
        if (inner[2] != 0x78) {  // 0x78: the answer is pending, keep waiting
            t->gotNrc_ = inner[2];
            t->gotLen_ = -2;
        }
        return;
    }
    // the positive answer echoes the request's parameters (the DID, the PID, the sub-function)
    const unsigned echo = q[0] == 0x19 ? 1 : t->wantLen_ - 1;
    if (inner[0] != static_cast<uint8_t>(q[0] + 0x40) || len < 1 + echo || memcmp(inner + 1, q + 1, echo) != 0 ||
        len > sizeof(t->got_)) {
        return;
    }
    memcpy(t->got_, inner, len);
    t->gotLen_ = static_cast<int>(len);
}

int Tunnel::exchange(uint8_t ecu, const uint8_t* req, unsigned n, uint8_t* reply, unsigned cap, unsigned timeoutMs,
                     unsigned* nrc, volatile sig_atomic_t* running) {
    if (!open_ || !readOnly(req, n)) {
        return -1;
    }
    uint8_t buf[64];
    const unsigned len = frame(ecu, req, n, buf, sizeof(buf));
    int fd = -1;
    if (!len || vmost_.fileDescriptorGet(&fd) != 0 || fd < 0) {
        return -1;
    }
    want_ = req;
    wantLen_ = n;
    gotLen_ = 0;
    gotNrc_ = 0;
    VmostMsgOut m;
    memset(&m, 0, sizeof(m));
    m.addr = 0xFFFF;  // vmostd finds the block's owner
    m.fblock = kFblock;
    m.inst = kInst;
    m.fkt = kFkt;
    m.op = 0x0;  // Start, as ID6 apps sends it
    m.len = static_cast<uint16_t>(len);
    m.data = buf;
    if (vmost_.send(&m) != 0) {
        want_ = nullptr;
        return -1;
    }
    const uint64_t end = nowMs() + timeoutMs;
    while (gotLen_ == 0 && (!running || *running)) {
        const uint64_t now = nowMs();
        if (now >= end) {
            break;
        }
        struct pollfd p;
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        if (poll(&p, 1, static_cast<int>(end - now)) > 0) {
            vmost_.processMessages();
        }
    }
    want_ = nullptr;
    if (gotLen_ == -2) {
        if (nrc) {
            *nrc = gotNrc_;
        }
        return -2;
    }
    if (gotLen_ <= 0) {
        return -3;
    }
    if (static_cast<unsigned>(gotLen_) > cap) {
        return -1;
    }
    memcpy(reply, got_, static_cast<size_t>(gotLen_));
    return gotLen_;
}

}  // namespace mostdiag
}  // namespace evo
