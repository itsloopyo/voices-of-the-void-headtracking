// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What the session hands the render hook when the tracker port is fed finite
// but absurd numbers.
//
// The receiver drops a datagram whose values are not finite floats, and that is
// the whole of its range check. Two finite samples at opposite ends of the float
// range - which any host that can reach the port can send - overflow the
// interpolator's (to - from) and the session's pose goes non-finite, and stays
// that way. These tests pin that the session really does that, so the guard in
// the render hook is not defending against nothing, and that the guard says no
// to exactly those poses and yes to every real one.

#include <cstdint>
#include <limits>

#include "session.h"
#include "test_harness.h"

namespace {

// Just enough of a receiver for HeadTrackingSession: a pose, and a timestamp
// that moves whenever a new "packet" lands.
struct FakeReceiver {
    float Yaw = 0.0f, Pitch = 0.0f, Roll = 0.0f;
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    std::int64_t Stamp = 0;

    void Send(float yaw, float pitch, float roll, float x, float y, float z) {
        Yaw = yaw; Pitch = pitch; Roll = roll;
        X = x; Y = y; Z = z;
        ++Stamp;
    }

    bool GetRotation(float& yaw, float& pitch, float& roll) const {
        yaw = Yaw; pitch = Pitch; roll = Roll;
        return Stamp != 0;
    }
    bool GetPosition(float& x, float& y, float& z) const {
        x = X; y = Y; z = Z;
        return Stamp != 0;
    }
    std::int64_t GetLastReceiveTimestamp() const { return Stamp; }
    void Recenter() {}
};

using FakeSession = cameraunlock::HeadTrackingSession<FakeReceiver>;

constexpr float kHuge = 3.0e38f;
constexpr float kFrame = 1.0f / 60.0f;

// Alternate the two ends of the float range for a few frames, the way a hostile
// sender would, and report whether the session's pose ever stopped being finite.
bool SessionGoesNonFinite(float FakeReceiver::*axis) {
    FakeReceiver rx;
    FakeSession session(rx);
    bool nonFinite = false;
    for (int i = 0; i < 8; ++i) {
        rx.Send(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        rx.*axis = (i % 2 == 0) ? kHuge : -kHuge;
        session.Update(kFrame);
        float yaw = 0, pitch = 0, roll = 0, x = 0, y = 0, z = 0;
        if (session.GetRotation(yaw, pitch, roll) && !votv_ht::Finite3(yaw, pitch, roll))
            nonFinite = true;
        if (session.GetPositionOffset(x, y, z) && !votv_ht::Finite3(x, y, z)) nonFinite = true;
    }
    return nonFinite;
}

void TestFiniteExtremesPoisonTheSession() {
    CHECK_MSG(SessionGoesNonFinite(&FakeReceiver::Pitch),
              "alternating +/-3e38 pitch drives the session's pose non-finite");
}

void TestTheGuard() {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(votv_ht::Finite3(0.0f, 0.0f, 0.0f));
    CHECK(votv_ht::Finite3(179.9f, -89.9f, -179.9f));
    CHECK(votv_ht::Finite3(kHuge, -kHuge, kHuge));
    CHECK(!votv_ht::Finite3(inf, 0.0f, 0.0f));
    CHECK(!votv_ht::Finite3(0.0f, -inf, 0.0f));
    CHECK(!votv_ht::Finite3(0.0f, 0.0f, nan));
}

// A real tracker's stream never trips it.
void TestAnOrdinaryStreamStaysFinite() {
    FakeReceiver rx;
    FakeSession session(rx);
    bool allFinite = true;
    for (int i = 0; i < 240; ++i) {
        const float t = static_cast<float>(i) / 240.0f;
        rx.Send(-170.0f + 340.0f * t, 60.0f - 120.0f * t, 30.0f * t, 25.0f * t, -10.0f * t, 40.0f * t);
        session.Update(kFrame);
        float yaw = 0, pitch = 0, roll = 0, x = 0, y = 0, z = 0;
        if (!session.GetRotation(yaw, pitch, roll) || !votv_ht::Finite3(yaw, pitch, roll))
            allFinite = false;
        if (session.GetPositionOffset(x, y, z) && !votv_ht::Finite3(x, y, z)) allFinite = false;
    }
    CHECK_MSG(allFinite, "a sweep across the full range of a real head stays finite");
}

}  // namespace

int main() {
    TestFiniteExtremesPoisonTheSession();
    TestTheGuard();
    TestAnOrdinaryStreamStaysFinite();
    return gr_test::Report();
}
