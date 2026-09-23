// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

// HeadTracking.ini, next to the game exe.
namespace votv_ht {

struct Config {
    // UDP port the tracker sends to. 4242 is the OpenTrack default.
    int udp_port = 4242;

    // Smoothing for a tracker on this machine (loopback) and for one on another
    // device on the network. 0 = none, 1 = heaviest.
    float local_smoothing = 0.0f;
    float remote_smoothing = 0.15f;

    int yaw_mode_key = 0x22;  // VK_NEXT (Page Down)

    // True: head yaw turns about the world up axis (horizon stays level).
    bool world_space_yaw = true;

    // Voices of the Void has no multiplayer mode, so this never fires today.
    // It stays on by default as the mod's own guarantee that it only ever moves
    // a solo camera: if a build, a co-op branch or another mod ever puts a
    // second player state in the session, tracking stands down without anyone
    // having to remember to switch it off.
    bool disable_in_multiplayer = true;

    // Move the game's crosshair onto the point the interaction ray stops on.
    // Off leaves it where the game laid it out, in the middle of the screen,
    // which is where the player is pointing only while the head is centred.
    bool move_crosshair = true;

    // Keep a lean from putting the eye inside the level.
    //
    // On: verified in game. With the eye 20.1 cm from the garage door and a
    // 40 cm forward lean asked for, the sweep on trace channel 0 logged
    // `lean-clamp: holding the view off geometry (wanted 5.3cm, allowed 5.1cm)
    // on baseBuilding_C Basev2Final / StaticMeshComponent base4segment_garage`
    // - the margin below, to the millimetre - and the wall stayed drawn solid
    // at that distance, so the standoff clears the near clip plane.
    bool collision_enabled = true;
    // Standoff from a surface in cm, along its normal; must exceed the near clip
    // plane.
    float collision_margin = 15.0f;
    // ETraceTypeQuery index for the lean trace and the aim trace.
    int collision_channel = 0;
    int aim_trace_channel = 0;
    float collision_release_smoothing = 0.9f;

    // Dev only.
    bool dev_commands = false;
};

}  // namespace votv_ht

namespace votv_ht::config {

// Fill `out` from the INI; absent keys keep their defaults, out-of-range values
// fall back to the default and say so in the log.
void Load(const std::string& exe_dir, Config& out);

// Write a commented default INI unless one already exists.
void WriteDefaultIfMissing(const std::string& exe_dir);

}  // namespace votv_ht::config
