// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>

// Development command channel, off unless [Dev] DevCommands=true.
//
// While enabled, the game thread polls HeadTracking.devcmd next to the game exe,
// runs each line in it, and deletes the file. It lets a test session drive the
// engine (console commands, screenshots, reflection dumps) without keyboard
// focus on the game window.
namespace votv_ht::dev_console {

void SetEnabled(bool enabled, const std::string& exeDir);

// Keep the channel polled from UObject::ProcessEvent while no camera is asking
// for a view point (title screen, loading). Needs the reflection runtime, so it
// runs once the view hook has installed.
void ArmFrontEndPolling();

// Game thread only. `controller` is the player controller the view hook holds.
void Poll(std::uintptr_t controller);

}  // namespace votv_ht::dev_console
