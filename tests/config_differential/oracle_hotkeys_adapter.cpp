// Compiled into the hotkey oracle library only, with `cameraunlock` and `votv_ht` renamed, so
// "mod_hotkeys.h" here is the published build's and the poller, keyboard, view hook and
// session under it are oracle_fake's.
#include "mod_hotkeys.h"
#include "oracle_adapter.h"
#include "view_hook.h"

#include "cameraunlock/input/hotkey_poller.h"

namespace cameraunlock::input {

int& FakeHeld() {
    static int held = 0;
    return held;
}

std::vector<FakeRegistration>& FakeRegistrations() {
    static std::vector<FakeRegistration> registrations;
    return registrations;
}

}  // namespace cameraunlock::input

namespace votv_ht::view_hook {

int& FakeToggles() {
    static int n = 0;
    return n;
}

int& FakeYawToggles() {
    static int n = 0;
    return n;
}

}  // namespace votv_ht::view_hook

namespace votv_oracle_view {

FireTable OracleFires(int yaw_mode_key) {
    namespace input = cameraunlock::input;
    namespace view_hook = votv_ht::view_hook;
    votv_ht::Config c;
    c.yaw_mode_key = yaw_mode_key;
    votv_ht::Session session;

    input::FakeRegistrations().clear();
    votv_ht::hotkeys::Register(c, session);
    const std::vector<input::FakeRegistration> registered = input::FakeRegistrations();
    votv_ht::hotkeys::Stop();

    // The published poller's Poll: every callback on a key runs when that key goes down.
    FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            view_hook::FakeToggles() = 0;
            view_hook::FakeYawToggles() = 0;
            session.cycles = 0;
            input::FakeHeld() = held;
            for (const input::FakeRegistration& r : registered) {
                if (r.vk == vk && r.callback) r.callback();
            }
            table.push_back({view_hook::FakeToggles(), session.cycles, view_hook::FakeYawToggles()});
        }
    }
    input::FakeHeld() = 0;
    return table;
}

}  // namespace votv_oracle_view
