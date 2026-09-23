// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <windows.h>

namespace votv_ht {

// Entry points called from DllMain. Initialize spins up a bootstrap thread so
// the heavy work never runs under the loader lock.
void Initialize(HMODULE self);
void Shutdown();

}  // namespace votv_ht
