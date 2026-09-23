// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/unreal/ue_math.h>

#include "aim_projection.h"
#include "game_state.h"
#include "session.h"
#include "tracking_gate.h"

// Everything one render frame decided, in the shape the log reads it back in.
//
// The hook fills it as it works through the frame; hook_log.h turns it into
// lines. Keeping it a plain record is what lets the reporting be a pure
// function of the frame rather than a second reader of the hook's globals -
// which is how a heartbeat once said "listening" while a tracker was feeding
// the mod.
namespace votv_ht {

struct FrameReport {
    // Whether the head pose reached the view, and the gate that decided it.
    TrackingVerdict Verdict = TrackingVerdict::NotGameplay;
    game_state::Verdict Gate;
    bool TrackingEnabled = false;
    bool HavePose = false;
    // The pose as it was written, after the entry ease and the zoom scaling.
    Pose Applied;

    // The zoom compensation's terms. The factor reads 1.0000 in ordinary play.
    float ZoomFactor = 1.0f;
    float RenderFov = 0.0f;
    float BaseFov = 0.0f;
    float TanX = 0.0f, TanY = 0.0f;

    cameraunlock::unreal::FVector PositionOffset{0.0, 0.0, 0.0};
    bool CollisionEnabled = false;
    bool LeanQueryFailed = false;
    bool LeanInContact = false;

    bool TraceValid = false;
    bool TraceHit = false;
    double TraceDistance = 0.0;
    cameraunlock::unreal::FVector AimPoint{0.0, 0.0, 0.0};

    aim_projection::Ndc Mark;
    // Whether the mark landed inside the frame. A valid mark outside it is
    // still published, pinned to the edge, so this is not "was the crosshair
    // moved" - it is the column that tells a pinned crosshair from a free one.
    bool MarkOnScreen = false;
};

}  // namespace votv_ht
