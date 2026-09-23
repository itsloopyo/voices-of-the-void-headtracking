// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// Make the mod say where it died, in its own log, in the build that died.
//
// A player's machine has no symbols, no debugger and no matching binary on our
// side. All Windows Error Reporting leaves behind is a module name, an exception
// code and a fault offset - and that offset is only meaningful against the exact
// binary that crashed. Resolving it through a later build's linker map lands in
// whatever function happens to occupy that address now, which reads like an
// answer and is not one.
//
// So the diagnosis has to be written at the moment of the fault, by the code
// that faulted. This installs two handlers:
//
//   - A CRT invalid-parameter handler. The release CRT's default is to
//     __fastfail(FAST_FAIL_INVALID_ARG), which surfaces as exception 0xc0000409
//     and is deliberately uncatchable - no __try, no vectored handler and no
//     unhandled-exception filter sees it. Setting a handler is the ONLY way to
//     get a line out of it, and it is the signature this mod has been dying
//     with: a heap or string call handed something it will not accept.
//   - A SIGABRT handler, for abort() and std::terminate, which fast-fail the
//     same uncatchable way.
//   - cameraunlock-core's unhandled-exception filter. It chains to whatever
//     filter was already installed, so it covers the startup window; a crash
//     the engine later catches in its own top-level filter never reaches it,
//     which is why this goes in early rather than late.
//
// Deliberately NOT a vectored exception handler for the second one. The mod
// probes engine memory that can be unmapped - SafeRead and friends raise an
// access violation and absorb it in their own __except, several times a frame -
// and those faults are raised at addresses inside this module. A first-chance
// handler cannot tell one from a real crash, so it would spend the one-shot
// report on a probe within seconds of load and then have nothing left to say
// when the mod actually died.
//
// Frames are written as module+offset, which is all a later triage needs: the
// offsets are against the build that was running, and the map for that build is
// beside it.
namespace votv_ht::crash_report {

// Install all three handlers. Call as early as the log exists - a crash before this
// is a crash with nothing to read, and the CRT invalid-parameter handler is the
// only way to get a line out of the uncatchable 0xc0000409.
void Install();

}  // namespace votv_ht::crash_report
