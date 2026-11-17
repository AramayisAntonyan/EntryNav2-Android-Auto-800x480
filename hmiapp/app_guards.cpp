// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The off switch, the crash guard, the boot-loop guard and the automatic start (app_guards.hpp).
#include "app_guards.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../platform/service_contract.hpp"
#include "app_log.hpp"
#include "service_link.hpp"

namespace evo {
namespace app {

namespace {

const long kAutostartDelayS = 30;
const int kBootGuardMax = 3;
const long kBootGuardWindowS = 15 * 60;
const int kCrashGuardMax = 3;  // starts without 2 min of uptime before the app turns itself off
const long kStableAfterS = 120;
const double kFreshBootS = 90;  // iDrive loads the app ~12 s after the unit starts; later: iDrive restarted alone

bool g_autostartDone = false, g_stableMarked = false;

bool fileExists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

// A setting that is a file: there or not. content: what a new file holds.
void setSwitch(const char* what, const char* path, bool on, const char* content) {
    if (!on) {
        if (unlink(path) != 0 && errno != ENOENT) {
            alog("%s: cannot remove %s (errno %d)", what, path, errno);
        }
        return;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        alog("%s: cannot create %s (errno %d)", what, path, errno);
        return;
    }
    ssize_t w = write(fd, content, strlen(content));
    (void)w;
    fsync(fd);
    close(fd);
}

bool bootGuardAllows() {
    time_t now = time(nullptr);
    long t[8];
    int n = 0;
    FILE* f = fopen(contract::kBootGuard, "r");
    if (f) {
        long v;
        while (n < 8 && fscanf(f, "%ld", &v) == 1) {
            if (v <= now && now - v < kBootGuardWindowS) {
                t[n++] = v;
            }
        }
        fclose(f);
    }
    if (n >= kBootGuardMax) {
        alog("autostart SKIPPED by the bootloop guard (%d starts within %ld min): use Start in the menu", n,
             kBootGuardWindowS / 60);
        return false;
    }
    f = fopen(contract::kBootGuard, "w");
    if (f) {
        for (int i = 0; i < n; ++i) {
            fprintf(f, "%ld\n", t[i]);
        }
        fprintf(f, "%ld\n", static_cast<long>(now));
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }
    return true;
}

}  // namespace

bool appDisabled() {
    if (fileExists(contract::kAppDisable)) {
        alog("disabled by %s", contract::kAppDisable);
        return true;
    }
    return false;
}

// ---- crash guard (HMI crash loop protection) ------------------------------------------------------------------------
// create() notes the load in app-guard-load; 2 minutes later the status poll deletes it. A note still there at the
// next create() = the last run was short: judged by how it ended, a crash adds a line to app-guard. kCrashGuardMax
// lines: create() refuses (iDrive starts without the app).
LastRunEnd judgeLastRun(double systemUptimeS, const char* coresThen, const char* coresNow) {
    if (systemUptimeS > kFreshBootS) {
        return LastRunEnd::kHmiRestart;  // iDrive went down and came back while the unit kept running
    }
    if (strcmp(coresThen ? coresThen : "", coresNow ? coresNow : "") != 0) {
        return LastRunEnd::kNewCoreDump;  // a process crashed, then the unit restarted
    }
    return LastRunEnd::kUnitRestart;  // coding, the volume knob, ignition: not a crash
}

namespace {

double systemUptime() {
    double up = 0;
    FILE* f = fopen("/proc/uptime", "r");
    if (f) {
        if (fscanf(f, "%lf", &up) != 1) {
            up = 0;
        }
        fclose(f);
    }
    return up;
}

// The unit's core dumps as one line, "00/core.vmostd.230.gz 01/...": a new crash changes it (names carry the pid).
void listCores(char* out, size_t cap) {
    out[0] = 0;
    size_t used = 0;
    static const char* const kSlots[] = {"00", "01", "02", "03", "04"};
    for (unsigned s = 0; s < sizeof(kSlots) / sizeof(kSlots[0]); ++s) {
        char dir[96];
        snprintf(dir, sizeof(dir), "%s/%s", contract::kCoreDumpDir, kSlots[s]);
        DIR* d = opendir(dir);
        if (!d) {
            continue;
        }
        while (struct dirent* e = readdir(d)) {
            if (strncmp(e->d_name, "core.", 5) != 0) {
                continue;
            }
            const int n = snprintf(out + used, cap - used, "%s%s/%s", used ? " " : "", kSlots[s], e->d_name);
            if (n < 0 || static_cast<size_t>(n) >= cap - used) {
                break;
            }
            used += static_cast<size_t>(n);
        }
        closedir(d);
    }
}

void writeFileSynced(const char* path, const char* text, bool append) {
    FILE* f = fopen(path, append ? "a" : "w");
    if (f) {
        fputs(text, f);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }
}

}  // namespace

bool crashGuardAllows() {
    const double up = systemUptime();
    char coresNow[512];
    listCores(coresNow, sizeof(coresNow));
    char note[600] = "";
    FILE* f = fopen(contract::kAppGuardLoad, "r");
    if (f) {
        if (!fgets(note, sizeof(note), f)) {
            note[0] = 0;
        }
        fclose(f);
        note[strcspn(note, "\r\n")] = 0;
        // "<time> <cores...>": the load before this one, which never reached 2 stable minutes
        const char* coresThen = strchr(note, ' ');
        coresThen = coresThen ? coresThen + 1 : "";
        const LastRunEnd end = judgeLastRun(up, coresThen, coresNow);
        if (end == LastRunEnd::kUnitRestart) {
            alog("crash guard: the last run ended by a restart of the unit (no crash record): not counted");
        } else {
            const char* why = end == LastRunEnd::kHmiRestart ? "iDrive restarted while the unit kept running"
                                                             : "the unit stored a new core dump";
            alog("crash guard: the last run was a crash (%s; cores now: %s)", why, coresNow[0] ? coresNow : "none");
            char line[160];
            snprintf(line, sizeof(line), "%ld %s\n", static_cast<long>(time(nullptr)),
                     end == LastRunEnd::kHmiRestart ? "hmi-restart" : "core-dump");
            writeFileSynced(contract::kAppGuard, line, true);
        }
    }
    int lines = 0;
    f = fopen(contract::kAppGuard, "r");
    if (f) {
        char b[64];
        while (fgets(b, sizeof(b), f)) {
            ++lines;
        }
        fclose(f);
    }
    if (lines >= kCrashGuardMax) {
        alog(
            "crash guard: iDrive crashed %d times shortly after loading this app: app turned off. "
            "Remove %s after checking the logs, then restart the unit.",
            kCrashGuardMax, contract::kAppGuard);
        return false;
    }
    char line[600];
    snprintf(line, sizeof(line), "%ld %s\n", static_cast<long>(time(nullptr)), coresNow);
    writeFileSynced(contract::kAppGuardLoad, line, false);
    return true;
}

void resetGuardsWhenStable(time_t initTime) {
    if (!g_stableMarked && initTime && time(nullptr) - initTime >= kStableAfterS) {
        g_stableMarked = true;
        unlink(contract::kAppGuard);  // the HMI has been up 2 min with us: not a crash loop
        unlink(contract::kAppGuardLoad);
        unlink(contract::kBootGuard);
        alog("stable for %ld s: crash and boot guards reset", kStableAfterS);
    }
}

// ---- start automatically (with a bootloop guard) --------------------------------------------------------------------
void serviceAutostart(time_t initTime) {
    if (g_autostartDone || !initTime || time(nullptr) - initTime < kAutostartDelayS) {
        return;
    }
    g_autostartDone = true;
    if (!autostartOn()) {
        return;
    }
    ServiceState d = readServiceState();
    if (d.pid != 0) {
        alog("autostart: service already running (pid %d)", d.pid);
        return;
    }
    if (!bootGuardAllows()) {
        return;
    }
    alog("autostart: launching the service");
    launchService();
}

void forgetBootGuard() {
    unlink(contract::kBootGuard);
}

bool autostartOn() {
    return fileExists(contract::kAutostart);
}

void toggleAutostart() {
    bool on = autostartOn();
    setSwitch("autostart", contract::kAutostart, !on, "1\n");
    alog("menu: start automatically %s -> %s", on ? "on" : "off", autostartOn() ? "on" : "off");
}

bool usbCableOn() {
    return fileExists(contract::kUsb);
}

void toggleUsbCable() {
    bool on = usbCableOn();
    setSwitch("USB cable", contract::kUsb, !on, "");  // empty: every USB port, as `touch` leaves it
    alog("menu: USB cable %s -> %s", on ? "on" : "off", usbCableOn() ? "on" : "off");
}

bool anyDistanceOn() {
    return fileExists(contract::kKombiAnyDistance);
}

void toggleAnyDistance() {
    bool on = anyDistanceOn();
    setSwitch("directions at any distance", contract::kKombiAnyDistance, !on, "");
    alog("menu: directions at any distance %s -> %s (from the next Android Auto start)", on ? "on" : "off",
         anyDistanceOn() ? "on" : "off");
}

}  // namespace app
}  // namespace evo
