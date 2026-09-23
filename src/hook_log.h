// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/math/vec3.h>

#include "aim_trace.h"
#include "frame_report.h"

// Everything the view hook writes to the log, and the rate limiting that keeps
// it readable.
//
// Split from the hook itself because the two change for different reasons: the
// hook decides what the camera does, this decides what a reader is told about
// it. Every entry point here is rate limited or edge triggered - a line per
// frame buries the build match, the link and the gate transitions a report is
// read for - and each owns the static that remembers what it last said.
namespace votv_ht::hook_log {

// Caller discovery (inject mode 0): count GetPlayerViewPoint calls by return
// RVA and summarise them periodically. `rotLocStride` is the gap between the
// call's two out-params, which is what identifies the render caller.
void CountCaller(std::uintptr_t returnRva, std::uint64_t call, std::int64_t rotLocStride);

// Write the heartbeat and aim lines on the next frame, whatever the interval.
void RequestReport();

// The state of the whole frame, every 30s and on request.
void Heartbeat(const FrameReport& report, std::uintptr_t returnRva);

// The lean clamp entering or leaving contact, and its sweep failing or coming
// back. Only the transitions, and only once one has held for a moment: what the
// lean stops on changes several times a second in ordinary play, and a lean held
// right at the depth where the clamp bites crosses that boundary about as often.
void LeanClamp(const cameraunlock::math::Vec3& wanted,
               const cameraunlock::math::Vec3& allowed,
               bool queryFailed, bool inContact);

// Once per axis group per session: the session does not recover from it, so a
// line per frame would say nothing the first one did not.
void NonFinitePose(bool rotation);

// The zoom compensation's terms, once the factor has held still for a second
// and again if it settles somewhere new. `tick` is GetTickCount64().
void ZoomTerms(const FrameReport& report, std::uint64_t tick);

// A field of view that stops or starts reading, which is the difference
// between "this game never zooms" and "the pose is going in unscaled".
void FovReadable(const FrameReport& report);

// The aim trace starting or stopping. `maxDistanceCm` is the reach it was cast
// with, for the no-contact line.
void AimTrace(const aim_trace::Result& hit, double maxDistanceCm);

// What one pass of the hook cost and how long it had been since the last pass,
// both in microseconds. The render caller does a full pass about twice per
// engine frame, so the gap is half a frame in steady play and the length of the
// stall in a hitch. Summarised at most once a second, and only for a second that
// held a pass over a millisecond or a gap over 50 ms, so a stutter report can be
// read against the mod's own share of it.
void FrameCost(std::uint64_t hookUs, std::uint64_t gapUs);

}  // namespace votv_ht::hook_log
