// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// The HeadTracking.ini reader of the last build that read the file in its
// pre-canonical layout, frozen so a player updating from any older build is
// converted exactly as that build read the file. Nothing in this folder is ever
// edited. Three things differ from the reader it was taken from: it fills this
// frozen copy of that build's Config and defaults rather than the runtime type,
// it is handed the file's path rather than the folder holding it, and it never
// writes the file (a missing file reads as the defaults, which is what the old
// reader read from the file it created there). NormalizeUdpPort, which the old
// reader took from a part of core that is not frozen, is copied beside it.

#include <string>
#include <vector>

#include "cameraunlock/config/legacy_import.h"

namespace votv_ht::legacy {

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

    bool disable_in_multiplayer = true;

    bool move_crosshair = true;

    bool collision_enabled = true;
    // Standoff from a surface in cm, along its normal.
    float collision_margin = 15.0f;
    // ETraceTypeQuery index for the lean trace and the aim trace.
    int collision_channel = 0;
    int aim_trace_channel = 0;
    float collision_release_smoothing = 0.9f;

    bool dev_commands = false;
};

enum class ReadStatus {
    Read,
    // No file at the path. The Config holds the defaults.
    Absent,
};

// Fill `out`, a default-constructed Config, from the INI at `ini_path`; absent
// keys keep their defaults, out-of-range values fall back to the default and say
// so in the log.
ReadStatus Read(const std::string& ini_path, Config& out);

// Every section and key Read reads, in the order it reads them.
std::vector<cameraunlock::config::LegacyKey> ReadKeys();

}  // namespace votv_ht::legacy
