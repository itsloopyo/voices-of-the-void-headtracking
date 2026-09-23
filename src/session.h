// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/tracking/head_tracking_session.h"

namespace votv_ht {

// The one tracking session the mod runs: core's session over the OpenTrack UDP
// receiver. Named once here so the hook, the hotkeys and the bootstrap all
// speak about the same type instead of each re-spelling the template.
using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;

// The receiver drops a datagram whose values are not finite floats, and that is
// all the range checking the pose gets before it reaches the hook. Two finite
// samples at opposite ends of the float range overflow the interpolator's
// (to - from), and the session's output is then inf or NaN for the rest of the
// session. Whatever sends to the port can do that, so the hook asks this before
// anything the session produced is written into the engine's view.
inline bool Finite3(float a, float b, float c) {
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
}

// One frame's head pose at the engine boundary: rotation in degrees, position
// in the processor's metres. Filled from the session and then shaped (entry
// ease, zoom compensation) before it reaches the camera.
struct Pose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

}  // namespace votv_ht
