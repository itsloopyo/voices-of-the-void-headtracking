// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>
#include <cstdint>

// The field of view the frame is actually drawn with, and how it spreads over
// the two screen axes.
//
// The aim mark divides by the half-field tangents, so a wrong field of view puts
// it a proportional distance from where the rounds go, and the zoom compensation
// needs the live value against the game's un-zoomed one.
//
// The engine side lives in camera_fov.cpp. Everything above that line here is
// pure and header-only, so the tangent maths runs in tests with no game.
namespace votv_ht::camera_fov {

// What counts as a believable field of view, in degrees. Wide enough for every
// realistic FOV, so a value outside it is struct drift after a game patch or an
// uninitialised frame rather than a setting.
constexpr float kMinDegrees = 10.0f;
constexpr float kMaxDegrees = 170.0f;

// How the engine spreads one FOV scalar over the two axes. UE picks between
// these from ULocalPlayer::AspectRatioAxisConstraint and the viewport shape,
// exactly as ULocalPlayer::GetProjectionData does, and the two differ by the
// whole viewport aspect - a factor of 1.78 on a 16:9 display - so which one is
// in force is not a detail.
enum class AspectModel {
    // MaintainXFOV, and MajorAxisFOV on a viewport wider than it is tall. The
    // scalar IS the horizontal field; the vertical narrows as the display gets
    // wider (Vert-).
    //
    // This is what Voices of the Void runs: the local player reports
    // AspectRatioAxisConstraint = 1, so its 110 degree setting is the
    // HORIZONTAL field and the vertical works out at 77.6 on 16:9. Measured by
    // asking the engine's own APlayerController::ProjectWorldLocationToScreen -
    // which builds its matrix through ULocalPlayer::GetProjectionData, the same
    // code the renderer uses - where the aim point lands, and comparing it with
    // the mark this model puts there: at 1280x720 and FOV 110 the two agree to
    // a tenth of a pixel at yaw +/-20 (476.9 and 803.1 px) and pitch +/-15
    // (480.1 and 239.9 px). The other reading of the scalar - vertical, with the
    // horizontal derived from the viewport - would put those marks 1.78x
    // further from centre.
    HorizontalFixed,
    // MaintainYFOV, stock UE elsewhere: the scalar is the VERTICAL field and the
    // horizontal grows with the display (Hor+).
    VerticalFixed,
};

// EAspectRatioAxisConstraint, in the order the shipping exe's own UEnum name
// table lists it. -1 is this mod's "not read yet", not an engine value.
inline constexpr int kConstraintUnknown = -1;
inline constexpr int kMaintainYFOV = 0;
inline constexpr int kMaintainXFOV = 1;
inline constexpr int kMajorAxisFOV = 2;

// UE's own rule, from the branch that picks the projection matrix multipliers:
// the horizontal field is the fixed one when the constraint says so outright,
// or when it says "major axis" and the viewport's major axis is the horizontal.
inline AspectModel ModelFor(int constraint, float viewportAspect) {
    if (constraint == kMaintainXFOV) return AspectModel::HorizontalFixed;
    if (constraint == kMajorAxisFOV && viewportAspect > 1.0f)
        return AspectModel::HorizontalFixed;
    return AspectModel::VerticalFixed;
}

// Phrased as a range test rather than its negation so a NaN, which fails every
// comparison, is rejected instead of passed through.
inline bool Plausible(float degrees) {
    return degrees >= kMinDegrees && degrees <= kMaxDegrees;
}

constexpr float kDegreesToRadians = 0.01745329252f;

// tan of half an angle given in degrees: the half-field tangent of one axis,
// and the term the zoom factor is built from.
inline float TanHalf(float degrees) {
    return std::tan(degrees * 0.5f * kDegreesToRadians);
}

// Half-field tangents for the frame: tan(fovX/2) and tan(fovY/2).
//
// False - project nothing - when the field of view is not usable, or when the
// engine's aspect constraint has not been read. The two models differ by the
// whole viewport aspect, so on any display that is not square there is no
// reading that is safe to guess at, and no mark beats a mark 1.78x from where
// the rounds are going.
inline bool HalfFieldTangents(int constraint, float fovDegrees,
                              float viewportAspect, float& tanX, float& tanY) {
    if (!Plausible(fovDegrees) || !(viewportAspect > 0.0f)) return false;
    if (constraint == kConstraintUnknown) return false;

    const float t = TanHalf(fovDegrees);
    if (!(t > 0.0f)) return false;
    if (ModelFor(constraint, viewportAspect) == AspectModel::HorizontalFixed) {
        tanX = t;
        tanY = t / viewportAspect;
    } else {
        tanY = t;
        tanX = t * viewportAspect;
    }
    return tanX > 0.0f && tanY > 0.0f;
}

// ---- the engine side -----------------------------------------------------

// The FOV the render caller's FMinimalViewInfo carries, or 0 when the two
// out-params are not fields of one view info or the value is not an angle.
float ReadRenderFov(const void* outLocation, const void* outRotation);

// ULocalPlayer::AspectRatioAxisConstraint, re-read every call once its offset
// is resolved: the game can change it when its own FOV setting is applied.
void RefreshAspectConstraint(std::uintptr_t controller);
int AspectConstraint();

}  // namespace votv_ht::camera_fov
