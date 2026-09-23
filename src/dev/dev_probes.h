// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// The two dev probes that outlive the command that started them: the shot log
// and the ProcessEvent trace.
//
// Both hang handlers off process_event_hook and both need a tick from the
// command channel's poll - one to report projectiles queued since the last
// poll, the other to close a timed window - which is what puts them together
// and apart from the verbs that merely print something and return.
namespace votv_ht::dev_probes {

// Where each projectile starts and where it lands, next to where the mod put
// the crosshair on that frame. Arms once; a second call does nothing.
void InstallShotLog();

// Report the projectiles queued since the last poll. Game thread.
void PollShotLog();

// Count every UFunction dispatched on `target` (0 for every object) for `ms`.
void StartProcessEventTrace(std::uintptr_t target, int ms);

// Once the window has run out, stop observing and write what it counted.
void PollProcessEventTrace();

}  // namespace votv_ht::dev_probes
