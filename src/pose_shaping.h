// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "camera_fov.h"
#include "session.h"

#include "cameraunlock/camera/zoom_compensation.h"

// What happens to the head pose between the tracker and the camera write.
//
// Two things, and neither is a setting: the ease that fades the pose in when
// tracking starts applying again, and the zoom compensation that keeps a head
// movement worth the same distance across the screen whatever field of view the
// game is drawing at.
//
// Pure and header-only, so both run in tests with no game.
namespace votv_ht::pose_shaping {

// How long the view takes to ease onto the head pose when tracking starts
// applying again.
constexpr std::uint64_t kEntryEaseMs = 400;

// Smoothstep over the ease window.
inline float EntryEase(std::uint64_t elapsedMs) {
    if (elapsedMs >= kEntryEaseMs) return 1.0f;
    const float t = static_cast<float>(elapsedMs) / static_cast<float>(kEntryEaseMs);
    return t * t * (3.0f - 2.0f * t);
}

// 1.0 - the pose passes through unscaled - whenever either field of view is
// unreadable, rather than a guess at what the game is zooming by.
//
// Both arguments are the camera's own FOV scalar in degrees: the one the frame
// is being drawn with, and the un-zoomed one the player's own field of view
// setting put there. Same accessor, same axis, so the factor is exactly 1.0
// whenever nothing is zoomed.
inline float ZoomFactor(float renderFov, float baseFov) {
    if (!camera_fov::Plausible(renderFov) || !camera_fov::Plausible(baseFov)) return 1.0f;
    return cameraunlock::camera::FovZoomFactor(camera_fov::TanHalf(renderFov),
                                               camera_fov::TanHalf(baseFov));
}

// The pose as it reaches the camera. `entry` eases the whole of it in coming out
// of a menu, a cutscene, a load or a tracker dropout, where the frame before was
// the game's clean camera and the head can be anywhere by now. The zoom factor
// then scales back what a narrowed field of view magnifies - all but roll, which
// rotates the image rather than moving it across the frame and so is the same
// tilt at every field of view.
inline void ShapePose(Pose& pose, float entry, float zoomFactor) {
    pose.yaw *= entry;
    pose.pitch *= entry;
    pose.roll *= entry;
    pose.x *= entry;
    pose.y *= entry;
    pose.z *= entry;

    pose.yaw = cameraunlock::camera::ScaleAngleForZoom(pose.yaw, zoomFactor);
    pose.pitch = cameraunlock::camera::ScaleAngleForZoom(pose.pitch, zoomFactor);
    pose.x *= zoomFactor;
    pose.y *= zoomFactor;
    pose.z *= zoomFactor;
}

}  // namespace votv_ht::pose_shaping
