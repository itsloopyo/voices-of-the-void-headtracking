#pragma once

// Stands in for core's HotkeyPoller in the hotkey oracle library only, so the published
// build's hotkeys::Register (oracle/src/mod_hotkeys.cpp, verbatim) runs with no polling thread
// and the test sees what it registered. Compiled with that library's renaming, so it cannot
// collide with the real poller the current core links.

#include <functional>
#include <vector>

namespace cameraunlock::input {

using HotkeyCallback = std::function<void()>;

struct FakeRegistration {
    int vk;
    HotkeyCallback callback;
};

// Every registration any poller made since the test last cleared it, in order.
std::vector<FakeRegistration>& FakeRegistrations();

class HotkeyPoller {
public:
    int AddHotkey(int vkCode, HotkeyCallback callback) {
        FakeRegistrations().push_back({vkCode, std::move(callback)});
        return static_cast<int>(FakeRegistrations().size());
    }
    void Start(unsigned) {}
    void Stop() {}
};

}  // namespace cameraunlock::input
