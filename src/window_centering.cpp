// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "window_centering.h"

#include <windows.h>

#include <cwchar>

#include "logging.h"
#include "window_placement.h"

namespace votv_ht::window_centering {

namespace {

constexpr int kPollIntervalMs = 250;
constexpr int kPollAttempts = 240;  // 60s, which covers a cold start off a hard disk.

// Two seconds of an unchanged rect before acting, rather than a fixed delay that
// would be a guess: the window exists before the engine has finished applying
// the resolution from GameUserSettings.ini, and a window centred at one size and
// then resized is off centre again by however much the size changed.
constexpr int kSettlePolls = 8;

// UE4's render window class on the Steam build, the only build this mod carries
// a profile for. Matching the class exactly is what keeps the single move on the
// render window: the process puts up a visible, unowned 601x201
// "SplashScreenClass" window around three seconds before the render window, and
// core's FindGameWindow takes the first visible unowned window over 200x200
// whatever its class, so it would centre the splash and count the job done.
constexpr wchar_t kRenderWindowClass[] = L"UnrealWindow";

struct FindState {
    DWORD pid = 0;
    HWND window = nullptr;
};

BOOL CALLBACK PickRenderWindow(HWND window, LPARAM lparam) {
    auto* state = reinterpret_cast<FindState*>(lparam);

    DWORD windowPid = 0;
    GetWindowThreadProcessId(window, &windowPid);
    if (windowPid != state->pid) return TRUE;
    if (!IsWindowVisible(window)) return TRUE;
    if (GetWindow(window, GW_OWNER) != nullptr) return TRUE;

    wchar_t className[64] = {};
    if (GetClassNameW(window, className, ARRAYSIZE(className)) == 0) return TRUE;
    if (std::wcscmp(className, kRenderWindowClass) != 0) return TRUE;

    state->window = window;
    return FALSE;
}

bool ClientRectOnScreen(HWND window, RECT& out) {
    RECT client{};
    if (!GetClientRect(window, &client)) return false;
    POINT topLeft{client.left, client.top};
    POINT bottomRight{client.right, client.bottom};
    if (!ClientToScreen(window, &topLeft) || !ClientToScreen(window, &bottomRight)) return false;
    out = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    return true;
}

// A render window whose rect has held still for kSettlePolls. False when none
// settled in time or the mod is shutting down, in which case the out-params are
// untouched.
bool WaitForSettledWindow(const std::atomic<bool>& cancelled, HWND& settled, RECT& settledRect) {
    HWND window = nullptr;
    RECT previous{};
    bool havePrevious = false;
    int stablePolls = 0;

    for (int attempt = 0; attempt < kPollAttempts; ++attempt) {
        Sleep(kPollIntervalMs);
        if (cancelled.load(std::memory_order_acquire)) return false;

        if (window == nullptr) window = FindRenderWindow();
        RECT current{};
        // A minimised window reports its rect parked far off the top-left of the
        // desktop, and centring from that would put the restored window anywhere.
        if (!window || !IsWindowVisible(window) || IsIconic(window) ||
            !GetWindowRect(window, &current)) {
            window = nullptr;
            havePrevious = false;
            stablePolls = 0;
            continue;
        }

        if (havePrevious && EqualRect(&previous, &current)) {
            if (++stablePolls < kSettlePolls) continue;
            settled = window;
            settledRect = current;
            return true;
        }
        previous = current;
        havePrevious = true;
        stablePolls = 0;
    }
    return false;
}

void CenterUnlessAlready(HWND window, const RECT& rect) {
    RECT client{};
    if (!ClientRectOnScreen(window, client)) {
        Log::Line("WARNING: window: reading the client area failed: %lu; leaving placement alone",
                  GetLastError());
        return;
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) {
        Log::Line("WARNING: window: GetMonitorInfoW failed: %lu; leaving placement alone",
                  GetLastError());
        return;
    }

    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int workWidth = static_cast<int>(info.rcWork.right - info.rcWork.left);
    const int workHeight = static_cast<int>(info.rcWork.bottom - info.rcWork.top);

    POINT target{};
    switch (window_placement::Decide(rect, client, info.rcWork, target)) {
    case window_placement::Placement::AlreadyCentered:
        Log::Line("window: %dx%d at (%d, %d) is already centred on the %dx%d work area, "
                  "leaving it alone",
                  width, height, static_cast<int>(rect.left), static_cast<int>(rect.top),
                  workWidth, workHeight);
        return;
    case window_placement::Placement::DoesNotFit:
        Log::Line("window: %dx%d does not fit inside the %dx%d work area (fullscreen, "
                  "borderless or oversized), leaving it alone",
                  width, height, workWidth, workHeight);
        return;
    case window_placement::Placement::Move:
        break;
    }

    if (!SetWindowPos(window, nullptr, static_cast<int>(target.x), static_cast<int>(target.y),
                      0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Log::Line("WARNING: window: SetWindowPos failed: %lu", GetLastError());
        return;
    }
    // Read back rather than echoing the target, so the log says where the window
    // actually is.
    RECT moved{};
    if (!GetWindowRect(window, &moved)) {
        Log::Line("WARNING: window: moved to (%d, %d), but reading it back failed: %lu",
                  static_cast<int>(target.x), static_cast<int>(target.y), GetLastError());
        return;
    }
    Log::Line("window: centred the %dx%d window on the %dx%d work area: (%d, %d) -> (%d, %d)",
              width, height, workWidth, workHeight,
              static_cast<int>(rect.left), static_cast<int>(rect.top),
              static_cast<int>(moved.left), static_cast<int>(moved.top));
}

}  // namespace

HWND FindRenderWindow() {
    FindState state;
    state.pid = GetCurrentProcessId();
    EnumWindows(PickRenderWindow, reinterpret_cast<LPARAM>(&state));
    return state.window;
}

void CenterWhenReady(const std::atomic<bool>& cancelled) {
    HWND window = nullptr;
    RECT rect{};
    if (!WaitForSettledWindow(cancelled, window, rect)) {
        if (cancelled.load(std::memory_order_acquire)) return;
        Log::Line("window: no render window settled within %ds, leaving placement alone",
                  kPollAttempts * kPollIntervalMs / 1000);
        return;
    }
    CenterUnlessAlready(window, rect);
}

}  // namespace votv_ht::window_centering
