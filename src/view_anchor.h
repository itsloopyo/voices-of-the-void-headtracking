// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "ue4_types.h"

namespace votv_ht::view_anchor {

bool Update(std::uintptr_t controller, const ue4::FVector& offset);

}
