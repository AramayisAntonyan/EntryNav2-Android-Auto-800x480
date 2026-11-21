// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The head unit's diagnosis tunnel over MOST: a UDS request to an ECU wrapped in a routine of the diagnosis block
// (FBlock 0x06, instance 0xF0, function 0x050, routine 0x0F0B, source 0x63: the head unit), as ID6 apps' MOST fallback
// sends it (github.com/inotbarnacle/ID6_apps_prerelease, source/native/src/most/most_link.c, MIT; written anew here).
// It reaches the DME without the diagnostic gateway's Ethernet, which the gateway opens only while a diagnostic
// cable is in the OBD port (seen on the F22, 2026-11-21: the tunnel answered while the gateway did not). Read
// requests only: 22 with one identifier, OBD 01 with one PID, 19 (fault memory).
#ifndef ENTRY_AAD_PLATFORM_MOST_DIAG_HPP
#define ENTRY_AAD_PLATFORM_MOST_DIAG_HPP

#include <signal.h>
#include <stdint.h>

#include "vmost_client.hpp"

namespace evo {
namespace mostdiag {

const uint8_t kFblock = 0x06, kInst = 0xF0, kSource = 0x63;
const uint16_t kFkt = 0x050, kRoutine = 0x0F0B;

// Only reads go through: 22 xx xx, 01 xx, 19 ...
bool readOnly(const uint8_t* req, unsigned n);

// [63][F0] 31 01 0F 0B <ecu> <len:2> <request> into out; its length, 0 when it does not fit.
unsigned frame(uint8_t ecu, const uint8_t* req, unsigned n, uint8_t* out, unsigned cap);

// A message of the tunnel: 1 with the inner UDS reply (after [src][tgt] 71 01 0F 0B <ecu> <len:2>), -2 when the
// routine itself was refused (7F 31 nrc), 0 when it is not a tunnel answer.
int parse(const uint8_t* p, unsigned n, const uint8_t** inner, unsigned* innerLen, unsigned* nrc);

// One vMOST connection (one per process: libvmostclient keeps a single one) with a listen-only shadow on 06.F0.
class Tunnel {
public:
    Tunnel();
    // Loads the library, connects, listens on the tunnel's block. False, with why, when vMOST is not there.
    bool open(const char** why);
    bool isOpen() const { return open_; }
    // One read to an ECU: the reply's length (it starts with the positive service byte), -2 refused (nrc set),
    // -3 no answer in time, -1 the tunnel failed or the request is not a read.
    int exchange(uint8_t ecu, const uint8_t* req, unsigned n, uint8_t* reply, unsigned cap, unsigned timeoutMs,
                 unsigned* nrc, volatile sig_atomic_t* running);

private:
    static void onMessage(void* msg, void* self);
    VmostClient vmost_;
    bool open_;
    // the answer the current exchange waits for
    const uint8_t* want_;
    unsigned wantLen_;
    uint8_t got_[4096];  // a fault memory answer can be long
    int gotLen_;         // > 0 an answer, -2 refused, 0 none yet
    unsigned gotNrc_;
};

}  // namespace mostdiag
}  // namespace evo

#endif
