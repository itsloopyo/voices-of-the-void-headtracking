// Compiled into the config oracle library only, with `cameraunlock` and `votv_ht` renamed, so
// "config.h" here is the published build's (oracle/src/config.h).
#include "config.h"
#include "oracle_adapter.h"

namespace votv_oracle_view {

OracleConfig RunOracle(const std::string& dir) {
    votv_ht::Config c;
    votv_ht::config::WriteDefaultIfMissing(dir);
    votv_ht::config::Load(dir, c);
    OracleConfig o{};
    o.udp_port = c.udp_port;
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.yaw_mode_key = c.yaw_mode_key;
    o.world_space_yaw = c.world_space_yaw;
    o.disable_in_multiplayer = c.disable_in_multiplayer;
    o.move_crosshair = c.move_crosshair;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_margin;
    o.collision_channel = c.collision_channel;
    o.aim_trace_channel = c.aim_trace_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.dev_commands = c.dev_commands;
    return o;
}

}  // namespace votv_oracle_view
