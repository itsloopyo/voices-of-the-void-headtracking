// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "tracking.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "logging.h"
#include "udp_link.h"

namespace votv_ht::tracking {

namespace {

using udp_link::LinkState;

// How often the link is sampled. Half the receiver's own 500ms bind-retry
// interval, so the line saying the port came free lands within a tick of the
// bind rather than up to a full retry behind it.
constexpr int kSampleIntervalMs = 250;

std::unique_ptr<cameraunlock::UdpReceiver> g_receiver;
std::unique_ptr<Session> g_session;
// Joined in its destructor as well as in Stop(). On process exit DllMain skips
// Shutdown, the kernel has already ended the thread, and the CRT still runs this
// DLL's static destructors - where a joinable std::thread calls std::terminate
// and turns every quit into an abort.
struct Watcher {
    std::thread thread;
    ~Watcher() {
        if (thread.joinable()) thread.join();
    }
} g_watch;
std::atomic<bool> g_watching{false};
std::uint16_t g_port = 0;

std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Sample the link and write a line whenever it moves.
//
// Change-driven rather than periodic: the receiver already logs its own bind,
// rebind and first-packet lines, and a fixed heartbeat on top of those would
// bury them. What this adds is the pairing - how long the port was held before
// it came free, and whether binding it actually produced any traffic - which is
// the difference between "another game had the port" and "the tracker app is not
// sending here", two faults that look identical from inside the game.
void WatchThread() {
    udp_link::LinkWatch watch;
    while (g_watching.load(std::memory_order_acquire)) {
        const LinkState state = udp_link::ClassifyLink(
            g_receiver->IsRetrying(), g_receiver->IsRunning(), g_receiver->IsReceiving());

        const udp_link::LinkChange change = watch.Note(state, NowMs());
        if (change.changed) {
            if (change.held_ms > 0) {
                Log::Line("link: UDP %u %s -> %s after %.1fs", g_port,
                          udp_link::LinkStateName(change.from),
                          udp_link::LinkStateName(change.to),
                          change.held_ms / 1000.0);
            } else {
                Log::Line("link: UDP %u %s", g_port, udp_link::LinkStateName(change.to));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kSampleIntervalMs));
    }
}

}  // namespace

void Start(const Config& config) {
    g_port = static_cast<std::uint16_t>(config.udp_port);

    g_receiver = std::make_unique<cameraunlock::UdpReceiver>();
    g_receiver->SetLog([](const std::string& m) { Log::Line("udp: %s", m.c_str()); });

    // The result is deliberately unused. False says only that the port was busy
    // at this instant, and the supervisor this call leaves running is the one
    // thing that reclaims it.
    g_receiver->Start(g_port);

    g_session = std::make_unique<Session>(*g_receiver);
    g_session->SetLocalSmoothing(config.local_smoothing);
    g_session->SetRemoteSmoothing(config.remote_smoothing);

    // The session picks between the local and remote smoothing values per
    // connection, re-read from the receiver on every Update(). Without this the
    // detection compiles away silently and every remote tracker gets the local
    // value.
    static_assert(Session::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() or smoothing silently stays local");

    g_watching.store(true, std::memory_order_release);
    g_watch.thread = std::thread(WatchThread);
}

void Stop() {
    g_watching.store(false, std::memory_order_release);
    if (g_watch.thread.joinable()) g_watch.thread.join();
    if (g_receiver) g_receiver->Stop();
    g_session.reset();
    g_receiver.reset();
}

Session* Get() { return g_session.get(); }

cameraunlock::UdpReceiver* Receiver() { return g_receiver.get(); }

}  // namespace votv_ht::tracking
