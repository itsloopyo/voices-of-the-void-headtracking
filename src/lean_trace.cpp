// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "lean_trace.h"

#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#include "aim_trace.h"
#include "kismet_trace.h"
#include "logging.h"
#include "ue_call.h"
#include "ue4_types.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::lean_trace {

namespace {

namespace ue = ::cameraunlock::unreal;
using cameraunlock::camera::LeanObstruction;
using cameraunlock::math::Vec3;

constexpr float kContactTolerance = 0.05f;
constexpr int kMaxQueries = 16;

struct ActorArray {
    const std::uintptr_t* Data;
    std::int32_t Num, Max;
};

ue_vm::ResolveRetry g_resolveRetry;

struct ReadbackLayout {
    std::size_t BlockingHit = 0;
    std::uint8_t BlockingHitMask = 0;
    std::size_t StartPenetrating = 0;
    std::uint8_t StartPenetratingMask = 0;
    std::size_t Location = 0;
    std::size_t PenetrationDepth = 0;
    std::size_t HitActor = 0;       // Actor, a weak pointer
    std::size_t HitComponent = 0;   // Component, a weak pointer
};

ue_call::Function g_trace;
ReadbackLayout g_readback;
std::uintptr_t g_kismetSystemCdo = 0;
std::uintptr_t g_pawn = 0;
kismet_trace::WeakObject g_lastHitActor;
kismet_trace::WeakObject g_lastHitComponent;
bool  g_ready = false;
bool  g_failed = false;
float g_margin = 10.0f;
int   g_channel = 0;

// Actors the trace is repeated through, one per attempt.
constexpr int kMaxIgnored = 6;
std::uintptr_t g_ignoreStorage[kMaxIgnored] = {};

void GiveUp(const char* what) {
    if (g_failed) return;
    g_failed = true;
    Log::Line("lean-trace: %s. Positional lean is withheld while collision is unavailable.", what);
}

bool Resolve() {
    if (g_ready) return true;
    if (g_failed) return false;
    if (!g_resolveRetry.Due()) return false;
    if (!ue_vm::Ready()) return false;

    const std::uintptr_t hitResult = ue::FindLiveObject("ScriptStruct", "HitResult", nullptr);
    std::vector<ue_reflect::FieldInfo> h;
    if (!hitResult ||
        !ue_reflect::ResolveAll("HitResult", hitResult,
                                {"bBlockingHit", "bStartPenetrating", "Location", "PenetrationDepth",
                                 "Actor", "Component"}, h)) {
        GiveUp("FHitResult's layout did not resolve");
        return false;
    }
    constexpr std::size_t kWeakBytes = kismet_trace::kWeakObjectBytes;
    if (h[0].BoolMask == 0 || h[1].BoolMask == 0 || h[2].Size != sizeof(ue4::FVector) ||
        h[3].Size != sizeof(float) || h[4].Size != kWeakBytes ||
        h[5].Size != kWeakBytes) {
        GiveUp("FHitResult is not the UE4 layout this mod reads");
        return false;
    }
    g_kismetSystemCdo = ue_call::DefaultObject("KismetSystemLibrary");
    if (!g_kismetSystemCdo || !g_trace.Resolve("KismetSystemLibrary", "SphereTraceSingle",
            {{"WorldContextObject", sizeof(std::uintptr_t)}, {"Start", sizeof(ue4::FVector)},
             {"End", sizeof(ue4::FVector)}, {"Radius", sizeof(float)}, {"TraceChannel", 1},
             {"bTraceComplex", 1}, {"ActorsToIgnore", sizeof(ActorArray)},
             {"OutHit", ue_reflect::StructSize(hitResult)}, {"bIgnoreSelf", 1}})) {
        GiveUp("SphereTraceSingle's parameter frame could not be resolved");
        return false;
    }

    g_readback.BlockingHit          = h[0].Offset;
    g_readback.BlockingHitMask      = h[0].BoolMask;
    g_readback.StartPenetrating     = h[1].Offset;
    g_readback.StartPenetratingMask = h[1].BoolMask;
    g_readback.Location             = h[2].Offset;
    g_readback.PenetrationDepth     = h[3].Offset;
    g_readback.HitActor             = h[4].Offset;
    g_readback.HitComponent         = h[5].Offset;

    Log::Line("lean-trace: SphereTraceSingle frame=%zu radius=%.1fcm channel=%d",
              g_trace.FrameSize(), g_margin, g_channel);
    g_ready = true;
    return true;
}

}  // namespace

void SetMargin(float centimetres) { g_margin = centimetres; }
void SetChannel(int traceTypeQuery) { g_channel = traceTypeQuery; }
void SetPawn(std::uintptr_t pawn) { g_pawn = pawn; }

bool Failed() { return g_failed; }

std::string LastHitDescription() {
    const std::uintptr_t actor = ue_call::ResolveWeak(g_lastHitActor.Index, g_lastHitActor.Serial);
    const std::uintptr_t component =
        ue_call::ResolveWeak(g_lastHitComponent.Index, g_lastHitComponent.Serial);
    std::string out = actor ? ue::ClassName(actor) + " " + ue::ObjectName(actor) : std::string("-");
    out += " / ";
    out += component ? ue::ClassName(component) + " " + ue::ObjectName(component) : std::string("-");
    return out;
}

LeanObstruction Query(void*, const Vec3& start, const Vec3& direction, float maxDistance) {
    LeanObstruction out;
    if (!Resolve() || g_pawn == 0) return out;

    const ue4::FVector from{start.x, start.y, start.z};
    const ue4::FVector to{start.x + direction.x * maxDistance,
                         start.y + direction.y * maxDistance,
                         start.z + direction.z * maxDistance};
    float radius = g_margin;
    std::int32_t ignored = 0;
    for (int attempt = 0; attempt < kMaxQueries; ++attempt) {
        ue_call::Frame frame(g_trace);
        frame.Set(0, g_pawn);
        frame.Set(1, from);
        frame.Set(2, to);
        frame.Set(3, radius);
        frame.Set(4, static_cast<std::uint8_t>(g_channel));
        frame.Set(6, ActorArray{g_ignoreStorage, ignored, ignored});
        frame.Set(8, std::uint8_t{1});
        if (!frame.Call(g_kismetSystemCdo)) return LeanObstruction{};

        out = LeanObstruction{};
        out.queried = true;
        const unsigned char* hit = frame.At(7);
        if ((hit[g_readback.BlockingHit] & g_readback.BlockingHitMask) == 0) return out;

        g_lastHitActor = kismet_trace::ReadWeak(hit + g_readback.HitActor);
        g_lastHitComponent = kismet_trace::ReadWeak(hit + g_readback.HitComponent);
        const std::uintptr_t actor = ue_call::ResolveWeak(g_lastHitActor.Index, g_lastHitActor.Serial);

        if (actor && aim_trace::OwnedBy(actor, g_pawn) && ignored < kMaxIgnored) {
            g_ignoreStorage[ignored++] = actor;
            continue;
        }
        out.blocked = true;
        if ((hit[g_readback.StartPenetrating] & g_readback.StartPenetratingMask) != 0) {
            float depth = 0;
            std::memcpy(&depth, hit + g_readback.PenetrationDepth, sizeof(depth));
            if (!std::isfinite(depth) || depth < 0.0f) return LeanObstruction{};
            // Preserve the clearance the game's clean eye already has. Ignoring
            // this actor would also discard its other walls; keeping the full
            // radius would stop even a lean away from the overlapping surface.
            radius -= depth + kContactTolerance;
            if (radius <= 0.0f) return out;
            continue;
        }

        ue4::FVector location{};
        std::memcpy(&location, hit + g_readback.Location, sizeof(location));
        const float room = (location.X - start.x) * direction.x +
                           (location.Y - start.y) * direction.y +
                           (location.Z - start.z) * direction.z - kContactTolerance;
        if (!std::isfinite(room)) return LeanObstruction{};
        out.distance = room > 0.0f ? room : 0.0f;
        return out;
    }
    return LeanObstruction{};
}

}  // namespace votv_ht::lean_trace
