// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>

#include <cameraunlock/rendering/aim_ndc_projection.h>
#include <cameraunlock/unreal/ue_math.h>

// Where a world point lands in the frame the head is looking at.
//
// The mark says where the round lands, which is a POINT: the impact of the
// shot's own ray. It is projected from the eye the frame is drawn from, along
// the basis the camera hook wrote, so a lean and a head turn both move it by
// exactly as much as they move the world behind it. Nothing here re-derives the
// camera composition from Euler angles.
namespace votv_ht::aim_projection {

struct View {
    cameraunlock::unreal::FVector Eye{0.0, 0.0, 0.0};
    cameraunlock::unreal::FQuat4d Rotation{0.0, 0.0, 0.0, 1.0};
    float TanX = 0.0f;   // tan(horizontal half field) of the drawn frame
    float TanY = 0.0f;   // tan(vertical half field)
};

struct Ndc {
    bool Valid = false;
    float X = 0.0f;   // right, -1..1 across the frame
    float Y = 0.0f;   // up
};

// NDC of a vector from the eye, in UE axes (x forward, y right, z up).
inline Ndc ProjectVector(const View& view, const cameraunlock::unreal::FVector& v) {
    namespace ue = cameraunlock::unreal;
    Ndc out;
    const double len = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
    if (!(len > 1e-6)) return out;
    const ue::FVector fwd = ue::QuatRotateVec(view.Rotation, ue::FVector{1.0, 0.0, 0.0});
    const ue::FVector right = ue::QuatRotateVec(view.Rotation, ue::FVector{0.0, 1.0, 0.0});
    const ue::FVector up = ue::QuatRotateVec(view.Rotation, ue::FVector{0.0, 0.0, 1.0});
    const float aim[3] = {static_cast<float>(v.X / len), static_cast<float>(v.Y / len),
                          static_cast<float>(v.Z / len)};
    const float f[3] = {static_cast<float>(fwd.X), static_cast<float>(fwd.Y), static_cast<float>(fwd.Z)};
    const float r[3] = {static_cast<float>(right.X), static_cast<float>(right.Y), static_cast<float>(right.Z)};
    const float u[3] = {static_cast<float>(up.X), static_cast<float>(up.Y), static_cast<float>(up.Z)};
    out.Valid = cameraunlock::rendering::ProjectAimToNdc(aim, f, r, u, view.TanX, view.TanY, out.X, out.Y);
    return out;
}

inline Ndc ProjectPoint(const View& view, const cameraunlock::unreal::FVector& point) {
    return ProjectVector(view, cameraunlock::unreal::FVector{
        point.X - view.Eye.X, point.Y - view.Eye.Y, point.Z - view.Eye.Z});
}

// A definite no-hit is a target at infinity: only the direction survives.
inline Ndc ProjectDirection(const View& view, const cameraunlock::unreal::FVector& dir) {
    return ProjectVector(view, dir);
}

inline bool OnScreen(const Ndc& n) {
    return n.Valid && n.X >= -1.0f && n.X <= 1.0f && n.Y >= -1.0f && n.Y <= 1.0f;
}

}  // namespace votv_ht::aim_projection
