// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "mod_hotkeys.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "logging.h"
#include "view_hook.h"

#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace votv_ht::hotkeys {

namespace {

using cameraunlock::TrackingMode;

// The game's whole action-mapping table was read off the live UInputSettings (78
// mappings): the keys it binds are A C D E F Q R S V W X Z, the digits and the
// numpad decimal, Enter, Escape, Tab, Space, the mouse buttons and wheel,
// LeftAlt, LeftControl, LeftShift, F1, F5 and F6. The default lists, End, Page
// Up, Page Down and the Ctrl+Shift+Y, G and H chords, are all free.
//
// Holding a chord still presses the game's own LeftControl and LeftShift
// mappings - crouch and sprint - because an Unreal action mapping with no
// modifiers fires while other modifiers are held. That is a crouch-sprint the
// player did not ask for, not a game action they cannot undo.

// How often the poller samples the keyboard, in milliseconds.
constexpr unsigned kPollIntervalMs = 16;

std::unique_ptr<cameraunlock::input::HotkeyPoller> g_poller;
Session* g_session = nullptr;

void Bind(const char* key, const std::string& list, std::function<void()> action) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error(std::string("[Hotkeys] ") + key + "=" + list +
                                             " passed the config table and does not parse: " + parsed.error);
    cameraunlock::input::RegisterKeyBindings(*g_poller, parsed.bindings, std::move(action));
    Log::Line("hotkey: %s=%s", key, list.c_str());
}

}  // namespace

void ToggleTracking() {
    const bool enabled = !view_hook::TrackingEnabled();
    view_hook::SetTrackingEnabled(enabled);
    Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
}

void CycleTrackingMode() {
    const TrackingMode mode = g_session->CycleMode();
    const char* name = mode == TrackingMode::RotationOnly ? "rotation only"
                     : mode == TrackingMode::PositionOnly ? "position only"
                                                          : "rotation and position";
    Log::Line("hotkey: tracking mode -> %s", name);
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    config::Save([channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

void ToggleYawMode() {
    const bool worldSpaceYaw = !view_hook::WorldSpaceYaw();
    view_hook::SetWorldSpaceYaw(worldSpaceYaw);
    Log::Line("hotkey: yaw mode %s", worldSpaceYaw ? "world" : "local");
    config::Save([worldSpaceYaw](Config& c) { c.world_space_yaw = worldSpaceYaw; });
}

void Register(const Config& config, Session& session) {
    g_session = &session;
    g_poller = std::make_unique<cameraunlock::input::HotkeyPoller>();
    Bind("ToggleKey", config.toggle_key_name, [] { ToggleTracking(); });
    Bind("CycleTrackingModeKey", config.cycle_tracking_mode_key_name, [] { CycleTrackingMode(); });
    Bind("YawModeKey", config.yaw_mode_key_name, [] { ToggleYawMode(); });
    g_poller->Start(kPollIntervalMs);
}

void Stop() {
    if (g_poller) g_poller->Stop();
}

}  // namespace votv_ht::hotkeys
