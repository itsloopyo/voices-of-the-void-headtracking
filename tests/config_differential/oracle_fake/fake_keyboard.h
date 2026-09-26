#pragma once

// Force-included into the hotkey oracle library only (/FI), ahead of every header, with that
// library's renaming. It declares GetAsyncKeyState in the namespace chord_hotkeys.h's
// IsChordHeld is defined in, so that function's unqualified call finds this one before
// ::GetAsyncKeyState. That is how the test holds modifiers without a keyboard. IsChordHeld is an
// inline function, not a template, so the declaration has to be seen before chord_hotkeys.h is,
// which only a forced include guarantees.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cameraunlock::input {

// The modifiers the test holds: 1 Ctrl, 2 Shift, 4 Alt.
int& FakeHeld();

inline SHORT GetAsyncKeyState(int vk) {
    const int held = FakeHeld();
    const bool down = (vk == VK_CONTROL && (held & 1) != 0) || (vk == VK_SHIFT && (held & 2) != 0) ||
                      (vk == VK_MENU && (held & 4) != 0);
    return down ? static_cast<SHORT>(0x8000) : static_cast<SHORT>(0);
}

}  // namespace cameraunlock::input
