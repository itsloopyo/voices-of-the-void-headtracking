// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <functional>
#include <string>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/head_tracking_config.h"

// CameraUnlock.ini, next to the game exe, in cameraunlock-core's canonical
// format. The owner imports HeadTracking.ini, the file every earlier build read
// from the same folder, once while CameraUnlock.ini is absent, and never writes
// it.
namespace votv_ht {

struct Config : cameraunlock::HeadTrackingConfig {
    Config() {
        // CollisionMargin is in centimetres here, the engine's unit, and lives in
        // lean_clamp.skin; lean_trace carries it along the surface normal.
        lean_clamp.skin = 15.0f;
    }

    // Voices of the Void has no multiplayer mode, so this never fires today.
    // It stays on by default as the mod's own guarantee that it only ever moves
    // a solo camera: if a build, a co-op branch or another mod ever puts a
    // second player state in the session, tracking stands down without anyone
    // having to remember to switch it off.
    bool disable_in_multiplayer = true;

    // ETraceTypeQuery index for the aim trace.
    int aim_trace_channel = 0;

    // Dev only: read by a build configured with VOTV_DEV_COMMANDS.
    bool dev_commands = false;
};

}  // namespace votv_ht

namespace votv_ht::config {

constexpr const char* kDisplayName = "Voices of the Void";
constexpr const wchar_t* kConfigFileName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyFileName = L"HeadTracking.ini";

cameraunlock::config::ConfigTable<Config> MakeTable();
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for CameraUnlock.ini in `exe_dir`, with HeadTracking.ini
// beside it as the legacy file. The mod passes the player's own Defaults.ini,
// a test one at a scratch path.
cameraunlock::config::ConfigOwnerOptions<Config> MakeOwnerOptions(
    const std::wstring& exe_dir, cameraunlock::config::DefaultsFile defaults);

// Build the process's owner for CameraUnlock.ini in `exe_dir`, load it, and
// write every line the load returned to the log. Bootstrap thread, once, after
// the log is open.
Config Load(const std::wstring& exe_dir);

// Apply-then-save for a toggle: the caller has applied the new value to the
// running game; this writes it through the owner and logs what happened. A
// failed save leaves the session running on the new value. Never from a
// per-frame path.
void Save(const std::function<void(Config&)>& change);

}  // namespace votv_ht::config
