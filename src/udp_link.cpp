// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "udp_link.h"

namespace votv_ht::udp_link {

LinkState ClassifyLink(bool retrying, bool running, bool receiving) {
    if (retrying) return LinkState::WaitingForPort;
    if (!running) return LinkState::Down;
    return receiving ? LinkState::Receiving : LinkState::Listening;
}

const char* LinkStateName(LinkState state) {
    switch (state) {
        case LinkState::WaitingForPort: return "waiting-for-port";
        case LinkState::Listening:      return "listening";
        case LinkState::Receiving:      return "receiving";
        case LinkState::Down:           return "down";
    }
    return "unknown";
}

LinkChange LinkWatch::Note(LinkState state, std::int64_t now_ms) {
    LinkChange change;
    if (m_seeded && state == m_state) return change;

    change.changed = true;
    change.from = m_state;
    change.to = state;
    change.held_ms = m_seeded ? now_ms - m_since_ms : 0;

    m_state = state;
    m_since_ms = now_ms;
    m_seeded = true;
    return change;
}

}  // namespace votv_ht::udp_link
