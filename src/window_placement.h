// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <windows.h>

#include <cstdlib>

namespace votv_ht::window_placement {

enum class Placement {
    AlreadyCentered,
    DoesNotFit,
    Move,
};

// Where the game window belongs on `work` (its monitor's work area), and whether
// it is already there. `window` is the whole window rect and `client` its client
// area, both in screen coordinates. `target` is the window origin to move to and
// is written for AlreadyCentered and Move.
//
// The CLIENT area is what gets centred, because that is what the engine does
// itself. Launched `-windowed -ResX=1280 -ResY=720` on a 1920x1032 work area,
// Voices of the Void puts its 1280x720 client at (320, 156) - centred on that
// work area - which leaves the 1296x759 window rect at (312, 125), 11 px above
// where a centred window RECT would sit. Centring the rect instead would drop
// every ordinary launch by those 11 px, and a window this one moves ends up
// exactly where a default launch would have put it.
inline Placement Decide(const RECT& window, const RECT& client, const RECT& work,
                        POINT& target) {
    const long width = window.right - window.left;
    const long height = window.bottom - window.top;
    const long workWidth = work.right - work.left;
    const long workHeight = work.bottom - work.top;

    // Fullscreen and borderless land here as well as an oversized windowed
    // client. Nowhere on the work area shows all of it, and centring one taller
    // than the work area puts its title bar off the top of the screen, where the
    // player cannot drag it back.
    if (width > workWidth || height > workHeight) return Placement::DoesNotFit;

    // Kept inside the work area: a window only just shorter than it would
    // otherwise centre its client with the title bar poking above the top edge.
    auto centre = [](long start, long extent, long clientExtent, long clientInset,
                     long windowExtent) {
        const long origin = start + (extent - clientExtent) / 2 - clientInset;
        const long last = start + extent - windowExtent;
        return origin < start ? start : (origin > last ? last : origin);
    };
    target.x = centre(work.left, workWidth, client.right - client.left,
                      client.left - window.left, width);
    target.y = centre(work.top, workHeight, client.bottom - client.top,
                      client.top - window.top, height);

    // Integer halving rounds an odd gap down where the engine may round it up,
    // so an exact comparison would nudge an already centred window by a pixel
    // and call it a fix.
    constexpr long kTolerance = 2;
    if (std::labs(window.left - target.x) <= kTolerance &&
        std::labs(window.top - target.y) <= kTolerance) {
        return Placement::AlreadyCentered;
    }
    return Placement::Move;
}

}  // namespace votv_ht::window_placement
