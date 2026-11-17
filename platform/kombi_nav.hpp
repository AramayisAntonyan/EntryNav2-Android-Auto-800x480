// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Android Auto's turn-by-turn arrows on the instrument cluster and the head-up display, without any iDrive route.

// The phone's navigation messages (channel 10, from the session's nav-status callback) drive the guide
// (kombi_guide.hpp), whose requests go from the head unit straight to the cluster (kombi_client.hpp), on the address,
// service and instance handle seen on the car. A worker thread ticks every 0.25 s.
//   - On by default; the off file (written by the iDrive app's menu) turns the arrows off, checked once a second.
//   - Cleared when the guidance stops for 8 s, the Android Auto session ends, the setting goes off, the service exits,
//     or a fatal signal arrives (async-signal-safe cancel).
//   - Fail closed: a request the cluster refuses stops the arrows until the next Android Auto session (a wrong car or a
//     stale instance handle is refused at the first request). Five link failures in a row without an arrow do the
//     same; otherwise the link is tried again 30 s later.
//   - The BMW navigation wins: while it guides (the probe given to start(), a read-only D-Bus getter), the arrows stop
//     writing without a cancel, and come back when it ends.
//   - The marker file says an arrow may be up; it survives a SIGKILL, and the next start sends the cancel again.
#ifndef ENTRY_AAD_PLATFORM_KOMBI_NAV_HPP
#define ENTRY_AAD_PLATFORM_KOMBI_NAV_HPP

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include <string>

#include "hud6wa.hpp"
#include "kombi_client.hpp"
#include "kombi_graph.hpp"
#include "kombi_guide.hpp"
#include "kombi_phone.hpp"

namespace evo {
namespace kombi {

// Where the arrows go and where their files are; the defaults are the car's.
struct ArrowsConfig {
    std::string clusterAddress;  // the KOMBI on the car's network
    uint16_t clusterPort;        // its route-guidance service
    uint32_t handle;             // the guidance instance, 0xC8 on every boot seen
    std::string offFile;         // present: the arrows are off
    std::string markerFile;      // present: an arrow may be up
    std::string singleFile;      // present at construction: only the next maneuver's graph
    // Present at construction: an F-series 6WA cluster (MOST HUDTerminal, platform/hud6wa.hpp) instead of the G-series
    // KOMBI. Log only for now: every call is written to the service log, nothing reaches the car.
    std::string hud6waFile;
    std::string hud6waLiveFile;  // present: the 6WA calls are sent (PipeSink)
    // Present at construction: the next maneuver shows as soon as the phone sends it, at any distance (NBT-AAD #205,
    // "Show directions at any distance"): the approach is off.
    std::string anyDistanceFile;
    // Present at construction: the 6WA keeps its arrow up to the maneuver, no junction view (NBT-AAD #162).
    std::string junctionOffFile;
    // Read at construction: the 6WA's approach rule (hud6wa.hpp HudApproach, parseApproach); absent: time 50 20.
    std::string approachFile;
    // Present at construction: the phone's lanes on the 6WA (layers 9/10).
    std::string lanesFile;
    GuideSettings guide;
    ArrowsConfig();
};

class Arrows {
public:
    // 1: the BMW navigation guides (the arrows give way), 0: it doesn't, -1: unknown (the arrows stay).
    using BmwProbe = int (*)();

    explicit Arrows(const ArrowsConfig& config = ArrowsConfig());
    ~Arrows();

    // Starts the worker, idle while the setting is off. probe: null never gives way. Once, from main().
    void start(BmwProbe probe = nullptr);
    // Stops the threads and clears the arrow, one quick attempt. Safe to call twice.
    void stop();

    // The session's nav-status callback (session thread): keeps the message and returns.
    static void onNavStatus(uint16_t id, const uint8_t* body, size_t size, void* arrows);
    // The Android Auto session ended (session thread).
    void onSessionEnded();
    // From the fatal-signal handler: the emergency cancel when an arrow may be up. Async-signal-safe.
    static void onFatalSignal();

    // On unless the off file exists.
    bool settingOn() const;

private:
    class Output : public GuideOutput {
    public:
        explicit Output(Arrows& arrows) : arrows_(arrows) {}
        SendOutcome send(const std::vector<Request>& requests, std::string* why) override;
        bool cancel(std::string* why) override;

    private:
        Arrows& arrows_;
    };

    static void* workerMain(void* self);
    static void* watchMain(void* self);
    static bool quitting(void* self);
    void work();
    void watchBmwGuidance();
    void tick(double now);
    void markArrow(bool up, bool force = false);

    const ArrowsConfig config_;
    const bool singleManeuver_;
    RouteGraph graph_;
    ClusterLink link_;
    Output output_;
    Guide guide_;
    const bool hud6wa_;  // the 6WA driver runs instead of guide_
    hudterm::ShadowSink shadowSink_;
    hudterm::PipeSink pipeSink_;
    hudterm::HudSink& hudSink_;  // pipeSink_ with kombi-6wa-live, else shadowSink_
    hudterm::Hud6wa hud_;
    BmwProbe probe_;

    // From the session thread, under mu_
    pthread_mutex_t mu_;
    NavInbox inbox_;
    double lastMessageAt_;
    bool sessionEnded_;
    bool quit_;

    bool started_;
    pthread_t worker_;

    // The worker's own
    bool enabled_;
    double settingCheckedAt_;
    bool offUntilNextSession_;  // the cluster refused a request, or never answered
    double retryAt_;
    bool noSecondsNoted_;  // the 6WA's "no seconds" line was written this Android Auto session
    int linkFailures_;
    bool marked_;

    // The BMW navigation watch, under watchMu_
    pthread_mutex_t watchMu_;
    bool bmwGuiding_;
    bool watching_;
    pthread_t watcher_;
};

}  // namespace kombi
}  // namespace evo

#endif
