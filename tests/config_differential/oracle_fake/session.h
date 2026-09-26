#pragma once

// Stands in for the published build's session.h in the hotkey oracle library only: the one
// member the published hotkeys call, counting each call.

namespace cameraunlock {

enum class TrackingMode {
    RotationAndPosition = 0,
    RotationOnly = 1,
    PositionOnly = 2,
};

}  // namespace cameraunlock

namespace votv_ht {

struct Session {
    int cycles = 0;
    cameraunlock::TrackingMode CycleMode() {
        ++cycles;
        return cameraunlock::TrackingMode::RotationAndPosition;
    }
};

}  // namespace votv_ht
