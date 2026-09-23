// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>

// The verb table behind the development command channel.
namespace votv_ht::dev_commands {

// Run one line of HeadTracking.devcmd. `controller` is the player controller
// the command runs against, and is remembered as the context every later
// command's `controller` target resolves to. Game thread only, and only
// reachable while [Dev] DevCommands=true.
void Run(const std::string& line, std::uintptr_t controller);

}  // namespace votv_ht::dev_commands
