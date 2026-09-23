// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// How the UDP link reads from outside, and when a change in it is worth a line
// in the log.
//
// Pure, with no Windows and no receiver behind it, so the transitions can be
// exercised without a socket. What the receiver does underneath - how long a
// held port takes to come back and how fast a pose follows it - is measured
// against a real socket in tests/udp_recovery_tests.cpp instead.
namespace votv_ht::udp_link {

enum class LinkState {
    // The bind is failing, and the receiver's supervisor is retrying it every
    // 500ms until it succeeds. Which failure it is, only the receiver's own log
    // line says - it carries the code and text the OS gave. Nothing here acts on
    // the state; it is reported because the usual cause, another program already
    // on the port, is the one the player can fix.
    WaitingForPort,
    // Bound, with no packet inside the receiver's freshness window. The tracker
    // app is not running, or is not pointed at this port.
    Listening,
    // Bound and fed.
    Receiving,
    // Neither bound nor retrying: before Start(), and after Stop().
    Down,
};

// Classify the three flags cameraunlock::UdpReceiver reports.
//
// Retrying is asked first because it is not exclusive with the other two in any
// useful way: a receiver waiting for the port is also not running and not
// receiving, and of the three answers that produces, only "another app has the
// port" tells the player what to do.
LinkState ClassifyLink(bool retrying, bool running, bool receiving);

// Never null.
const char* LinkStateName(LinkState state);

struct LinkChange {
    bool changed = false;
    LinkState from = LinkState::Down;
    LinkState to = LinkState::Down;
    // How long `from` had been in force. This is the number that answers "how
    // long was the port held" without the reader diffing two log timestamps.
    // Zero on the first sample, which has no previous state to have held.
    std::int64_t held_ms = 0;
};

// Edge detector over the sampled state. The first sample always reports a
// change, so a session that comes up already listening says so once rather than
// staying silent until something goes wrong.
class LinkWatch {
public:
    LinkChange Note(LinkState state, std::int64_t now_ms);

private:
    LinkState m_state = LinkState::Down;
    bool m_seeded = false;
    std::int64_t m_since_ms = 0;
};

}  // namespace votv_ht::udp_link
