// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// User-space UVC over usbfs: the head unit's kernel (3.12) has no uvcvideo, so a UVC grabber's video interfaces stay
// unclaimed and are driven here the way libuvc does, through /dev/bus/usb (the same usbfs our wired Android Auto
// uses). Probe/commit of an MJPEG format, the streaming interface's alternate setting with the isochronous endpoint,
// isochronous transfers, the UVC payload headers assembled into JPEG frames. Only the grabber is configured, and put
// back (alternate setting 0, interface released) at the end.
//
// Used by uvc-probe (--usbfs-grab, one frame) and the Camera app (hmiapp/camera_app.cpp, a stream). Tested with a
// MacroSilicon "AV TO USB2.0" (534d:0021): MJPEG 480x320, 640x480, 720x480 at 25 fps, endpoint 0x83, 3072 bytes.
#ifndef ENTRY_AAD_PLATFORM_UVC_USBFS_HPP
#define ENTRY_AAD_PLATFORM_UVC_USBFS_HPP

#include <stddef.h>

namespace evo {
namespace uvc {

const unsigned kMaxFrameSizes = 8;

// The device's descriptors (sysfs: device then active configuration) and what streaming needs from them.
struct Info {
    unsigned bcdUvc = 0;
    int vsInterface = -1;                     // the VideoStreaming interface
    int altSetting = -1;                      // its alternate setting with the largest isochronous endpoint
    unsigned endpoint = 0;                    // that endpoint
    unsigned packetSize = 0;                  // bytes per (micro)frame: max packet size x transactions
    int mjpegFormat = -1;                     // the MJPEG format's index
    unsigned frames[kMaxFrameSizes][2] = {};  // its frame sizes (index 1..): width, height
    unsigned frameCount = 0;
};

// Parses a device's raw descriptors (the sysfs "descriptors" file); false without an MJPEG format and an
// isochronous streaming endpoint.
bool parseInfo(const unsigned char* d, size_t n, Info* info);
// The same from /sys/bus/usb/devices/<port>/descriptors.
bool readInfo(const char* port, Info* info);

// The sysfs name (e.g. "3-1") of the first USB device with this vendor:product under `root` (normally
// /sys/bus/usb/devices); false when none. Interfaces (names with ':') and root hubs ("usbN") are skipped.
bool findDevice(unsigned vendor, unsigned product, char* port, size_t cap, const char* root = "/sys/bus/usb/devices");

// Assembles the isochronous packets' UVC payloads into frames: the header's FID bit flips at each new frame, EOF
// ends one. The first frame seen may have started earlier: dropped.
class Assembler {
public:
    Assembler(unsigned char* buffer, size_t cap) : buf_(buffer), cap_(cap) {}
    // One packet as received (header then data). True when it completed a JPEG frame: frame() / size() hold it until
    // the next call.
    bool feed(const unsigned char* pkt, unsigned length);
    const unsigned char* frame() const { return buf_; }
    size_t size() const { return done_; }
    unsigned frames() const { return frames_; }  // completed frames (JPEG or not)
    unsigned errors() const { return errors_; }  // packets with the error bit
    unsigned payloadBytes() const { return payload_; }

private:
    unsigned char* buf_;
    size_t cap_;
    size_t have_ = 0, done_ = 0;
    int fid_ = -1;
    bool started_ = false;
    unsigned frames_ = 0, errors_ = 0, payload_ = 0;
};

struct Stats {
    unsigned packets = 0, payloadBytes = 0, frames = 0, errors = 0;
};

// A JPEG frame (number: 1.. counted from the start); return false to stop the stream.
typedef bool (*FrameSink)(void* ctx, const unsigned char* jpeg, size_t size, unsigned number);
// One log line (no trailing newline).
typedef void (*LogLine)(const char* fmt, ...);

enum StreamResult {
    kStreamEnded = 0,   // the sink or *stop ended it
    kStreamFailed = 1,  // open, claim, probe/commit, alternate setting or submit refused
    kStreamNoUvc = 2,   // no MJPEG UVC device on that port
    kStreamTimeout = 3  // totalMs passed, or no JPEG frame for idleMs
};

// Streams the MJPEG frame size frameIndex (1..; 0 or out of range: the largest) at 25 fps from the UVC device on
// `port`, giving each JPEG frame to sink, until the sink returns false, *stop (if given) becomes non-zero, totalMs
// passes (0: no limit) or no JPEG frame comes for idleMs (0: no limit). The device is always put back.
StreamResult stream(const char* port, unsigned frameIndex, FrameSink sink, void* ctx, const int* stop, unsigned totalMs,
                    unsigned idleMs, LogLine log, Stats* stats);

}  // namespace uvc
}  // namespace evo

#endif
