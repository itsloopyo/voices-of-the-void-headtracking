// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/effects/head_follow_light.h>
#include <cameraunlock/unreal/ue_math.h>

// The player's flashlight follows the head's rotation and clamped position.
//
// mainPlayer_C::light_R is the flashlight's SpotLightComponent. It hangs off the
// player's Camera component through the lag_fl spring arm, and the mod never
// turns that component, so left alone the beam would stay on the mouse aim and
// light the crosshair rather than what the player is looking at. The light is
// turned about the same axis as the rotation the head adds to the view, through
// LightMultiplier times the angle (1.5 by default), so the beam leads the view in
// the direction the head is turning. LightFollowsHead=false leaves it on the aim.
// The light origin follows the final camera offset so its shadows follow a lean.
//
// Restore the clean transform before the player's tick so its light smoothing
// cannot accumulate tracking. Reapply after the tick for the renderer.
namespace votv_ht::flashlight {

// Game thread. `player` is the mainPlayer_C (0 when there
// is none). `headDelta` is the world-space rotation the head adds to the view
// this frame; `headOffset` is the collision-clamped camera displacement.
// `apply` false, or `light.follows_head` false, hands the light back to the game.
void Update(std::uintptr_t player, bool apply, const cameraunlock::unreal::FQuat4d& headDelta,
            const cameraunlock::unreal::FVector& headOffset,
            const cameraunlock::effects::HeadFollowLightSettings& light);

}  // namespace votv_ht::flashlight
