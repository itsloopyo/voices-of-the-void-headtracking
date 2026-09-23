// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

// Where the shot stops, this frame.
//
// The reticle marks a POINT, not a direction. With the eye on the shot's own
// ray the two project to the same pixel, which is why a direction is enough
// until 6DOF lands; a lean up to 30cm off that ray separates them, and a
// reticle built on a direction then slides off the thing it marks, worse the
// closer the target. So the parallax term needs the live distance to whatever
// the player is pointing at, and it has to be THIS frame's distance: a fixed,
// smoothed or stale depth leaves an error proportional to
// lean * (1/d0 - 1/d), which is zero at exactly one range and changes sign
// either side of it.
//
// The cast runs through the engine's own UKismetSystemLibrary::LineTraceSingle
// so there is no second physics query implementation in this mod to disagree
// with the game's, and its parameter frame is read out of the engine's
// reflection data rather than assumed - see ue_reflect.h.
namespace votv_ht::aim_trace {

struct Result {
    // False when the trace could not be performed at all: the reflection data
    // did not resolve, the VM is not up, or the dispatch faulted. The caller
    // must invalidate the reticle rather than reuse an older point.
    bool Valid = false;
    // True when something blocked the ray. False is a definite no-hit, which is
    // a target at infinity: the caller projects the aim DIRECTION for that frame
    // rather than substituting a magic distance.
    bool Hit = false;
    cameraunlock::unreal::FVector Point{0.0, 0.0, 0.0};
    double Distance = 0.0;   // along `dir` from `start`, in UE units (cm)
    std::uintptr_t Actor = 0;
    std::uintptr_t Component = 0;
};

// Cast from `start` along `dir` (unit vector) for `maxDistance` UE units. The
// UFunction, its parameter frame and FHitResult's fields resolve on the first
// call and answer from cache after it; a layout that does not resolve logs which
// lookup failed and leaves every Result invalid, so no mark is drawn.
//
// `pawn` is excluded, along with anything up its Owner chain the ray meets, so
// the cast does not stop on the player's own body, sword or hands. Must be
// called on the game thread.
Result Cast(std::uintptr_t pawn,
            const cameraunlock::unreal::FVector& start,
            const cameraunlock::unreal::FVector& dir,
            double maxDistance);

// True when `actor`, or an actor up its Owner chain, is `pawn`: the player's own
// gear, which neither the aim nor the lean stops on.
bool OwnedBy(std::uintptr_t actor, std::uintptr_t pawn);

// ETraceTypeQuery index the cast uses. Configurable because which query channel
// a game's bullets stop on is a project setting, not an engine constant, and
// the only way to know is to fire at a wall and compare.
void SetTraceChannel(int channel);

}  // namespace votv_ht::aim_trace
