// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "session.h"

// The mod's key bindings: the AGENTS.md nav-cluster defaults and their
// Ctrl+Shift chord alternatives. Every binding does its work through view_hook
// or the session and says what it did in the log, so this is the only place
// that knows which key means what.
namespace votv_ht::hotkeys {

// Register the bindings and start polling. `session` must outlive the poller.
void Register(const Config& config, Session& session);

// Stop the polling thread. Safe to call when nothing was registered.
void Stop();

// What each binding does, exposed so the developer command channel can run the
// action itself rather than a copy of it. The poller reads GetAsyncKeyState
// behind a foreground guard, so on a machine where another window holds the
// foreground - a second game under test, say - there is no way to reach these
// through the keyboard, and a copy of the body in the dev console would be a
// second implementation that can drift from the one a player presses.
void ToggleTracking();
void CycleTrackingMode();
void ToggleYawMode();

}  // namespace votv_ht::hotkeys
