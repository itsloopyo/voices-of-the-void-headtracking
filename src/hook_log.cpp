// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hook_log.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <unordered_map>

#include <windows.h>

#include "camera_fov.h"
#include "lean_trace.h"
#include "logging.h"
#include "tracking.h"
#include "udp_link.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::hook_log {

namespace {

namespace ue = ::cameraunlock::unreal;

constexpr std::uint64_t kHeartbeatMs = 30000;
constexpr std::uint64_t kZoomSettleMs = 1000;
constexpr std::uint64_t kCallerSummaryEvery = 3000;
constexpr std::uint64_t kLeanSettleMs = 250;

// A zoom factor moving by less than this is the same factor: it is a quarter of
// the last digit the log prints, so a change below it could not be read anyway.
constexpr float kFactorEpsilon = 0.0005f;

std::atomic<bool> g_reportRequested{false};

struct CallerStats { std::uint64_t Count = 0; std::int64_t Stride = 0; };
std::mutex g_callerMutex;
std::unordered_map<std::uintptr_t, CallerStats> g_callerCounts;
std::atomic<std::uint64_t> g_callerLastSummary{0};

// How the UDP link reads right now, through udp_link's classifier rather than a
// second walk of the same three flags: the hand-rolled one here could never
// report "receiving", so a heartbeat taken with a tracker feeding the mod said
// "listening".
const char* LinkName() {
    auto* receiver = tracking::Receiver();
    if (!receiver) return "none";
    return udp_link::LinkStateName(udp_link::ClassifyLink(
        receiver->IsRetrying(), receiver->IsRunning(), receiver->IsReceiving()));
}

const char* LeanName(const FrameReport& r) {
    if (!r.CollisionEnabled) return "off";
    if (r.LeanQueryFailed) return "unavailable";
    return r.LeanInContact ? "contact" : "clear";
}

}  // namespace

void CountCaller(std::uintptr_t returnRva, std::uint64_t call, std::int64_t rotLocStride) {
    {
        std::lock_guard<std::mutex> lk(g_callerMutex);
        CallerStats& c = g_callerCounts[returnRva];
        ++c.Count;
        c.Stride = rotLocStride;
    }
    if (call - g_callerLastSummary.load(std::memory_order_relaxed) < kCallerSummaryEvery) return;
    g_callerLastSummary.store(call, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_callerMutex);
    Log::Line("caller-summary @%llu calls: %zu unique return RVAs:",
              static_cast<unsigned long long>(call), g_callerCounts.size());
    for (const auto& kv : g_callerCounts)
        Log::Line("  ret RVA 0x%08llx  count=%llu  rot-loc=%lld",
                  static_cast<unsigned long long>(kv.first),
                  static_cast<unsigned long long>(kv.second.Count),
                  static_cast<long long>(kv.second.Stride));
}

void RequestReport() { g_reportRequested.store(true); }

void Heartbeat(const FrameReport& r, std::uintptr_t returnRva) {
    static std::uint64_t s_last = 0;
    const std::uint64_t now = GetTickCount64();
    const bool requested = g_reportRequested.exchange(false);
    if (!requested && s_last != 0 && now - s_last < kHeartbeatMs) return;
    s_last = now;

    Log::Line("heartbeat ret=0x%08llx tracking=%s verdict=%s gate=%s viewOff=%.1fcm/%.1fdeg "
              "udp=%s pose=%d "
              "applied=(Y%.2f P%.2f R%.2f x%.3f y%.3f z%.3f) fov=%.2f base=%.2f zoom=%.4f "
              "constraint=%d tan=(%.4f,%.4f) lean=%s",
              static_cast<unsigned long long>(returnRva),
              r.TrackingEnabled ? "ON" : "OFF", Reason(r.Verdict),
              game_state::BlockerName(r.Gate.Why), r.Gate.ViewOffsetCm, r.Gate.ViewAngleDeg,
              LinkName(), r.HavePose ? 1 : 0, r.Applied.yaw, r.Applied.pitch, r.Applied.roll,
              r.Applied.x, r.Applied.y, r.Applied.z, r.RenderFov, r.BaseFov, r.ZoomFactor,
              camera_fov::AspectConstraint(), r.TanX, r.TanY, LeanName(r));
    Log::Line("aim trace=%d hit=%d dist=%.1fcm point=(%.1f,%.1f,%.1f) "
              "posOff=(%.2f,%.2f,%.2f) mark=%d ndc=(%.4f,%.4f) onScreen=%d",
              r.TraceValid ? 1 : 0, r.TraceHit ? 1 : 0, r.TraceDistance,
              r.AimPoint.X, r.AimPoint.Y, r.AimPoint.Z, r.PositionOffset.X, r.PositionOffset.Y,
              r.PositionOffset.Z, r.Mark.Valid ? 1 : 0, r.Mark.X, r.Mark.Y, r.MarkOnScreen ? 1 : 0);
}

// A transition only counts once it has stood still for kLeanSettleMs. The clamp
// engages the frame the wall is nearer than the lean and releases the frame it
// is not, and its settle tolerance is a thousandth of the lean - under a
// millimetre - so a lean HELD at that depth crosses the boundary on tracker
// noise alone, every few frames, for as long as the player holds it. Edge
// triggering alone does not bound that: it writes a pair of lines per crossing
// and buries the heartbeat the file is read for. Waiting costs a quarter second
// on a real change, and the live state rides every heartbeat regardless.
void LeanClamp(const cameraunlock::math::Vec3& wanted, const cameraunlock::math::Vec3& allowed,
               bool queryFailed, bool inContact) {
    static bool s_contact = false;
    static bool s_failed = false;
    static bool s_pendingContact = false;
    static bool s_pendingFailed = false;
    static std::uint64_t s_pendingSince = 0;

    if (inContact != s_pendingContact || queryFailed != s_pendingFailed) {
        s_pendingContact = inContact;
        s_pendingFailed = queryFailed;
        s_pendingSince = GetTickCount64();
        return;
    }
    if (inContact == s_contact && queryFailed == s_failed) return;
    if (GetTickCount64() - s_pendingSince < kLeanSettleMs) return;

    if (queryFailed != s_failed) {
        s_failed = queryFailed;
        Log::Line("lean-clamp: sweep %s", queryFailed ? "FAILED - the lean is running unclamped"
                                                      : "working again");
    }
    if (inContact != s_contact) {
        s_contact = inContact;
        Log::Line("lean-clamp: %s (wanted %.1fcm, allowed %.1fcm)%s%s",
                  inContact ? "holding the view off geometry" : "clear",
                  wanted.Magnitude(), allowed.Magnitude(), inContact ? " on " : "",
                  inContact ? lean_trace::LastHitDescription().c_str() : "");
    }
}

void NonFinitePose(bool rotation) {
    static bool s_rotation = false;
    static bool s_position = false;
    bool& logged = rotation ? s_rotation : s_position;
    if (logged) return;
    logged = true;
    const char* what = rotation ? "rotation" : "position";
    Log::Line("pose: the tracker session's %s went non-finite (inf/NaN) after a sample far "
              "outside any real head pose arrived on the UDP port - it is not applied to the "
              "view for the rest of this session; restart the game to recover", what);
}

// Both FOVs are the camera's own scalar in degrees - the one being drawn and
// the un-zoomed one the player's FOV setting put there - so the factor reads
// 1.0000 at the hip in gameplay, with or without a tracker; anything else there
// means the units do not match.
void ZoomTerms(const FrameReport& r, std::uint64_t tick) {
    static float s_loggedFactor = 0.0f;
    static float s_heldFactor = 0.0f;
    static std::uint64_t s_heldSince = 0;
    if (!r.Gate.InGameplay || !camera_fov::Plausible(r.RenderFov) ||
        !camera_fov::Plausible(r.BaseFov))
        return;
    if (std::fabs(r.ZoomFactor - s_heldFactor) > kFactorEpsilon) {
        s_heldFactor = r.ZoomFactor;
        s_heldSince = tick;
        return;
    }
    if (tick - s_heldSince < kZoomSettleMs ||
        std::fabs(r.ZoomFactor - s_loggedFactor) <= kFactorEpsilon)
        return;
    s_loggedFactor = r.ZoomFactor;
    Log::Line("fov: zoom compensation terms at the hip: render %.2f deg, base "
              "(un-zoomed) %.2f deg, constraint %d, factor %.4f",
              r.RenderFov, r.BaseFov, camera_fov::AspectConstraint(), r.ZoomFactor);
}

// A field of view that does not read leaves the pose unscaled rather than
// scaled by a guess, and the log has to say so: a factor parked at 1.0 through a
// scope looks exactly like a game that never zooms. The 0.00 in the line says
// which of the two went missing.
void FovReadable(const FrameReport& r) {
    static bool s_readable = true;
    if (!r.Gate.InGameplay) return;
    const bool readable = camera_fov::Plausible(r.RenderFov) && camera_fov::Plausible(r.BaseFov);
    if (readable == s_readable) return;
    s_readable = readable;
    Log::Line("fov: render %.2f deg, un-zoomed %.2f deg - %s", r.RenderFov, r.BaseFov,
              readable ? "both readable again, zoom compensation is back on"
                       : "not both readable, so the pose is applied with no zoom "
                         "compensation");
}

// Only when the trace starts or stops running. What it stops on changes several
// times a second in ordinary play, and whether it stops on anything within reach
// flips as often as the player looks between near and open space, so a line for
// either is per-frame logging wearing a state change's clothes. The first result
// is named once, as evidence the cast works; the live point, distance and hit
// flag ride the heartbeat.
void AimTrace(const aim_trace::Result& hit, double maxDistanceCm) {
    static int s_running = -1;
    const int running = hit.Valid ? 1 : 0;
    if (running == s_running) return;
    s_running = running;
    if (!hit.Valid)
        Log::Line("aim-trace: not running - no mark is drawn, and the game's own "
                  "crosshair stays where it laid it out");
    else if (!hit.Hit)
        Log::Line("aim-trace: running, nothing within %.0fcm of the aim", maxDistanceCm);
    else
        Log::Line("aim-trace: running, stops on %s %s / %s %s at %.1fcm",
                  hit.Actor ? ue::ClassName(hit.Actor).c_str() : "-",
                  hit.Actor ? ue::ObjectName(hit.Actor).c_str() : "-",
                  hit.Component ? ue::ClassName(hit.Component).c_str() : "-",
                  hit.Component ? ue::ObjectName(hit.Component).c_str() : "-", hit.Distance);
}

void FrameCost(std::uint64_t hookUs, std::uint64_t gapUs) {
    constexpr std::uint64_t kSlowHookUs = 1000;
    constexpr std::uint64_t kStallUs = 50000;
    constexpr std::uint64_t kWindowMs = 1000;
    static std::uint64_t s_windowStart = 0;
    static std::uint64_t s_passes = 0, s_hookTotal = 0, s_hookMax = 0, s_gapMax = 0;
    static std::uint64_t s_hookAtGapMax = 0, s_slow = 0, s_stalls = 0;

    ++s_passes;
    s_hookTotal += hookUs;
    if (hookUs > s_hookMax) s_hookMax = hookUs;
    if (hookUs > kSlowHookUs) ++s_slow;
    if (gapUs > kStallUs) ++s_stalls;
    if (gapUs > s_gapMax) {
        s_gapMax = gapUs;
        s_hookAtGapMax = hookUs;
    }

    const std::uint64_t now = GetTickCount64();
    if (s_windowStart == 0) s_windowStart = now;
    if (now - s_windowStart < kWindowMs) return;
    if (s_slow > 0 || s_stalls > 0)
        Log::Line("perf: %llu hook passes, avg %.3fms max %.3fms (%llu over 1ms); longest gap "
                  "between passes %.1fms, the pass after it %.3fms; %llu gaps over 50ms",
                  static_cast<unsigned long long>(s_passes),
                  s_hookTotal / 1000.0 / static_cast<double>(s_passes), s_hookMax / 1000.0,
                  static_cast<unsigned long long>(s_slow), s_gapMax / 1000.0,
                  s_hookAtGapMax / 1000.0, static_cast<unsigned long long>(s_stalls));
    s_windowStart = now;
    s_passes = s_hookTotal = s_hookMax = s_gapMax = s_hookAtGapMax = s_slow = s_stalls = 0;
}

}  // namespace votv_ht::hook_log
