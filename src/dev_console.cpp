// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Compiled only into a development build - see VOTV_DEV_COMMANDS in
// CMakeLists.txt. The entry points below the #else are what a shipped binary
// links, so the bootstrap and the view hook call the same three functions in
// either build and carry no build-conditional code of their own.
#ifdef VOTV_DEV_COMMANDS

// The command channel itself: find the file, read a line at a time, hand each
// one to the verb table, and keep being called in the states where the view
// hook is quiet.
//
// The verbs live in dev/dev_commands.h, the engine toolbox they are written
// against in dev/dev_ue.h, and the two probes that outlive a command in
// dev/dev_probes.h.

#include "dev_console.h"

#include <fstream>
#include <string>
#include <vector>

#include <windows.h>

#include "dev/dev_commands.h"
#include "dev/dev_probes.h"
#include "dev/dev_ue.h"
#include "logging.h"
#include "process_event_hook.h"

namespace votv_ht::dev_console {

namespace {

bool g_enabled = false;
std::string g_path;
std::uint64_t g_lastPollMs = 0;
std::uint64_t g_lastViewPollMs = 0;

// How often the command file is read, and how often the ProcessEvent tick
// stands in for a view hook that has gone quiet.
constexpr std::uint64_t kFilePollMs = 250;
constexpr std::uint64_t kFrontEndTickMs = 1000;

// Every non-empty line of the command file, which is deleted once read. Empty
// when there is no file.
std::vector<std::string> TakeCommandLines() {
    std::vector<std::string> lines;
    {
        std::ifstream f(g_path);
        if (!f) return lines;
        std::string l;
        while (std::getline(f, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!l.empty()) lines.push_back(l);
        }
    }
    DeleteFileA(g_path.c_str());
    return lines;
}

void TickFromProcessEvent() {
    // Its own stamp, not g_lastViewPollMs. That one is only written on the
    // view-hook path, and this tick exists precisely for the states where the
    // view hook is quiet - so the gate never closed, and every single
    // ProcessEvent ran a full UObject table walk looking for a controller that
    // is not there yet. UE dispatches through ProcessEvent many times a frame.
    static std::uint64_t s_lastTickMs = 0;
    const std::uint64_t now = GetTickCount64();
    if (now - s_lastTickMs < kFrontEndTickMs) return;
    s_lastTickMs = now;
    if (now - g_lastViewPollMs < kFrontEndTickMs) return;
    Poll(0);
}

}  // namespace

void SetEnabled(bool enabled, const std::string& exeDir) {
    g_enabled = enabled && !exeDir.empty();
    g_path = exeDir + "\\HeadTracking.devcmd";
    if (g_enabled) Log::Line("dev: command channel enabled, polling %s", g_path.c_str());
}

void ArmFrontEndPolling() {
    if (!g_enabled) return;
    process_event_hook::Install();
    process_event_hook::SetTick(&TickFromProcessEvent);
}

void Poll(std::uintptr_t controller) {
    if (!g_enabled) return;
    const std::uint64_t now = GetTickCount64();
    if (controller) g_lastViewPollMs = now;
    else controller = dev_ue::Context() ? dev_ue::Context() : dev_ue::FindLocalController();
    dev_probes::PollShotLog();
    dev_probes::PollProcessEventTrace();
    if (now - g_lastPollMs < kFilePollMs) return;
    g_lastPollMs = now;

    for (const std::string& l : TakeCommandLines()) dev_commands::Run(l, controller);
}

}  // namespace votv_ht::dev_console

#else

#include "dev_console.h"

namespace votv_ht::dev_console {

// No command file is read, no verb exists, and no UObject::ProcessEvent detour
// is installed.
void SetEnabled(bool, const std::string&) {}
void ArmFrontEndPolling() {}
void Poll(std::uintptr_t) {}

}  // namespace votv_ht::dev_console

#endif  // VOTV_DEV_COMMANDS
