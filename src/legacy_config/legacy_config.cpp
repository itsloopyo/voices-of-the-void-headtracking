// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "legacy_config/legacy_config.h"

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <windows.h>

#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"

namespace votv_ht::legacy {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

// cameraunlock::NormalizeUdpPort as the last pre-canonical build compiled it.
inline uint16_t NormalizeUdpPort(int raw, uint16_t fallback, bool& valid) {
    valid = (raw >= 1024 && raw <= 65535);
    return valid ? static_cast<uint16_t>(raw) : fallback;
}

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

ReadStatus Read(const std::string& ini_path, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(ini_path)) {
        Log::Line("config: no %s next to the game exe - using defaults", kIniName);
        return ReadStatus::Absent;
    }

    bool port_valid = false;
    out.udp_port = NormalizeUdpPort(
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
    return ReadStatus::Read;
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    return {
        {"Network", "Port"},
        {"Tracking", "LocalSmoothing"},
        {"Tracking", "RemoteSmoothing"},
        {"General", "WorldSpaceYaw"},
        {"General", "DisableInMultiplayer"},
        {"Hotkeys", "YawMode"},
        {"Camera", "CollisionEnabled"},
        {"Camera", "MoveCrosshair"},
        {"Camera", "CollisionMargin"},
        {"Camera", "CollisionChannel"},
        {"Camera", "CollisionReleaseSmoothing"},
        {"Camera", "AimTraceChannel"},
        {"Dev", "DevCommands"},
    };
}

}  // namespace votv_ht::legacy
