// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Compiled only into a development build - see VOTV_DEV_COMMANDS in
// CMakeLists.txt. Both probes hang off the UObject::ProcessEvent detour, which
// a shipped binary never installs.
#ifdef VOTV_DEV_COMMANDS

#include "dev/dev_probes.h"

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <windows.h>

#include "dev/dev_ue.h"
#include "logging.h"
#include "process_event_hook.h"
#include "ue4_types.h"
#include "ue_reflect.h"
#include "view_hook.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::dev_probes {

namespace {

namespace ue = ::cameraunlock::unreal;

// ---- shot log -------------------------------------------------------------

std::uintptr_t g_fnOnImpact = 0;
std::uintptr_t g_fnSpawnedBullet = 0;
std::size_t g_impactPointOffset = 0;
std::vector<std::uintptr_t> g_pendingProjectiles;

void OnShotEvent(std::uintptr_t self, std::uintptr_t fn, void* params) {
    if (!params) return;
    const view_hook::AimSample a = view_hook::LastAim();
    if (fn == g_fnSpawnedBullet) {
        std::uintptr_t projectile = 0;
        std::memcpy(&projectile, params, sizeof(projectile));
        if (projectile) g_pendingProjectiles.push_back(projectile);
        return;
    }
    ue4::FVector impact{};
    std::memcpy(&impact, static_cast<const char*>(params) + g_impactPointOffset, sizeof(impact));
    Log::Line("shot: impact %s at (%.2f,%.2f,%.2f) | mark point (%.2f,%.2f,%.2f) hit=%d delta=(%.2f,%.2f,%.2f) "
              "ndc=(%.4f,%.4f) valid=%d eye=(%.2f,%.2f,%.2f)",
              ue::ClassName(self).c_str(), impact.X, impact.Y, impact.Z, a.PointX, a.PointY, a.PointZ,
              a.Hit ? 1 : 0, impact.X - a.PointX, impact.Y - a.PointY, impact.Z - a.PointZ, a.NdcX, a.NdcY,
              a.NdcValid ? 1 : 0, a.EyeX, a.EyeY, a.EyeZ);
}

// ---- ProcessEvent trace ---------------------------------------------------

std::mutex g_peMutex;
std::unordered_map<std::uintptr_t, int> g_peSeen;
std::uintptr_t g_peTarget = 0;
std::uint64_t g_peUntil = 0;

void PeObserver(std::uintptr_t self, std::uintptr_t fn, void*) {
    if (g_peTarget && self != g_peTarget) return;
    std::lock_guard<std::mutex> lk(g_peMutex);
    ++g_peSeen[fn];
}

}  // namespace

void InstallShotLog() {
    static bool s_tried = false;
    if (s_tried) return;
    s_tried = true;
    g_fnOnImpact = ue::FindLiveObject("Function", "OnImpact", "BaseProjectile");
    g_fnSpawnedBullet = ue::FindLiveObject("Function", "BPOnSpawnedBullet", "BaseWeaponBP_C");
    const std::uintptr_t hitResult = ue::FindLiveObject("ScriptStruct", "HitResult", nullptr);
    std::vector<ue_reflect::FieldInfo> h, p;
    if (!g_fnOnImpact || !g_fnSpawnedBullet || !hitResult ||
        !ue_reflect::ResolveAll("HitResult", hitResult, {"ImpactPoint"}, h) ||
        !ue_reflect::ResolveAll("OnImpact", g_fnOnImpact, {"HitResult"}, p)) {
        Log::Line("dev: shot log unavailable");
        return;
    }
    g_impactPointOffset = p[0].Offset + h[0].Offset;
    if (!process_event_hook::Install() ||
        !process_event_hook::AddPostHandler(g_fnOnImpact, &OnShotEvent) ||
        !process_event_hook::AddPostHandler(g_fnSpawnedBullet, &OnShotEvent)) {
        Log::Line("dev: shot log hook failed");
        return;
    }
    Log::Line("dev: shot log armed (BaseProjectile::OnImpact, BaseWeaponBP_C::BPOnSpawnedBullet)");
}

void PollShotLog() {
    if (g_pendingProjectiles.empty()) return;
    const std::vector<std::uintptr_t> pending = std::move(g_pendingProjectiles);
    g_pendingProjectiles.clear();
    const view_hook::AimSample a = view_hook::LastAim();
    for (std::uintptr_t projectile : pending) {
        ue4::FVector loc{};
        if (!dev_ue::ActorLocation(projectile, loc)) continue;
        Log::Line("shot: spawned %s at (%.2f,%.2f,%.2f) | aim origin (%.2f,%.2f,%.2f) dir (%.5f,%.5f,%.5f)",
                  ue::ClassName(projectile).c_str(), loc.X, loc.Y, loc.Z, a.MuzzleX, a.MuzzleY, a.MuzzleZ,
                  a.DirX, a.DirY, a.DirZ);
    }
}

void StartProcessEventTrace(std::uintptr_t target, int ms) {
    g_peTarget = target;
    { std::lock_guard<std::mutex> lk(g_peMutex); g_peSeen.clear(); }
    g_peUntil = GetTickCount64() + static_cast<std::uint64_t>(ms);
    process_event_hook::Install();
    process_event_hook::SetObserver(&PeObserver);
}

void PollProcessEventTrace() {
    if (!g_peUntil || GetTickCount64() <= g_peUntil) return;
    g_peUntil = 0;
    process_event_hook::SetObserver(nullptr);
    std::lock_guard<std::mutex> lk(g_peMutex);
    for (const auto& kv : g_peSeen)
        Log::Line("dev: petrace %6d x %s::%s", kv.second, ue::OuterName(kv.first).c_str(),
                  ue::ObjectName(kv.first).c_str());
}

}  // namespace votv_ht::dev_probes

#endif  // VOTV_DEV_COMMANDS
