// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

#include "player_rig.h"

// When head tracking is allowed to touch the view at all. Polled every render
// frame from the live controller, never latched.
//
// Gameplay is the frame drawn from the player character's own first-person
// camera component, with no cursor up, the game not paused, the pawn alive and
// in ordinary control, and - unless the INI says otherwise - nobody else in the
// session. Anything unreadable reads as the blocking answer.
//
// A cutscene, a dream sequence and the sleep camera need no flag of their own:
// each hands the view to a camera that is not the pawn's, which the last check
// catches, and the main menu has no player character at all.
namespace votv_ht::game_state {

enum class Blocker {
    None,
    NoPlayer,
    Multiplayer,
    Cursor,
    Paused,
    NotInControl,
    NotFirstPerson,
};

struct Verdict {
    bool InGameplay = false;
    Blocker Why = Blocker::NoPlayer;
    // How far the drawn view sits from the pawn's camera component, for the log.
    double ViewOffsetCm = -1.0;
    double ViewAngleDeg = -1.0;
    // How many player states the game state carries, which is how many people
    // are in the session. -1 when it did not read.
    int PlayerCount = -1;
};

// Whether anyone else is in the session, from the number of player states the
// game state carries. Solo is exactly one player state. A count that did not
// read is -1 and counts as shared, because that is the direction that leaves
// the game stock.
//
// Voices of the Void ships no multiplayer mode: its menu offers Tutorial,
// Story, Infinite, Ambience and Sandbox and nothing else, and every one of them
// starts a session holding one player state. So this check costs a TArray
// length read per frame and never fires - which is the point. It is what stands
// the mod down on its own if a build, a co-op branch or another mod ever puts a
// second player in the world.
//
// The player count is the signal rather than the net mode: a solo session still
// runs through the engine's own session code. The net mode is logged alongside
// it so the two can be compared in a report.
inline bool SessionIsShared(int playerCount) { return playerCount != 1; }

// Whether the session gate is armed. Seeded from Config::disable_in_multiplayer
// at startup.
void SetSoloOnly(bool soloOnly);

// The view the game built this frame, before any head pose. The rotation
// arrives as the quaternion the render hook has already composed from it: the
// only thing read off it here is the view's forward vector, and building a
// second quaternion from the same Euler angles is six trig calls a frame for
// the same answer.
Verdict Evaluate(std::uintptr_t controller, const player_rig::Snapshot& rig,
                 const cameraunlock::unreal::FVector& cleanLocation,
                 const cameraunlock::unreal::FQuat4d& cleanRotation);

void LogTransitions(const Verdict& v);

const char* BlockerName(Blocker b);

}  // namespace votv_ht::game_state
