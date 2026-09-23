// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "game_state.h"

// Whether the head pose reaches the view this frame, and why not when it does
// not.
//
// Kept out of the render hook as a pure function so the walk can be exercised
// without the game. Every one of its answers is a frame the player either sees
// their head in or does not.
//
// game_state.h owns the gate that reads the live process. This file is the
// order it is asked in, plus the master toggle and the tracker.
//
// There is no aim-down-sights slot here. Voices of the Void fires from the hip:
// no weapon raises an iron sight, the crosshair is drawn at the same place for
// the whole of a mission, and the field-of-view changes the game does have are
// answered by the zoom compensation rather than by an aim state.
namespace votv_ht {

enum class TrackingVerdict {
    // The head pose is applied in full.
    Active,
    // The master toggle is off.
    Disabled,
    // A menu, the pause screen, a cutscene, photo mode, death or a loading
    // screen.
    NotGameplay,
    // The tracker has published nothing this frame.
    NoTracker,
};

inline TrackingVerdict DecideTracking(const game_state::Verdict& gate,
                                      bool trackingEnabled, bool havePose) {
    if (!trackingEnabled) return TrackingVerdict::Disabled;
    if (!gate.InGameplay) return TrackingVerdict::NotGameplay;
    if (!havePose) return TrackingVerdict::NoTracker;
    return TrackingVerdict::Active;
}

inline bool PoseApplies(TrackingVerdict verdict) {
    return verdict == TrackingVerdict::Active;
}

// One line for the log and the heartbeat. Never null.
inline const char* Reason(TrackingVerdict verdict) {
    switch (verdict) {
        case TrackingVerdict::Active:      return "gameplay";
        case TrackingVerdict::Disabled:    return "tracking toggled off";
        case TrackingVerdict::NotGameplay: return "menu or loading";
        case TrackingVerdict::NoTracker:   return "no tracker data";
    }
    return "unknown";
}

}  // namespace votv_ht
