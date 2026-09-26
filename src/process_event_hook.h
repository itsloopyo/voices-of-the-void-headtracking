// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// A detour on UObject::ProcessEvent that hands chosen UFunctions to the mod
// before or after the engine runs them. Blueprint events are dispatched through
// ProcessEvent whether or not a blueprint implements them, which makes it a
// per-frame callback site that needs no RVA beyond ProcessEvent itself.
namespace votv_ht::process_event_hook {

using Handler = void (*)(std::uintptr_t self, std::uintptr_t function, void* params);

// Install once the active build profile is selected. Logs and returns false on
// failure; nothing is patched in that case. Idempotent.
bool Install();

// Restore ProcessEvent's prologue and drop every handler. Without this an
// explicit FreeLibrary leaves the engine jumping into the detour after the
// module has unmapped, and UE calls ProcessEvent many times a frame.
void Shutdown();

// Register a handler for one UFunction. Game thread. At most eight handlers.
bool AddPostHandler(std::uintptr_t function, Handler handler);
bool AddPreHandler(std::uintptr_t function, Handler handler);

// Dev: call `observer` for every ProcessEvent until cleared with nullptr.
void SetObserver(Handler observer);

// Dev: call `tick` after every top-level ProcessEvent (never from a nested one),
// so a command channel keeps running on the game thread in front-end states
// where no camera is asking for a view point.
void SetTick(void (*tick)());

}  // namespace votv_ht::process_event_hook
