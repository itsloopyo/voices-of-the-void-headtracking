// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"

// The GetPlayerViewPoint detour, plus the view state it reads on the hot path
// and the hotkeys write.
//
// The state is held here rather than in the bootstrap because a key press has
// to land on the frame after it, with no lock between the poller thread and the
// render thread.
namespace votv_ht::view_hook {

// What the hook needs from the rest of the mod, for the life of the process.
// The pointer is non-owning and must outlive Shutdown().
struct Dependencies {
    const Config* config = nullptr;
};

// Point cameraunlock-core's reflection layer at this module and install the
// detour on the active profile's GetPlayerViewPoint. Must run after
// builds::SelectProfile has returned Matched - it reads Offsets().
//
// False means no hook was installed and the game is running unmodified; the
// reason is already in the log.
bool Install(const Dependencies& deps);

// Disable the detour. The trampoline is left in place: this runs from DllMain
// under the loader lock, and a thread already inside the detour has to be able
// to finish.
void Shutdown();

bool TrackingEnabled();
void SetTrackingEnabled(bool enabled);

// true = world-space yaw (horizon-locked): the pose is added to the engine
// FRotator, so yaw turns about the world up-axis and the horizon stays level
// however the mouse has the camera pitched. false = camera-local yaw, which
// leans the horizon on a pitched turn. camera_boundary::ApplyHeadPose is where
// the two differ; this is the flag it branches on, seeded from
// Config::world_space_yaw at startup and toggled by the YawModeKey list.
bool WorldSpaceYaw();
void SetWorldSpaceYaw(bool worldSpaceYaw);

// The last render frame's aim, for the dev shot log.
struct AimSample {
    bool Valid = false;
    double MuzzleX = 0, MuzzleY = 0, MuzzleZ = 0;
    double DirX = 0, DirY = 0, DirZ = 0;
    bool Hit = false;
    double PointX = 0, PointY = 0, PointZ = 0;
    double EyeX = 0, EyeY = 0, EyeZ = 0;
    float NdcX = 0, NdcY = 0;
    bool NdcValid = false;
};
AimSample LastAim();

// Dev: write the heartbeat and aim lines on the next render frame.
void RequestReport();

// Dev: project this world point instead of the aim point (valid=false clears).
void SetMarkOverride(bool valid, double x, double y, double z);

// Which GetPlayerViewPoint caller gets the head pose. See inject_mode.h.
int  InjectMode();
void SetInjectMode(int mode);

}  // namespace votv_ht::view_hook
