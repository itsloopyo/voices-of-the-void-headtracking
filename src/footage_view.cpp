// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "footage_view.h"

#include <string>

#include "logging.h"
#include "ue_call.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::footage_view {

namespace {

namespace ue = ::cameraunlock::unreal;

// tutorial3_C is the tutorial's own director actor, and it is what marks the
// map the checkerboard appears on. In the running tutorial it sits at
// (-590, 0, 157), a hand's width from the RCM_camera_C the sequence is built
// around, and no other map spawns one.
constexpr const char* kDirectorClass = "tutorial3_C";

std::uintptr_t g_world = 0;
bool g_blocks = false;

bool HasDirector() {
    bool found = false;
    ue::ForEachUObject([&](std::uintptr_t obj) -> bool {
        if (ue::ClassName(obj) != kDirectorClass) return false;
        // The class default object is in the table on every map, so matching it
        // would stand the lean down everywhere.
        if (ue_call::IsDefaultObject(obj)) return false;
        found = true;
        return true;
    });
    return found;
}

}  // namespace

bool BlocksLean(std::uintptr_t controller) {
    const std::uintptr_t world = ue_call::WorldOf(controller);
    // No world is no answer, and the direction that leaves the game stock is to
    // apply nothing. The next frame with a world decides properly.
    if (!world) return true;
    if (world == g_world) return g_blocks;

    g_world = world;
    g_blocks = HasDirector();
    if (g_blocks)
        Log::Line("footage-view: this map draws its frame through the tutorial's camera path, "
                  "which will not take a view position the game did not produce - lean held at "
                  "zero here, head rotation unaffected");
    else
        Log::Line("footage-view: ordinary map, lean applied normally");
    return g_blocks;
}

void Forget() {
    g_world = 0;
    g_blocks = false;
}

}  // namespace votv_ht::footage_view
