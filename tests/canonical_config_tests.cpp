// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// CameraUnlock.ini: the committed file against the table that renders it, what
// the owner creates on a first start and on the first start after an update,
// and what a toggle's save changes.
//
// Every owner here reads and creates Defaults.ini at a scratch path
// (DefaultsFile::At), never the developer's own.
//
// `--render-config <path>` writes the table's fresh render to <path> and exits,
// which is how `pixi run render-config` rewrites the committed file.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>

#include "config.h"
#include "test_harness.h"

#include "cameraunlock/config/config_table.h"

namespace {

namespace cfg = ::cameraunlock::config;
namespace fs = std::filesystem;
using votv_ht::Config;

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

std::string Fresh() {
    return cfg::RenderCanonicalFresh(votv_ht::config::MakeTable(), cfg::RenderHeader{votv_ht::config::kDisplayName});
}

class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() / ("votv-canonical-config-" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "global");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    fs::path Fresh(const std::string& leaf) {
        const fs::path dir = root_ / (leaf + std::to_string(next_++));
        fs::create_directories(dir);
        return dir;
    }
    // One Defaults.ini for every owner, created by the first with the built-in values.
    cfg::DefaultsFile Defaults() const { return cfg::DefaultsFile::At((root_ / "global" / "Defaults.ini").wstring()); }
    fs::path DefaultsPath() const { return root_ / "global" / "Defaults.ini"; }

private:
    fs::path root_;
    int next_ = 0;
};

cfg::ConfigLoadResult<Config> LoadIn(const Scratch& scratch, const fs::path& dir) {
    cfg::ConfigOwner<Config> owner(votv_ht::config::MakeOwnerOptions(dir.wstring(), scratch.Defaults()));
    return owner.Load();
}

// The lines of `after` that differ from `before`, which must have as many.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    auto split = [](const std::string& s) {
        std::vector<std::string> lines;
        std::size_t start = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\n') {
                lines.push_back(s.substr(start, i + 1 - start));
                start = i + 1;
            }
        }
        if (start < s.size()) lines.push_back(s.substr(start));
        return lines;
    };
    const std::vector<std::string> a = split(before);
    const std::vector<std::string> b = split(after);
    if (a.size() != b.size()) return {"the line count changed"};
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

void TestTheCommittedFileIsTheFreshRender() {
    CHECK_MSG(ReadBytes(VOTV_COMMITTED_CONFIG) == Fresh(),
              "CameraUnlock.ini differs from the table's fresh render; run pixi run render-config");
}

// A first start with no HeadTracking.ini creates the committed file and a
// Defaults.ini holding core's built-in values.
void TestAFirstStartCreatesTheCommittedFile(Scratch& scratch) {
    const fs::path dir = scratch.Fresh("created");
    const cfg::ConfigLoadResult<Config> loaded = LoadIn(scratch, dir);
    CHECK(loaded.status == cfg::ConfigLoadStatus::Created);
    CHECK(ReadBytes(dir / "CameraUnlock.ini") == ReadBytes(VOTV_COMMITTED_CONFIG));
    CHECK(!fs::exists(dir / "HeadTracking.ini"));
    CHECK_MSG(ReadBytes(scratch.DefaultsPath()) == ReadBytes(VOTV_DEFAULTS_FIXTURE),
              "the created Defaults.ini is core's fixture");

    const Config& c = loaded.config;
    CHECK(c.udp_port == 4242);
    CHECK(c.enable_on_startup);
    CHECK(c.rotation_enabled && c.position_enabled);
    CHECK(c.world_space_yaw);
    CHECK(c.collision_enabled);
    CHECK(c.lean_clamp.skin == 15.0f);
    CHECK(c.collision_channel == 0);
    CHECK(c.aim_trace_channel == 0);
    CHECK(c.disable_in_multiplayer);
    CHECK(c.light.follows_head);
    CHECK(c.light.multiplier == 1.5f);
    CHECK(c.toggle_key_name == "End, Ctrl+Shift+Y");
    CHECK(c.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G");
    CHECK(c.yaw_mode_key_name == "PageDown, Ctrl+Shift+H");
}

// The newest published build put its first-run output on a player's disk and
// nothing else. Imported with Defaults.ini at the built-in values, it gives the
// same bytes a new player gets, and HeadTracking.ini is left as it was.
void TestTheFirstUpdateGivesTheCommittedFile(Scratch& scratch) {
    const std::string firstRun = ReadBytes(fs::path(VOTV_DIFFERENTIAL_DATA) / "dev-first-run.ini");
    const fs::path dir = scratch.Fresh("upgrade");
    WriteBytes(dir / "HeadTracking.ini", firstRun);
    const cfg::ConfigLoadResult<Config> loaded = LoadIn(scratch, dir);
    CHECK(loaded.status == cfg::ConfigLoadStatus::Migrated);
    CHECK_MSG(ReadBytes(dir / "CameraUnlock.ini") == ReadBytes(VOTV_COMMITTED_CONFIG),
              "the published first run imports into the committed file");
    CHECK(ReadBytes(dir / "HeadTracking.ini") == firstRun);

    const cfg::ConfigLoadResult<Config> again = LoadIn(scratch, dir);
    CHECK(again.status == cfg::ConfigLoadStatus::Canonical);
    bool saysLegacyIsNotRead = false;
    for (const std::string& line : again.log) {
        if (line.find("is left as it was and is not read") != std::string::npos) saysLegacyIsNotRead = true;
    }
    CHECK_MSG(saysLegacyIsNotRead, "the second start says HeadTracking.ini is not read");
}

// The yaw toggle's save writes WorldSpaceYaw and no other byte; the mode
// cycle's writes the pair and no other byte; the next start reads both back.
void TestASaveChangesOnlyItsRows(Scratch& scratch) {
    const fs::path dir = scratch.Fresh("save");
    const fs::path file = dir / "CameraUnlock.ini";
    {
        cfg::ConfigOwner<Config> owner(votv_ht::config::MakeOwnerOptions(dir.wstring(), scratch.Defaults()));
        CHECK(owner.Load().status == cfg::ConfigLoadStatus::Created);
        const std::string created = ReadBytes(file);

        const cfg::ConfigSaveResult yaw = owner.Save([](Config& c) { c.world_space_yaw = false; });
        CHECK(yaw.status == cfg::ConfigSaveStatus::Saved);
        const std::vector<std::string> yawLines = ChangedLines(created, ReadBytes(file));
        CHECK(yawLines.size() == 1 && yawLines[0] == "WorldSpaceYaw=false\r\n");
        bool named = false;
        for (const std::string& line : yaw.log) {
            if (line.find("WorldSpaceYaw=false is now set for this game") != std::string::npos) named = true;
        }
        CHECK_MSG(named, "the save says WorldSpaceYaw no longer follows Defaults.ini");

        const std::string afterYaw = ReadBytes(file);
        const cfg::ConfigSaveResult mode = owner.Save([](Config& c) {
            c.rotation_enabled = true;
            c.position_enabled = false;
        });
        CHECK(mode.status == cfg::ConfigSaveStatus::Saved);
        const std::vector<std::string> modeLines = ChangedLines(afterYaw, ReadBytes(file));
        CHECK(modeLines.size() == 2 && modeLines[0] == "RotationEnabled=true\r\n" &&
              modeLines[1] == "PositionEnabled=false\r\n");
    }
    const cfg::ConfigLoadResult<Config> again = LoadIn(scratch, dir);
    CHECK(again.status == cfg::ConfigLoadStatus::Canonical);
    CHECK(!again.config.world_space_yaw);
    CHECK(again.config.rotation_enabled && !again.config.position_enabled);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
        WriteBytes(argv[2], Fresh());
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
    try {
        Scratch scratch;
        TestTheCommittedFileIsTheFreshRender();
        TestAFirstStartCreatesTheCommittedFile(scratch);
        TestTheFirstUpdateGivesTheCommittedFile(scratch);
        TestASaveChangesOnlyItsRows(scratch);
    } catch (const std::exception& e) {
        std::printf("FAIL threw: %s\n", e.what());
        return 1;
    }
    return gr_test::Report();
}
