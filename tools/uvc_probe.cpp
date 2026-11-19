// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Read-only probe of USB video adapters on the car, for a camera on its own input: UVC cameras and the analog
// "EasyCap" grabbers (UTV007 usbtv, STK1160, EM28xx, Somagic). A developer tool, not part of a release
// (make dev-tools), run by hand from /tmp/stagex.
//
//   uvc-probe                    the video drivers' presence, every video device; capabilities, formats, inputs and
//                                frame sizes of the USB ones
//   uvc-probe --usb              every USB device: vendor:product, names, each interface's class and driver, and the
//                                grabber chip when the id is a known one (with the driver it needs)
//   uvc-probe --usbfs-grab PORT [FRAME [SKIP]]
//                                without a kernel driver: one MJPEG frame of the UVC device on USB port PORT (as
//                                --usb lists it, e.g. 3-1) to /tmp/stagex/video-frame.jpg, the format's frame size
//                                FRAME (1.., default the largest) after SKIP frames (default 10: the picture settles)
//   uvc-probe --grab /dev/videoN [--input N] [--pal|--ntsc]
//                                one frame of that USB device to /tmp/stagex/video-frame.<mjpg|yuyv|uyvy|raw>; an
//                                analog grabber may need its input (0 composite, 1 S-video) and TV standard chosen
//
// A device is identified from sysfs (its driver link) before anything is opened, and only the USB video drivers
// below are ever opened: /dev/video0 (BMW's camera capture, dra7xx-vip) and /dev/video1 (the VPE scaler) are listed
// and left alone. Nothing is configured but the capture of --grab (input and standard when asked, the format as the
// device has it, two buffers, then stopped).
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/usbdevice_fs.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

namespace {

const char kOutDir[] = "/tmp/stagex";

int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r < 0 && errno == EINTR);
    return r;
}

void fourcc(uint32_t f, char out[5]) {
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((f >> (8 * i)) & 0xff);
        out[i] = (c >= 32 && c < 127) ? c : '?';
    }
    out[4] = 0;
}

// The driver of /sys/class/video4linux/<node>, from its device/driver link: "" when unknown.
void driverOf(const char* node, char* out, size_t size) {
    char link[PATH_MAX], target[PATH_MAX];
    snprintf(link, sizeof(link), "/sys/class/video4linux/%s/device/driver", node);
    const ssize_t n = readlink(link, target, sizeof(target) - 1);
    out[0] = 0;
    if (n > 0) {
        target[n] = 0;
        const char* base = strrchr(target, '/');
        snprintf(out, size, "%s", base ? base + 1 : target);
    }
}

void readLine(const char* path, char* out, size_t size) {
    out[0] = 0;
    FILE* f = fopen(path, "r");
    if (f) {
        if (fgets(out, static_cast<int>(size), f)) {
            out[strcspn(out, "\r\n")] = 0;
        }
        fclose(f);
    }
}

// The USB video drivers this probe opens: UVC cameras and the analog grabbers sold as "EasyCap".
const char* const kVideoDrivers[] = {"uvcvideo", "usbtv", "stk1160", "em28xx", "smi2021", "somagic_easycap"};
const unsigned kVideoDriverCount = sizeof(kVideoDrivers) / sizeof(kVideoDrivers[0]);

bool isUsbVideo(const char* node) {
    char driver[64];
    driverOf(node, driver, sizeof(driver));
    for (unsigned i = 0; i < kVideoDriverCount; ++i) {
        if (!strcmp(driver, kVideoDrivers[i])) {
            return true;
        }
    }
    return false;
}

// The first file named `name` under dir (depth-first, a few levels), into out; false when none.
bool findFile(const char* dir, const char* name, int depth, char* out, size_t size) {
    DIR* d = opendir(dir);
    if (!d) {
        return false;
    }
    bool found = false;
    while (!found) {
        struct dirent* e = readdir(d);
        if (!e) {
            break;
        }
        if (e->d_name[0] == '.') {
            continue;
        }
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            found = depth > 0 && findFile(path, name, depth - 1, out, size);
        } else if (!strcmp(e->d_name, name)) {
            snprintf(out, size, "%s", path);
            found = true;
        }
    }
    closedir(d);
    return found;
}

void driverPresence() {
    struct utsname u;
    uname(&u);
    struct stat st;
    char modules[PATH_MAX];
    snprintf(modules, sizeof(modules), "/lib/modules/%s", u.release);
    printf("kernel %s\n", u.release);
    printf("videodev (V4L2 core): %s\n", stat("/sys/module/videodev", &st) == 0 ? "loaded or built in" : "not seen");
    for (unsigned i = 0; i < kVideoDriverCount; ++i) {
        char sys[PATH_MAX], file[PATH_MAX], ko[64];
        snprintf(sys, sizeof(sys), "/sys/bus/usb/drivers/%s", kVideoDrivers[i]);
        const bool registered = stat(sys, &st) == 0;
        snprintf(sys, sizeof(sys), "/sys/module/%s", kVideoDrivers[i]);
        const bool loaded = stat(sys, &st) == 0;
        snprintf(ko, sizeof(ko), "%s.ko", kVideoDrivers[i]);
        const bool file_ = findFile(modules, ko, 6, file, sizeof(file));
        printf("%s: %s, %s, module file %s\n", kVideoDrivers[i], registered ? "registered" : "not registered",
               loaded ? "loaded" : "not loaded", file_ ? file : "absent");
    }
}

// Known grabber and camera chips by USB id: what they are and the driver they need.
struct Chip {
    unsigned vendor, product;
    const char* what;
};
const Chip kChips[] = {
    {0x1b71, 0x3002, "Fushicai UTV007 grabber (EasyCap): driver usbtv"},
    {0x1f71, 0x3301, "Fushicai UTV007 grabber: driver usbtv"},
    {0x1f71, 0x3306, "Fushicai UTV007 grabber: driver usbtv"},
    {0x05e1, 0x0408, "Syntek STK1160 grabber (EasyCap): driver stk1160"},
    {0xeb1a, 0x2860, "Empia EM2860 grabber (EasyCap): driver em28xx"},
    {0xeb1a, 0x2861, "Empia EM2861 grabber: driver em28xx"},
    {0xeb1a, 0x2862, "Empia EM2862 grabber: driver em28xx"},
    {0xeb1a, 0x2863, "Empia EM2863 grabber: driver em28xx"},
    {0xeb1a, 0x2870, "Empia EM2870: driver em28xx"},
    {0xeb1a, 0x5051, "Empia EM2860 (EasyCap DC60): driver em28xx"},
    {0x1c88, 0x0007, "Somagic SMI2021 grabber (EasyCap), firmware not loaded: no mainline driver"},
    {0x1c88, 0x003c, "Somagic SMI2021 grabber (EasyCap): no mainline driver (smi2021, out of tree)"},
    {0x1c88, 0x003d, "Somagic SMI2021 grabber: no mainline driver"},
    {0x1c88, 0x003e, "Somagic SMI2021 grabber: no mainline driver"},
    {0x1c88, 0x003f, "Somagic SMI2021 grabber: no mainline driver"},
    {0x534d, 0x2109, "MacroSilicon MS2109 HDMI grabber: driver uvcvideo"},
    {0x534d, 0x0021, "MacroSilicon MS210x grabber: driver uvcvideo"},
};

unsigned hexFile(const char* path) {
    char text[16];
    readLine(path, text, sizeof(text));
    return static_cast<unsigned>(strtoul(text, nullptr, 16));
}

// Every USB device, its interfaces' classes and drivers, and the chip when known.
int usb() {
    const char* base = "/sys/bus/usb/devices";
    DIR* d = opendir(base);
    if (!d) {
        printf("no %s\n", base);
        return 1;
    }
    while (struct dirent* e = readdir(d)) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s/idVendor", base, e->d_name);
        struct stat st;
        if (e->d_name[0] == '.' || strchr(e->d_name, ':') || stat(path, &st) != 0) {
            continue;  // interfaces are listed under their device
        }
        const unsigned vendor = hexFile(path);
        snprintf(path, sizeof(path), "%s/%s/idProduct", base, e->d_name);
        const unsigned product = hexFile(path);
        char maker[96], name[96], speed[16];
        snprintf(path, sizeof(path), "%s/%s/manufacturer", base, e->d_name);
        readLine(path, maker, sizeof(maker));
        snprintf(path, sizeof(path), "%s/%s/product", base, e->d_name);
        readLine(path, name, sizeof(name));
        snprintf(path, sizeof(path), "%s/%s/speed", base, e->d_name);
        readLine(path, speed, sizeof(speed));
        printf("%s: %04x:%04x '%s' '%s', %s Mbit/s\n", e->d_name, vendor, product, maker, name, speed);
        for (unsigned i = 0; i < sizeof(kChips) / sizeof(kChips[0]); ++i) {
            if (kChips[i].vendor == vendor && kChips[i].product == product) {
                printf("  chip: %s\n", kChips[i].what);
            }
        }
        DIR* sub = opendir(base);
        while (sub) {
            struct dirent* f = readdir(sub);
            if (!f) {
                break;
            }
            const size_t n = strlen(e->d_name);
            if (strncmp(f->d_name, e->d_name, n) != 0 || f->d_name[n] != ':') {
                continue;
            }
            char iface[PATH_MAX], target[PATH_MAX], driver[64] = "none";
            snprintf(iface, sizeof(iface), "%s/%s/bInterfaceClass", base, f->d_name);
            const unsigned cls = hexFile(iface);
            snprintf(iface, sizeof(iface), "%s/%s/driver", base, f->d_name);
            const ssize_t k = readlink(iface, target, sizeof(target) - 1);
            if (k > 0) {
                target[k] = 0;
                const char* slash = strrchr(target, '/');
                snprintf(driver, sizeof(driver), "%s", slash ? slash + 1 : target);
            }
            printf("  interface %s: class 0x%02x%s, driver %s\n", f->d_name, cls,
                   cls == 0x0e   ? " (video)"
                   : cls == 0x01 ? " (audio)"
                   : cls == 0xff ? " (vendor)"
                                 : "",
                   driver);
        }
        if (sub) {
            closedir(sub);
        }
    }
    closedir(d);
    return 0;
}

void describe(int fd) {
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        printf("  QUERYCAP failed: %s\n", strerror(errno));
        return;
    }
    printf("  card '%s', bus '%s', driver %s, caps 0x%08x%s%s\n", cap.card, cap.bus_info, cap.driver, cap.capabilities,
           (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ? " capture" : "",
           (cap.capabilities & V4L2_CAP_STREAMING) ? " streaming" : "");
    for (unsigned i = 0;; ++i) {
        struct v4l2_fmtdesc fd_;
        memset(&fd_, 0, sizeof(fd_));
        fd_.index = i;
        fd_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd, VIDIOC_ENUM_FMT, &fd_) < 0) {
            break;
        }
        char cc[5];
        fourcc(fd_.pixelformat, cc);
        printf("  format %u: %s '%s'%s\n", i, cc, fd_.description,
               (fd_.flags & V4L2_FMT_FLAG_COMPRESSED) ? " (compressed)" : "");
        for (unsigned k = 0; k < 32; ++k) {
            struct v4l2_frmsizeenum fs;
            memset(&fs, 0, sizeof(fs));
            fs.index = k;
            fs.pixel_format = fd_.pixelformat;
            if (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) < 0) {
                break;
            }
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                struct v4l2_frmivalenum fi;
                memset(&fi, 0, sizeof(fi));
                fi.pixel_format = fd_.pixelformat;
                fi.width = fs.discrete.width;
                fi.height = fs.discrete.height;
                const bool rate = xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0 &&
                                  fi.type == V4L2_FRMIVAL_TYPE_DISCRETE && fi.discrete.numerator;
                printf("    %ux%u%s", fs.discrete.width, fs.discrete.height, rate ? "" : "\n");
                if (rate) {
                    printf(" @ %u/%u s\n", fi.discrete.numerator, fi.discrete.denominator);
                }
            } else {
                printf("    stepwise %ux%u .. %ux%u\n", fs.stepwise.min_width, fs.stepwise.min_height,
                       fs.stepwise.max_width, fs.stepwise.max_height);
                break;
            }
        }
    }
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
        char cc[5];
        fourcc(fmt.fmt.pix.pixelformat, cc);
        printf("  current: %ux%u %s, %u bytes per frame\n", fmt.fmt.pix.width, fmt.fmt.pix.height, cc,
               fmt.fmt.pix.sizeimage);
    }
    struct v4l2_input in;
    for (unsigned i = 0; i < 8; ++i) {
        memset(&in, 0, sizeof(in));
        in.index = i;
        if (xioctl(fd, VIDIOC_ENUMINPUT, &in) < 0) {
            break;
        }
        printf("  input %u: '%s', status 0x%x%s, standards 0x%llx\n", i, in.name, in.status,
               (in.status & V4L2_IN_ST_NO_SIGNAL) ? " (no signal)" : "", static_cast<unsigned long long>(in.std));
    }
}

int list() {
    driverPresence();
    DIR* d = opendir("/sys/class/video4linux");
    if (!d) {
        printf("no /sys/class/video4linux\n");
        return 0;
    }
    int uvc = 0;
    while (struct dirent* e = readdir(d)) {
        if (strncmp(e->d_name, "video", 5) != 0) {
            continue;
        }
        char name[128], driver[64], path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/class/video4linux/%s/name", e->d_name);
        readLine(path, name, sizeof(name));
        driverOf(e->d_name, driver, sizeof(driver));
        printf("/dev/%s: '%s', driver %s\n", e->d_name, name, driver[0] ? driver : "?");
        if (!isUsbVideo(e->d_name)) {
            printf("  not a USB video device: left alone\n");
            continue;
        }
        ++uvc;
        snprintf(path, sizeof(path), "/dev/%s", e->d_name);
        const int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            printf("  open failed: %s\n", strerror(errno));
            continue;
        }
        describe(fd);
        close(fd);
    }
    closedir(d);
    printf("%d USB video device(s)\n", uvc);
    return 0;
}

int grab(const char* dev, int input, v4l2_std_id std) {
    const char* node = strrchr(dev, '/');
    node = node ? node + 1 : dev;
    if (!isUsbVideo(node)) {
        fprintf(stderr, "%s is not a USB video device: refused (only USB cameras and grabbers are opened)\n", dev);
        return 2;
    }
    const int fd = open(dev, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
        return 1;
    }
    if (input >= 0) {
        printf("input %d: %s\n", input, xioctl(fd, VIDIOC_S_INPUT, &input) == 0 ? "set" : strerror(errno));
    }
    if (std) {
        printf("standard %s: %s\n", std == V4L2_STD_PAL ? "PAL" : "NTSC",
               xioctl(fd, VIDIOC_S_STD, &std) == 0 ? "set" : strerror(errno));
    }
    int rc = 1;
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    void* maps[2] = {MAP_FAILED, MAP_FAILED};
    size_t lens[2] = {0, 0};
    bool streaming = false;
    if (xioctl(fd, VIDIOC_G_FMT, &fmt) < 0 || xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 1) {
        fprintf(stderr, "format or buffers refused: %s\n", strerror(errno));
    } else {
        unsigned mapped = 0;
        for (unsigned i = 0; i < req.count && i < 2; ++i) {
            struct v4l2_buffer b;
            memset(&b, 0, sizeof(b));
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            b.index = i;
            if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0) {
                break;
            }
            maps[i] = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
            lens[i] = b.length;
            if (maps[i] == MAP_FAILED || xioctl(fd, VIDIOC_QBUF, &b) < 0) {
                break;
            }
            ++mapped;
        }
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (mapped && xioctl(fd, VIDIOC_STREAMON, &type) == 0) {
            streaming = true;
            fd_set rf;
            FD_ZERO(&rf);
            FD_SET(fd, &rf);
            struct timeval tv = {5, 0};
            struct v4l2_buffer b;
            memset(&b, 0, sizeof(b));
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            if (select(fd + 1, &rf, nullptr, nullptr, &tv) <= 0) {
                fprintf(stderr, "no frame within 5 s (no signal on that input or standard?)\n");
            } else if (xioctl(fd, VIDIOC_DQBUF, &b) < 0 || b.index >= 2 || maps[b.index] == MAP_FAILED) {
                fprintf(stderr, "dequeue failed: %s\n", strerror(errno));
            } else {
                char cc[5];
                fourcc(fmt.fmt.pix.pixelformat, cc);
                const char* ext = fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG  ? "mjpg"
                                  : fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV ? "yuyv"
                                  : fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_UYVY ? "uyvy"
                                                                                 : "raw";
                char out[PATH_MAX];
                mkdir(kOutDir, 0755);
                snprintf(out, sizeof(out), "%s/video-frame.%s", kOutDir, ext);
                FILE* f = fopen(out, "wb");
                if (f && fwrite(maps[b.index], 1, b.bytesused, f) == b.bytesused && fclose(f) == 0) {
                    printf("%s: %ux%u %s, %u bytes\n", out, fmt.fmt.pix.width, fmt.fmt.pix.height, cc, b.bytesused);
                    rc = 0;
                } else {
                    if (f) {
                        fclose(f);
                    }
                    fprintf(stderr, "write %s failed\n", out);
                }
            }
        }
    }
    if (streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(fd, VIDIOC_STREAMOFF, &type);
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (maps[i] != MAP_FAILED) {
            munmap(maps[i], lens[i]);
        }
    }
    req.count = 0;
    xioctl(fd, VIDIOC_REQBUFS, &req);  // gives the buffers back
    close(fd);
    return rc;
}

// ---- user-space UVC over usbfs
// ---------------------------------------------------------------------------------------- The head unit's kernel has
// no uvcvideo, so a UVC grabber's video interfaces stay unclaimed: this drives one the way libuvc does, through
// /dev/bus/usb (the same usbfs our wired Android Auto uses). Probe/commit of an MJPEG format, the streaming interface's
// alternate setting with the isochronous endpoint, isochronous transfers, the UVC payload headers assembled into
// frames; the chosen frame is saved as a JPEG. Only the grabber is configured, and put back (alternate setting 0,
// interface released) at the end.

// The device's descriptors (sysfs: device then active configuration) and what the grab needs from them.
struct UvcInfo {
    unsigned bcdUvc = 0;
    int vsInterface = -1;        // the VideoStreaming interface
    int altSetting = -1;         // its alternate setting with the largest isochronous endpoint
    unsigned endpoint = 0;       // that endpoint
    unsigned packetSize = 0;     // bytes per (micro)frame: max packet size x transactions
    int mjpegFormat = -1;        // the MJPEG format's index
    unsigned frames[8][2] = {};  // its frame sizes (index 1..)
    unsigned frameCount = 0;
};

bool readUvcInfo(const char* port, UvcInfo* info) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/descriptors", port);
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    unsigned char d[4096];
    const size_t n = fread(d, 1, sizeof(d), f);
    fclose(f);
    int ifClass = -1, ifSub = -1, ifNum = -1, alt = -1;
    for (size_t i = 0; i + 2 <= n && d[i] >= 2 && i + d[i] <= n; i += d[i]) {
        const unsigned char* b = d + i;
        if (b[1] == 4) {  // interface
            ifNum = b[2], alt = b[3], ifClass = b[5], ifSub = b[6];
        } else if (b[1] == 0x24 && ifClass == 0x0e && ifSub == 1 && b[2] == 1 && b[0] >= 5) {
            info->bcdUvc = b[3] | (b[4] << 8);
        } else if (b[1] == 0x24 && ifClass == 0x0e && ifSub == 2 && b[2] == 6 && info->mjpegFormat < 0) {
            info->mjpegFormat = b[3];
            info->vsInterface = ifNum;
        } else if (b[1] == 0x24 && ifClass == 0x0e && ifSub == 2 && b[2] == 7 && b[0] >= 9 &&
                   info->vsInterface == ifNum && b[3] >= 1 && b[3] <= 8) {
            info->frames[b[3] - 1][0] = b[5] | (b[6] << 8);
            info->frames[b[3] - 1][1] = b[7] | (b[8] << 8);
            info->frameCount = b[3] > info->frameCount ? b[3] : info->frameCount;
        } else if (b[1] == 5 && ifClass == 0x0e && ifSub == 2 && (b[3] & 3) == 1 && (b[2] & 0x80)) {
            const unsigned mps = b[4] | (b[5] << 8);
            const unsigned size = (mps & 0x7ff) * (1 + ((mps >> 11) & 3));
            if (size > info->packetSize) {
                info->packetSize = size, info->endpoint = b[2], info->altSetting = alt, info->vsInterface = ifNum;
            }
        }
    }
    return info->vsInterface >= 0 && info->altSetting > 0 && info->mjpegFormat > 0 && info->frameCount > 0;
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

int usbfsGrab(const char* port, unsigned frameIndex, unsigned skip) {
    UvcInfo info;
    if (!readUvcInfo(port, &info)) {
        fprintf(stderr, "%s: no UVC device with an MJPEG format and an isochronous endpoint\n", port);
        return 2;
    }
    if (frameIndex < 1 || frameIndex > info.frameCount) {
        frameIndex = info.frameCount;  // the largest, listed last
    }
    printf("UVC %x.%02x, streaming interface %d alt %d, endpoint 0x%02x, %u bytes per packet\n", info.bcdUvc >> 8,
           info.bcdUvc & 0xff, info.vsInterface, info.altSetting, info.endpoint, info.packetSize);
    for (unsigned i = 0; i < info.frameCount; ++i) {
        printf("  MJPEG frame %u: %ux%u%s\n", i + 1, info.frames[i][0], info.frames[i][1],
               i + 1 == frameIndex ? "  <- grabbed" : "");
    }
    char path[PATH_MAX], text[16];
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/busnum", port);
    readLine(path, text, sizeof(text));
    const int bus = atoi(text);
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/devnum", port);
    readLine(path, text, sizeof(text));
    const int dev = atoi(text);
    snprintf(path, sizeof(path), "/dev/bus/usb/%03d/%03d", bus, dev);
    const int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return 1;
    }
    unsigned iface = static_cast<unsigned>(info.vsInterface);
    if (xioctl(fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0) {
        fprintf(stderr, "claim interface %u: %s (a driver holds it?)\n", iface, strerror(errno));
        close(fd);
        return 1;
    }
    int rc = 1;
    // probe, then commit: MJPEG, the chosen frame size, its default rate
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
        printf("probe: format %u frame %u, max frame %u bytes, max payload %u bytes\n", probe[2], probe[3], maxFrame,
               maxPayload);
        ok = control(fd, 0x21, 0x01, 0x0200, iface, probe, len) >= 0;  // SET_CUR VS_COMMIT_CONTROL
    }
    struct usbdevfs_setinterface si;
    si.interface = iface;
    si.altsetting = static_cast<unsigned>(info.altSetting);
    if (!ok) {
        fprintf(stderr, "probe/commit refused: %s\n", strerror(errno));
    } else if (xioctl(fd, USBDEVFS_SETINTERFACE, &si) < 0) {
        fprintf(stderr, "alternate setting %d: %s\n", info.altSetting, strerror(errno));
        ok = false;
    }
    const unsigned kUrbs = 8, kPackets = 8;  // 8 x 8 microframes in flight: 8 ms of video
    struct usbdevfs_urb* urbs[kUrbs] = {};
    unsigned submitted = 0;
    if (ok) {
        for (unsigned u = 0; u < kUrbs; ++u) {
            const size_t size = sizeof(struct usbdevfs_urb) + kPackets * sizeof(struct usbdevfs_iso_packet_desc);
            urbs[u] = static_cast<struct usbdevfs_urb*>(calloc(1, size));
            urbs[u]->type = USBDEVFS_URB_TYPE_ISO;
            urbs[u]->endpoint = static_cast<unsigned char>(info.endpoint);
            urbs[u]->flags = USBDEVFS_URB_ISO_ASAP;
            urbs[u]->buffer = malloc(kPackets * info.packetSize);
            urbs[u]->buffer_length = static_cast<int>(kPackets * info.packetSize);
            urbs[u]->number_of_packets = static_cast<int>(kPackets);
            for (unsigned p = 0; p < kPackets; ++p) {
                urbs[u]->iso_frame_desc[p].length = info.packetSize;
            }
            if (xioctl(fd, USBDEVFS_SUBMITURB, urbs[u]) < 0) {
                fprintf(stderr, "submit: %s\n", strerror(errno));
                break;
            }
            ++submitted;
        }
    }
    // frames: the UVC payload header's FID bit flips at each new frame, EOF ends one
    const size_t cap = maxFrame > 0 && maxFrame < 4u << 20 ? maxFrame + 65536 : 1u << 20;
    unsigned char* frame = static_cast<unsigned char*>(malloc(cap));
    size_t have = 0;
    int fid = -1;
    unsigned complete = 0, packets = 0, payloadBytes = 0, errors = 0;
    bool started = false;
    const uint64_t deadline = static_cast<uint64_t>(time(nullptr)) + 6;
    while (submitted == kUrbs && rc != 0 && static_cast<uint64_t>(time(nullptr)) < deadline) {
        struct pollfd pfd = {fd, POLLOUT, 0};
        poll(&pfd, 1, 200);
        struct usbdevfs_urb* done = nullptr;
        while (xioctl(fd, USBDEVFS_REAPURBNDELAY, &done) == 0 && done) {
            const unsigned char* buf = static_cast<const unsigned char*>(done->buffer);
            for (int p = 0; p < done->number_of_packets && rc != 0; ++p) {
                const unsigned char* pkt = buf + p * info.packetSize;
                const unsigned got = done->iso_frame_desc[p].actual_length;
                ++packets;
                if (done->iso_frame_desc[p].status != 0) {
                    ++errors;
                    continue;
                }
                if (got < 2 || pkt[0] < 2 || pkt[0] > got) {
                    continue;
                }
                const unsigned head = pkt[0], bits = pkt[1];
                const int f = bits & 1;
                if (bits & 0x40) {  // error bit
                    ++errors;
                }
                if (f != fid) {  // a new frame starts here
                    if (started && have > 0) {
                        ++complete;  // the last one ended without EOF
                    }
                    started = fid >= 0;  // the first frame seen may have started earlier: skipped
                    fid = f;
                    have = 0;
                }
                payloadBytes += got - head;
                if (started && have + (got - head) <= cap) {
                    memcpy(frame + have, pkt + head, got - head);
                    have += got - head;
                }
                if ((bits & 2) && started && have > 0) {  // EOF
                    ++complete;
                    if (complete > skip && have > 2 && frame[0] == 0xff && frame[1] == 0xd8) {
                        mkdir(kOutDir, 0755);
                        char out[PATH_MAX];
                        snprintf(out, sizeof(out), "%s/video-frame.jpg", kOutDir);
                        FILE* o = fopen(out, "wb");
                        if (o && fwrite(frame, 1, have, o) == have && fclose(o) == 0) {
                            printf("%s: %zu bytes (frame %u)\n", out, have, complete);
                            rc = 0;
                        } else if (o) {
                            fclose(o);
                        }
                    }
                    have = 0;
                    started = false;  // the next FID change starts the next one
                }
            }
            if (rc != 0 && xioctl(fd, USBDEVFS_SUBMITURB, done) < 0) {
                fprintf(stderr, "resubmit: %s\n", strerror(errno));
                --submitted;
                break;
            }
        }
    }
    printf("%u packets, %u payload bytes, %u frames, %u errors\n", packets, payloadBytes, complete, errors);
    if (rc != 0) {
        fprintf(stderr, "no JPEG frame saved (no picture from the input? try another frame size)\n");
    }
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
    free(frame);
    si.altsetting = 0;
    xioctl(fd, USBDEVFS_SETINTERFACE, &si);
    xioctl(fd, USBDEVFS_RELEASEINTERFACE, &iface);
    close(fd);
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        return list();
    }
    if (argc == 2 && !strcmp(argv[1], "--usb")) {
        return usb();
    }
    if (argc >= 3 && argc <= 5 && !strcmp(argv[1], "--usbfs-grab")) {
        return usbfsGrab(argv[2], argc >= 4 ? static_cast<unsigned>(atoi(argv[3])) : 0,
                         argc >= 5 ? static_cast<unsigned>(atoi(argv[4])) : 10);
    }
    if (argc >= 3 && !strcmp(argv[1], "--grab")) {
        int input = -1;
        v4l2_std_id std = 0;
        for (int i = 3; i < argc; ++i) {
            if (!strcmp(argv[i], "--input") && i + 1 < argc) {
                input = atoi(argv[++i]);
            } else if (!strcmp(argv[i], "--pal")) {
                std = V4L2_STD_PAL;
            } else if (!strcmp(argv[i], "--ntsc")) {
                std = V4L2_STD_NTSC;
            } else {
                fprintf(stderr, "unknown option %s\n", argv[i]);
                return 2;
            }
        }
        return grab(argv[2], input, std);
    }
    fprintf(stderr,
            "Usage: uvc-probe | uvc-probe --usb | uvc-probe --usbfs-grab PORT [FRAME [SKIP]]\n"
            "       uvc-probe --grab /dev/videoN [--input N] [--pal|--ntsc]\n"
            "Read-only: only USB cameras and grabbers are opened; --grab saves one frame to %s.\n",
            kOutDir);
    return argc == 2 && !strcmp(argv[1], "--help") ? 0 : 2;
}
