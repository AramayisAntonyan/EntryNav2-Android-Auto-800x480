// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Read-only probe of USB video (UVC) adapters on the car, for a front camera on its own input. A developer tool, not
// part of a release (make dev-tools), run by hand from /tmp/stagex.
//
//   uvc-probe                    the UVC driver's presence, every video device; capabilities, formats and frame
//                                sizes of the UVC ones
//   uvc-probe --grab /dev/videoN one frame of that UVC device to /tmp/stagex/uvc-frame.<mjpg|yuyv|raw>
//
// A device is identified from sysfs (its driver link) before anything is opened, and only uvcvideo devices are ever
// opened: /dev/video0 (BMW's camera capture, dra7xx-vip) and /dev/video1 (the VPE scaler) are listed and left alone.
// Nothing is configured but the capture of --grab (format as the device has it, two buffers, then stopped).
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/utsname.h>
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

bool isUvc(const char* node) {
    char driver[64];
    driverOf(node, driver, sizeof(driver));
    return !strcmp(driver, "uvcvideo");
}

void driverPresence() {
    struct utsname u;
    uname(&u);
    struct stat st;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/lib/modules/%s/kernel/drivers/media/usb/uvc/uvcvideo.ko", u.release);
    printf("kernel %s\n", u.release);
    printf("uvcvideo loaded: %s\n", stat("/sys/module/uvcvideo", &st) == 0 ? "yes" : "no");
    printf("uvcvideo module file: %s\n", stat(path, &st) == 0 ? path : "absent (may be built in or missing)");
    printf("uvcvideo built in: %s\n", stat("/sys/bus/usb/drivers/uvcvideo", &st) == 0 ? "driver registered" : "no");
}

void describe(int fd) {
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        printf("  QUERYCAP failed: %s\n", strerror(errno));
        return;
    }
    printf("  card '%s', bus '%s', driver %s, caps 0x%08x%s%s\n", cap.card, cap.bus_info, cap.driver,
           cap.capabilities, (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ? " capture" : "",
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
               (in.status & V4L2_IN_ST_NO_SIGNAL) ? " (no signal)" : "",
               static_cast<unsigned long long>(in.std));
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
        if (!isUvc(e->d_name)) {
            printf("  not UVC: left alone\n");
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
    printf("%d UVC device(s)\n", uvc);
    return 0;
}

int grab(const char* dev) {
    const char* node = strrchr(dev, '/');
    node = node ? node + 1 : dev;
    if (!isUvc(node)) {
        fprintf(stderr, "%s is not a UVC device: refused (only uvcvideo devices are opened)\n", dev);
        return 2;
    }
    const int fd = open(dev, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
        return 1;
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
            struct timeval tv = {3, 0};
            struct v4l2_buffer b;
            memset(&b, 0, sizeof(b));
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            if (select(fd + 1, &rf, nullptr, nullptr, &tv) <= 0) {
                fprintf(stderr, "no frame within 3 s\n");
            } else if (xioctl(fd, VIDIOC_DQBUF, &b) < 0 || b.index >= 2 || maps[b.index] == MAP_FAILED) {
                fprintf(stderr, "dequeue failed: %s\n", strerror(errno));
            } else {
                char cc[5];
                fourcc(fmt.fmt.pix.pixelformat, cc);
                const char* ext = fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG  ? "mjpg"
                                  : fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV ? "yuyv"
                                                                                  : "raw";
                char out[PATH_MAX];
                mkdir(kOutDir, 0755);
                snprintf(out, sizeof(out), "%s/uvc-frame.%s", kOutDir, ext);
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

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        return list();
    }
    if (argc == 3 && !strcmp(argv[1], "--grab")) {
        return grab(argv[2]);
    }
    fprintf(stderr,
            "Usage: uvc-probe | uvc-probe --grab /dev/videoN\n"
            "Read-only: only uvcvideo devices are opened; --grab saves one frame to %s.\n",
            kOutDir);
    return argc == 2 && !strcmp(argv[1], "--help") ? 0 : 2;
}
