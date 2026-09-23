// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <atomic>

#include <windows.h>

namespace votv_ht::window_centering {

// This process's visible, unowned UnrealWindow, or null when there is none yet.
HWND FindRenderWindow();

// Waits for the game's render window to come up and hold still, then centres it
// on the work area of the monitor it is on, once. A window the game already
// centred, or one that does not fit the work area (fullscreen, borderless), is
// left alone.
//
// Blocks for as long as the engine takes to put the window up, so it runs last
// in the bootstrap. `cancelled` is the mod's shutdown flag: the wait drops out
// when it is set, because on a FreeLibrary this thread has to be out of the
// module before it unmaps and Shutdown() only gives it two seconds.
void CenterWhenReady(const std::atomic<bool>& cancelled);

}  // namespace votv_ht::window_centering
