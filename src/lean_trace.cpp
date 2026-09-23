// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "lean_trace.h"

#include <cstring>
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

// Floor on the cosine between the lean and a surface's normal. The margin along
// the lean is margin / cos, which runs away at a grazing approach; past this
// angle (about 75 degrees) the lean is held at margin / kMinCos, and the trace
// overreaches the lean by that much so it sees the surface the eye would come
// to rest against.
constexpr float kMinCos = 0.25f;

ue_vm::ResolveRetry g_resolveRetry;

// What this trace reads back out of the frame kismet_trace fills: the
// FHitResult fields under OutHit. The return value is not among them - see the
// note in Query().
struct ReadbackLayout {
    std::size_t BlockingHit = 0;
    std::uint8_t BlockingHitMask = 0;
    std::size_t StartPenetrating = 0;
    std::uint8_t StartPenetratingMask = 0;
    std::size_t ImpactPoint = 0;
    std::size_t ImpactNormal = 0;
    std::size_t HitActor = 0;       // Actor, a weak pointer
    std::size_t HitComponent = 0;   // Component, a weak pointer
};

kismet_trace::Layout g_trace;
ReadbackLayout g_readback;
std::uintptr_t g_lineTraceFn = 0;
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
    Log::Line("lean-trace: %s. The lean runs UNCLAMPED for this session - head "
              "position still works, but leaning into a wall will put the view "
              "through it.", what);
}

bool Resolve() {
    if (g_ready) return true;
    if (g_failed) return false;
    if (!g_resolveRetry.Due()) return false;
    if (!ue_vm::Ready()) return false;

    switch (kismet_trace::Resolve("lean-trace", g_kismetSystemCdo, g_lineTraceFn, g_trace)) {
        case kismet_trace::Resolution::NotYet:
            return false;
        case kismet_trace::Resolution::Unusable:
            GiveUp("the trace's parameter frame is not the shape this mod writes");
            return false;
        case kismet_trace::Resolution::Ok:
            break;
    }

    const std::uintptr_t hitResult = ue::FindLiveObject("ScriptStruct", "HitResult", nullptr);
    std::vector<ue_reflect::FieldInfo> h;
    if (!hitResult ||
        !ue_reflect::ResolveAll("HitResult", hitResult,
                                {"bBlockingHit", "bStartPenetrating", "ImpactPoint", "ImpactNormal",
                                 "Actor", "Component"}, h)) {
        GiveUp("FHitResult's layout did not resolve");
        return false;
    }
    constexpr std::size_t kWeakBytes = kismet_trace::kWeakObjectBytes;
    if (h[0].BoolMask == 0 || h[1].BoolMask == 0 || h[2].Size != sizeof(ue4::FVector) ||
        h[3].Size != sizeof(ue4::FVector) || h[4].Size != kWeakBytes ||
        h[5].Size != kWeakBytes) {
        Log::Line("lean-trace: FHitResult bBlockingHit mask 0x%02x, bStartPenetrating mask 0x%02x, "
                  "ImpactPoint %zu bytes, ImpactNormal %zu bytes (expected %zu)",
                  h[0].BoolMask, h[1].BoolMask, h[2].Size, h[3].Size, sizeof(ue4::FVector));
        GiveUp("FHitResult is not the UE4 layout this mod reads");
        return false;
    }
    if (!kismet_trace::OutHitHolds(g_trace, ue_reflect::StructSize(hitResult))) {
        GiveUp("LineTraceSingle.OutHit cannot hold a whole FHitResult");
        return false;
    }

    g_readback.BlockingHit          = h[0].Offset;
    g_readback.BlockingHitMask      = h[0].BoolMask;
    g_readback.StartPenetrating     = h[1].Offset;
    g_readback.StartPenetratingMask = h[1].BoolMask;
    g_readback.ImpactPoint          = h[2].Offset;
    g_readback.ImpactNormal         = h[3].Offset;
    g_readback.HitActor             = h[4].Offset;
    g_readback.HitComponent         = h[5].Offset;

    Log::Line("lean-trace: LineTraceSingle frame=%zu Start=+0x%zx End=+0x%zx OutHit=+0x%zx | "
              "FHitResult bBlockingHit=+0x%zx ImpactPoint=+0x%zx ImpactNormal=+0x%zx | "
              "margin=%.1fcm channel=%d",
        g_trace.ParamsSize, g_trace.Start, g_trace.End, g_trace.OutHit,
        g_readback.BlockingHit, g_readback.ImpactPoint, g_readback.ImpactNormal, g_margin, g_channel);
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

    const float reach = maxDistance + g_margin / kMinCos;
    kismet_trace::Shot shot;
    shot.WorldContext = g_pawn;
    shot.Start = ue4::FVector{start.x, start.y, start.z};
    shot.End = ue4::FVector{start.x + direction.x * reach, start.y + direction.y * reach,
                            start.z + direction.z * reach};
    shot.Channel = g_channel;
    shot.Complex = false;
    shot.Ignore = g_ignoreStorage;

    std::int32_t ignored = 0;
    for (int attempt = 0; attempt <= kMaxIgnored; ++attempt) {
        alignas(16) unsigned char buf[kismet_trace::kMaxParams];
        shot.IgnoreCount = ignored;
        kismet_trace::FillFrame(g_trace, shot, buf);

        if (!ue_vm::Dispatch(reinterpret_cast<void*>(g_kismetSystemCdo),
                             reinterpret_cast<void*>(g_lineTraceFn), buf))
            return LeanObstruction{};   // queried stays false: the clamp then passes the lean through

        // The return value is ignored on purpose - bBlockingHit in the struct is
        // the same answer and reading it keeps this independent of how the
        // shipping build passes a bool back.
        out = LeanObstruction{};
        out.queried = true;
        const unsigned char* hit = buf + g_trace.OutHit;
        if ((hit[g_readback.BlockingHit] & g_readback.BlockingHitMask) == 0) return out;

        g_lastHitActor = kismet_trace::ReadWeak(hit + g_readback.HitActor);
        g_lastHitComponent = kismet_trace::ReadWeak(hit + g_readback.HitComponent);
        const std::uintptr_t actor = ue_call::ResolveWeak(g_lastHitActor.Index, g_lastHitActor.Serial);

        // The player's own gear is owned by the pawn and moves with the view, and
        // anything the clean eye already starts inside was put there by the game,
        // not by the lean. Neither is a wall to hold the eye off, so the trace is
        // repeated through it.
        const bool startInside =
            (hit[g_readback.StartPenetrating] & g_readback.StartPenetratingMask) != 0;
        if (actor && (startInside || aim_trace::OwnedBy(actor, g_pawn)) && ignored < kMaxIgnored) {
            g_ignoreStorage[ignored++] = actor;
            continue;
        }

        ue4::FVector point{}, normal{};
        std::memcpy(&point, hit + g_readback.ImpactPoint, sizeof(point));
        std::memcpy(&normal, hit + g_readback.ImpactNormal, sizeof(normal));
        const float along = (point.X - start.x) * direction.x + (point.Y - start.y) * direction.y +
                            (point.Z - start.z) * direction.z;
        const float facing = -(normal.X * direction.x + normal.Y * direction.y + normal.Z * direction.z);
        const float room = along - g_margin / (facing > kMinCos ? facing : kMinCos);
        out.blocked = true;
        out.distance = room > 0.0f ? room : 0.0f;
        return out;
    }
    return out;
}

}  // namespace votv_ht::lean_trace
