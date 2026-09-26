#pragma once

// The oracle: the config reader and hotkey registration of the newest published build (the
// rolling `dev` pre-release, b38440c), compiled from oracle/ with the core sources they
// included at its pin (7e84f94). Two libraries build it, each with its namespaces renamed at
// compile time so it links beside the current core and the current mod: the reader with the
// published IniReader and value guards, and hotkeys::Register against oracle_fake's recording
// poller and keyboard. This header names no core or mod type, so the test includes it without
// the renaming.

#include <array>
#include <string>
#include <vector>

namespace votv_oracle_view {

struct OracleConfig {
    int udp_port;
    float local_smoothing, remote_smoothing;
    int yaw_mode_key;
    bool world_space_yaw;
    bool disable_in_multiplayer;
    bool move_crosshair;
    bool collision_enabled;
    float collision_margin;
    int collision_channel;
    int aim_trace_channel;
    float collision_release_smoothing;
    bool dev_commands;
};

// What the published build's LoadSettings ran on a default Config: WriteDefaultIfMissing, then
// Load, in `dir`. Writes the default file when there is none, as that build did.
OracleConfig RunOracle(const std::string& dir);

// Which actions a key press fires, for every key a binding can name (0x01-0xFE) under every
// set of held modifiers. Entry (vk - kFirstKey) * kHeldStates + held counts the toggle, cycle
// and yaw mode actions fired, in that order. held: 1 Ctrl, 2 Shift, 4 Alt.
constexpr int kFirstKey = 0x01;
constexpr int kLastKey = 0xFE;
constexpr int kHeldStates = 8;
using FireTable = std::vector<std::array<int, 3>>;

// The published build's hotkeys::Register run with `yaw_mode_key`, the one key its config
// named, pressing each key under each held set.
FireTable OracleFires(int yaw_mode_key);

}  // namespace votv_oracle_view
