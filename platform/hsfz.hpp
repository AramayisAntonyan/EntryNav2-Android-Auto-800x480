// SPDX-License-Identifier: MIT
// Read-only diagnostic requests over HSFZ (BMW's diagnostics over TCP 6801 at the car's gateway), and the vehicle
// identification broadcast (UDP 6811) that finds the gateway, as E-Sys and ISTA do.
//
// Ported from ID6 apps (github.com/inotbarnacle/ID6_apps_prerelease: source/native/src/collector.c,
// diagnostic_lease.h, enet.go), MIT licence, Copyright (c) 2026 ID6 Apps contributors; the full notice is in
// platform/dme_gauges.hpp and applies to this file and platform/hsfz.cpp. Changes: C++ for Linux, one exchange API
// for the collector and the probe, the request services limited to reading (0x22 ReadDataByIdentifier, 0x01 OBD
// current data, 0x19 ReadDTCInformation). The one exception is clearFaultMemory(): the DME's fault memory cleared
// with fixed requests (the owner's choice, 2026-11-19, as ID6 apps' and EntryNav-Apps' Engine Fault Codes do).
#ifndef ENTRY_AAD_PLATFORM_HSFZ_HPP
#define ENTRY_AAD_PLATFORM_HSFZ_HPP

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

namespace evo {
namespace hsfz {

const unsigned kPort = 6801;
const unsigned kIdentPort = 6811;
const unsigned kTester = 0xf4;
const unsigned kDme = 0x12;
const unsigned kEgs = 0x18;
const unsigned kLockPort = 42861;  // loopback: one diagnostic user at a time (BoostDeck's lease)

// The services this code may send. Anything else is refused before it reaches the socket.
bool readOnlyService(unsigned char service);

// A TCP connection to the gateway, 1 s to connect; -1 on failure. *running stops every wait early.
int connectGateway(const char* host, unsigned port, volatile sig_atomic_t* running);

// One request to `ecu` and its answer: the reply bytes after the HSFZ address bytes, starting with the positive
// response service (e.g. 62 <DID> ...). Returns their count, -2 for a negative response (*nrc set), -3 when no answer
// came in time (the connection stays usable: a late answer is skipped by its echo), -1 for a transport failure (the
// connection is unusable). "Response pending" (7F xx 78) is waited for; the gateway's alive check answered.
int exchange(int fd, unsigned ecu, const unsigned char* uds, unsigned udsSize, unsigned char* reply, unsigned replySize,
             unsigned timeoutMs, unsigned* nrc, volatile sig_atomic_t* running);

// Clears the DME's fault memory: ClearDiagnosticInformation for every group (14 FF FF FF), the only request here that
// changes anything. A DME that refuses it for its session (NRC 22, 31, 7E, 7F) gets it once more in the extended
// session (10 03), then is put back in the default session (10 01); *usedSession tells. Only the DME (kDme): -1 for any
// other unit. Returns 1 cleared, -2 refused (*nrc set), -1 no answer.
const unsigned kClearTimeoutMs = 8000;
int clearFaultMemory(int fd, unsigned ecu, unsigned* nrc, bool* usedSession, volatile sig_atomic_t* running);

// The gateway's answer to the identification broadcast: true when it carries DIAGADR10 (the gateway), with the
// address the answer came from. Head units answer DIAGADR62/63 and are not the gateway.
bool identIsGateway(const unsigned char* data, size_t n);

// Broadcast the identification request from `local` (a 169.254.x.x address of this unit) and collect the gateways
// that answer within waitMs: up to max addresses written to out[i] (16 chars). Returns the count, -1 on error.
int findGateways(const char* local, unsigned waitMs, char (*out)[16], int max);

// Every answer to one identification broadcast, gateway or not: who answered and its DIAGADR ("?" if none).
struct IdentAnswer {
    char ip[16];
    char diagAddr[8];
    bool gateway;
};
// From local (an address of this unit) to broadcast (that network's broadcast address); -1 on a socket error.
int identify(const char* local, const char* broadcast, unsigned waitMs, IdentAnswer* out, int max);
// The DIAGADR digits of an identification answer, "?" when it carries none.
void identDiagAddr(const unsigned char* data, size_t n, char* out, size_t cap);

// The loopback lock: a bound UDP port on 127.0.0.1:kLockPort. -1 when another user holds it.
int reserveGateway();

}  // namespace hsfz
}  // namespace evo

#endif  // ENTRY_AAD_PLATFORM_HSFZ_HPP
