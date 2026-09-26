// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Whether the head pose reaches the view, and the reason logged when it does
// not. The order the gate is asked in decides which reason a frame reports when
// more than one applies, and the heartbeat line is read against that reason, so
// the order is locked here.

#include "test_harness.h"
#include "tracking_gate.h"

#include <cstring>

using namespace votv_ht;

namespace {

game_state::Verdict Gate(bool inGameplay) {
    game_state::Verdict v;
    v.InGameplay = inGameplay;
    v.Why = inGameplay ? game_state::Blocker::None : game_state::Blocker::Paused;
    return v;
}

void TestOnlyAFullyOpenGateApplies() {
    CHECK(DecideTracking(Gate(true), true, true) == TrackingVerdict::Active);
    CHECK(PoseApplies(TrackingVerdict::Active));
    CHECK(!PoseApplies(TrackingVerdict::Disabled));
    CHECK(!PoseApplies(TrackingVerdict::NotGameplay));
    CHECK(!PoseApplies(TrackingVerdict::NoTracker));
}

// The master toggle is asked first, then gameplay, then the tracker.
void TestTheOrderTheGateIsAskedIn() {
    CHECK_MSG(DecideTracking(Gate(false), false, false) == TrackingVerdict::Disabled,
              "toggled off outranks every other reason");
    CHECK(DecideTracking(Gate(true), false, true) == TrackingVerdict::Disabled);
    CHECK_MSG(DecideTracking(Gate(false), true, false) == TrackingVerdict::NotGameplay,
              "a menu outranks a missing tracker");
    CHECK(DecideTracking(Gate(false), true, true) == TrackingVerdict::NotGameplay);
    CHECK(DecideTracking(Gate(true), true, false) == TrackingVerdict::NoTracker);
}

void TestOnlyOnePlayerStateIsSolo() {
    CHECK_MSG(!game_state::SessionIsShared(1), "one player state is solo");
    CHECK_MSG(game_state::SessionIsShared(2), "two players is a shared session");
    CHECK_MSG(game_state::SessionIsShared(4), "so is a full one");
    CHECK_MSG(game_state::SessionIsShared(0), "an empty game state is not solo either");
    CHECK_MSG(game_state::SessionIsShared(-1),
              "a count that did not read leaves the game stock");
}

void TestWorldInterfacesKeepTrackingWithoutOpeningMenusOrIncapacitatedStates() {
    using game_state::Blocker;
    using game_state::ControlBlocker;
    player_rig::Snapshot rig;
    rig.HaveState = true;
    CHECK(ControlBlocker(rig, false, false) == Blocker::None);
    CHECK(ControlBlocker(rig, true, false) == Blocker::Cursor);
    rig.MouseInputOff = true;
    CHECK(ControlBlocker(rig, false, false) == Blocker::NotInControl);
    rig.WorldInterface = true;
    CHECK(ControlBlocker(rig, true, false) == Blocker::None);
    CHECK(ControlBlocker(rig, false, false) == Blocker::None);
    CHECK(ControlBlocker(rig, true, true) == Blocker::Paused);
    bool* states[] = {&rig.Dead, &rig.Ragdoll, &rig.WakingUp};
    for (bool* state : states) {
        *state = true;
        CHECK(ControlBlocker(rig, true, false) == Blocker::NotInControl);
        *state = false;
    }
    rig.HaveState = false;
    CHECK(ControlBlocker(rig, true, false) == Blocker::NotInControl);
}

void TestEveryVerdictHasItsLogReason() {
    CHECK(std::strcmp(Reason(TrackingVerdict::Active), "gameplay") == 0);
    CHECK(std::strcmp(Reason(TrackingVerdict::Disabled), "tracking toggled off") == 0);
    CHECK(std::strcmp(Reason(TrackingVerdict::NotGameplay), "menu or loading") == 0);
    CHECK(std::strcmp(Reason(TrackingVerdict::NoTracker), "no tracker data") == 0);
}

}  // namespace

int main() {
    TestOnlyAFullyOpenGateApplies();
    TestTheOrderTheGateIsAskedIn();
    TestOnlyOnePlayerStateIsSolo();
    TestWorldInterfacesKeepTrackingWithoutOpeningMenusOrIncapacitatedStates();
    TestEveryVerdictHasItsLogReason();
    return gr_test::Report();
}
