// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "session.h"

// The mod's one UDP receiver, the tracking session over it, and the watch that
// keeps reporting what the link is doing.
namespace votv_ht::tracking {

// Bring the link up and keep it up for the life of the process.
//
// A busy port is NOT a failure of this call and never stands the mod down. The
// receiver's Start() leaves a supervisor behind that re-attempts the bind every
// 500ms, so a player who launched this game with the previous one still running
// gets tracking within half a second of closing it - nothing to press, no
// relaunch. Treating the first bind as pass/fail is what would take that away.
void Start(const Config& config);

// Stop the watch and the receiver, and drop the session.
void Stop();

// Null until Start() has run.
Session* Get();
cameraunlock::UdpReceiver* Receiver();

}  // namespace votv_ht::tracking
