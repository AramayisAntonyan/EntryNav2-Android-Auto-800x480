// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "uvc_usbfs.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/usbdevice_fs.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

namespace evo {
namespace uvc {
namespace {

int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r < 0 && errno == EINTR);
    return r;
}

uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

bool readText(const char* path, char* out, size_t cap) {
    out[0] = 0;
    FILE* f = fopen(path, "r");
    if (!f) {
        return false;
    }
    const bool ok = fgets(out, static_cast<int>(cap), f) != nullptr;
    fclose(f);
    out[strcspn(out, "\r\n")] = 0;
    return ok;
}

int control(int fd, unsigned char type, unsigned char request, unsigned short value, unsigned short index, void* data,
            unsigned short length) {
    struct usbdevfs_ctrltransfer c;
    memset(&c, 0, sizeof(c));
    c.bRequestType = type;
    c.bRequest = request;
    c.wValue = value;
    c.wIndex = index;
    c.wLength = length;
    c.timeout = 1000;
    c.data = data;
    return xioctl(fd, USBDEVFS_CONTROL, &c);
}

void noLog(const char*, ...) {}

}  // namespace

bool parseInfo(const unsigned char* d, size_t n, Info* info) {
    int ifClass = -1, ifSub = -1, ifNum = -1, alt = -1;
    for (size_t i = 0; i + 2 <= n && d[i] >= 2 && i + d[i] <= n; i += d[i]) {
        const unsigned char* b = d + i;
        if (b[1] == 4 && b[0] >= 7) {  // interface
            ifNum = b[2], alt = b[3], ifClass = b[5], ifSub = b[6];
        } else if (b[1] == 0x24 && ifClass == 0x0e && ifSub == 1 && b[2] == 1 && b[0] >= 5) {
            info->bcdUvc = b[3] | (b[4] << 8);
        } else if (b[1] == 0x24 && ifClass == 0x0e && ifSub == 2 && b[2] == 6 && b[0] >= 4 && info->mjpegFormat < 0) {
            info->mjpegFormat = b[3];
            info->vsInterface = ifNum;
        } else if (b[1] == 0x24 && ifClass == 0x0e && ifSub == 2 && b[2] == 7 && b[0] >= 9 &&
                   info->vsInterface == ifNum && b[3] >= 1 && b[3] <= kMaxFrameSizes) {
            info->frames[b[3] - 1][0] = b[5] | (b[6] << 8);
            info->frames[b[3] - 1][1] = b[7] | (b[8] << 8);
            info->frameCount = b[3] > info->frameCount ? b[3] : info->frameCount;
        } else if (b[1] == 5 && b[0] >= 6 && ifClass == 0x0e && ifSub == 2 && (b[3] & 3) == 1 && (b[2] & 0x80)) {
            const unsigned mps = b[4] | (b[5] << 8);
            const unsigned size = (mps & 0x7ff) * (1 + ((mps >> 11) & 3));
            if (size > info->packetSize) {
                info->packetSize = size, info->endpoint = b[2], info->altSetting = alt, info->vsInterface = ifNum;
            }
        }
    }
    return info->vsInterface >= 0 && info->altSetting > 0 && info->mjpegFormat > 0 && info->frameCount > 0;
}

bool readInfo(const char* port, Info* info) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/descriptors", port);
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    unsigned char d[4096];
    const size_t n = fread(d, 1, sizeof(d), f);
    fclose(f);
    return parseInfo(d, n, info);
}

bool findDevice(unsigned vendor, unsigned product, char* port, size_t cap, const char* root) {
    DIR* dir = opendir(root);
    if (!dir) {
        return false;
    }
    bool found = false;
    while (struct dirent* e = readdir(dir)) {
        if (e->d_name[0] == '.' || strchr(e->d_name, ':') || !strncmp(e->d_name, "usb", 3)) {
            continue;
        }
        char path[PATH_MAX], text[16];
        snprintf(path, sizeof(path), "%s/%s/idVendor", root, e->d_name);
        if (!readText(path, text, sizeof(text)) || strtoul(text, nullptr, 16) != vendor) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s/idProduct", root, e->d_name);
        if (!readText(path, text, sizeof(text)) || strtoul(text, nullptr, 16) != product) {
            continue;
        }
        if (strlen(e->d_name) < cap) {
            snprintf(port, cap, "%s", e->d_name);
            found = true;
            break;
        }
    }
    closedir(dir);
    return found;
}

bool Assembler::feed(const unsigned char* pkt, unsigned got) {
    if (got < 2 || pkt[0] < 2 || pkt[0] > got) {
        return false;
    }
    const unsigned head = pkt[0], bits = pkt[1];
    const int f = bits & 1;
    if (bits & 0x40) {  // error bit
        ++errors_;
    }
    if (f != fid_) {  // a new frame starts here
        if (started_ && have_ > 0) {
            ++frames_;  // the last one ended without EOF
        }
        started_ = fid_ >= 0;  // the first frame seen may have started earlier: skipped
        fid_ = f;
        have_ = 0;
    }
    payload_ += got - head;
    if (started_ && have_ + (got - head) <= cap_) {
        memcpy(buf_ + have_, pkt + head, got - head);
        have_ += got - head;
    }
    if ((bits & 2) && started_ && have_ > 0) {  // EOF
        ++frames_;
        const bool jpeg = have_ > 2 && buf_[0] == 0xff && buf_[1] == 0xd8;
        done_ = jpeg ? have_ : 0;
        have_ = 0;
        started_ = false;  // the next FID change starts the next one
        return jpeg;
    }
    return false;
}

StreamResult stream(const char* port, unsigned frameIndex, FrameSink sink, void* ctx, const int* stop, unsigned totalMs,
                    unsigned idleMs, LogLine log, Stats* stats) {
    if (!log) {
        log = &noLog;
    }
    Stats local;
    Stats& st = stats ? *stats : local;
    st = Stats();
    Info info;
    if (!readInfo(port, &info)) {
        log("%s: no UVC device with an MJPEG format and an isochronous endpoint", port);
        return kStreamNoUvc;
    }
    if (frameIndex < 1 || frameIndex > info.frameCount) {
        frameIndex = info.frameCount;  // the largest, listed last
    }
    char path[PATH_MAX], text[16];
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/busnum", port);
    readText(path, text, sizeof(text));
    const int bus = atoi(text);
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/devnum", port);
    readText(path, text, sizeof(text));
    const int dev = atoi(text);
    snprintf(path, sizeof(path), "/dev/bus/usb/%03d/%03d", bus, dev);
    const int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        log("open %s: %s", path, strerror(errno));
        return kStreamFailed;
    }
    unsigned iface = static_cast<unsigned>(info.vsInterface);
    if (xioctl(fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0) {
        log("claim interface %u: %s (a driver holds it?)", iface, strerror(errno));
        close(fd);
        return kStreamFailed;
    }
    StreamResult rc = kStreamFailed;
    // probe, then commit: MJPEG, the chosen frame size, 25 fps
    const unsigned short len = info.bcdUvc >= 0x0110 ? 34 : 26;
    unsigned char probe[34];
    memset(probe, 0, sizeof(probe));
    probe[0] = 1;  // bmHint: keep dwFrameInterval
    probe[2] = static_cast<unsigned char>(info.mjpegFormat);
    probe[3] = static_cast<unsigned char>(frameIndex);
    const unsigned interval = 400000;  // 25 fps in 100 ns units
    memcpy(probe + 4, &interval, 4);
    bool ok = control(fd, 0x21, 0x01, 0x0100, iface, probe, len) >= 0 &&  // SET_CUR VS_PROBE_CONTROL
              control(fd, 0xa1, 0x81, 0x0100, iface, probe, len) >= 0;    // GET_CUR VS_PROBE_CONTROL
    unsigned maxFrame = 0, maxPayload = 0;
    if (ok) {
        memcpy(&maxFrame, probe + 18, 4);
        memcpy(&maxPayload, probe + 22, 4);
        log("probe: format %u frame %u (%ux%u), max frame %u bytes, max payload %u bytes", probe[2], probe[3],
            info.frames[frameIndex - 1][0], info.frames[frameIndex - 1][1], maxFrame, maxPayload);
        ok = control(fd, 0x21, 0x01, 0x0200, iface, probe, len) >= 0;  // SET_CUR VS_COMMIT_CONTROL
    }
    struct usbdevfs_setinterface si;
    si.interface = iface;
    si.altsetting = static_cast<unsigned>(info.altSetting);
    if (!ok) {
        log("probe/commit refused: %s", strerror(errno));
    } else if (xioctl(fd, USBDEVFS_SETINTERFACE, &si) < 0) {
        log("alternate setting %d: %s", info.altSetting, strerror(errno));
        ok = false;
    }
    const unsigned kUrbs = 8, kPackets = 8;  // 8 x 8 microframes in flight: 8 ms of video
    struct usbdevfs_urb* urbs[kUrbs] = {};
    unsigned submitted = 0;
    if (ok) {
        for (unsigned u = 0; u < kUrbs; ++u) {
            const size_t size = sizeof(struct usbdevfs_urb) + kPackets * sizeof(struct usbdevfs_iso_packet_desc);
            urbs[u] = static_cast<struct usbdevfs_urb*>(calloc(1, size));
            if (!urbs[u]) {
                break;
            }
            urbs[u]->type = USBDEVFS_URB_TYPE_ISO;
            urbs[u]->endpoint = static_cast<unsigned char>(info.endpoint);
            urbs[u]->flags = USBDEVFS_URB_ISO_ASAP;
            urbs[u]->buffer = malloc(kPackets * info.packetSize);
            urbs[u]->buffer_length = static_cast<int>(kPackets * info.packetSize);
            urbs[u]->number_of_packets = static_cast<int>(kPackets);
            for (unsigned p = 0; p < kPackets; ++p) {
                urbs[u]->iso_frame_desc[p].length = info.packetSize;
            }
            if (!urbs[u]->buffer || xioctl(fd, USBDEVFS_SUBMITURB, urbs[u]) < 0) {
                log("submit: %s", strerror(errno));
                break;
            }
            ++submitted;
        }
    }
    const size_t cap = maxFrame > 0 && maxFrame < 4u << 20 ? maxFrame + 65536 : 1u << 20;
    unsigned char* buffer = static_cast<unsigned char*>(malloc(cap));
    Assembler frames(buffer, buffer ? cap : 0);
    const uint64_t start = nowMs();
    uint64_t lastFrame = start;
    bool running = submitted == kUrbs && buffer;
    while (running) {
        if (stop && __atomic_load_n(stop, __ATOMIC_ACQUIRE)) {
            rc = kStreamEnded;
            break;
        }
        const uint64_t now = nowMs();
        if ((totalMs && now - start >= totalMs) || (idleMs && now - lastFrame >= idleMs)) {
            rc = kStreamTimeout;
            break;
        }
        struct pollfd pfd = {fd, POLLOUT, 0};
        poll(&pfd, 1, 200);
        struct usbdevfs_urb* done = nullptr;
        while (running && xioctl(fd, USBDEVFS_REAPURBNDELAY, &done) == 0 && done) {
            const unsigned char* buf = static_cast<const unsigned char*>(done->buffer);
            for (int p = 0; p < done->number_of_packets && running; ++p) {
                ++st.packets;
                if (done->iso_frame_desc[p].status != 0) {
                    ++st.errors;
                    continue;
                }
                if (frames.feed(buf + p * info.packetSize, done->iso_frame_desc[p].actual_length)) {
                    lastFrame = nowMs();
                    if (!sink(ctx, frames.frame(), frames.size(), frames.frames())) {
                        rc = kStreamEnded;
                        running = false;
                    }
                }
            }
            if (running && xioctl(fd, USBDEVFS_SUBMITURB, done) < 0) {
                log("resubmit: %s", strerror(errno));  // the device went away
                running = false;
            }
        }
    }
    st.payloadBytes = frames.payloadBytes();
    st.frames = frames.frames();
    st.errors += frames.errors();
    for (unsigned u = 0; u < kUrbs; ++u) {  // give everything back: discard, reap, alternate setting 0, release
        if (urbs[u]) {
            xioctl(fd, USBDEVFS_DISCARDURB, urbs[u]);
        }
    }
    struct usbdevfs_urb* done = nullptr;
    for (int i = 0; i < 50 && xioctl(fd, USBDEVFS_REAPURBNDELAY, &done) == 0; ++i) {
    }
    for (unsigned u = 0; u < kUrbs; ++u) {
        if (urbs[u]) {
            free(urbs[u]->buffer);
            free(urbs[u]);
        }
    }
    free(buffer);
    si.altsetting = 0;
    xioctl(fd, USBDEVFS_SETINTERFACE, &si);
    xioctl(fd, USBDEVFS_RELEASEINTERFACE, &iface);
    close(fd);
    return rc;
}

}  // namespace uvc
}  // namespace evo
