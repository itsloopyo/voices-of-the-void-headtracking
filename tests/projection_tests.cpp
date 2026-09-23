// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Where the mark goes: a world point projected from the eye the camera hook
// writes, through the field-of-view model the engine uses. The views here are
// built with the same camera_boundary functions the hook calls, so a change to a
// sign or to the composition moves these numbers.

#include <cmath>
#include <initializer_list>

#include "aim_projection.h"
#include "camera_boundary.h"
#include "camera_fov.h"
#include "test_harness.h"

using namespace votv_ht;
namespace fov = votv_ht::camera_fov;
namespace ue = ::cameraunlock::unreal;

namespace {

constexpr float kDegToRad = 0.01745329252f;
constexpr float k16x9 = 16.0f / 9.0f;
constexpr float k32x9 = 5120.0f / 1440.0f;

// The clean camera sits at the origin looking down +X (UE forward), level.
aim_projection::View TrackedView(double yaw, double pitch, double roll, float x, float y, float z,
                                 int constraint, float fovDegrees, float aspect) {
    ue::FRotator rot{0.0, 0.0, 0.0};
    const ue::FQuat4d cleanQ = ue::QuatFromEulerDeg(rot.Pitch, rot.Yaw, rot.Roll);
    camera_boundary::ApplyHeadPose(rot, yaw, pitch, roll, true);
    const ue::FVector offset = camera_boundary::PositionOffset(cleanQ, x, y, z);
    aim_projection::View v;
    v.Eye = offset;
    v.Rotation = ue::QuatFromEulerDeg(rot.Pitch, rot.Yaw, rot.Roll);
    fov::HalfFieldTangents(constraint, fovDegrees, aspect, v.TanX, v.TanY);
    return v;
}

// The shot lands `distance` cm straight ahead of the clean eye.
bool Project(double yaw, double pitch, double roll, int constraint, float fovDegrees,
             float aspect, float& ndcX, float& ndcY, double distance = 1000.0) {
    const auto view = TrackedView(yaw, pitch, roll, 0.0f, 0.0f, 0.0f, constraint, fovDegrees, aspect);
    const auto n = aim_projection::ProjectPoint(view, ue::FVector{distance, 0.0, 0.0});
    ndcX = n.X;
    ndcY = n.Y;
    return n.Valid;
}

// ---- which axis the engine holds the field of view on ---------------------

void ModelSelection() {
    CHECK(fov::ModelFor(fov::kMaintainXFOV, k16x9) == fov::AspectModel::HorizontalFixed);
    CHECK(fov::ModelFor(fov::kMaintainYFOV, k16x9) == fov::AspectModel::VerticalFixed);
    CHECK(fov::ModelFor(fov::kMajorAxisFOV, k32x9) == fov::AspectModel::HorizontalFixed);
    CHECK(fov::ModelFor(fov::kMajorAxisFOV, 0.5f) == fov::AspectModel::VerticalFixed);
}

// The scalar means a different axis under each constraint, so the two models
// disagree by the whole viewport aspect wherever the viewport is not square.
void TheTwoModelsDifferByTheViewportAspect() {
    float hx = 0.0f, hy = 0.0f, vx = 0.0f, vy = 0.0f;
    CHECK(fov::HalfFieldTangents(fov::kMaintainXFOV, 90.0f, k16x9, hx, hy));
    CHECK(fov::HalfFieldTangents(fov::kMaintainYFOV, 90.0f, k16x9, vx, vy));
    CHECK_NEAR_MSG(hx, 1.0, 1e-5, "horizontal-fixed reads 90 degrees as the horizontal field");
    CHECK_NEAR_MSG(hy, 1.0 / k16x9, 1e-5, "and derives the vertical from the viewport");
    CHECK_NEAR_MSG(vy, 1.0, 1e-5, "vertical-fixed reads the same 90 degrees as the vertical");
    CHECK_NEAR_MSG(vx, k16x9, 1e-5, "and derives the horizontal from the viewport");
    CHECK_NEAR_MSG(vx / hx, k16x9, 1e-4, "a whole aspect apart");
    CHECK_NEAR_MSG(vy / hy, k16x9, 1e-4, "on both axes");
}

// Voices of the Void runs MaintainXFOV: FMinimalViewInfo::FOV is the HORIZONTAL
// field and the vertical narrows as the window widens. Measured in game - see
// the AspectModel comment in camera_fov.h.
void MaintainXFovHoldsTheHorizontalField() {
    float hx = 0.0f, hy = 0.0f;
    CHECK(fov::HalfFieldTangents(fov::kMaintainXFOV, 110.0f, k16x9, hx, hy));
    CHECK_NEAR_MSG(hx, std::tan(55.0f * kDegToRad), 1e-5,
                   "the scalar is the horizontal half field");
    CHECK_NEAR_MSG(hx, 1.4281f, 1e-3, "which is the tan the mod logged in game");
    CHECK_NEAR_MSG(hy, std::tan(55.0f * kDegToRad) / k16x9, 1e-5,
                   "the vertical is that over the viewport aspect");
    CHECK_NEAR_MSG(hy, 0.8033f, 1e-3, "also as logged");
    float wx = 0.0f, wy = 0.0f;
    CHECK(fov::HalfFieldTangents(fov::kMaintainXFOV, 110.0f, k32x9, wx, wy));
    CHECK_NEAR_MSG(wx, hx, 1e-5, "a wider window does not change the horizontal field");
    CHECK_MSG(wy < hy, "it narrows the vertical one");
}

// The two models are a whole aspect apart, so an unread constraint is not
// something to guess at on any window that is not square.
void AnUnknownConstraintProjectsNothing() {
    float tanX = 0.0f, tanY = 0.0f;
    CHECK(!fov::HalfFieldTangents(fov::kConstraintUnknown, 90.0f, k16x9, tanX, tanY));
    CHECK(!fov::HalfFieldTangents(fov::kConstraintUnknown, 90.0f, k32x9, tanX, tanY));
    CHECK(!fov::HalfFieldTangents(fov::kConstraintUnknown, 90.0f, 1.0f, tanX, tanY));
}

void ImplausibleFieldsAreRefused() {
    float tanX = 0.0f, tanY = 0.0f;
    CHECK(!fov::HalfFieldTangents(fov::kMaintainYFOV, 0.0f, k16x9, tanX, tanY));
    CHECK(!fov::HalfFieldTangents(fov::kMaintainYFOV, 200.0f, k16x9, tanX, tanY));
    CHECK(!fov::HalfFieldTangents(fov::kMaintainYFOV, std::nanf(""), k16x9, tanX, tanY));
    CHECK(!fov::HalfFieldTangents(fov::kMaintainYFOV, 90.0f, 0.0f, tanX, tanY));
    CHECK(fov::Plausible(90.0f));
    CHECK(!fov::Plausible(std::nanf("")));
}

// ---- head rotation --------------------------------------------------------

void CentredHeadPutsTheMarkAtTheCentre() {
    float ndcX = 1.0f, ndcY = 1.0f;
    CHECK(Project(0.0, 0.0, 0.0, fov::kMaintainYFOV, 90.0f, k16x9, ndcX, ndcY));
    CHECK_NEAR(ndcX, 0.0, 1e-6);
    CHECK_NEAR(ndcY, 0.0, 1e-6);
}

void TheMarkGoesTheOppositeWayToTheView() {
    float ndcX = 0.0f, ndcY = 0.0f;
    CHECK(Project(20.0, 0.0, 0.0, fov::kMaintainYFOV, 90.0f, k16x9, ndcX, ndcY));
    CHECK_MSG(ndcX < 0.0f, "view turned right leaves the aim left of centre");
    CHECK_NEAR_MSG(ndcY, 0.0, 1e-5, "a pure turn does not move the mark vertically");

    CHECK(Project(0.0, 15.0, 0.0, fov::kMaintainYFOV, 90.0f, k16x9, ndcX, ndcY));
    CHECK_MSG(ndcY < 0.0f, "view pitched up leaves the aim below centre");
    CHECK_NEAR_MSG(ndcX, 0.0, 1e-5, "a pure pitch does not move the mark sideways");
}

void TheEdgeOfTheFrameIsHalfTheFieldOfView() {
    float ndcX = 0.0f, ndcY = 0.0f;
    CHECK(Project(45.0, 0.0, 0.0, fov::kMaintainXFOV, 90.0f, k16x9, ndcX, ndcY));
    CHECK_NEAR_MSG(ndcX, -1.0, 1e-4, "half the horizontal field lands on the frame edge");
    CHECK(Project(45.0, 0.0, 0.0, fov::kMaintainXFOV, 120.0f, k16x9, ndcX, ndcY));
    CHECK_NEAR(ndcX, -std::tan(45.0f * kDegToRad) / std::tan(60.0f * kDegToRad), 1e-4);
}

void PureRollKeepsTheCentre() {
    float ndcX = 1.0f, ndcY = 1.0f;
    CHECK(Project(0.0, 0.0, 25.0, fov::kMaintainYFOV, 90.0f, k16x9, ndcX, ndcY));
    CHECK_NEAR(ndcX, 0.0, 1e-6);
    CHECK_NEAR(ndcY, 0.0, 1e-6);
}

// At a square viewport screen distance is angular distance, so the roll that
// follows a pitch must carry the offset round the centre without changing its
// length.
void PitchAndRollRotateTheOffsetAboutTheCentre() {
    float px = 0.0f, py = 0.0f, rx = 0.0f, ry = 0.0f;
    CHECK(Project(0.0, 12.0, 0.0, fov::kMaintainYFOV, 90.0f, 1.0f, px, py));
    CHECK(Project(0.0, 12.0, 30.0, fov::kMaintainYFOV, 90.0f, 1.0f, rx, ry));
    CHECK_NEAR(std::hypot(rx, ry), std::hypot(px, py), 1e-4);
    CHECK_MSG(std::fabs(rx) > 0.01f, "roll carries a pitch offset sideways");
}

void TheAspectModelMovesTheMarkOnAnUltrawide() {
    float horizontalFixed = 0.0f, verticalFixed = 0.0f, ndcY = 0.0f;
    CHECK(Project(20.0, 0.0, 0.0, fov::kMaintainXFOV, 90.0f, k32x9, horizontalFixed, ndcY));
    CHECK(Project(20.0, 0.0, 0.0, fov::kMaintainYFOV, 90.0f, k32x9, verticalFixed, ndcY));
    CHECK_NEAR(horizontalFixed / verticalFixed, k32x9, 1e-3);
}

void AimBehindTheViewProjectsNothing() {
    float ndcX = 0.0f, ndcY = 0.0f;
    CHECK(!Project(120.0, 0.0, 0.0, fov::kMaintainYFOV, 90.0f, k16x9, ndcX, ndcY));
}

// ---- head position ----------------------------------------------------------

// A lean moves the eye and not the gun, so the mark moves by exactly the
// parallax of the point it marks, at every distance. A reticle built on a
// direction, or on a fixed depth, fails this at all but one of them.
void LeanMovesTheMarkByTheParallaxAtEveryDistance() {
    for (const double distance : {60.0, 400.0, 1800.0}) {
        // Tracker x is mirrored at the boundary: +0.2 m puts the eye 20 cm to
        // the camera's left, so the point ahead sits right of centre.
        const auto left = TrackedView(0.0, 0.0, 0.0, 0.2f, 0.0f, 0.0f, fov::kMaintainYFOV, 90.0f, k16x9);
        const auto n = aim_projection::ProjectPoint(left, ue::FVector{distance, 0.0, 0.0});
        CHECK(n.Valid);
        CHECK_NEAR(n.X, (20.0 / distance) / left.TanX, 1e-5);
        CHECK_NEAR(n.Y, 0.0, 1e-6);

        const auto up = TrackedView(0.0, 0.0, 0.0, 0.0f, 0.15f, 0.0f, fov::kMaintainYFOV, 90.0f, k16x9);
        const auto m = aim_projection::ProjectPoint(up, ue::FVector{distance, 0.0, 0.0});
        CHECK(m.Valid);
        CHECK_NEAR(m.Y, -(15.0 / distance) / up.TanY, 1e-5);
        CHECK_NEAR(m.X, 0.0, 1e-6);
    }
}

// A definite no-hit projects the direction, which is where the point at
// infinity along it lands under any pose.
void ADirectionIsThePointAtInfinity() {
    const auto view = TrackedView(12.0, -7.0, 18.0, 0.1f, 0.05f, 0.0f, fov::kMaintainYFOV, 90.0f, k32x9);
    const ue::FVector dir{0.99, 0.1, -0.05};
    const auto far = aim_projection::ProjectPoint(view, ue::FVector{dir.X * 1e9, dir.Y * 1e9, dir.Z * 1e9});
    const auto inf = aim_projection::ProjectDirection(view, dir);
    CHECK(far.Valid && inf.Valid);
    CHECK_NEAR(far.X, inf.X, 1e-5);
    CHECK_NEAR(far.Y, inf.Y, 1e-5);
}

}  // namespace

int main() {
    ModelSelection();
    TheTwoModelsDifferByTheViewportAspect();
    MaintainXFovHoldsTheHorizontalField();
    AnUnknownConstraintProjectsNothing();
    ImplausibleFieldsAreRefused();
    CentredHeadPutsTheMarkAtTheCentre();
    TheMarkGoesTheOppositeWayToTheView();
    TheEdgeOfTheFrameIsHalfTheFieldOfView();
    PureRollKeepsTheCentre();
    PitchAndRollRotateTheOffsetAboutTheCentre();
    TheAspectModelMovesTheMarkOnAnUltrawide();
    AimBehindTheViewProjectsNothing();
    LeanMovesTheMarkByTheParallaxAtEveryDistance();
    ADirectionIsThePointAtInfinity();
    return gr_test::Report();
}
