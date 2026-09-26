// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cerrno>
#include <cstdio>
#include <string>

#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace votv_ht::config {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

std::string IniPath(const std::string& exe_dir) { return exe_dir + "\\" + kIniName; }

}  // namespace

void Load(const std::string& exe_dir, Config& out) {
    legacy::Config read;
    legacy::Read(IniPath(exe_dir), read);
    out.udp_port = read.udp_port;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.yaw_mode_key = read.yaw_mode_key;
    out.world_space_yaw = read.world_space_yaw;
    out.disable_in_multiplayer = read.disable_in_multiplayer;
    out.move_crosshair = read.move_crosshair;
    out.collision_enabled = read.collision_enabled;
    out.collision_margin = read.collision_margin;
    out.collision_channel = read.collision_channel;
    out.aim_trace_channel = read.aim_trace_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;
    out.dev_commands = read.dev_commands;
}

void WriteDefaultIfMissing(const std::string& exe_dir) {
    const std::string path = IniPath(exe_dir);
    const Config d{};
    FILE* f = std::fopen(path.c_str(), "wbx");
    if (!f) {
        if (errno != EEXIST)
            Log::Line("config: could not write %s (errno %d)", path.c_str(), errno);
        return;
    }

    std::fprintf(f,
        "; Voices of the Void Head Tracking\r\n"
        "; Delete this file to get the defaults back.\r\n"
        "\r\n"
        "[Network]\r\n"
        "; UDP port the tracker sends to. 4242 is the OpenTrack default.\r\n"
        "Port=%d\r\n"
        "\r\n"
        "[Tracking]\r\n"
        "; Smoothing, 0.0 (none) to 1.0 (heaviest). LocalSmoothing applies to a\r\n"
        "; tracker sending to 127.0.0.1; RemoteSmoothing to any other address,\r\n"
        "; including this PC's own LAN address.\r\n"
        "LocalSmoothing=%.2f\r\n"
        "RemoteSmoothing=%.2f\r\n"
        "\r\n"
        "[General]\r\n"
        "; 1 = head yaw turns about the world's up axis (horizon stays level).\r\n"
        "; 0 = about the camera's own up axis. Page Down (or Ctrl+Shift+H)\r\n"
        "; toggles this for the session.\r\n"
        "WorldSpaceYaw=%d\r\n"
        "; 1 = head tracking stands down as soon as a second player is in the\r\n"
        "; session, so it only ever moves a solo camera. This game has no\r\n"
        "; multiplayer mode, so it never fires today.\r\n"
        "DisableInMultiplayer=%d\r\n"
        "\r\n"
        "[Camera]\r\n"
        "; 1 = move the game's crosshair onto the point you are actually\r\n"
        "; pointing at. 0 = leave it in the middle of the screen.\r\n"
        "MoveCrosshair=%d\r\n"
        "; Stop a positional lean from putting the view inside walls.\r\n"
        "CollisionEnabled=%d\r\n"
        "; Distance held off a surface, in centimetres (5 to 40).\r\n"
        "CollisionMargin=%.1f\r\n"
        "\r\n"
        "[Hotkeys]\r\n"
        "; Virtual-key codes. End (toggle tracking), Page Up (cycle tracking\r\n"
        "; mode) and the Ctrl+Shift chords (Y, G, H) are fixed.\r\n"
        "YawMode=0x%02X\r\n",
        d.udp_port, d.local_smoothing, d.remote_smoothing,
        d.world_space_yaw ? 1 : 0, d.disable_in_multiplayer ? 1 : 0,
        d.move_crosshair ? 1 : 0, d.collision_enabled ? 1 : 0, d.collision_margin,
        d.yaw_mode_key);
    std::fclose(f);
    Log::Line("config: wrote default %s", path.c_str());
}

}  // namespace votv_ht::config
