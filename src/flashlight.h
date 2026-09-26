// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/effects/head_follow_light.h>
#include <cameraunlock/unreal/ue_math.h>

// The player's flashlight, turned with the head.
//
// mainPlayer_C::light_R is the flashlight's SpotLightComponent. It hangs off the
// player's Camera component through the lag_fl spring arm, and the mod never
// turns that component, so left alone the beam would stay on the mouse aim and
// light the crosshair rather than what the player is looking at. The light is
// turned about the same axis as the rotation the head adds to the view, through
// LightMultiplier times the angle (1.5 by default), so the beam leads the view in
// the direction the head is turning. LightFollowsHead=false leaves it on the aim.
//
// The turn is written as the light's relative rotation and put back at the start
// of the next frame, so for the one game tick in between anything that asks the
// light where it points gets the head's direction.
namespace votv_ht::flashlight {

// Game thread, once per render frame. `player` is the mainPlayer_C (0 when there
// is none). `headDelta` is the world-space rotation the head adds to the view
// this frame; `apply` false, or `light.follows_head` false, hands the light back
// to the game.
void Update(std::uintptr_t player, bool apply, const cameraunlock::unreal::FQuat4d& headDelta,
            const cameraunlock::effects::HeadFollowLightSettings& light);

}  // namespace votv_ht::flashlight
