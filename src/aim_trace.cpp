// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "aim_trace.h"

#include <cstring>
#include <vector>

#include "kismet_trace.h"
#include "logging.h"
#include "ue4_types.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::aim_trace {

namespace {

namespace ue = ::cameraunlock::unreal;

ue_vm::ResolveRetry g_resolveRetry;

// What this trace reads back out of the frame kismet_trace fills: the one
// parameter it wants beyond the nine, and the FHitResult fields under OutHit.
struct ReadbackLayout {
    std::size_t ReturnValue = 0;
    std::size_t ImpactPoint = 0;
    std::size_t HitDistance = 0;
    std::size_t HitActor = 0;       // Actor, a weak pointer
    std::size_t HitComponent = 0;   // Component, a weak pointer
};

kismet_trace::Layout g_trace;
ReadbackLayout g_readback;
std::uintptr_t g_lineTraceFn = 0;
std::uintptr_t g_kismetSystemCdo = 0;
bool g_ready = false;
bool g_failed = false;
int  g_traceChannel = 0;

// The player's own gear the ray is repeated through, one actor per attempt.
constexpr int kMaxIgnored = 6;
std::uintptr_t g_ignoreStorage[kMaxIgnored] = {};
std::size_t g_actorOwnerOffset = 0;

bool Resolve() {
    if (g_ready) return true;
    if (g_failed) return false;
    if (!g_resolveRetry.Due()) return false;
    if (!ue_vm::Ready()) return false;

    switch (kismet_trace::Resolve("aim-trace", g_kismetSystemCdo, g_lineTraceFn, g_trace)) {
        case kismet_trace::Resolution::NotYet:
            return false;
        case kismet_trace::Resolution::Unusable:
            Log::Line("aim-trace: the trace's parameter frame is not the shape this mod "
                      "writes - standing the parallax down");
            g_failed = true;
            return false;
        case kismet_trace::Resolution::Ok:
            break;
    }

    // The trace's own answer, which this caller reads and the lean clamp does not.
    ue_reflect::FieldInfo returnValue;
    if (!ue_reflect::FindProperty(g_lineTraceFn, "ReturnValue", returnValue) ||
        !ue_reflect::FieldFits(returnValue, 1, g_trace.ParamsSize)) {
        Log::Line("aim-trace: LineTraceSingle.ReturnValue did not resolve - standing the "
                  "parallax down");
        g_failed = true;
        return false;
    }

    const std::uintptr_t hitResult = ue::FindLiveObject("ScriptStruct", "HitResult", nullptr);
    std::vector<ue_reflect::FieldInfo> h;
    if (!hitResult ||
        !ue_reflect::ResolveAll("HitResult", hitResult,
                                {"ImpactPoint", "Distance", "Actor", "Component"}, h)) {
        Log::Line("aim-trace: FHitResult layout did not resolve - standing the "
                  "parallax down");
        g_failed = true;
        return false;
    }
    // ImpactPoint is a UE4 FVector_NetQuantize: three floats. Anything else says
    // the field found is not the one meant, and reading it would produce a
    // plausible-looking aim point made of whatever follows.
    if (h[0].Size != sizeof(ue4::FVector)) {
        Log::Line("aim-trace: FHitResult::ImpactPoint is %zu bytes, expected %zu "
                  "(float FVector) - standing the parallax down",
                  h[0].Size, sizeof(ue4::FVector));
        g_failed = true;
        return false;
    }
    constexpr std::size_t kWeakBytes = kismet_trace::kWeakObjectBytes;
    if (h[2].Size != kWeakBytes || h[3].Size != kWeakBytes) {
        Log::Line("aim-trace: FHitResult Actor is %zu bytes and Component %zu, expected "
                  "%zu each - standing the parallax down", h[2].Size, h[3].Size, kWeakBytes);
        g_failed = true;
        return false;
    }
    if (!kismet_trace::OutHitHolds(g_trace, ue_reflect::StructSize(hitResult))) {
        Log::Line("aim-trace: LineTraceSingle.OutHit cannot hold a whole FHitResult - "
                  "standing the parallax down");
        g_failed = true;
        return false;
    }

    const std::uintptr_t actorClass = ue::FindLiveObject("Class", "Actor", nullptr);
    ue_reflect::FieldInfo owner;
    if (!actorClass || !ue_reflect::FindPropertyInChain(actorClass, "Owner", owner) ||
        owner.Size != sizeof(std::uintptr_t)) {
        Log::Line("aim-trace: AActor::Owner did not resolve - standing the aim trace down");
        g_failed = true;
        return false;
    }
    g_actorOwnerOffset = owner.Offset;

    g_readback.ReturnValue  = returnValue.Offset;
    g_readback.ImpactPoint  = h[0].Offset;
    g_readback.HitDistance  = h[1].Offset;
    g_readback.HitActor     = h[2].Offset;
    g_readback.HitComponent = h[3].Offset;

    Log::Line("aim-trace: LineTraceSingle frame=%zu Start=+0x%zx End=+0x%zx "
              "OutHit=+0x%zx Return=+0x%zx | FHitResult ImpactPoint=+0x%zx "
              "Distance=+0x%zx | channel=%d",
        g_trace.ParamsSize, g_trace.Start, g_trace.End, g_trace.OutHit,
        g_readback.ReturnValue, g_readback.ImpactPoint, g_readback.HitDistance, g_traceChannel);
    g_ready = true;
    return true;
}

}  // namespace

void SetTraceChannel(int channel) { g_traceChannel = channel; }

// The player's own gear (the katana actor, the hand mesh, an attachment) is
// owned by the pawn and does not stop what the player is aiming at, so a
// contact on it is added to the ignore list and the cast repeated.
bool OwnedBy(std::uintptr_t actor, std::uintptr_t pawn) {
    if (!g_actorOwnerOffset) return false;
    std::uintptr_t cur = actor;
    for (int depth = 0; cur && depth < 4; ++depth) {
        std::uintptr_t owner = 0;
        if (!ue::SafeReadPtr(cur + g_actorOwnerOffset, owner)) return false;
        if (owner == pawn) return true;
        cur = owner;
    }
    return false;
}

Result Cast(std::uintptr_t pawn, const ue::FVector& start, const ue::FVector& dir,
            double maxDistance) {
    Result r;
    if (!Resolve() || pawn == 0) return r;

    std::int32_t ignored = 0;
    g_ignoreStorage[ignored++] = pawn;

    kismet_trace::Shot shot;
    shot.WorldContext = pawn;
    shot.Start = ue4::FromCore(start);
    shot.End = ue4::FromCore(ue::FVector{
        start.X + dir.X * maxDistance,
        start.Y + dir.Y * maxDistance,
        start.Z + dir.Z * maxDistance,
    });
    shot.Channel = g_traceChannel;
    shot.Complex = true;   // per-triangle, so the point is on the surface drawn
    shot.Ignore = g_ignoreStorage;

    for (int attempt = 0; attempt < kMaxIgnored; ++attempt) {
        alignas(16) unsigned char buf[kismet_trace::kMaxParams];
        shot.IgnoreCount = ignored;
        kismet_trace::FillFrame(g_trace, shot, buf);

        if (!ue_vm::Dispatch(reinterpret_cast<void*>(g_kismetSystemCdo),
                             reinterpret_cast<void*>(g_lineTraceFn), buf))
            return Result{};

        r = Result{};
        r.Valid = true;
        r.Hit = buf[g_readback.ReturnValue] != 0;
        if (!r.Hit) return r;

        const unsigned char* hit = buf + g_trace.OutHit;
        ue4::FVector impact{};
        std::memcpy(&impact, hit + g_readback.ImpactPoint, sizeof(impact));
        r.Point = ue4::ToCore(impact);
        const kismet_trace::WeakObject actor = kismet_trace::ReadWeak(hit + g_readback.HitActor);
        const kismet_trace::WeakObject component =
            kismet_trace::ReadWeak(hit + g_readback.HitComponent);
        r.Actor = ue_call::ResolveWeak(actor.Index, actor.Serial);
        r.Component = ue_call::ResolveWeak(component.Index, component.Serial);
        // The contact's own position projected onto the ray is a distance by
        // construction; FHitResult::Distance is whatever the engine put there.
        r.Distance = (r.Point.X - start.X) * dir.X + (r.Point.Y - start.Y) * dir.Y +
                     (r.Point.Z - start.Z) * dir.Z;

        if (!r.Actor || !OwnedBy(r.Actor, pawn) || ignored >= kMaxIgnored) return r;
        g_ignoreStorage[ignored++] = r.Actor;
    }
    return r;
}

}  // namespace votv_ht::aim_trace
