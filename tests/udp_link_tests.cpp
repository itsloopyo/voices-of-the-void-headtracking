// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The UDP link as it reads from outside, and what gets written to the log when
// it moves.
//
// The case these lock is the one a player actually hits: this game is launched
// with the previous one still running, so the port is held; the player then
// closes the other game and the link has to come up on its own. What that walk
// through ClassifyLink and LinkWatch puts in the log is here; how long the
// receiver underneath takes to do it is measured in udp_recovery_tests.cpp.

#include "test_harness.h"
#include "udp_link.h"

namespace {

using votv_ht::udp_link::ClassifyLink;
using votv_ht::udp_link::LinkChange;
using votv_ht::udp_link::LinkState;
using votv_ht::udp_link::LinkStateName;
using votv_ht::udp_link::LinkWatch;

// A receiver waiting for the port reports retrying, not running and not
// receiving all at once. Only the first of those tells the player anything, so
// it has to win.
void TestABusyPortReadsAsWaitingNotDown() {
    CHECK(ClassifyLink(true, false, false) == LinkState::WaitingForPort);
}

void TestBoundWithNoTrafficIsListening() {
    CHECK(ClassifyLink(false, true, false) == LinkState::Listening);
}

void TestBoundAndFedIsReceiving() {
    CHECK(ClassifyLink(false, true, true) == LinkState::Receiving);
}

// Before Start() and after Stop(): nothing bound and nothing retrying. Distinct
// from waiting-for-port, because this one never recovers on its own.
void TestNeitherBoundNorRetryingIsDown() {
    CHECK(ClassifyLink(false, false, false) == LinkState::Down);
}

void TestEveryStateHasAName() {
    const LinkState all[] = {LinkState::WaitingForPort, LinkState::Listening,
                             LinkState::Receiving, LinkState::Down};
    for (LinkState s : all) {
        const char* name = LinkStateName(s);
        CHECK(name != nullptr);
        CHECK(name[0] != '\0');
    }
}

// The session's first sample has no previous state, and would otherwise be
// silent whenever it happened to match the initial value - so a mod that came up
// already down would never say so.
void TestTheFirstSampleAlwaysReports() {
    LinkWatch watch;
    const LinkChange first = watch.Note(LinkState::Down, 1000);
    CHECK(first.changed);
    CHECK(first.to == LinkState::Down);
    CHECK_MSG(first.held_ms == 0, "the first sample has no previous state to have held");
}

void TestASteadyLinkIsSilent() {
    LinkWatch watch;
    watch.Note(LinkState::Receiving, 0);
    for (int64_t t = 250; t <= 60000; t += 250) {
        CHECK(!watch.Note(LinkState::Receiving, t).changed);
    }
}

// The scenario in full: the port is held by the game the player forgot to close,
// they close it, the bind succeeds and the tracker's packets arrive. One line for
// the wait, one for the bind, one for the traffic.
void TestPortHeldThenReleasedWalksToReceiving() {
    LinkWatch watch;

    const LinkChange waiting = watch.Note(LinkState::WaitingForPort, 0);
    CHECK(waiting.changed);
    CHECK(waiting.to == LinkState::WaitingForPort);

    for (int64_t t = 250; t < 47000; t += 250) {
        CHECK(!watch.Note(LinkState::WaitingForPort, t).changed);
    }

    const LinkChange bound = watch.Note(LinkState::Listening, 47000);
    CHECK(bound.changed);
    CHECK(bound.from == LinkState::WaitingForPort);
    CHECK(bound.to == LinkState::Listening);
    CHECK_MSG(bound.held_ms == 47000,
              "the held time is the whole wait, not the interval since the last sample");

    const LinkChange fed = watch.Note(LinkState::Receiving, 47250);
    CHECK(fed.changed);
    CHECK(fed.from == LinkState::Listening);
    CHECK(fed.to == LinkState::Receiving);
    CHECK(fed.held_ms == 250);
}

// A tracker that stops sending and starts again is a change each way. Both
// matter: the first says the tracker went quiet with the port still ours, which
// is a different fault from the port being taken.
void TestTrafficStoppingAndResumingBothReport() {
    LinkWatch watch;
    watch.Note(LinkState::Receiving, 0);

    const LinkChange quiet = watch.Note(LinkState::Listening, 5000);
    CHECK(quiet.changed);
    CHECK(quiet.from == LinkState::Receiving);
    CHECK(quiet.held_ms == 5000);

    const LinkChange back = watch.Note(LinkState::Receiving, 9000);
    CHECK(back.changed);
    CHECK(back.from == LinkState::Listening);
    CHECK(back.held_ms == 4000);
}

// The receive thread dying on a socket error puts the receiver back into the
// retry loop. That is the same recoverable state as a busy port at startup, and
// it has to read as such rather than as a fresh, silent session.
void TestLosingTheSocketMidSessionReturnsToWaiting() {
    LinkWatch watch;
    watch.Note(LinkState::Receiving, 0);

    const LinkChange lost = watch.Note(LinkState::WaitingForPort, 30000);
    CHECK(lost.changed);
    CHECK(lost.from == LinkState::Receiving);
    CHECK(lost.to == LinkState::WaitingForPort);

    const LinkChange regained = watch.Note(LinkState::Receiving, 30500);
    CHECK(regained.changed);
    CHECK(regained.held_ms == 500);
}

}  // namespace

int main() {
    TestABusyPortReadsAsWaitingNotDown();
    TestBoundWithNoTrafficIsListening();
    TestBoundAndFedIsReceiving();
    TestNeitherBoundNorRetryingIsDown();
    TestEveryStateHasAName();
    TestTheFirstSampleAlwaysReports();
    TestASteadyLinkIsSilent();
    TestPortHeldThenReleasedWalksToReceiving();
    TestTrafficStoppingAndResumingBothReport();
    TestLosingTheSocketMidSessionReturnsToWaiting();

    return gr_test::Report();
}
