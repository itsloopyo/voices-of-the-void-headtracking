// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What the zoom compensation does to the head pose, and what it leaves alone.
//
// The numbers are the ones measured in game: Voices of the Void renders 110
// degrees at the default field-of-view setting, and driving the pawn's Zoom
// timeline narrowed the camera to 55 degrees, which put the factor at 0.3645
// and a 15 degree head yaw on screen at 5.58 degrees of camera yaw.

#include <cmath>

#include "pose_shaping.h"
#include "test_harness.h"

using namespace votv_ht;
namespace shaping = votv_ht::pose_shaping;

namespace {

// The game's un-zoomed field of view at the default setting: the camera
// component's FieldOfView while its Zoom timeline sits at rest.
constexpr float kHipFov = 110.0f;

void OrdinaryPlayIsExactlyUnscaled() {
    CHECK_NEAR_MSG(shaping::ZoomFactor(kHipFov, kHipFov), 1.0f, 1e-6f,
                   "the drawn field of view and the un-zoomed one are the same number at the hip");
    // The release gate: a factor that is not 1.0 with nothing zoomed is the two
    // fields of view being read on different axes.
    CHECK_NEAR(shaping::ZoomFactor(90.0f, 90.0f), 1.0f, 1e-6f);
}

void ANarrowerFieldScalesThePoseDown() {
    const float factor = shaping::ZoomFactor(55.0f, kHipFov);
    CHECK_NEAR_MSG(factor, 0.3645f, 5e-4f, "tan(27.5) / tan(55), measured in game as 0.3645");

    Pose pose{15.0f, 6.0f, 8.0f, 0.15f, 0.0f, 0.0f};
    shaping::ShapePose(pose, 1.0f, factor);
    CHECK_NEAR_MSG(pose.yaw, 5.58f, 0.01f, "15 degrees of head yaw at the zoom, measured in game");
    CHECK_NEAR_MSG(pose.pitch, 2.19f, 0.01f, "6 degrees of head pitch at the same zoom");
    CHECK_NEAR_MSG(pose.x, 0.0547f, 1e-4f, "a 15cm lean scales linearly, unlike the angles");
}

// Roll rotates the image about the view axis rather than moving it across the
// frame, so ten degrees of head roll is ten degrees of picture at every field of
// view there is. Scaling it would flatten a tilt the player is holding.
void RollIsNotScaled() {
    for (float renderFov : {30.0f, 55.0f, kHipFov, 150.0f}) {
        Pose pose{0.0f, 0.0f, 8.0f, 0.0f, 0.0f, 0.0f};
        shaping::ShapePose(pose, 1.0f, shaping::ZoomFactor(renderFov, kHipFov));
        CHECK_NEAR_MSG(pose.roll, 8.0f, 1e-5f, "head roll survives every zoom unchanged");
    }
}

// A field of view that does not read leaves the pose alone rather than scaling
// it by a guess: an unreadable value is 0, and 0 is not an angle.
void AnUnreadableFieldOfViewScalesNothing() {
    CHECK_NEAR(shaping::ZoomFactor(0.0f, kHipFov), 1.0f, 1e-6f);
    CHECK_NEAR(shaping::ZoomFactor(kHipFov, 0.0f), 1.0f, 1e-6f);
    CHECK_NEAR(shaping::ZoomFactor(std::nanf(""), kHipFov), 1.0f, 1e-6f);
    CHECK_NEAR(shaping::ZoomFactor(200.0f, kHipFov), 1.0f, 1e-6f);

    Pose pose{10.0f, 6.0f, 8.0f, 0.15f, -0.05f, 0.2f};
    shaping::ShapePose(pose, 1.0f, shaping::ZoomFactor(0.0f, kHipFov));
    CHECK_NEAR(pose.yaw, 10.0f, 1e-4f);
    CHECK_NEAR(pose.pitch, 6.0f, 1e-4f);
    CHECK_NEAR(pose.x, 0.15f, 1e-6f);
    CHECK_NEAR(pose.y, -0.05f, 1e-6f);
    CHECK_NEAR(pose.z, 0.2f, 1e-6f);
}

// The ease covers the whole pose, roll included: it is the fade from the game's
// own camera onto the head, not a field-of-view correction.
void TheEntryEaseCoversEveryAxis() {
    CHECK_NEAR(shaping::EntryEase(0), 0.0f, 1e-6f);
    CHECK_NEAR(shaping::EntryEase(shaping::kEntryEaseMs / 2), 0.5f, 1e-6f);
    CHECK_NEAR(shaping::EntryEase(shaping::kEntryEaseMs), 1.0f, 1e-6f);
    CHECK_NEAR(shaping::EntryEase(shaping::kEntryEaseMs * 10), 1.0f, 1e-6f);

    Pose pose{10.0f, 6.0f, 8.0f, 0.15f, 0.0f, 0.0f};
    shaping::ShapePose(pose, 0.0f, 1.0f);
    CHECK_NEAR(pose.yaw, 0.0f, 1e-6f);
    CHECK_NEAR(pose.pitch, 0.0f, 1e-6f);
    CHECK_NEAR(pose.roll, 0.0f, 1e-6f);
    CHECK_NEAR(pose.x, 0.0f, 1e-6f);
}

}  // namespace

int main() {
    OrdinaryPlayIsExactlyUnscaled();
    ANarrowerFieldScalesThePoseDown();
    RollIsNotScaled();
    AnUnreadableFieldOfViewScalesNothing();
    TheEntryEaseCoversEveryAxis();
    return gr_test::Report();
}
