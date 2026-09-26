// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/input/key_bindings.h"

namespace votv_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using C = cfg::schema::Concept;

constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;
constexpr int kVkH = 0x48;

// The yaw mode list the published build registered: the one code its file named, then the
// Ctrl+Shift+H chord it always bound. A code End or Page Up already had was refused there with
// a log line and left unbound, so only the chord carries over for it.
std::string YawModeKeys(int vk, std::vector<cfg::DroppedValue>& dropped) {
    const std::string chord = cameraunlock::input::FormatKeyBindings(
        {{cameraunlock::input::KeyModifiers::kCtrl | cameraunlock::input::KeyModifiers::kShift, kVkH}});
    if (vk == kVkEnd || vk == kVkPageUp) return chord;
    const std::string key = cfg::LegacyVirtualKeyToBindings(vk, "Hotkeys", "YawMode", dropped);
    return key.empty() ? chord : key + ", " + chord;
}

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    std::vector<cfg::DroppedValue> dropped;
    legacy::Config read;
    // The published build read the file through the game folder's ANSI form. Where the folder
    // had none it read nothing and ran on the defaults, so the import reads nothing either.
    const bool absent = input.ansi_lossy || legacy::Read(input.ansi_path, read) == legacy::ReadStatus::Absent;

    out.udp_port = read.udp_port;
    out.local_smoothing = read.local_smoothing;
    out.position.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.position.remote_smoothing = read.remote_smoothing;
    out.world_space_yaw = read.world_space_yaw;
    out.disable_in_multiplayer = read.disable_in_multiplayer;
    out.collision_enabled = read.collision_enabled;
    out.lean_clamp.skin = read.collision_margin;
    out.collision_channel = read.collision_channel;
    out.lean_clamp.release_smoothing = read.collision_release_smoothing;
    out.aim_trace_channel = read.aim_trace_channel;

    // The game's crosshair now always follows the aim.
    if (!read.move_crosshair) dropped.push_back({cfg::DropRule::Reticle, "Camera", "MoveCrosshair", "false"});

    // End, Page Up and their chords were fixed in the published build, which is what the
    // defaults hold.
    out.yaw_mode_key_name = YawModeKeys(read.yaw_mode_key, dropped);

    // [Dev] DevCommands is not carried. Only a build configured with VOTV_DEV_COMMANDS reads
    // it, and no published build was, so for every player it did nothing.

    return absent ? cfg::ImportResult::Absent(std::move(dropped)) : cfg::ImportResult::Imported(std::move(dropped));
}

std::optional<cfg::ConfigOwner<Config>> g_owner;

void WriteLines(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) Log::Line("config: %s", line.c_str());
}

}  // namespace

cfg::ConfigTable<Config> MakeTable() {
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {C::UdpPort, C::EnableOnStartup, C::WorldSpaceYaw, C::RotationEnabled, C::LocalSmoothing,
         C::RemoteSmoothing, C::PositionEnabled, C::CollisionEnabled, C::CollisionMargin, C::CollisionChannel,
         C::CollisionReleaseSmoothing, C::ToggleKey, C::CycleTrackingModeKey, C::YawModeKey, C::LightFollowsHead,
         C::LightMultiplier});
    table.Select(C::WorldSpaceYaw).Writable()
        .Select(C::RotationEnabled).Writable()
        .Select(C::PositionEnabled).Writable()
        .Select(C::CollisionMargin)
        .Comment("How far the view is held off a wall when you lean into it, in centimetres.\n"
                 "Keep it above the camera's near clip distance.")
        .Select(C::CollisionChannel)
        .Comment("Which of the game's trace channels, 0 to 31, the wall check tests against.");
    table.Local("General", "DisableInMultiplayer", &Config::disable_in_multiplayer, cfg::BoolCodec(),
                "true: head tracking stands down as soon as a second player is in the session.\n"
                "Voices of the Void has no multiplayer mode, so it never does today.");
    table.Local("Camera", "AimTraceChannel", &Config::aim_trace_channel, cfg::IntCodec<int>(0, 31),
                "Which of the game's trace channels, 0 to 31, finds the point the crosshair moves onto.")
        .Engine();
#ifdef VOTV_DEV_COMMANDS
    table.Local("Dev", "DevCommands", &Config::dev_commands, cfg::BoolCodec(),
                "true: read test commands from HeadTracking.devcmd beside the game.");
#endif
    return table;
}

cfg::LegacyImport<Config> MakeLegacyImport() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    import.keys = legacy::ReadKeys();
    return import;
}

cfg::ConfigOwnerOptions<Config> MakeOwnerOptions(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = exe_dir + L"\\" + kConfigFileName;
    options.table = MakeTable();
    options.import = MakeLegacyImport();
    options.legacy_path = exe_dir + L"\\" + kLegacyFileName;
    options.header.display_name = kDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

Config Load(const std::wstring& exe_dir) {
    g_owner.emplace(MakeOwnerOptions(exe_dir, cfg::DefaultsFile::PerUser()));
    cfg::ConfigLoadResult<Config> loaded = g_owner->Load();
    WriteLines(loaded.log);
    Log::Line("config: %s (udpPort=%d, onStartup=%d, mode rotation=%d position=%d, yaw=%s, "
              "smoothing local=%.2f remote=%.2f, soloOnly=%d, collision=%d margin=%.1f channel=%d "
              "release=%.2f, aimChannel=%d, light follows=%d x%.2f)",
              cfg::ConfigLoadStatusName(loaded.status), loaded.config.udp_port,
              loaded.config.enable_on_startup ? 1 : 0, loaded.config.rotation_enabled ? 1 : 0,
              loaded.config.position_enabled ? 1 : 0, loaded.config.world_space_yaw ? "world" : "local",
              loaded.config.local_smoothing, loaded.config.remote_smoothing,
              loaded.config.disable_in_multiplayer ? 1 : 0, loaded.config.collision_enabled ? 1 : 0,
              loaded.config.lean_clamp.skin, loaded.config.collision_channel,
              loaded.config.lean_clamp.release_smoothing, loaded.config.aim_trace_channel,
              loaded.config.light.follows_head ? 1 : 0, loaded.config.light.multiplier);
    return loaded.config;
}

void Save(const std::function<void(Config&)>& change) {
    const cfg::ConfigSaveResult saved = g_owner->Save(change);
    WriteLines(saved.log);
    if (saved.status != cfg::ConfigSaveStatus::Saved)
        Log::Line("config: %s: %s", cfg::ConfigSaveStatusName(saved.status), saved.reason.c_str());
}

}  // namespace votv_ht::config
