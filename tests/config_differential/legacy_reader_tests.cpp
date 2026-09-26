// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What the frozen HeadTracking.ini reader (src/legacy_config) takes from a
// file, the reader every published build ran and the legacy import still runs.
//
// The INI was the one place a player's text became a float the render hook
// multiplies a pose by, so the reader was a system boundary and every hazard
// belonged there rather than downstream. Three of them are reachable from a
// single typo:
//
//   - "nan" parses. A range test phrased as a rejection lets it through
//     (a NaN fails both comparisons), and a NaN smoothing value or collision
//     margin reaches the camera as a NaN rotation written every frame with
//     nothing in the log.
//   - "0,15" - a European decimal comma - parses as a PREFIX. It yields 0.0,
//     which is inside every valid range, so the user's setting is silently
//     replaced by one they did not choose.
//   - "1e400" overflows to +inf.
//
// The suite writes real INI files into a temp directory and reads them back
// through legacy::Read, because the reader underneath is
// GetPrivateProfileStringA and its behaviour is the thing being pinned.

#include <cstdio>
#include <string>

#include <windows.h>

#include "legacy_config/legacy_config.h"
#include "test_harness.h"

namespace {

using votv_ht::legacy::Config;

std::string TempDirectory() {
    char base[MAX_PATH] = {};
    const DWORD n = GetTempPathA(MAX_PATH, base);
    std::string dir(base, n);
    dir += "votv_legacy_reader_tests";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

const std::string& Directory() {
    static const std::string dir = TempDirectory();
    return dir;
}

std::string IniPath() { return Directory() + "\\HeadTracking.ini"; }

// Fresh file every time: GetPrivateProfile* caches by path plus write time, so
// a rewritten file has to be written through the same API surface the reader
// uses rather than left to a second-granularity timestamp.
void WriteIni(const std::string& body) {
    DeleteFileA(IniPath().c_str());
    FILE* f = std::fopen(IniPath().c_str(), "wb");
    if (!f) return;
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
    WritePrivateProfileStringA(nullptr, nullptr, nullptr, IniPath().c_str());
}

Config Load(const std::string& body) {
    WriteIni(body);
    Config out;
    votv_ht::legacy::Read(IniPath(), out);
    return out;
}

void TestAWellFormedValueIsTaken() {
    const Config c = Load(
        "[Tracking]\r\nLocalSmoothing=0.25\r\nRemoteSmoothing=0.40\r\n"
        "[Camera]\r\nCollisionMargin=12.5\r\n");
    CHECK_NEAR(c.local_smoothing, 0.25, 1e-6);
    CHECK_NEAR(c.remote_smoothing, 0.40, 1e-6);
    CHECK_NEAR(c.collision_margin, 12.5, 1e-6);
}

void TestNotANumberIsRejected() {
    const Config c = Load("[Tracking]\r\nLocalSmoothing=nan\r\nRemoteSmoothing=-nan(ind)\r\n"
                          "[Camera]\r\nCollisionMargin=nan\r\n");
    CHECK_MSG(c.local_smoothing == 0.0f, "a NaN LocalSmoothing must leave the default in force");
    CHECK_MSG(c.remote_smoothing == 0.15f, "a NaN RemoteSmoothing must leave the default in force");
    CHECK_MSG(c.collision_margin == 15.0f, "a NaN CollisionMargin must leave the default in force");
}

void TestInfinityIsRejected() {
    const Config c = Load("[Tracking]\r\nLocalSmoothing=inf\r\nRemoteSmoothing=1e400\r\n");
    CHECK(c.local_smoothing == 0.0f);
    CHECK(c.remote_smoothing == 0.15f);
}

// The typo this catches used to pass every check: strtod stops at the comma,
// yields 0.0, and 0.0 is a legal smoothing value.
void TestADecimalCommaIsRejectedRatherThanTruncated() {
    const Config c = Load("[Tracking]\r\nRemoteSmoothing=0,15\r\n");
    CHECK_MSG(c.remote_smoothing == 0.15f,
              "a European decimal comma must not be read as its integer part");
}

void TestTrailingTextIsRejected() {
    const Config c = Load("[Camera]\r\nCollisionMargin=12.5cm\r\n");
    CHECK(c.collision_margin == 15.0f);
}

// A comment after a numeric value is the one form that has to keep working:
// GetPrivateProfileStringA hands the comment back as part of the value, and the
// shipped default INI puts comments on their own lines only because of it.
void TestATrailingCommentStillParses() {
    const Config c = Load("[Camera]\r\nCollisionMargin=20.0 ; held off the wall\r\n");
    CHECK_NEAR(c.collision_margin, 20.0, 1e-6);
}

// A bool went through IniReader::ReadBool, which compares the whole value and
// so read `0 ; comment` as no match at all - the user's edit discarded in the
// direction that leaves the clamp on.
void TestABoolWithATrailingCommentIsTaken() {
    const Config c = Load("[Camera]\r\nCollisionEnabled=0 ; walls are fine\r\n");
    CHECK_MSG(c.collision_enabled == false,
              "a bool with a trailing comment must be read, not silently defaulted");
    const Config on = Load("[Dev]\r\nDevCommands=1 # switched on for a test\r\n");
    CHECK_MSG(on.dev_commands == true,
              "a '#' comment must not hide a bool whose default is false");
}

// The session gate defaults to standing tracking down as soon as a second
// player appears, so the key that turns it off has to actually read - someone
// who set it and got
// the default back would report the mod as dead in multiplayer.
void TestTheMultiplayerGateCanBeTurnedOff() {
    const Config d;
    CHECK(d.disable_in_multiplayer == true);
    const Config off = Load("[General]\r\nDisableInMultiplayer=0\r\n");
    CHECK(off.disable_in_multiplayer == false);
    const Config junk = Load("[General]\r\nDisableInMultiplayer=sometimes\r\n");
    CHECK_MSG(junk.disable_in_multiplayer == true,
              "an unparseable DisableInMultiplayer must leave the gate armed");
}

// The trace channels are the only integer keys, and they went through
// GetPrivateProfileIntA, which parses a prefix. `CollisionChannel=2two` read
// back as channel 2 - a number the user never asked for, on the one setting
// that decides whether the lean clamp stops on walls or on nothing - and
// `AimTraceChannel=0x2` read back as 0 with nothing in the log. Both keys
// default to 0, so a value-only test cannot tell the old reader from the new
// one; these use the forms where a prefix parse produced a WRONG non-default.
void TestAnIntegerChannelIsTaken() {
    const Config c = Load("[Camera]\r\nCollisionChannel=3\r\nAimTraceChannel=7\r\n");
    CHECK(c.collision_channel == 3);
    CHECK(c.aim_trace_channel == 7);
    const Config commented = Load("[Camera]\r\nCollisionChannel=5 ; what walls block\r\n");
    CHECK_MSG(commented.collision_channel == 5,
              "an integer with a trailing comment must be read, not silently defaulted");
}

void TestTrailingTextOnAnIntegerIsRejected() {
    const Config d;
    const Config c = Load("[Camera]\r\nCollisionChannel=2two\r\nAimTraceChannel=1,5\r\n");
    CHECK_MSG(c.collision_channel == d.collision_channel,
              "a channel with trailing text must not be read as its integer prefix");
    CHECK_MSG(c.aim_trace_channel == d.aim_trace_channel,
              "a decimal comma must not be read as its integer part");
    const Config hex = Load("[Camera]\r\nCollisionChannel=0x2\r\n");
    CHECK_MSG(hex.collision_channel == d.collision_channel,
              "a hex channel is not a decimal channel and must fall back, with a line");
}

void TestAnOutOfRangeChannelFallsBack() {
    const Config d;
    CHECK(Load("[Camera]\r\nCollisionChannel=99\r\n").collision_channel == d.collision_channel);
    CHECK(Load("[Camera]\r\nAimTraceChannel=-1\r\n").aim_trace_channel == d.aim_trace_channel);
}

void TestAnUnparseableBoolKeepsItsDefault() {
    const Config d;
    const Config c = Load("[Camera]\r\nCollisionEnabled=maybe\r\n");
    CHECK(c.collision_enabled == d.collision_enabled);
}

void TestAnOutOfRangeValueFallsBackToTheDefault() {
    const Config c = Load("[Tracking]\r\nLocalSmoothing=2.0\r\n[Camera]\r\nCollisionMargin=400\r\n");
    CHECK(c.local_smoothing == 0.0f);
    CHECK(c.collision_margin == 15.0f);
}

void TestAnAbsentKeyKeepsItsDefault() {
    const Config c = Load("[Network]\r\nPort=5771\r\n");
    CHECK(c.udp_port == 5771);
    CHECK(c.local_smoothing == 0.0f);
    CHECK(c.remote_smoothing == 0.15f);
    CHECK(c.collision_margin == 15.0f);
    CHECK(c.collision_release_smoothing == 0.9f);
}

void TestAnEmptyValueKeepsItsDefault() {
    const Config c = Load("[Tracking]\r\nLocalSmoothing=\r\nRemoteSmoothing= ; nothing here\r\n");
    CHECK(c.local_smoothing == 0.0f);
    CHECK(c.remote_smoothing == 0.15f);
}

void TestAPortOutsideTheRangeFallsBack() {
    CHECK(Load("[Network]\r\nPort=70000\r\n").udp_port == 4242);
    CHECK(Load("[Network]\r\nPort=80\r\n").udp_port == 4242);
    CHECK(Load("[Network]\r\nPort=notaport\r\n").udp_port == 4242);
}

void TestAnUnbindableHotkeyFallsBack() {
    const Config c = Load("[Hotkeys]\r\nYawMode=0x230\r\nAdsMode=0x10\r\n");
    CHECK_MSG(c.yaw_mode_key == 0x22, "a key code the poller cannot watch must not be bound");
}

}  // namespace

int main() {
    TestAWellFormedValueIsTaken();
    TestNotANumberIsRejected();
    TestInfinityIsRejected();
    TestADecimalCommaIsRejectedRatherThanTruncated();
    TestTrailingTextIsRejected();
    TestATrailingCommentStillParses();
    TestABoolWithATrailingCommentIsTaken();
    TestAnIntegerChannelIsTaken();
    TestTrailingTextOnAnIntegerIsRejected();
    TestAnOutOfRangeChannelFallsBack();
    TestTheMultiplayerGateCanBeTurnedOff();
    TestAnUnparseableBoolKeepsItsDefault();
    TestAnOutOfRangeValueFallsBackToTheDefault();
    TestAnAbsentKeyKeepsItsDefault();
    TestAnEmptyValueKeepsItsDefault();
    TestAPortOutsideTheRangeFallsBack();
    TestAnUnbindableHotkeyFallsBack();

    DeleteFileA(IniPath().c_str());
    return gr_test::Report();
}
