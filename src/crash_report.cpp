// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "crash_report.h"

#include <csignal>
#include <cstdlib>

#include <windows.h>

#include <cameraunlock/diagnostics/crash_handler.h>

#include "logging.h"

namespace votv_ht::crash_report {

namespace {

std::uintptr_t g_selfBase = 0;
std::uintptr_t g_selfEnd = 0;

// Enough to see who called into the CRT without turning one fault into a wall
// of text. The interesting frames are the first few above the handler.
constexpr int kMaxFrames = 24;

// One report only. The CRT can call the handler again from inside the report,
// and the first stack is the honest one.
volatile LONG g_reported = 0;

// Module + offset for an address, resolved against the range captured at
// install time. GetModuleHandleExW / GetModuleFileNameW would read better and
// take the loader lock: a fault raised while that lock is held - inside DllMain,
// or with a LoadLibrary in flight on another thread - would hang the game on its
// last frame with no report at all, which is worse than the crash. Frames
// outside this module print raw; this handler fires on the mod's own CRT calls,
// so the frames that matter are the mod's and its own map resolves them.
void LogFrame(int index, void* addr) {
    const auto a = reinterpret_cast<std::uintptr_t>(addr);
    if (a >= g_selfBase && a < g_selfEnd)
        Log::EmergencyLine("crash:   [%2d] VoicesOfTheVoidHeadTracking.asi+0x%llx", index,
                           static_cast<unsigned long long>(a - g_selfBase));
    else
        Log::EmergencyLine("crash:   [%2d] 0x%llx", index, static_cast<unsigned long long>(a));
}

void LogBacktrace() {
    void* frames[kMaxFrames]{};
    const USHORT n = RtlCaptureStackBackTrace(0, kMaxFrames, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) LogFrame(i, frames[i]);
}

// The CRT calls this instead of __fastfail once it is set. Everything but the
// backtrace is empty in a release CRT - the expression, function and file
// arguments are all null, which is why the stack is the whole point.
//
// Every line goes through EmergencyLine rather than Line: the faulting thread
// may already hold the log's mutex, and re-entering a non-recursive mutex from
// here deadlocks the game instead of reporting.
void __cdecl OnInvalidParameter(const wchar_t* expression, const wchar_t* function,
                                const wchar_t* file, unsigned int line, uintptr_t) {
    if (InterlockedCompareExchange(&g_reported, 1, 0) != 0) return;
    Log::EmergencyLine("crash: CRT invalid parameter - this is the 0xc0000409 the mod has been "
                       "dying with. expr=%ls fn=%ls file=%ls line=%u",
                       expression ? expression : L"(none)", function ? function : L"(none)",
                       file ? file : L"(none)", line);
    LogBacktrace();
    Log::EmergencyLine("crash: the frames above are module+offset against THIS build - resolve "
                       "them with the VoicesOfTheVoidHeadTracking.map beside the .asi");
}

// abort() - which is also where std::terminate and an exception escaping a
// thread or noexcept function end up - raises SIGABRT and then fast-fails with
// FAST_FAIL_FATAL_APP_EXIT, another uncatchable 0xc0000409. The stack is still
// the one that called it, so the backtrace names the culprit.
void __cdecl OnAbort(int) {
    if (InterlockedCompareExchange(&g_reported, 1, 0) != 0) return;
    Log::EmergencyLine("crash: abort() - std::terminate or an uncaught exception");
    LogBacktrace();
    Log::EmergencyLine("crash: the frames above are module+offset against THIS build - resolve "
                       "them with the VoicesOfTheVoidHeadTracking.map beside the .asi");
}

}  // namespace

void Install() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&Install), &self) || !self)
        return;

    g_selfBase = reinterpret_cast<std::uintptr_t>(self);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(self);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_selfBase + dos->e_lfanew);
    g_selfEnd = g_selfBase + nt->OptionalHeader.SizeOfImage;

    _set_invalid_parameter_handler(&OnInvalidParameter);
    std::signal(SIGABRT, &OnAbort);
    cameraunlock::diagnostics::InstallCrashHandler();

    Log::Line("crash-report: armed (module 0x%llx..0x%llx)",
        static_cast<unsigned long long>(g_selfBase),
        static_cast<unsigned long long>(g_selfEnd));
}

}  // namespace votv_ht::crash_report
