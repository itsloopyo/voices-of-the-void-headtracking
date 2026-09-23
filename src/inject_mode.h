// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <array>
#include <cstdint>

// The GetPlayerViewPoint caller gate, which IS the look/aim decoupling: the
// head pose is written back for the render-path caller only, so weapon traces,
// projectile spawning, the audio listener, AI perception and replication all
// keep the clean mouse/pad rotation.
//
// Modes are a flat integer so the log can print them and the dev channel can set
// one by number:
//
//   kAllCallers (0)          discovery: no caller is injected, and every call is
//                            counted by return RVA. This is how the caller table
//                            is built in the first place - GetPlayerViewPoint is
//                            virtually dispatched, so it has no static call
//                            sites and the set can only be observed live. It
//                            writes nothing back, deliberately: the measurement
//                            has to be taken against the game behaving exactly
//                            as it does unmodified, and injecting for every
//                            caller in a shooter would put the head into the
//                            weapon trace.
//   1 .. kCallerSlots        only callers[mode - 1]
//   kNone (kCallerSlots + 1) no caller - tracking off at the hook
namespace votv_ht::inject {

// The slot count for OffsetTable::kKnownCallerRvas, which is declared as the
// CallerRvas alias below so the two cannot drift apart.
inline constexpr std::size_t kCallerSlots = 16;

using CallerRvas = std::array<std::uintptr_t, kCallerSlots>;

inline constexpr int kAllCallers = 0;
inline constexpr int kFirstCaller = 1;
inline constexpr int kNone = kFirstCaller + static_cast<int>(kCallerSlots);

inline bool IsSingleCaller(int mode) { return mode >= kFirstCaller && mode < kNone; }

// The RVA a single-caller mode is gated on, or 0 for kAllCallers / kNone.
inline std::uintptr_t CallerRva(int mode, const CallerRvas& callers) {
    return IsSingleCaller(mode) ? callers[static_cast<std::size_t>(mode - kFirstCaller)] : 0;
}

// kAllCallers answers false here as well as kNone: it is the counting mode and
// injects for nobody. See the mode list above.
inline bool ShouldInject(std::uintptr_t retRva, int mode, const CallerRvas& callers) {
    const std::uintptr_t gate = CallerRva(mode, callers);
    return gate != 0 && retRva == gate;
}

}  // namespace votv_ht::inject
