// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "process_event_hook.h"

#include <atomic>

#include "builds/build_registry.h"
#include "logging.h"

#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::process_event_hook {

namespace {

namespace ue = ::cameraunlock::unreal;

using ProcessEvent_t = void(__fastcall*)(void* self, void* function, void* params);
ProcessEvent_t g_original = nullptr;
void* g_target = nullptr;
bool g_installed = false;

constexpr int kMaxHandlers = 8;
struct Entry {
    std::atomic<std::uintptr_t> Function{0};
    std::atomic<Handler> Callback{nullptr};
};
Entry g_entries[kMaxHandlers];
std::atomic<int> g_count{0};
std::atomic<Handler> g_observer{nullptr};
std::atomic<void (*)()> g_tick{nullptr};
thread_local int t_depth = 0;

// The depth is restored in a __finally, not by a destructor. ue_vm::Invoke
// dispatches through this same patched ProcessEvent and absorbs an access
// violation raised beneath it, which is a designed-for path - it is how
// Dispatch reports that an object stopped being what the caller thought it
// was. This target is built /EHsc (see the build's ExceptionHandling=Sync),
// under which MSVC does NOT run C++ destructors during an SEH unwind, so a
// scope guard here would be skipped exactly like a bare `--t_depth` and leave
// the thread's depth permanently above zero - the front-end tick silently dead
// for the rest of the session. A termination handler runs under both models.
//
// Detour holds no C++ object with a destructor, which is what makes __try legal
// in this function (MSVC C2712).
void __fastcall Detour(void* self, void* function, void* params) {
    ++t_depth;
    __try {
        g_original(self, function, params);
    } __finally {
        --t_depth;
    }
    if (t_depth == 0) {
        if (const auto tick = g_tick.load(std::memory_order_relaxed)) {
            ++t_depth;
            __try {
                tick();
            } __finally {
                --t_depth;
            }
        }
    }
    const auto fn = reinterpret_cast<std::uintptr_t>(function);
    if (const Handler o = g_observer.load(std::memory_order_relaxed))
        o(reinterpret_cast<std::uintptr_t>(self), fn, params);
    const int count = g_count.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
        if (g_entries[i].Function.load(std::memory_order_relaxed) != fn) continue;
        const Handler h = g_entries[i].Callback.load(std::memory_order_relaxed);
        if (h) h(reinterpret_cast<std::uintptr_t>(self), fn, params);
    }
}

}  // namespace

bool Install() {
    if (g_installed) return true;
    const std::uintptr_t rva = Offsets().kProcessEventRva;
    if (rva == 0 || ue::ModuleBase() == 0) {
        Log::Line("process-event: no ProcessEvent RVA in the active profile - not hooked");
        return false;
    }
    auto& hm = cameraunlock::hooks::HookManager::Instance();
    const auto target = reinterpret_cast<void*>(ue::ModuleBase() + rva);
    // ErrorAlreadyCreated is not a failure to recover from, it is the hook
    // already existing - which is what a retry after a failed EnableHook sees.
    // Returning here instead would leave a created hook that never gets enabled
    // and that Shutdown would skip. MinHook also leaves ppOriginal untouched on
    // that status, so g_original must not be cleared for it either: that would
    // null a trampoline the enabled detour is about to call.
    if (auto s = hm.CreateHook(target, reinterpret_cast<void*>(&Detour),
                               reinterpret_cast<void**>(&g_original));
        s != cameraunlock::hooks::HookStatus::Ok &&
        s != cameraunlock::hooks::HookStatus::ErrorAlreadyCreated) {
        Log::Line("process-event: CreateHook failed: %s", cameraunlock::hooks::HookStatusToString(s));
        g_original = nullptr;
        return false;
    }
    if (!g_original) {
        Log::Line("process-event: no trampoline for ProcessEvent - not hooked");
        return false;
    }
    if (auto s = hm.EnableHook(target); s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("process-event: EnableHook failed: %s", cameraunlock::hooks::HookStatusToString(s));
        return false;
    }
    g_target = target;
    g_installed = true;
    Log::Line("process-event: hooked UObject::ProcessEvent at RVA 0x%08llx",
              static_cast<unsigned long long>(rva));
    return true;
}

void Shutdown() {
    if (!g_installed) return;
    // Handlers first: once the prologue is restored the detour cannot be
    // entered again, and a thread already inside it must find nothing left to
    // call into.
    g_tick.store(nullptr);
    g_observer.store(nullptr);
    g_count.store(0);
    // The count alone is not enough: a thread already past its load in Detour
    // still walks the entries it read, so the callbacks themselves have to go.
    for (Entry& entry : g_entries) {
        entry.Callback.store(nullptr);
        entry.Function.store(0);
    }
    cameraunlock::hooks::HookManager::Instance().DisableHook(g_target);
    g_installed = false;
    g_target = nullptr;
}

bool AddPostHandler(std::uintptr_t function, Handler handler) {
    const int index = g_count.load();
    if (!function || index >= kMaxHandlers) return false;
    g_entries[index].Function.store(function);
    g_entries[index].Callback.store(handler);
    g_count.store(index + 1);
    return true;
}

void SetObserver(Handler observer) { g_observer.store(observer); }

void SetTick(void (*tick)()) { g_tick.store(tick); }

}  // namespace votv_ht::process_event_hook
