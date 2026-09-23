// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// Calling into the game's script VM, and reading the object graph around it.
//
// Every engine query the mod makes goes the same way: resolve
// UObject::ProcessEvent from the active build profile once, then dispatch a
// UFUNCTION through it behind a fault guard. Every caller must already be on
// the game thread.
namespace votv_ht::ue_vm {

// Resolve UObject::ProcessEvent from the active build profile, once. False when
// the profile carries no RVA for it, in which case nothing else here can run.
bool Ready();

// Dispatch a UFUNCTION. Returns false if the call faulted - the object stopped
// being what the caller thought it was between its liveness test and here - so
// the caller can drop it and go looking for its replacement rather than pushing
// into a dead object forever.
bool Dispatch(void* self, void* function, void* params);

// Rate limit for a resolve that has not succeeded yet.
//
// Every lookup here goes through FindLiveObject, which walks the whole
// GUObjectArray and builds a std::string for each live object it passes -
// hundreds of thousands of them in a shipping build. A feature whose
// objects are not visible yet must therefore NOT retry on every frame, or the
// render thread stalls for as long as they stay invisible.
//
// One instance per feature rather than one shared clock, so a probe that has
// just retried does not block another feature's first attempt. The objects
// appear at engine init, and a quarter second is invisible against a level
// load.
class ResolveRetry {
public:
    // True at most once per interval. On the frames in between an unresolved
    // feature costs one clock read.
    bool Due();

private:
    static constexpr std::uint64_t kIntervalMs = 250;
    std::uint64_t m_lastAttemptMs = 0;
};

}  // namespace votv_ht::ue_vm
