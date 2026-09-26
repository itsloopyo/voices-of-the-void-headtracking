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
    // The pawn the controller possesses when it is one the player looks out of:
    // mainPlayer_C on foot, or the ATV_C the player is riding. 0 for any other
    // pawn (or none). The main menu's own pawn fails this and so closes the
    // gate without a flag of its own.
    std::uintptr_t Pawn = 0;
    // The mainPlayer_C: the pawn itself on foot, the rider on the ATV. The
    // state flags below are read from it either way.
    std::uintptr_t Player = 0;
    // True while riding. The ATV's camera rides a vehicle that pitches and
    // rolls with the ground.
    bool Riding = false;
    // The possessed pawn's own Camera component. Gameplay is drawn from it; a
    // cutscene or a death/sleep camera is drawn from somewhere else.
    Transform Camera;
    // Mouse input can belong to an interface while the view remains first person.
    bool HaveState = false;
    bool Dead = false;
    bool Ragdoll = false;
    bool WakingUp = false;
    bool MouseInputOff = false;
    // Both the game's 3D-interface flag and a non-null active interface.
    bool WorldInterface = false;
    // The un-zoomed field of view the pose is scaled against, in degrees.
    // mainPlayer_C drives its zoom through the `Zoom` timeline and ATV_C
    // through `Timeline`, and both write their own CameraComponent's
    // FieldOfView; the un-zoomed value is that same field sampled while the
    // timeline sits at position 0. The drawn value comes off the view info the
    // render caller carries, and the two are equal at rest, which is what makes
    // the zoom factor read 1.0000 in ordinary play.
    bool HaveFov = false;
    float BaseFov = 0.0f;
};

// Game thread only. `controller` is the player controller the view hook holds.
Snapshot Read(std::uintptr_t controller);

}  // namespace votv_ht::player_rig
