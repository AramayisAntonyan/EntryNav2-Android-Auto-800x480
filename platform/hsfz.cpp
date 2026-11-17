// SPDX-License-Identifier: MIT
// Ported from ID6 apps (MIT, Copyright (c) 2026 ID6 Apps contributors); notice in platform/dme_gauges.hpp.
// See platform/hsfz.hpp.
#include "hsfz.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

namespace evo {
namespace hsfz {
namespace {

uint64_t nowMs() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000 + static_cast<uint64_t>(t.tv_nsec) / 1000000;
}

bool ready(int fd, short events, uint64_t deadline, volatile sig_atomic_t* running) {
    while (!running || *running) {
        const uint64_t now = nowMs();
        if (now >= deadline) {
            return false;
        }
        struct pollfd p;
        p.fd = fd;
        p.events = events;
        p.revents = 0;
        const int rc = poll(&p, 1, static_cast<int>(deadline - now));
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        return rc > 0 && (p.revents & events);
    }
    return false;
}

bool transfer(int fd, unsigned char* buf, size_t n, bool sending, uint64_t deadline, volatile sig_atomic_t* running) {
    while (n && (!running || *running)) {
        if (!ready(fd, sending ? POLLOUT : POLLIN, deadline, running)) {
            return false;
        }
        const ssize_t k = sending ? send(fd, buf, n, MSG_NOSIGNAL) : recv(fd, buf, n, 0);
        if (k < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        if (k <= 0) {
            return false;
        }
        buf += k;
        n -= static_cast<size_t>(k);
    }
    return n == 0;
}

}  // namespace

bool readOnlyService(unsigned char service) {
    return service == 0x22 || service == 0x01 || service == 0x19;
}

int connectGateway(const char* host, unsigned port, volatile sig_atomic_t* running) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(port));
    if (!host || inet_pton(AF_INET, host, &a.sin_addr) != 1) {
        return -1;
    }
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&a), sizeof(a)) < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    int error = 0;
    socklen_t len = sizeof(error);
    if (!ready(fd, POLLOUT, nowMs() + 1000, running) || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) ||
        error) {
        close(fd);
        return -1;
    }
    return fd;
}

int exchange(int fd, unsigned ecu, const unsigned char* uds, unsigned udsSize, unsigned char* reply,
             unsigned replySize, unsigned timeoutMs, unsigned* nrc, volatile sig_atomic_t* running) {
    unsigned char request[16] = {0, 0, 0, 0, 0, 1, static_cast<unsigned char>(kTester), 0};
    if (fd < 0 || !uds || udsSize < 1 || udsSize > sizeof(request) - 8 || !readOnlyService(uds[0])) {
        return -1;
    }
    request[3] = static_cast<unsigned char>(udsSize + 2);
    request[7] = static_cast<unsigned char>(ecu);
    memcpy(request + 8, uds, udsSize);
    if (nrc) {
        *nrc = 0;
    }
    const uint64_t deadline = nowMs() + timeoutMs;
    if (!transfer(fd, request, 8 + udsSize, true, deadline, running)) {
        return -1;
    }
    const unsigned char positive = static_cast<unsigned char>(uds[0] + 0x40);
    unsigned char header[6], body[4096];
    while (!running || *running) {
        if (!transfer(fd, header, 6, false, deadline, running)) {
            return -1;
        }
        const uint32_t len = (static_cast<uint32_t>(header[0]) << 24) | (static_cast<uint32_t>(header[1]) << 16) |
                             (static_cast<uint32_t>(header[2]) << 8) | header[3];
        const unsigned ctrl = (static_cast<unsigned>(header[4]) << 8) | header[5];
        if (len > sizeof(body) || !transfer(fd, body, len, false, deadline, running)) {
            return -1;
        }
        if (ctrl == 0x12) {  // the gateway's alive check: answered, as every tester does
            unsigned char alive[8] = {0, 0, 0, 2, 0, 0x13, 0, static_cast<unsigned char>(kTester)};
            if (!transfer(fd, alive, 8, true, deadline, running)) {
                return -1;
            }
            continue;
        }
        if (ctrl != 1 || len < 3 || body[0] != ecu || body[1] != kTester) {
            continue;  // acknowledgements and other units' traffic
        }
        if (len == 5 && body[2] == 0x7f && body[3] == uds[0]) {
            if (body[4] == 0x78) {
                continue;  // response pending
            }
            if (nrc) {
                *nrc = body[4];
            }
            return -2;
        }
        if (body[2] != positive || len - 2 > replySize) {
            continue;
        }
        // The positive answer echoes the request's parameters (the DID, the PID, the sub-function).
        const unsigned echo = udsSize - 1 < len - 3 ? udsSize - 1 : len - 3;
        if (memcmp(body + 3, uds + 1, uds[0] == 0x19 ? 1 : echo)) {
            continue;
        }
        memcpy(reply, body + 2, len - 2);
        return static_cast<int>(len - 2);
    }
    return -1;
}

bool identIsGateway(const unsigned char* data, size_t n) {
    if (!data || n < 6 || data[4] != 0 || data[5] != 0x11) {
        return false;
    }
    for (size_t i = 6; i + 9 <= n; ++i) {
        if (!memcmp(data + i, "DIAGADR10", 9)) {
            return true;
        }
    }
    return false;
}

void identDiagAddr(const unsigned char* data, size_t n, char* out, size_t cap) {
    snprintf(out, cap, "?");
    for (size_t i = 0; data && i + 7 <= n; ++i) {
        if (memcmp(data + i, "DIAGADR", 7) != 0) {
            continue;
        }
        size_t k = 0;
        while (i + 7 + k < n && k + 1 < cap && data[i + 7 + k] >= '0' && data[i + 7 + k] <= '9') {
            out[k] = static_cast<char>(data[i + 7 + k]);
            ++k;
        }
        out[k] = 0;
        if (!k) {
            snprintf(out, cap, "?");
        }
        return;
    }
}

int identify(const char* local, const char* broadcast, unsigned waitMs, IdentAnswer* out, int max) {
    struct sockaddr_in from, to;
    memset(&from, 0, sizeof(from));
    memset(&to, 0, sizeof(to));
    from.sin_family = to.sin_family = AF_INET;
    to.sin_port = htons(kIdentPort);
    if (!local || !broadcast || inet_pton(AF_INET, local, &from.sin_addr) != 1 ||
        inet_pton(AF_INET, broadcast, &to.sin_addr) != 1) {
        return -1;
    }
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    const unsigned char ident[6] = {0, 0, 0, 0, 0, 0x11};
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&from), sizeof(from)) < 0 ||
        sendto(fd, ident, sizeof(ident), 0, reinterpret_cast<struct sockaddr*>(&to), sizeof(to)) !=
            static_cast<ssize_t>(sizeof(ident))) {
        close(fd);
        return -1;
    }
    int found = 0;
    const uint64_t deadline = nowMs() + waitMs;
    while (found < max && ready(fd, POLLIN, deadline, nullptr)) {
        unsigned char buf[1024];
        struct sockaddr_in src;
        socklen_t sl = sizeof(src);
        const ssize_t k = recvfrom(fd, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&src), &sl);
        if (k <= 0 || src.sin_addr.s_addr == from.sin_addr.s_addr) {
            continue;  // nothing, or our own broadcast looped back
        }
        IdentAnswer& a = out[found];
        if (!inet_ntop(AF_INET, &src.sin_addr, a.ip, sizeof(a.ip))) {
            continue;
        }
        bool seen = false;
        for (int i = 0; i < found; ++i) {
            seen = seen || !strcmp(out[i].ip, a.ip);
        }
        if (seen) {
            continue;
        }
        identDiagAddr(buf, static_cast<size_t>(k), a.diagAddr, sizeof(a.diagAddr));
        a.gateway = identIsGateway(buf, static_cast<size_t>(k));
        ++found;
    }
    close(fd);
    return found;
}

int findGateways(const char* local, unsigned waitMs, char (*out)[16], int max) {
    struct sockaddr_in from;
    memset(&from, 0, sizeof(from));
    from.sin_family = AF_INET;
    if (!local || inet_pton(AF_INET, local, &from.sin_addr) != 1) {
        return -1;
    }
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&from), sizeof(from)) < 0) {
        close(fd);
        return -1;
    }
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(kIdentPort);
    inet_pton(AF_INET, "169.254.255.255", &to.sin_addr);
    const unsigned char ident[6] = {0, 0, 0, 0, 0, 0x11};
    if (sendto(fd, ident, sizeof(ident), 0, reinterpret_cast<struct sockaddr*>(&to), sizeof(to)) !=
        static_cast<ssize_t>(sizeof(ident))) {
        close(fd);
        return -1;
    }
    int found = 0;
    const uint64_t deadline = nowMs() + waitMs;
    while (found < max && ready(fd, POLLIN, deadline, nullptr)) {
        unsigned char buf[1024];
        struct sockaddr_in src;
        socklen_t sl = sizeof(src);
        const ssize_t k = recvfrom(fd, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&src), &sl);
        if (k <= 0 || !identIsGateway(buf, static_cast<size_t>(k))) {
            continue;
        }
        char ip[16];
        if (!inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip))) {
            continue;
        }
        bool seen = false;
        for (int i = 0; i < found; ++i) {
            seen = seen || !strcmp(out[i], ip);
        }
        if (!seen) {
            strncpy(out[found], ip, 15);
            out[found][15] = 0;
            ++found;
        }
    }
    close(fd);
    return found;
}

int reserveGateway() {
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(kLockPort);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&a), sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

}  // namespace hsfz
}  // namespace evo
