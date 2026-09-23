// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Bootstrap: everything that has to happen once, in order, before the hook can
// run. It runs on its own thread so none of it happens under the loader lock.

#include "headtracking_mod.h"

#include <atomic>
#include <cwchar>
#include <string>

#include <windows.h>

#include "builds/build_registry.h"
#include "crash_report.h"
#include "dev_console.h"
#include "config.h"
#include "logging.h"
#include "mod_hotkeys.h"
#include "process_event_hook.h"
#include "tracking.h"
#include "view_hook.h"
#include "window_centering.h"

#include "cameraunlock/os/module_paths.h"

namespace votv_ht {

namespace {

HANDLE g_bootstrapThread = nullptr;

// Set by Shutdown() so the bootstrap's window wait gives up instead of running
// on inside a module that is being unmapped.
std::atomic<bool> g_shuttingDown{false};

// Held for the life of the process: tracking::Start reads the port from it, and
// the hook and hotkey passes read the rest.
Config g_config;

// The exe directory comes from core rather than a local GetModuleFileName
// pair, and that is not a tidy-up. The hand-rolled version truncated silently
// at MAX_PATH, so a game installed under a long path wrote its log and INI
// somewhere the player would never find them; it turned a path with no
// separator into its own directory; and the narrow half went through
// GetModuleFileNameA, whose best-fit ANSI mapping can rewrite a directory name
// into a DIFFERENT directory that exists. See cameraunlock/os/module_paths.h.
void OpenLog() {
    const std::wstring exeDir = cameraunlock::os::HostExeDirectory();
    if (exeDir.empty()) return;

    // Core opens with CREATE_ALWAYS, so the log holds this session only, and
    // rotates the outgoing one to VoicesOfTheVoidHeadTracking.prev.log first. It
    // warns by itself when the rotation fails, so there is nothing to do here
    // but name the file.
    Log::Open(exeDir + L"\\VoicesOfTheVoidHeadTracking.log");
    Log::Line("=== Voices of the Void Head Tracking v" VOTV_HT_VERSION " (UE4.27) ===");
}

// Which shipped build of the game this is. It runs here, immediately after the
// banner, rather than in the pass that installs the hook: the fingerprint and
// the profile that claims it are the first thing a report of "no head tracking"
// has to be read against, and they have to be in the log whether or not a hook
// is ever reached. SelectProfile installs the active profile as its side
// effect, so everything that reads Offsets() runs after this point.
bool CheckBuild() {
    if (builds::SelectProfile(GetModuleHandleW(nullptr)) == builds::MatchResult::Matched)
        return true;
    Log::Line("build-check: no usable profile for this build - the mod stays dormant "
              "for this session and the game runs unmodified");
    return false;
}

// The INI, and the settings in it that have to be in force before the first
// frame.
void LoadSettings() {
    // Empty means the game directory has no ANSI form, and the INI layer is
    // ANSI-only (GetPrivateProfile*A). A relative path would put the file
    // wherever the game was started from and a best-fit narrowing would put it
    // in somebody else's folder, so the settings stay at their defaults and the
    // log says why.
    const std::string exeDir = cameraunlock::os::HostExeDirectoryNarrow();
    if (exeDir.empty()) {
        Log::Line("config: the game directory has no ANSI form on this system, so "
                  "HeadTracking.ini cannot be read or written - using defaults");
        return;
    }
    config::WriteDefaultIfMissing(exeDir);
    config::Load(exeDir, g_config);
    dev_console::SetEnabled(g_config.dev_commands, exeDir);
}

DWORD WINAPI BootstrapThread(LPVOID) {
    OpenLog();
    // Straight after the log and before anything else: a fault earlier than this
    // is a fault with nothing to read, and the CRT invalid-parameter path is
    // uncatchable unless the handler is already in place when it fires.
    crash_report::Install();
    const bool haveProfile = CheckBuild();
    LoadSettings();

    // Before the window wait, and unconditional. The link comes up on its own
    // schedule from here - a port another game is still holding is waited out
    // rather than reported as a failure - so there is nothing for the rest of
    // the bootstrap to branch on.
    tracking::Start(g_config);

    // The hook, and the keys that drive it. Both are skipped without a matching
    // build profile: the hook because installing against offsets nothing claims
    // would crash the game seconds in, and the keys because every binding drives
    // a hook that is not there, so they would be live keys on dead actions.
    if (haveProfile && view_hook::Install({&g_config})) {
        hotkeys::Register(g_config, *tracking::Get());
        dev_console::ArmFrontEndPolling();
    }

    // Last: it blocks for as long as the engine takes to put a window up, and
    // nothing else in the bootstrap should wait behind that. Skipped without a
    // profile as well, because moving the window is something the player would
    // see, and a dormant session leaves the game exactly as it shipped.
    if (haveProfile) window_centering::CenterWhenReady(g_shuttingDown);
    return 0;
}

}  // namespace

void Initialize(HMODULE) {
    g_bootstrapThread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
    // The log is opened by that thread, so without it there is no file to say
    // why the mod never started. The debugger stream is the one channel left.
    if (!g_bootstrapThread) {
        wchar_t line[128];
        swprintf_s(line, L"VoicesOfTheVoidHeadTracking: CreateThread for the bootstrap failed "
                         L"(%lu) - the mod is not running\n", GetLastError());
        OutputDebugStringW(line);
    }
}

void Shutdown() {
    // Runs from DllMain on an explicit FreeLibrary, which holds the loader lock.
    //
    // Before anything else, so the window wait is already unwinding while the
    // receiver is being torn down.
    g_shuttingDown.store(true, std::memory_order_release);
    // Both detours first: they read the config and the session, so they have to
    // stop seeing frames before either goes away. ProcessEvent matters as much
    // as the view hook here - UE dispatches through it many times a frame, so a
    // prologue left patched over an unmapped module is an immediate crash.
    view_hook::Shutdown();
    process_event_hook::Shutdown();
    // Known limitation, stated rather than hidden: both of these join worker
    // threads, and a thread cannot finish exiting without the loader lock this
    // function is holding. Ultimate ASI Loader never calls FreeLibrary on a
    // plugin and process exit takes the other DllMain branch, so nothing
    // reaches it today. Making it safe needs a signal-only stop on
    // HotkeyPoller and UdpReceiver, which lives in cameraunlock-core.
    hotkeys::Stop();
    tracking::Stop();

    if (g_bootstrapThread) {
        // Bounded, and deliberately so. This runs from DllMain, which holds the
        // loader lock, and a thread cannot finish exiting without it - an
        // unbounded wait here is a deadlock rather than a guarantee. What the
        // wait buys is the ordinary case: the bootstrap observes the flag, gets
        // out of our code, and the module unmaps behind it.
        WaitForSingleObject(g_bootstrapThread, 2000);
        CloseHandle(g_bootstrapThread);
        g_bootstrapThread = nullptr;
    }

    Log::Line("shutdown");
    Log::Close();
}

}  // namespace votv_ht
