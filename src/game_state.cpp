// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <atomic>
#include <cmath>

#include "logging.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::game_state {

namespace {

namespace ue = ::cameraunlock::unreal;

// The drawn view and the pawn's own Camera component agree to within the camera
// manager's own shakes in gameplay; a dream, a cutscene camera or the sleep view
// is metres away or pointed elsewhere. Voices of the Void shakes the camera on
// landings and on the headbob, so the angle allowance has to clear those.
constexpr double kMaxViewOffsetCm = 60.0;
constexpr double kMaxViewAngleDeg = 35.0;

constexpr double kRadiansToDegrees = 57.29577951308232;

// TArray is a pointer then two int32s, so Num sits one pointer in.
constexpr std::size_t kTArrayNumOffset = sizeof(std::uintptr_t);

std::atomic<bool> g_soloOnly{true};

ue_vm::ResolveRetry g_retry;
bool g_resolved = false;

std::uintptr_t g_staticsCdo = 0;
std::uintptr_t g_systemCdo = 0;
ue_call::Function g_isGamePaused;       // GameplayStatics::IsGamePaused
ue_call::Function g_isStandalone;       // KismetSystemLibrary::IsStandalone
std::size_t g_worldGameStateOffset = 0;
std::size_t g_playerArrayOffset = 0;

struct ControllerFields {
    std::uintptr_t Class = 0;
    ue_reflect::FieldInfo Cursor;
};
ControllerFields g_controllerFields;

bool Resolve(std::uintptr_t controller) {
    if (g_resolved) return true;
    if (!g_retry.Due() || !ue_vm::Ready()) return false;

    g_staticsCdo = ue_call::DefaultObject("GameplayStatics");
    g_systemCdo = ue_call::DefaultObject("KismetSystemLibrary");
    const std::uintptr_t world = ue_call::WorldOf(controller);
    ue_reflect::FieldInfo gameState;
    if (!g_staticsCdo || !g_systemCdo || !world ||
        !ue_reflect::FindPropertyInChain(ue_call::ClassOf(world), "GameState", gameState) ||
        gameState.Size != sizeof(std::uintptr_t))
        return false;

    // A class that is not in the object table yet is the ordinary early-frame
    // case and stays silent, like the reads above it. A class that IS there
    // without the property is a fault, and one this retry loop will hit again
    // every 250ms, so it is said once.
    const std::uintptr_t gameStateClass = ue::FindLiveObject("Class", "GameStateBase", nullptr);
    if (!gameStateClass) return false;
    ue_reflect::FieldInfo players;
    if (!ue_reflect::FindPropertyInChain(gameStateClass, "PlayerArray", players)) {
        static bool s_said = false;
        if (!s_said) {
            s_said = true;
            Log::Line("gate: AGameStateBase::PlayerArray did not resolve - the session gate "
                      "cannot count players");
        }
        return false;
    }
    if (!g_isGamePaused.Resolve("GameplayStatics", "IsGamePaused",
                                {{"WorldContextObject", sizeof(std::uintptr_t)}, {"ReturnValue", 1}}))
        return false;
    // Not a gate: the net mode is only ever printed next to the player count, so
    // a build whose KismetSystemLibrary does not carry IsStandalone - which this
    // one does not - loses that word and nothing else.
    g_isStandalone.Resolve("KismetSystemLibrary", "IsStandalone",
                           {{"WorldContextObject", sizeof(std::uintptr_t)}, {"ReturnValue", 1}});

    g_worldGameStateOffset = gameState.Offset;
    g_playerArrayOffset = players.Offset;
    g_resolved = true;
    Log::Line("gate: resolved (IsGamePaused, net mode %s; World.GameState=+0x%zx, "
              "GameStateBase.PlayerArray=+0x%zx)",
              g_isStandalone.Ready() ? "readable" : "unavailable", g_worldGameStateOffset,
              g_playerArrayOffset);
    return true;
}

void RefreshControllerFields(std::uintptr_t controller) {
    const std::uintptr_t cls = ue_call::ClassOf(controller);
    if (!cls || cls == g_controllerFields.Class) return;
    g_controllerFields = ControllerFields{};
    g_controllerFields.Class = cls;
    ue_reflect::FindPropertyInChain(cls, "bShowMouseCursor", g_controllerFields.Cursor);
    Log::Line("gate: controller class %s (cursor flag %s)", ue::ObjectName(cls).c_str(),
              g_controllerFields.Cursor.BoolMask ? "found" : "MISSING");
}

// False when the flag could not be read, which the caller treats as blocking.
bool AskFlag(const ue_call::Function& fn, std::uintptr_t self, std::size_t index, bool& out,
             std::uintptr_t context = 0) {
    if (!self) return false;
    ue_call::Frame frame(fn);
    if (context) frame.Set(0, context);
    if (!frame.Call(self)) return false;
    out = (frame.Get<std::uint8_t>(index) & 1u) != 0;
    return true;
}

// How many player states the game state carries, which is how many people are
// in this session. -1 when the chain did not read.
int PlayerCount(std::uintptr_t controller) {
    const std::uintptr_t world = ue_call::WorldOf(controller);
    std::uintptr_t gameState = 0;
    if (!world || !ue::SafeReadPtr(world + g_worldGameStateOffset, gameState) || !gameState)
        return -1;
    std::uint32_t num = 0;
    if (!ue::SafeReadU32(gameState + g_playerArrayOffset + kTArrayNumOffset, num)) return -1;
    // A TArray Num read out of a struct that has moved would be anything at
    // all, and this game's sessions hold one player.
    if (num > 64) return -1;
    return static_cast<int>(num);
}

bool InMultiplayer(int playerCount) {
    if (!g_soloOnly.load(std::memory_order_relaxed)) return false;
    return SessionIsShared(playerCount);
}

void LogNetMode(std::uintptr_t controller, int playerCount) {
    static int s_players = -2;
    if (playerCount == s_players) return;
    s_players = playerCount;
    bool standalone = false;
    const bool read = g_isStandalone.Ready() &&
                      AskFlag(g_isStandalone, g_systemCdo, 1, standalone, controller);
    Log::Line("gate: session now has %d player%s (net mode %s)", playerCount,
              playerCount == 1 ? "" : "s",
              !read ? "unreadable" : standalone ? "standalone" : "networked");
}

}  // namespace

void SetSoloOnly(bool soloOnly) { g_soloOnly.store(soloOnly, std::memory_order_relaxed); }

Verdict Evaluate(std::uintptr_t controller, const player_rig::Snapshot& rig,
                 const ue::FVector& cleanLocation, const ue::FQuat4d& cleanRotation) {
    Verdict v;
    if (!rig.Pawn || !rig.Camera.Valid || !Resolve(controller)) {
        v.Why = Blocker::NoPlayer;
        return v;
    }

    v.PlayerCount = PlayerCount(controller);
    LogNetMode(controller, v.PlayerCount);
    if (InMultiplayer(v.PlayerCount)) {
        v.Why = Blocker::Multiplayer;
        return v;
    }

    RefreshControllerFields(controller);
    bool cursor = true;
    if (!ue_reflect::ReadBool(controller, g_controllerFields.Cursor, cursor) || cursor) {
        v.Why = Blocker::Cursor;
        return v;
    }

    bool paused = true;
    if (!AskFlag(g_isGamePaused, g_staticsCdo, 1, paused, controller) || paused) {
        v.Why = Blocker::Paused;
        return v;
    }

    if (!rig.HaveState || rig.Dead || rig.Ragdoll || rig.WakingUp || rig.MouseInputOff) {
        v.Why = Blocker::NotInControl;
        return v;
    }

    const ue::FVector& cam = rig.Camera.Position;
    const double dx = cleanLocation.X - cam.X, dy = cleanLocation.Y - cam.Y, dz = cleanLocation.Z - cam.Z;
    v.ViewOffsetCm = std::sqrt(dx * dx + dy * dy + dz * dz);
    const ue::FVector viewFwd = ue::QuatRotateVec(cleanRotation, ue::FVector{1.0, 0.0, 0.0});
    const ue::FVector& camFwd = rig.Camera.Forward;
    double dot = viewFwd.X * camFwd.X + viewFwd.Y * camFwd.Y + viewFwd.Z * camFwd.Z;
    dot = dot > 1.0 ? 1.0 : (dot < -1.0 ? -1.0 : dot);
    v.ViewAngleDeg = std::acos(dot) * kRadiansToDegrees;
    if (!(v.ViewOffsetCm <= kMaxViewOffsetCm) || !(v.ViewAngleDeg <= kMaxViewAngleDeg)) {
        v.Why = Blocker::NotFirstPerson;
        return v;
    }

    v.Why = Blocker::None;
    v.InGameplay = true;
    return v;
}

const char* BlockerName(Blocker b) {
    switch (b) {
        case Blocker::None:           return "gameplay";
        case Blocker::NoPlayer:       return "no player character (menu or loading)";
        case Blocker::Multiplayer:    return "another player is in the session";
        case Blocker::Cursor:         return "mouse cursor up (menu or terminal)";
        case Blocker::Paused:         return "game paused";
        case Blocker::NotInControl:   return "player is dead, ragdolled, waking up or typing on a screen";
        case Blocker::NotFirstPerson: return "view is not the pawn's camera (cutscene or dream)";
    }
    return "unknown";
}

void LogTransitions(const Verdict& v) {
    static std::atomic<int> s_lastWhy{-1};
    const int why = static_cast<int>(v.Why);
    if (s_lastWhy.exchange(why) != why)
        Log::Line("gate: %s (view offset %.1fcm %.1fdeg, players %d)", BlockerName(v.Why),
                  v.ViewOffsetCm, v.ViewAngleDeg, v.PlayerCount);
}

}  // namespace votv_ht::game_state
