// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Behaviour locks for the two pieces of the mod that are pure functions of
// their arguments: the tracker-to-Unreal camera boundary, and the
// GetPlayerViewPoint caller gate.
//
// The numbers here are not derived from first principles - they are the numbers
// the shipped mod produced before the code was reorganised, recorded so that a
// later change to signs, axis mapping or composition order cannot pass quietly.
// A failure means the view, the aim decoupling has moved.

#include <cmath>

#include "camera_boundary.h"
#include "inject_mode.h"
#include "test_harness.h"

namespace {

namespace ue = ::cameraunlock::unreal;
using votv_ht::camera_boundary::ApplyHeadPose;
using votv_ht::camera_boundary::PositionOffset;

// The position offsets come in as floats, so their exact decimal value is
// already a few parts in 10^7 off before any arithmetic happens; those checks
// pass the looser tolerance. The rotations are doubles throughout.
constexpr double kAngleTolerance = 1e-6;
constexpr double kOffsetToleranceCm = 1e-4;

// ---- camera boundary: rotation -------------------------------------------

void WorldYawAddsPoseAndNegatesRoll() {
    ue::FRotator r{10.0, 20.0, 30.0};
    ApplyHeadPose(r, 5.0, 3.0, 7.0, /*worldSpaceYaw=*/true);
    CHECK_NEAR_MSG(r.Pitch, 13.0, kAngleTolerance, "world yaw: pitch adds");
    CHECK_NEAR_MSG(r.Yaw,   25.0, kAngleTolerance, "world yaw: yaw adds");
    CHECK_NEAR_MSG(r.Roll,  23.0, kAngleTolerance, "world yaw: roll subtracts");
}

void ZeroPoseIsIdentityInBothModes() {
    ue::FRotator world{12.0, -34.0, 5.0};
    ApplyHeadPose(world, 0.0, 0.0, 0.0, true);
    CHECK_NEAR_MSG(world.Pitch, 12.0, kAngleTolerance, "world yaw: zero pose leaves pitch");
    CHECK_NEAR_MSG(world.Yaw,  -34.0, kAngleTolerance, "world yaw: zero pose leaves yaw");
    CHECK_NEAR_MSG(world.Roll,   5.0, kAngleTolerance, "world yaw: zero pose leaves roll");

    ue::FRotator local{12.0, -34.0, 5.0};
    ApplyHeadPose(local, 0.0, 0.0, 0.0, false);
    CHECK_NEAR_MSG(local.Pitch, 12.0, kAngleTolerance, "local yaw: zero pose leaves pitch");
    CHECK_NEAR_MSG(local.Yaw,  -34.0, kAngleTolerance, "local yaw: zero pose leaves yaw");
    CHECK_NEAR_MSG(local.Roll,   5.0, kAngleTolerance, "local yaw: zero pose leaves roll");
}

// From a level camera the two modes agree: camera-local up is world up, so the
// quaternion compose and the FRotator addition describe the same rotation. They
// only part company once the base rotation is pitched, which is the whole reason
// the yaw-mode toggle exists.
void ModesAgreeOnLevelBaseAndDivergeWhenPitched() {
    ue::FRotator world{0.0, 40.0, 0.0};
    ue::FRotator local = world;
    ApplyHeadPose(world, 15.0, 10.0, 0.0, true);
    ApplyHeadPose(local, 15.0, 10.0, 0.0, false);
    CHECK_NEAR_MSG(local.Pitch, world.Pitch, kAngleTolerance, "level base: pitch agrees across modes");
    CHECK_NEAR_MSG(local.Yaw,   world.Yaw, kAngleTolerance, "level base: yaw agrees across modes");
    CHECK_NEAR_MSG(local.Roll,  world.Roll, kAngleTolerance, "level base: roll agrees across modes");

    ue::FRotator pitchedWorld{-50.0, 40.0, 0.0};
    ue::FRotator pitchedLocal = pitchedWorld;
    ApplyHeadPose(pitchedWorld, 30.0, 0.0, 0.0, true);
    ApplyHeadPose(pitchedLocal, 30.0, 0.0, 0.0, false);
    CHECK_MSG(std::fabs(pitchedLocal.Roll - pitchedWorld.Roll) > 1.0,
          "pitched base: camera-local yaw leans the horizon, world yaw does not");
    CHECK_NEAR_MSG(pitchedWorld.Roll, 0.0, kAngleTolerance, "pitched base: world yaw keeps the horizon level");
}

// The composition the shipped mod renders with, recorded angle for angle.
void LocalYawCompositionIsUnchanged() {
    ue::FRotator r{-20.0, 35.0, 4.0};
    ApplyHeadPose(r, 12.0, 8.0, 6.0, false);
    CHECK_NEAR_MSG(r.Pitch, -12.375926322, kAngleTolerance, "local yaw: pitch");
    CHECK_NEAR_MSG(r.Yaw,    47.721496771, kAngleTolerance, "local yaw: yaw");
    CHECK_NEAR_MSG(r.Roll,   -6.410190882, kAngleTolerance, "local yaw: roll");
}

// ---- camera boundary: position -------------------------------------------

// With the camera at the world origin rotation, the three tracker axes land on
// the engine axes the mod's sign notes describe: -z is the forward lean, x is
// mirrored, y is up. Metres in, centimetres out.
void PositionOffsetMapsTrackerAxesToUnreal() {
    const ue::FQuat4d identity{0.0, 0.0, 0.0, 1.0};

    const ue::FVector surge = PositionOffset(identity, 0.0f, 0.0f, -0.40f);
    CHECK_NEAR_MSG(surge.X, 40.0, kOffsetToleranceCm, "surge: negative tracker z leans forward, in cm");
    CHECK_NEAR_MSG(surge.Y,  0.0, kOffsetToleranceCm, "surge: no sway");
    CHECK_NEAR_MSG(surge.Z,  0.0, kOffsetToleranceCm, "surge: no heave");

    const ue::FVector sway = PositionOffset(identity, 0.30f, 0.0f, 0.0f);
    CHECK_NEAR_MSG(sway.X,   0.0, kOffsetToleranceCm, "sway: no surge");
    CHECK_NEAR_MSG(sway.Y, -30.0, kOffsetToleranceCm, "sway: tracker x is mirrored against engine right");
    CHECK_NEAR_MSG(sway.Z,   0.0, kOffsetToleranceCm, "sway: no heave");

    const ue::FVector heave = PositionOffset(identity, 0.0f, 0.20f, 0.0f);
    CHECK_NEAR_MSG(heave.X,  0.0, kOffsetToleranceCm, "heave: no surge");
    CHECK_NEAR_MSG(heave.Y,  0.0, kOffsetToleranceCm, "heave: no sway");
    CHECK_NEAR_MSG(heave.Z, 20.0, kOffsetToleranceCm, "heave: tracker y maps straight to engine up");
}

// The offset is built in the CLEAN camera frame, so it follows the body: yaw the
// camera 90 degrees and a forward lean has to come out along engine +Y.
void PositionOffsetFollowsTheCameraBasis() {
    const ue::FQuat4d yawed = ue::QuatFromEulerDeg(0.0, 90.0, 0.0);
    const ue::FVector surge = PositionOffset(yawed, 0.0f, 0.0f, -0.40f);
    CHECK_NEAR_MSG(surge.X,  0.0, kOffsetToleranceCm, "yawed surge: nothing left on engine X");
    CHECK_NEAR_MSG(surge.Y, 40.0, kOffsetToleranceCm, "yawed surge: forward lean follows the camera");
    CHECK_NEAR_MSG(surge.Z,  0.0, kOffsetToleranceCm, "yawed surge: nothing on engine Z");
}

// ---- caller gate ----------------------------------------------------------

void InjectGateSelectsOnlyItsOwnCaller() {
    votv_ht::inject::CallerRvas callers{};
    callers[0] = 0x038d91ec;  // the render caller
    callers[1] = 0x03b1047f;

    using namespace votv_ht::inject;
    CHECK_MSG(ShouldInject(0x038d91ec, kFirstCaller, callers), "gate: render caller injects");
    CHECK_MSG(!ShouldInject(0x03b1047f, kFirstCaller, callers), "gate: other callers stay clean");
    CHECK_MSG(ShouldInject(0x03b1047f, kFirstCaller + 1, callers), "gate: mode 2 selects caller 2");

    CHECK_MSG(!ShouldInject(0x038d91ec, kAllCallers, callers), "gate: all-callers discovery mode injects nowhere");
    CHECK_MSG(!ShouldInject(0x038d91ec, kNone, callers), "gate: none mode injects nowhere");

    // An empty slot must never match, or a profile with fewer callers than slots
    // would inject for every caller whose return RVA failed to resolve.
    CHECK_MSG(!ShouldInject(0x0, kFirstCaller + 5, callers), "gate: empty slot never matches");
}

void InjectCallerRvaOnlyResolvesForSingleCallerModes() {
    using namespace votv_ht::inject;
    votv_ht::inject::CallerRvas callers{};
    callers[0] = 0x038d91ec;
    callers[kCallerSlots - 1] = 0x03b1047f;
    CHECK_MSG(CallerRva(kFirstCaller, callers) == 0x038d91ec, "rva: single-caller mode resolves");
    CHECK_MSG(CallerRva(kAllCallers, callers) == 0, "rva: all-callers mode has no single RVA");
    CHECK_MSG(CallerRva(kNone, callers) == 0, "rva: none mode has no RVA");
    // One mode per slot, so the mode below kNone is the last slot. Without this
    // an off-by-one in kNone would leave the last caller unreachable and read
    // as the off mode.
    CHECK_MSG(CallerRva(kNone - 1, callers) == 0x03b1047f, "rva: the last mode is the last slot");
}

}  // namespace

int main() {
    WorldYawAddsPoseAndNegatesRoll();
    ZeroPoseIsIdentityInBothModes();
    ModesAgreeOnLevelBaseAndDivergeWhenPitched();
    LocalYawCompositionIsUnchanged();
    PositionOffsetMapsTrackerAxesToUnreal();
    PositionOffsetFollowsTheCameraBasis();
    InjectGateSelectsOnlyItsOwnCaller();
    InjectCallerRvaOnlyResolvesForSingleCallerModes();
    return gr_test::Report();
}
