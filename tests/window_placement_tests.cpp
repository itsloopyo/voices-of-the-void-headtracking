// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Whether a player's game window gets moved, and where to.
//
// The measured rects are the ones Voices of the Void produced on a 1920x1080
// monitor with a 48 px taskbar along the bottom, so a 1920x1032 work area.
// Launched `-windowed -ResX=1280 -ResY=720` the game puts a 1296x759 window at
// (312, 125) around a 1280x720 client at (320, 156): 8 px of frame either side
// and below, and a 31 px title bar. Adding `-WinX=50 -WinY=60` puts the same
// window at (42, 29) instead.
//
// The cases below that need a work area this machine does not have are built
// from those same frame metrics and say so where they appear.

#include "test_harness.h"
#include "window_placement.h"

namespace {

using votv_ht::window_placement::Decide;
using votv_ht::window_placement::Placement;

constexpr RECT kWork{0, 0, 1920, 1032};

RECT At(long x, long y, long w, long h) { return {x, y, x + w, y + h}; }

// A 1280x720 client inside its frame, with the window origin at (x, y).
struct Framed {
    RECT window;
    RECT client;
};
Framed Windowed720p(long x, long y) {
    return {At(x, y, 1296, 759), At(x + 8, y + 31, 1280, 720)};
}

// Where the engine put the window on an ordinary windowed launch. Its client is
// centred on the work area and its rect is 11 px higher than a centred window
// RECT would be, so this is the case that decides which of the two gets centred:
// moving it would drop every ordinary launch by those 11 px.
void TestTheGamesOwnPlacementIsLeftAlone() {
    const Framed f = Windowed720p(312, 125);
    POINT target{};
    CHECK(Decide(f.window, f.client, kWork, target) == Placement::AlreadyCentered);
}

// -WinX=50 -WinY=60 puts the client there. It goes to where an ordinary launch
// would have put it - the move the mod was seen to make in game.
void TestAnOffCentreWindowMovesToTheGamesOwnSpot() {
    const Framed f = Windowed720p(42, 29);
    POINT target{};
    CHECK(Decide(f.window, f.client, kWork, target) == Placement::Move);
    CHECK(target.x == 312);
    CHECK(target.y == 125);
}

void TestOffByAPixelIsAlreadyCentred() {
    const Framed f = Windowed720p(313, 124);
    POINT target{};
    CHECK(Decide(f.window, f.client, kWork, target) == Placement::AlreadyCentered);
}

void TestThreePixelsOutIsMoved() {
    const Framed f = Windowed720p(312, 128);
    POINT target{};
    CHECK(Decide(f.window, f.client, kWork, target) == Placement::Move);
}

// -windowed -ResX=1920 -ResY=1080 on this monitor: the frame makes the window
// wider and taller than the work area, so no position on it shows the whole
// window and the game's own placement stands.
void TestAWindowBiggerThanTheWorkAreaIsLeftAlone() {
    const RECT window = At(-8, -31, 1936, 1119);
    const RECT client = At(0, 0, 1920, 1080);
    POINT target{};
    CHECK(Decide(window, client, kWork, target) == Placement::DoesNotFit);
}

// Fullscreen and borderless: the window covers the monitor, taskbar included,
// so it is taller than the work area.
void TestFullscreenIsLeftAlone() {
    const RECT monitor = At(0, 0, 1920, 1080);
    POINT target{};
    CHECK(Decide(monitor, monitor, kWork, target) == Placement::DoesNotFit);
}

// Constructed: a work area only 11 px taller than the window. Centring the
// picture alone would put the title bar 6 px above the top of it, where it
// cannot be grabbed.
void TestTheTitleBarStaysOnTheWorkArea() {
    const RECT work = At(0, 0, 1366, 770);
    const Framed f = Windowed720p(100, 5);
    POINT target{};
    CHECK(Decide(f.window, f.client, work, target) == Placement::Move);
    CHECK_MSG(target.y == 0, "window top clamped to the work area's top edge");
    CHECK(target.y + 759 <= work.bottom);
}

// Constructed: a second monitor to the right of this one, with its taskbar
// docked on the left. The window is centred on THAT monitor's work area, not
// dragged back to the primary.
void TestAWorkAreaAwayFromTheOriginIsHonoured() {
    const RECT work = At(1920 + 60, 0, 2560 - 60, 1440);
    const Framed f = Windowed720p(2000, 10);
    POINT target{};
    CHECK(Decide(f.window, f.client, work, target) == Placement::Move);
    CHECK(target.x == 1980 + (2500 - 1280) / 2 - 8);
    CHECK(target.y == (1440 - 720) / 2 - 31);
}

}  // namespace

int main() {
    TestTheGamesOwnPlacementIsLeftAlone();
    TestAnOffCentreWindowMovesToTheGamesOwnSpot();
    TestOffByAPixelIsAlreadyCentred();
    TestThreePixelsOutIsMoved();
    TestAWindowBiggerThanTheWorkAreaIsLeftAlone();
    TestFullscreenIsLeftAlone();
    TestTheTitleBarStaysOnTheWorkArea();
    TestAWorkAreaAwayFromTheOriginIsHonoured();

    return gr_test::Report();
}
