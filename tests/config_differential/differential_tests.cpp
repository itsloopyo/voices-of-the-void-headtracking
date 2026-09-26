// The config differential test (convert-a-mod-to-the-canonical-config, section 5).
//
// Oracle: the reader of the newest published build, the rolling `dev` pre-release at b38440c,
// with the core sources it compiled at its pin 7e84f94 (oracle_adapter.h).
// Import: the frozen reader in src/legacy_config/.
// Migration: the conversion, run by the config owner in a folder holding only a copy of the
// input as HeadTracking.ini, which imports it into a new CameraUnlock.ini, then the canonical
// reader and table on that file.
//
// Comparison 1, oracle against import, on every input: load status, every field both read
// (floats bit for bit), the startup state, and which actions every key press fires under every
// set of held modifiers. It finds no difference: nothing between b38440c and the conversion
// changed how HeadTracking.ini is read.
//
// Comparison 2, import against migration, on every input: the same, where the only difference
// allowed is the approved reticle drop the import records for [Camera] MoveCrosshair=false.
// [Dev] DevCommands is read and not carried: only a build configured with VOTV_DEV_COMMANDS
// reads it, and no published build was, so it did nothing for any player.
//
// Also asserted after every load: HeadTracking.ini keeps its bytes, last write time and
// attributes, the folder holds it and CameraUnlock.ini and nothing else, a read-only copy
// migrates as a writable one does, the migrated file draws no diagnostic, and a second load
// over the same Defaults.ini reads CameraUnlock.ini, gives the same settings and changes
// neither file.
//
// The key presses go through the published build's own hotkeys::Register and the guards it
// compiled (OracleFires), and through the guard the current build registers (CurrentFires).

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace votv_ht;

namespace {

constexpr const char* kFileName = "HeadTracking.ini";
constexpr const char* kConfigName = "CameraUnlock.ini";

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        if (g_failures < 200) std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

// Every file in a folder with its bytes, its last write time and its attributes.
struct Entry {
    std::string name;
    std::string bytes;
    FILETIME written;
    DWORD attributes;
    bool operator==(const Entry& o) const {
        return name == o.name && bytes == o.bytes && CompareFileTime(&written, &o.written) == 0 &&
               attributes == o.attributes;
    }
    bool operator<(const Entry& o) const { return name < o.name; }
};

std::vector<Entry> List(const fs::path& dir) {
    std::vector<Entry> entries;
    for (const auto& e : fs::directory_iterator(dir)) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(e.path().c_str(), GetFileExInfoStandard, &data)) {
            throw std::runtime_error("no attributes for " + e.path().string());
        }
        entries.push_back({e.path().filename().string(), ReadBytes(e.path()), data.ftLastWriteTime,
                           data.dwFileAttributes});
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

void SetReadOnly(const fs::path& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("no attributes for " + path.string());
    if (!SetFileAttributesW(path.c_str(), attrs | FILE_ATTRIBUTE_READONLY)) {
        throw std::runtime_error("could not set attributes on " + path.string());
    }
}

struct Input {
    std::string name;
    std::optional<std::string> bytes;  // nullopt: no file
};

// The startup state the published build derived from its config. view_hook.cpp at b38440c
// starts with tracking on (g_trackingEnabled{true}) whatever the file says, the session starts
// in RotationAndPosition (HeadTrackingSession's m_mode), and the yaw mode is world_space_yaw.
// Only the last comes from the file, so it is the one compared; the fields hold it already.

std::vector<std::string> FieldDifferences(const votv_oracle_view::OracleConfig& o, const legacy::Config& i) {
    std::vector<std::string> d;
    auto b = [&d](const char* n, bool x, bool y) { if (x != y) d.push_back(n); };
    auto f = [&d](const char* n, float x, float y) { if (!SameBits(x, y)) d.push_back(n); };
    auto n = [&d](const char* name, int x, int y) { if (x != y) d.push_back(name); };
    n("udp_port", o.udp_port, i.udp_port);
    f("local_smoothing", o.local_smoothing, i.local_smoothing);
    f("remote_smoothing", o.remote_smoothing, i.remote_smoothing);
    n("yaw_mode_key", o.yaw_mode_key, i.yaw_mode_key);
    b("world_space_yaw", o.world_space_yaw, i.world_space_yaw);
    b("disable_in_multiplayer", o.disable_in_multiplayer, i.disable_in_multiplayer);
    b("move_crosshair", o.move_crosshair, i.move_crosshair);
    b("collision_enabled", o.collision_enabled, i.collision_enabled);
    f("collision_margin", o.collision_margin, i.collision_margin);
    n("collision_channel", o.collision_channel, i.collision_channel);
    n("aim_trace_channel", o.aim_trace_channel, i.aim_trace_channel);
    f("collision_release_smoothing", o.collision_release_smoothing, i.collision_release_smoothing);
    b("dev_commands", o.dev_commands, i.dev_commands);
    return d;
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

// The first press whose fired actions differ, for the failure message.
std::string FirstFireDifference(const votv_oracle_view::FireTable& expected, const votv_oracle_view::FireTable& got) {
    for (std::size_t i = 0; i < expected.size() && i < got.size(); ++i) {
        if (expected[i] != got[i]) {
            char text[160];
            std::snprintf(text, sizeof text, "key 0x%02X held %d fires %d/%d/%d, not %d/%d/%d",
                          static_cast<int>(i / votv_oracle_view::kHeldStates) + votv_oracle_view::kFirstKey,
                          static_cast<int>(i % votv_oracle_view::kHeldStates), got[i][0], got[i][1], got[i][2],
                          expected[i][0], expected[i][1], expected[i][2]);
            return text;
        }
    }
    return expected.size() == got.size() ? "none" : "the tables differ in size";
}

// The corpus descriptor of every key the frozen reader reads.
std::vector<cameraunlock::config::testing::MutationKey> MutationKeys() {
    using cameraunlock::config::testing::MutationKey;
    auto plain = [](const char* s, const char* k, const char* alt, std::vector<std::string> oor = {}) {
        MutationKey m;
        m.section = s;
        m.key = k;
        m.alternate = alt;
        m.out_of_range = std::move(oor);
        return m;
    };
    MutationKey yaw = plain("Hotkeys", "YawMode", "0x70", {"0x10", "0xA1"});
    yaw.hotkey = true;
    return {
        plain("Network", "Port", "4243", {"1023", "65536"}),
        plain("Tracking", "LocalSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Tracking", "RemoteSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("General", "WorldSpaceYaw", "0"),
        plain("General", "DisableInMultiplayer", "0"),
        yaw,
        plain("Camera", "CollisionEnabled", "0"),
        plain("Camera", "MoveCrosshair", "0"),
        plain("Camera", "CollisionMargin", "20.0", {"4.9", "40.5"}),
        plain("Camera", "CollisionChannel", "3", {"-1", "32"}),
        plain("Camera", "CollisionReleaseSmoothing", "0.5", {"-0.5", "1.5"}),
        plain("Camera", "AimTraceChannel", "2", {"-1", "32"}),
        plain("Dev", "DevCommands", "1"),
    };
}

class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() / ("votv-config-differential-" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "global");
    }
    ~Scratch() {
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(root_, ec)) {
            if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(root_, ec);
    }
    // Every folder but Defaults.ini's, once an input is done with them, so the run holds a few
    // folders on disk at a time rather than thousands.
    void Clear() {
        for (const auto& dir : fs::directory_iterator(root_)) {
            if (dir.path().filename() == "global") continue;
            for (const auto& e : fs::recursive_directory_iterator(dir.path())) {
                if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
            }
            fs::remove_all(dir.path());
        }
    }
    fs::path Fresh(const std::string& leaf) {
        const fs::path dir = root_ / (leaf + std::to_string(next_++));
        fs::create_directories(dir);
        return dir;
    }
    // One Defaults.ini for every owner, created by the first at the built-in values.
    fs::path DefaultsPath() const { return root_ / "global" / "Defaults.ini"; }
    cameraunlock::config::DefaultsFile Defaults() const {
        return cameraunlock::config::DefaultsFile::At(DefaultsPath().wstring());
    }

private:
    fs::path root_;
    int next_ = 0;
};

fs::path Place(const fs::path& dir, const Input& input) {
    const fs::path file = dir / kFileName;
    if (input.bytes) WriteBytes(file, *input.bytes);
    return file;
}

struct ImportRun {
    legacy::Config config;
    legacy::ReadStatus status = legacy::ReadStatus::Read;
};

bool SameImport(const ImportRun& a, const ImportRun& b) {
    const votv_oracle_view::OracleConfig view{a.config.udp_port,
                                              a.config.local_smoothing,
                                              a.config.remote_smoothing,
                                              a.config.yaw_mode_key,
                                              a.config.world_space_yaw,
                                              a.config.disable_in_multiplayer,
                                              a.config.move_crosshair,
                                              a.config.collision_enabled,
                                              a.config.collision_margin,
                                              a.config.collision_channel,
                                              a.config.aim_trace_channel,
                                              a.config.collision_release_smoothing,
                                              a.config.dev_commands};
    return a.status == b.status && FieldDifferences(view, b.config).empty();
}

// The import on one copy of the input, which it must leave as it found it.
ImportRun RunImport(Scratch& scratch, const Input& input, bool readOnly) {
    const fs::path dir = scratch.Fresh(readOnly ? "import-ro" : "import");
    const fs::path file = Place(dir, input);
    if (input.bytes && readOnly) SetReadOnly(file);
    const std::vector<Entry> before = List(dir);
    ImportRun run;
    run.status = legacy::Read(file.string(), run.config);
    Check(List(dir) == before, input.name + ": the import changed its folder");
    return run;
}

ImportRun Comparison1(Scratch& scratch, const Input& input) {
    const fs::path odir = scratch.Fresh("oracle");
    Place(odir, input);
    const votv_oracle_view::OracleConfig oracle = votv_oracle_view::RunOracle(odir.string());
    const ImportRun import = RunImport(scratch, input, false);
    const ImportRun readOnly = RunImport(scratch, input, true);
    Check(SameImport(import, readOnly), input.name + ": a read-only copy imports differently");

    // The published build never refused a file: a missing one it wrote and then read.
    Check((import.status == legacy::ReadStatus::Absent) == !input.bytes.has_value(),
          input.name + ": the import's status");
    const std::vector<std::string> fields = FieldDifferences(oracle, import.config);
    Check(fields.empty(), input.name + ": fields differ: " + Join(fields));
    const votv_oracle_view::FireTable oracleFires = votv_oracle_view::OracleFires(oracle.yaw_mode_key);
    const votv_oracle_view::FireTable importFires = votv_oracle_view::OracleFires(import.config.yaw_mode_key);
    Check(oracleFires == importFires,
          input.name + ": hotkeys fire differently: " + FirstFireDifference(oracleFires, importFires));
    return import;
}

cameraunlock::input::KeyModifiers g_currentHeld = cameraunlock::input::KeyModifiers::kNone;

cameraunlock::input::KeyModifiers CurrentHeld() { return g_currentHeld; }

cameraunlock::input::KeyModifiers ModifiersOf(int held) {
    using cameraunlock::input::KeyModifiers;
    KeyModifiers m = KeyModifiers::kNone;
    if ((held & 1) != 0) m = m | KeyModifiers::kCtrl;
    if ((held & 2) != 0) m = m | KeyModifiers::kShift;
    if ((held & 4) != 0) m = m | KeyModifiers::kAlt;
    return m;
}

// OracleFires' table for the current build. Its hotkeys::Register parses each key list and
// hands it to RegisterKeyBindings, which puts one detail::GuardKey callback per distinct key on
// the poller, holding that key's bindings in list order. The same callbacks are built here with
// the held modifiers read from the test rather than the keyboard, since the poller keeps its
// callbacks to itself.
votv_oracle_view::FireTable CurrentFires(const Config& m) {
    using votv_oracle_view::kFirstKey;
    using votv_oracle_view::kHeldStates;
    using votv_oracle_view::kLastKey;
    std::array<int, 3> fired{};
    std::vector<std::pair<int, std::function<void()>>> registered;
    const std::string* lists[3] = {&m.toggle_key_name, &m.cycle_tracking_mode_key_name, &m.yaw_mode_key_name};
    for (int action = 0; action < 3; ++action) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*lists[action]);
        if (!parsed.ok()) throw std::logic_error("migrated hotkey list '" + *lists[action] + "' does not parse");
        std::vector<int> keys;
        std::vector<std::vector<cameraunlock::input::KeyModifiers>> modifiers;
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            const auto at = std::find(keys.begin(), keys.end(), b.vk);
            if (at == keys.end()) {
                keys.push_back(b.vk);
                modifiers.push_back({b.modifiers});
            } else {
                modifiers[static_cast<std::size_t>(at - keys.begin())].push_back(b.modifiers);
            }
        }
        for (std::size_t i = 0; i < keys.size(); ++i) {
            registered.emplace_back(keys[i], cameraunlock::input::detail::GuardKey(
                                                 std::move(modifiers[i]), [&fired, action] { ++fired[action]; },
                                                 &CurrentHeld));
        }
    }

    votv_oracle_view::FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            g_currentHeld = ModifiersOf(held);
            for (const auto& r : registered) {
                if (r.first == vk) r.second();
            }
            table.push_back(fired);
        }
    }
    g_currentHeld = cameraunlock::input::KeyModifiers::kNone;
    return table;
}

using cameraunlock::config::ConfigLoadStatus;
using cameraunlock::config::DropRule;
using cameraunlock::config::DroppedValue;
using cameraunlock::config::ImportResult;
using cameraunlock::config::ImportStatus;

cameraunlock::config::ConfigLoadResult<Config> LoadOwner(const Scratch& scratch, const fs::path& dir) {
    cameraunlock::config::ConfigOwner<Config> owner(config::MakeOwnerOptions(dir.wstring(), scratch.Defaults()));
    return owner.Load();
}

// The import with its map, on its own copy, for the values it drops.
ImportResult RunMappedImport(Scratch& scratch, const Input& input) {
    const fs::path file = Place(scratch.Fresh("mapped"), input);
    cameraunlock::config::LegacyInput legacyInput;
    legacyInput.path = file.wstring();
    legacyInput.ansi_path = file.string();
    Config out;
    return config::MakeLegacyImport().run(legacyInput, out);
}

// Every setting the migration carries, against the import's, and the startup state.
std::vector<std::string> MigrationDifferences(const legacy::Config& l, const Config& m) {
    std::vector<std::string> d;
    if (m.udp_port != l.udp_port) d.push_back("UdpPort");
    if (!SameBits(m.local_smoothing, l.local_smoothing) || !SameBits(m.position.local_smoothing, l.local_smoothing)) {
        d.push_back("LocalSmoothing");
    }
    if (!SameBits(m.remote_smoothing, l.remote_smoothing) ||
        !SameBits(m.position.remote_smoothing, l.remote_smoothing)) {
        d.push_back("RemoteSmoothing");
    }
    if (m.world_space_yaw != l.world_space_yaw) d.push_back("WorldSpaceYaw");
    if (m.disable_in_multiplayer != l.disable_in_multiplayer) d.push_back("DisableInMultiplayer");
    if (m.collision_enabled != l.collision_enabled) d.push_back("CollisionEnabled");
    if (!SameBits(m.lean_clamp.skin, l.collision_margin)) d.push_back("CollisionMargin");
    if (m.collision_channel != l.collision_channel) d.push_back("CollisionChannel");
    if (!SameBits(m.lean_clamp.release_smoothing, l.collision_release_smoothing)) {
        d.push_back("CollisionReleaseSmoothing");
    }
    if (m.aim_trace_channel != l.aim_trace_channel) d.push_back("AimTraceChannel");
    // The published build started with tracking on, in rotation and position, with the flashlight
    // turned 1.5 times as far as the head: none of them came from the file.
    if (!m.enable_on_startup) d.push_back("EnableOnStartup");
    const auto mode = cameraunlock::DecodeTrackingMode(m.rotation_enabled, m.position_enabled);
    if (!mode || *mode != cameraunlock::TrackingMode::RotationAndPosition) d.push_back("tracking mode");
    if (!m.light.follows_head) d.push_back("LightFollowsHead");
    if (!SameBits(m.light.multiplier, 1.5f)) d.push_back("LightMultiplier");
    return d;
}

// Every setting the session runs on that differs between two loads.
std::vector<std::string> SettingsDifferences(const Config& a, const Config& b) {
    std::vector<std::string> d;
    auto x = [&d](const char* n, bool same) { if (!same) d.push_back(n); };
    x("UdpPort", a.udp_port == b.udp_port);
    x("LocalSmoothing", SameBits(a.local_smoothing, b.local_smoothing));
    x("RemoteSmoothing", SameBits(a.remote_smoothing, b.remote_smoothing));
    x("WorldSpaceYaw", a.world_space_yaw == b.world_space_yaw);
    x("DisableInMultiplayer", a.disable_in_multiplayer == b.disable_in_multiplayer);
    x("CollisionEnabled", a.collision_enabled == b.collision_enabled);
    x("CollisionMargin", SameBits(a.lean_clamp.skin, b.lean_clamp.skin));
    x("CollisionChannel", a.collision_channel == b.collision_channel);
    x("CollisionReleaseSmoothing", SameBits(a.lean_clamp.release_smoothing, b.lean_clamp.release_smoothing));
    x("AimTraceChannel", a.aim_trace_channel == b.aim_trace_channel);
    x("EnableOnStartup", a.enable_on_startup == b.enable_on_startup);
    x("RotationEnabled", a.rotation_enabled == b.rotation_enabled);
    x("PositionEnabled", a.position_enabled == b.position_enabled);
    x("LightFollowsHead", a.light.follows_head == b.light.follows_head);
    x("LightMultiplier", SameBits(a.light.multiplier, b.light.multiplier));
    x("ToggleKey", a.toggle_key_name == b.toggle_key_name);
    x("CycleTrackingModeKey", a.cycle_tracking_mode_key_name == b.cycle_tracking_mode_key_name);
    x("YawModeKey", a.yaw_mode_key_name == b.yaw_mode_key_name);
    return d;
}

bool Ascii(const std::string& bytes) {
    for (const char c : bytes) {
        if (static_cast<unsigned char>(c) > 0x7F) return false;
    }
    return true;
}

bool CrlfOnly(const std::string& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '\n' && (i == 0 || bytes[i - 1] != '\r')) return false;
        if (bytes[i] == '\r' && (i + 1 == bytes.size() || bytes[i + 1] != '\n')) return false;
    }
    return !bytes.empty() && bytes.back() == '\n';
}

// The migration on one copy of the input. Returns what it ran on.
std::optional<Config> Migrate(Scratch& scratch, const Input& input, bool readOnly) {
    const fs::path dir = scratch.Fresh(readOnly ? "migration-ro" : "migration");
    const fs::path file = Place(dir, input);
    if (input.bytes && readOnly) SetReadOnly(file);
    const std::vector<Entry> before = List(dir);
    const std::string defaultsBefore = fs::exists(scratch.DefaultsPath()) ? ReadBytes(scratch.DefaultsPath()) : "";

    const cameraunlock::config::ConfigLoadResult<Config> loaded = LoadOwner(scratch, dir);
    const ConfigLoadStatus expected = input.bytes ? ConfigLoadStatus::Migrated : ConfigLoadStatus::Created;
    Check(loaded.status == expected, input.name + ": not " + cameraunlock::config::ConfigLoadStatusName(expected) +
                                         " but " + cameraunlock::config::ConfigLoadStatusName(loaded.status) +
                                         ": " + loaded.reason);
    if (loaded.status != expected) return std::nullopt;

    // HeadTracking.ini as it was, and CameraUnlock.ini beside it, and nothing else.
    std::vector<Entry> after = List(dir);
    const auto created = std::find_if(after.begin(), after.end(), [](const Entry& e) { return e.name == kConfigName; });
    Check(created != after.end(), input.name + ": no CameraUnlock.ini");
    if (created == after.end()) return std::nullopt;
    const std::string bytes = created->bytes;
    after.erase(created);
    Check(after == before, input.name + ": HeadTracking.ini or its folder changed");

    // What the owner wrote reads back as it is, with nothing to report.
    const cameraunlock::config::CanonicalIni doc = cameraunlock::config::ParseCanonicalIni(bytes);
    Check(doc.IsReadable() && cameraunlock::config::HasCanonicalStamp(bytes) && doc.diagnostics.empty(),
          input.name + ": the migrated file draws reader diagnostics");
    Check(Ascii(bytes) && CrlfOnly(bytes), input.name + ": the migrated file is not ASCII with CRLF endings");
    Check(loaded.diagnostics.empty(), input.name + ": the migrated file draws table diagnostics");

    // The next start reads CameraUnlock.ini, runs on the same settings and changes nothing.
    const std::vector<Entry> settled = List(dir);
    const cameraunlock::config::ConfigLoadResult<Config> again = LoadOwner(scratch, dir);
    Check(again.status == ConfigLoadStatus::Canonical, input.name + ": the second start did not read CameraUnlock.ini");
    Check(SettingsDifferences(again.config, loaded.config).empty(),
          input.name + ": the second start runs on other settings: " +
              Join(SettingsDifferences(again.config, loaded.config)));
    Check(List(dir) == settled, input.name + ": the second start changed a file");
    if (!defaultsBefore.empty()) {
        Check(ReadBytes(scratch.DefaultsPath()) == defaultsBefore, input.name + ": Defaults.ini changed");
    }
    return loaded.config;
}

void Comparison2(Scratch& scratch, const Input& input, const ImportRun& import) {
    const std::optional<Config> migrated = Migrate(scratch, input, false);
    const std::optional<Config> readOnly = Migrate(scratch, input, true);
    if (!migrated || !readOnly) return;
    Check(SettingsDifferences(*migrated, *readOnly).empty(),
          input.name + ": a read-only copy migrates differently: " + Join(SettingsDifferences(*migrated, *readOnly)));

    const legacy::Config& l = import.config;
    const Config& m = *migrated;
    const std::vector<std::string> d = MigrationDifferences(l, m);
    Check(d.empty(), input.name + ": migration differs from the import: " + Join(d));

    const ImportResult imported = RunMappedImport(scratch, input);
    Check(imported.status == (input.bytes ? ImportStatus::Imported : ImportStatus::Absent),
          input.name + ": the mapped import's status");
    Check(imported.pose_shaping.empty(), input.name + ": the import recorded pose shaping");
    bool reticleDropped = false;
    for (const DroppedValue& drop : imported.dropped) {
        const bool reticle = drop.rule == DropRule::Reticle && drop.section == "Camera" && drop.key == "MoveCrosshair";
        reticleDropped = reticleDropped || reticle;
        Check(reticle, input.name + ": unexpected drop " + cameraunlock::config::DescribeDroppedValue(drop));
    }
    Check(reticleDropped == !l.move_crosshair,
          input.name + ": MoveCrosshair dropped does not match the player's value");

    const votv_oracle_view::FireTable before = votv_oracle_view::OracleFires(l.yaw_mode_key);
    const votv_oracle_view::FireTable after = CurrentFires(m);
    Check(before == after, input.name + ": hotkeys fire differently: " + FirstFireDifference(before, after));
}


std::vector<Input> Inputs(const std::string& firstRun) {
    using cameraunlock::config::testing::GenerateIniMutations;
    std::vector<Input> inputs;
    inputs.push_back({"no file", std::nullopt});
    inputs.push_back({"empty file", std::string()});
    inputs.push_back({"dev first-run output", firstRun});
    for (auto& m : GenerateIniMutations(firstRun, legacy::ReadKeys(), MutationKeys())) {
        inputs.push_back({"corpus: " + m.name, std::move(m.bytes)});
    }
    return inputs;
}

}  // namespace

int main() {
    try {
        Scratch scratch;

        // The dev build's first-run output, committed once as test data, is what the oracle
        // still writes for a missing file. The published build shipped no config in its ZIP
        // and no launcher seed, so this is the only file it put on a player's disk.
        const std::string firstRun = ReadBytes(fs::path(VOTV_DIFFERENTIAL_DATA) / "dev-first-run.ini");
        {
            const fs::path dir = scratch.Fresh("first-run");
            votv_oracle_view::RunOracle(dir.string());
            Check(ReadBytes(dir / kFileName) == firstRun,
                  "the oracle's first-run output differs from data/dev-first-run.ini");
        }

        const std::vector<Input> inputs = Inputs(firstRun);
        std::printf("comparison 1 (oracle dev b38440c against the import) on %zu inputs\n", inputs.size());
        std::printf("comparison 2 (the import against the migration)\n");
        for (const Input& input : inputs) {
            Comparison2(scratch, input, Comparison1(scratch, input));
            scratch.Clear();
        }
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
