// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>

#include <cameraunlock/camera/lean_clamp.h>
#include <cameraunlock/math/vec3.h>

// The engine half of the lean collision clamp: ask the game how far the eye may
// travel from where it put the camera before it meets something solid.
//
// Core owns what to do with the answer (cameraunlock/camera/lean_clamp.h); this
// owns getting one. The query goes through the engine's own
// UKismetSystemLibrary::LineTraceSingle, dispatched by name through the script
// VM with its parameter frame read out of the engine's reflection data - the
// same route aim_trace takes - so there is no second physics query in this mod
// to disagree with the game's, and no RVA to re-pin every patch.
//
// A LINE rather than a swept sphere. A sphere as wide as the margin reports an
// overlap at zero distance whenever the clean eye already sits within that
// radius of anything the player's capsule does not collide with, and the lean
// then refuses to move in any direction, including away from it. A line only
// answers along the lean, so the margin is carried here instead: the reported
// distance is cut by the margin measured along the surface normal.
namespace votv_ht::lean_trace {

// Distance held off a surface, in UE units (cm), measured along its normal.
// Must exceed the camera's near clip distance or geometry is culled before the
// eye reaches it and the wall goes transparent anyway.
void SetMargin(float centimetres);

// Which ETraceTypeQuery the trace runs on. Configurable because the channel a
// level's geometry blocks is a project setting, not an engine constant, and the
// only way to know is to run it and read the log.
void SetChannel(int traceTypeQuery);

// True when the resolve failed for the session, for the heartbeat. The trace
// resolves itself against the live reflection data on the first Query() and
// answers from cache after it.
bool Failed();

// The pawn to trace for, set once per frame by the view hook. It is the
// trace's WorldContextObject with bIgnoreSelf set, which excludes the player's
// own capsule, and the owner the player's own gear is recognised by.
void SetPawn(std::uintptr_t pawn);

// "<actor class> <name> / <component class> <name>" for the last surface the
// trace stopped on, for the log line that reports contact.
std::string LastHitDescription();

// The query, in the shape core's clamp takes. `context` is unused - the pawn
// comes from SetPawn - and is present to match LeanQueryFn. Game thread only.
cameraunlock::camera::LeanObstruction Query(void* context,
                                            const cameraunlock::math::Vec3& start,
                                            const cameraunlock::math::Vec3& direction,
                                            float maxDistance);

}  // namespace votv_ht::lean_trace
