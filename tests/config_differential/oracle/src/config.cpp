// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <windows.h>

#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"
#include "cameraunlock/protocol/port_utils.h"

namespace votv_ht::config {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

std::string IniPath(const std::string& exe_dir) { return exe_dir + "\\" + kIniName; }

int ReadVirtualKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const int vk = ini.ReadHex("Hotkeys", key, fallback);
    if (cameraunlock::config::IsBindableVirtualKey(vk)) return vk;
    Log::Line("config: Hotkeys.%s=0x%X is not a key this mod can bind - using 0x%02X",
              key, vk, fallback);
    return fallback;
}

// The whole value has to parse, and the result has to be a real number in
// range. IniReader::ReadFloatInRange is neither: it parses a PREFIX, so a
// European decimal comma ("0,15") reads back as 0.0 and passes every check
// silently, and its range test is phrased as a rejection, so a NaN - which
// strtod accepts from a literal "nan" - fails both comparisons and is returned
// as valid. A NaN smoothing value or collision margin reaches the camera as a
// NaN rotation written every frame with nothing in the log.
void ReadFloat(const cameraunlock::IniReader& ini, const char* section, const char* key,
               float lo, float hi, float& value) {
    const float fallback = value;
    const std::string raw = cameraunlock::config::ReadRawValue(ini, section, key);
    if (raw.empty()) return;  // absent, empty or all comment: keep the default

    float parsed = 0.0f;
    if (!cameraunlock::config::ParseFloatStrict(raw, parsed)) {
        Log::Line("config: %s.%s=%s is not a number - using %.3f. Use a dot for the "
                  "decimal point.", section, key, raw.c_str(), fallback);
        return;
    }
    // Phrased as a range test rather than its negation, so a non-finite value,
    // which fails every comparison, is rejected instead of passed through.
    if (!(parsed >= lo && parsed <= hi)) {
        Log::Line("config: %s.%s=%.3f is outside %.2f..%.2f - using %.3f",
                  section, key, parsed, lo, hi, fallback);
        return;
    }
    value = parsed;
}

// IniReader::ReadBool compares the WHOLE value against true/1/yes, and
// GetPrivateProfileString does not strip an inline comment, so
// `CollisionEnabled=0 ; walls are fine` matches nothing and silently keeps the
// default - the user's edit discarded with the log printing the default back as
// though it had been read. Same route as the floats: take the raw value, cut
// the comment, compare the token.
void ReadBool(const cameraunlock::IniReader& ini, const char* section, const char* key,
              bool& value) {
    const std::string raw = cameraunlock::config::ReadRawValue(ini, section, key);
    if (raw.empty()) return;
    std::string token;
    for (const char c : raw) token += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (token == "1" || token == "true" || token == "yes" || token == "on") value = true;
    else if (token == "0" || token == "false" || token == "no" || token == "off") value = false;
    else
        Log::Line("config: %s.%s=%s is not a yes/no value - using %d", section, key, raw.c_str(),
                  value ? 1 : 0);
}

// Same route as the floats and the bools, and for the same reason.
// IniReader::ReadIntInRange goes through GetPrivateProfileIntA, which parses a
// PREFIX and - uniquely among the readers - answers 0 rather than the default
// for a present-but-unparseable value. So `CollisionChannel=two` and
// `AimTraceChannel=0x2` both read back as channel 0 with nothing in the log,
// and the load line then prints that 0 as though it were the user's setting.
// Both keys happen to default to 0 today, so the hole is silent rather than
// harmful - but a trace channel is the difference between the lean clamping on
// walls and clamping on nothing, and the next int key added with a non-zero
// default inherits it.
void ReadInt(const cameraunlock::IniReader& ini, const char* section, const char* key,
             int lo, int hi, int& value) {
    const int fallback = value;
    const std::string raw = cameraunlock::config::ReadRawValue(ini, section, key);
    if (raw.empty()) return;  // absent, empty or all comment: keep the default

    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(raw.c_str(), &end, 10);
    if (end != raw.c_str() + raw.size() || errno == ERANGE) {
        Log::Line("config: %s.%s=%s is not a whole number - using %d", section, key, raw.c_str(),
                  fallback);
        return;
    }
    if (parsed < lo || parsed > hi) {
        Log::Line("config: %s.%s=%ld is outside %d..%d - using %d", section, key, parsed, lo, hi,
                  fallback);
        return;
    }
    value = static_cast<int>(parsed);
}

}  // namespace

void Load(const std::string& exe_dir, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(IniPath(exe_dir))) {
        Log::Line("config: no %s next to the game exe - using defaults", kIniName);
        return;
    }

    bool port_valid = false;
    out.udp_port = cameraunlock::NormalizeUdpPort(
        ini.ReadInt("Network", "Port", out.udp_port),
        static_cast<uint16_t>(out.udp_port), port_valid);
    if (!port_valid)
        Log::Line("config: Network.Port is outside 1024-65535 - using %d", out.udp_port);

    ReadFloat(ini, "Tracking", "LocalSmoothing", 0.0f, 1.0f, out.local_smoothing);
    ReadFloat(ini, "Tracking", "RemoteSmoothing", 0.0f, 1.0f, out.remote_smoothing);

    ReadBool(ini, "General", "WorldSpaceYaw", out.world_space_yaw);
    ReadBool(ini, "General", "DisableInMultiplayer", out.disable_in_multiplayer);

    out.yaw_mode_key = ReadVirtualKey(ini, "YawMode", out.yaw_mode_key);

    ReadBool(ini, "Camera", "CollisionEnabled", out.collision_enabled);
    ReadBool(ini, "Camera", "MoveCrosshair", out.move_crosshair);
    ReadFloat(ini, "Camera", "CollisionMargin", 5.0f, 40.0f, out.collision_margin);
    ReadInt(ini, "Camera", "CollisionChannel", 0, 31, out.collision_channel);
    ReadFloat(ini, "Camera", "CollisionReleaseSmoothing", 0.0f, 1.0f,
              out.collision_release_smoothing);
    ReadInt(ini, "Camera", "AimTraceChannel", 0, 31, out.aim_trace_channel);

    ReadBool(ini, "Dev", "DevCommands", out.dev_commands);

    Log::Line("config: %s loaded (udpPort=%d, smoothing local=%.2f remote=%.2f, "
              "yaw=%s, soloOnly=%d, crosshair=%d, collision=%d margin=%.1f channel=%d, "
              "aimChannel=%d)",
              kIniName, out.udp_port, out.local_smoothing, out.remote_smoothing,
              out.world_space_yaw ? "world" : "local", out.disable_in_multiplayer ? 1 : 0,
              out.move_crosshair ? 1 : 0, out.collision_enabled ? 1 : 0, out.collision_margin,
              out.collision_channel, out.aim_trace_channel);
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
