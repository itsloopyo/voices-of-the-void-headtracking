#pragma once

// Stands in for the published build's view_hook.h in the hotkey oracle library only. The
// published hotkey actions flip these two switches; the fake counts each flip so the test can
// see which action a key press ran.

#include "config.h"

namespace votv_ht::view_hook {

// Presses of each action since the test last cleared them: toggle, yaw mode.
int& FakeToggles();
int& FakeYawToggles();

inline bool TrackingEnabled() { return true; }
inline void SetTrackingEnabled(bool) { ++FakeToggles(); }
inline bool WorldSpaceYaw() { return true; }
inline void SetWorldSpaceYaw(bool) { ++FakeYawToggles(); }

}  // namespace votv_ht::view_hook
