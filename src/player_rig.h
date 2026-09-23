// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

// What the player character is doing this frame, read off the live object graph
// through the engine's reflection data.
//
// The pawn's camera component is mainPlayer_C::Camera, and the drawn view is
// built from it. The interaction trace runs from that same component: the
// pawn's own HitResult carries a TraceStart equal to the component's world
// position and a TraceEnd one arm length along its forward, so what the player
// can pick up, open or read is decided there and not by the view the render
// caller is handed. The aim ray is read here, and the view hook never writes to
// the component.
namespace votv_ht::player_rig {

struct Transform {
    bool Valid = false;
    cameraunlock::unreal::FVector Position{0.0, 0.0, 0.0};
    cameraunlock::unreal::FVector Forward{1.0, 0.0, 0.0};
};

struct Snapshot {
    // The mainPlayer_C the controller possesses, or 0 for any other pawn (or
    // none). The main menu's own pawn fails this and so closes the gate without
    // a flag of its own.
    std::uintptr_t Pawn = 0;
    // mainPlayer_C::Camera. Gameplay is drawn from it; a cutscene or a
    // death/sleep camera is drawn from somewhere else.
    Transform Camera;
    // States the pawn reports that mean the player is not in ordinary control:
    // dead, ragdolled, waking up out of bed, or with mouse input handed to a
    // UI (the in-game computer, the tablet).
    bool HaveState = false;
    bool Dead = false;
    bool Ragdoll = false;
    bool WakingUp = false;
    bool MouseInputOff = false;
    // The un-zoomed field of view the pose is scaled against, in degrees.
    // mainPlayer_C drives its zoom through the `Zoom` timeline, which writes
    // CameraComponent::FieldOfView; the un-zoomed value is that same field
    // sampled while the timeline sits at position 0, which is what the player's
    // own FOV setting put there. The drawn value comes off the view info the
    // render caller carries, and the two are equal at rest, which is what makes
    // the zoom factor read 1.0000 in ordinary play.
    bool HaveFov = false;
    float BaseFov = 0.0f;
};

// Game thread only. `controller` is the player controller the view hook holds.
Snapshot Read(std::uintptr_t controller);

}  // namespace votv_ht::player_rig
