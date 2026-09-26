// The config differential test (convert-a-mod-to-the-canonical-config, section 5).
//
// Oracle: the reader of the newest published build, the rolling `dev` pre-release at b38440c,
// with the core sources it compiled at its pin 7e84f94 (oracle_adapter.h).
// Import: the frozen reader in src/legacy_config/.
//
// Comparison 1, oracle against import, on every input: load status, every field both read
// (floats bit for bit), the startup state, and which actions every key press fires under every
// set of held modifiers. It finds no difference: nothing between b38440c and the conversion
// changed how HeadTracking.ini is read.
//
// The key presses go through the published build's own hotkeys::Register and the guards it
// compiled (OracleFires).

#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/testing/ini_mutations.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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
        fs::create_directories(root_);
    }
    ~Scratch() {
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(root_, ec)) {
            if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(root_, ec);
    }
    fs::path Fresh(const std::string& leaf) {
        const fs::path dir = root_ / (leaf + std::to_string(next_++));
        fs::create_directories(dir);
        return dir;
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
        for (const Input& input : inputs) Comparison1(scratch, input);
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
