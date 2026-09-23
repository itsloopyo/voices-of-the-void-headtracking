// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ue_vm.h"

#include <windows.h>

#include "builds/build_registry.h"

#include "cameraunlock/memory/safe_memory.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::ue_vm {

namespace {

namespace ue = ::cameraunlock::unreal;

using ProcessEvent_t = void(__fastcall*)(void* self, void* function, void* params);
ProcessEvent_t g_processEvent = nullptr;

// Non-unwinding, so __try is legal here while callers hold objects with
// destructors.
//
// The filter takes access violations and nothing else. A blanket
// EXCEPTION_EXECUTE_HANDLER swallows a stack overflow - after which the guard
// page is gone and the process carries on with no stack left - a breakpoint,
// and any C++ exception travelling through the engine's own frames, and it
// erases all three from the crash report this mod exists to write. An access
// violation from a dead object is the one fault the caller can act on, which is
// what the false is for.
bool Invoke(void* self, void* function, void* params) {
    __try {
        g_processEvent(self, function, params);
        return true;
    } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        return false;
    }
}

}  // namespace

bool Ready() {
    if (g_processEvent) return true;
    const std::uintptr_t rva = Offsets().kProcessEventRva;
    // The module base as well as the RVA, because this answer is LATCHED. With
    // the runtime not yet pointed at the host module, ModuleBase() is 0 and the
    // RVA alone becomes the callee - a low address that faults. The fault is
    // absorbed by Invoke's filter, so nothing crashes and nothing is logged:
    // every engine query the mod makes just answers false for the rest of the
    // session, because the pointer is cached and never re-derived. Answering
    // false here instead leaves Ready() to resolve on a later call, once
    // SetRuntime has run. process_event_hook::Install and ue_call::ObjectItem
    // ask the same question for the same reason.
    if (rva == 0 || ue::ModuleBase() == 0) return false;
    g_processEvent = reinterpret_cast<ProcessEvent_t>(ue::ModuleBase() + rva);
    return true;
}

bool Dispatch(void* self, void* function, void* params) {
    if (!Ready()) return false;
    return Invoke(self, function, params);
}

bool ResolveRetry::Due() {
    const std::uint64_t now = GetTickCount64();
    // Unsigned wrap is not a concern: GetTickCount64 needs 585 million years
    // to overflow, and m_lastAttemptMs starts at zero, so the first call is
    // always due.
    if (now - m_lastAttemptMs < kIntervalMs) return false;
    m_lastAttemptMs = now;
    return true;
}

}  // namespace votv_ht::ue_vm
