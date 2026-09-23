// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// The tutorial draws its frame through a path that will not accept a camera
// POSITION the game did not produce itself. Give it one - a millimetre is
// enough, and every axis does it - and the whole 3D view comes back as the
// engine's missing-texture checkerboard, the same magenta and black squares the
// save browser shows for a save with no thumbnail, while the HUD carries on
// drawing over it. It is the position alone: head rotation through the same
// hook draws correctly, and so does a 30 cm lean anywhere in Sandbox, in the
// base, outdoors, lit, dark, near screens and in the open.
//
// So the lean is what stands down, and only while that path is what draws the
// frame. Rotation keeps working there, which is most of what the tutorial is
// for.
namespace votv_ht::footage_view {

// Whether this frame's view comes from that path, so the lean must not be
// applied. Cached per world and re-derived only when the world pointer changes,
// which is a level load.
bool BlocksLean(std::uintptr_t controller);

// Drop the cached answer, so the next call re-derives it. For a session that
// wants to re-test without reloading.
void Forget();

}  // namespace votv_ht::footage_view
