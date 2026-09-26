// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "flashlight.h"

#include <cmath>

#include "logging.h"
#include "process_event_hook.h"
#include "ue4_types.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::flashlight {

namespace {

namespace ue = ::cameraunlock::unreal;

constexpr std::size_t kInternalIndexOffset = 0x0c;

ue_vm::ResolveRetry g_retry;
bool g_resolved = false;
ue_call::Function g_getWorldRotation;   // SceneComponent::K2_GetComponentRotation
ue_call::Function g_setWorldRotation;   // SceneComponent::K2_SetWorldRotation
ue_call::Function g_setRelativeRotation;  // SceneComponent::K2_SetRelativeRotation
ue_call::Function g_getWorldLocation;
ue_call::Function g_setWorldLocation;
ue_call::Function g_setRelativeLocation;
std::size_t g_relativeRotationOffset = 0;
std::size_t g_relativeLocationOffset = 0;

// mainPlayer_C::light_R, resolved against the class it was first seen on.
std::uintptr_t g_playerClass = 0;
std::uintptr_t g_player = 0;
ue_reflect::FieldInfo g_lightField;
bool g_restoreRegistered = false;
bool g_registrationAttempted = false;
bool g_apply = false;
ue::FQuat4d g_headDelta{0, 0, 0, 1};
ue::FVector g_headOffset{};
cameraunlock::effects::HeadFollowLightSettings g_settings{};

void AfterPlayerTick(std::uintptr_t player, std::uintptr_t, void*) {
    // The renderer can consume component transforms before the next view query.
    if (player == g_player) Update(player, g_apply, g_headDelta, g_headOffset, g_settings);
}

void Release();

void BeforePlayerTick(std::uintptr_t player, std::uintptr_t, void*) {
    if (player == g_player) Release();
}

// Keep game-authored transforms if they replace the mod's last write.
struct Light {
    std::uintptr_t Component = 0;
    std::int32_t Index = -1;
    bool Turned = false;
    bool Moved = false;
    ue4::FRotator Base{0.0f, 0.0f, 0.0f};
    ue4::FRotator Written{0.0f, 0.0f, 0.0f};
    ue4::FVector BaseLocation{0.0f, 0.0f, 0.0f};
    ue4::FVector WrittenLocation{0.0f, 0.0f, 0.0f};
};
Light g_light;

bool Resolve() {
    if (g_resolved) return true;
    if (!g_retry.Due() || !ue_vm::Ready()) return false;
    const std::uintptr_t sceneComponent = ue::FindLiveObject("Class", "SceneComponent", nullptr);
    ue_reflect::FieldInfo relative, location;
    if (!sceneComponent ||
        !ue_reflect::FindPropertyInChain(sceneComponent, "RelativeRotation", relative) ||
        relative.Size != sizeof(ue4::FRotator) ||
        !ue_reflect::FindPropertyInChain(sceneComponent, "RelativeLocation", location) ||
        location.Size != sizeof(ue4::FVector))
        return false;
    // The sweep result is an out-param the mod never reads, and a teleport-free,
    // sweep-free set never fills it; its slot only has to exist.
    const std::size_t rot = sizeof(ue4::FRotator);
    if (!g_getWorldRotation.Resolve("SceneComponent", "K2_GetComponentRotation",
                                    {{"ReturnValue", rot}}) ||
        !g_setWorldRotation.Resolve("SceneComponent", "K2_SetWorldRotation",
                                    {{"NewRotation", rot}, {"bSweep", 1}, {"SweepHitResult", 1},
                                     {"bTeleport", 1}}) ||
        !g_setRelativeRotation.Resolve("SceneComponent", "K2_SetRelativeRotation",
                                       {{"NewRotation", rot}, {"bSweep", 1},
                                        {"SweepHitResult", 1}, {"bTeleport", 1}}) ||
        !g_getWorldLocation.Resolve("SceneComponent", "K2_GetComponentLocation",
                                     {{"ReturnValue", sizeof(ue4::FVector)}}) ||
        !g_setWorldLocation.Resolve("SceneComponent", "K2_SetWorldLocation",
                                     {{"NewLocation", sizeof(ue4::FVector)}, {"bSweep", 1},
                                      {"SweepHitResult", 1}, {"bTeleport", 1}}) ||
        !g_setRelativeLocation.Resolve("SceneComponent", "K2_SetRelativeLocation",
                                        {{"NewLocation", sizeof(ue4::FVector)}, {"bSweep", 1},
                                         {"SweepHitResult", 1}, {"bTeleport", 1}}))
        return false;
    g_relativeRotationOffset = relative.Offset;
    g_relativeLocationOffset = location.Offset;
    g_resolved = true;
    Log::Line("flashlight: resolved (SceneComponent.RelativeRotation=+0x%zx)",
              g_relativeRotationOffset);
    return true;
}

bool Alive(const Light& light) {
    const std::uintptr_t item = ue_call::ObjectItem(light.Index);
    std::uintptr_t live = 0;
    return light.Component != 0 && item != 0 && ue::SafeReadPtr(item, live) &&
           live == light.Component;
}

std::uintptr_t FindLight(std::uintptr_t player) {
    const std::uintptr_t cls = ue_call::ClassOf(player);
    if (!cls) return 0;
    if (!g_registrationAttempted) {
        g_registrationAttempted = true;
        const auto tick = ue::FindLiveObject("Function", "ReceiveTick", ue::ObjectName(cls).c_str());
        g_restoreRegistered = tick && process_event_hook::Install() &&
            process_event_hook::AddPreHandler(tick, &BeforePlayerTick) &&
            process_event_hook::AddPostHandler(tick, &AfterPlayerTick);
        if (!g_restoreRegistered)
            Log::Line("flashlight: player tick callbacks unavailable; head following disabled");
    }
    if (!g_restoreRegistered) return 0;
    g_player = player;
    if (cls != g_playerClass) {
        g_playerClass = cls;
        if (!ue_reflect::FindPropertyInChain(cls, "light_R", g_lightField) ||
            g_lightField.Size != sizeof(std::uintptr_t))
            g_lightField = ue_reflect::FieldInfo{};
        Log::Line("flashlight: %s.light_R %s", ue::ObjectName(cls).c_str(),
                  g_lightField.Field ? "found" : "MISSING - the flashlight is left on the aim");
    }
    std::uintptr_t light = 0;
    if (!g_lightField.Field || !ue::SafeReadPtr(player + g_lightField.Offset, light)) return 0;
    return light;
}

bool ReadRelative(std::uintptr_t component, ue4::FRotator& out) {
    return ue::SafeReadFloat(component + g_relativeRotationOffset, out.Pitch) &&
           ue::SafeReadFloat(component + g_relativeRotationOffset + 4, out.Yaw) &&
           ue::SafeReadFloat(component + g_relativeRotationOffset + 8, out.Roll);
}

bool SetRotation(const ue_call::Function& fn, std::uintptr_t component, const ue4::FRotator& r) {
    ue_call::Frame frame(fn);
    frame.Set(0, r);
    return frame.Call(component);
}

bool ReadLocation(std::uintptr_t component, ue4::FVector& out) {
    return ue::SafeReadFloat(component + g_relativeLocationOffset, out.X) &&
           ue::SafeReadFloat(component + g_relativeLocationOffset + 4, out.Y) &&
           ue::SafeReadFloat(component + g_relativeLocationOffset + 8, out.Z);
}

bool SetLocation(const ue_call::Function& fn, std::uintptr_t component, const ue4::FVector& p) {
    ue_call::Frame frame(fn);
    frame.Set(0, p);
    return frame.Call(component);
}

// The same turn about the same axis, through `gain` times the angle.
ue::FQuat4d ScaleTurn(ue::FQuat4d q, double gain) {
    if (q.W < 0.0) q = ue::FQuat4d{-q.X, -q.Y, -q.Z, -q.W};
    const double s = std::sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z);
    if (s < 1e-12) return ue::FQuat4d{0.0, 0.0, 0.0, 1.0};
    const double half = std::atan2(s, q.W) * gain;
    const double f = std::sin(half) / s;
    return ue::FQuat4d{q.X * f, q.Y * f, q.Z * f, std::cos(half)};
}

bool Same(const ue4::FRotator& a, const ue4::FRotator& b) {
    return a.Pitch == b.Pitch && a.Yaw == b.Yaw && a.Roll == b.Roll;
}

void Release() {
    const bool turned = g_light.Turned;
    const bool moved = g_light.Moved;
    g_light.Turned = false;
    g_light.Moved = false;
    if (!turned && !moved) return;
    if (!Alive(g_light)) return;
    ue4::FRotator now{};
    ue4::FVector position{};
    // Either setter can round the other relative transform field. Compare
    // both before writing either, against the snapshot after both setters.
    const bool restoreRotation = turned && ReadRelative(g_light.Component, now) &&
                                 Same(now, g_light.Written);
    const bool restoreLocation = moved && ReadLocation(g_light.Component, position) &&
        position.X == g_light.WrittenLocation.X && position.Y == g_light.WrittenLocation.Y &&
        position.Z == g_light.WrittenLocation.Z;
    if (restoreRotation)
        SetRotation(g_setRelativeRotation, g_light.Component, g_light.Base);
    if (restoreLocation)
        SetLocation(g_setRelativeLocation, g_light.Component, g_light.BaseLocation);
}

}  // namespace

void Update(std::uintptr_t player, bool apply, const ue::FQuat4d& headDelta,
            const ue::FVector& headOffset,
            const cameraunlock::effects::HeadFollowLightSettings& light) {
    g_apply = apply;
    g_headDelta = headDelta;
    g_headOffset = headOffset;
    g_settings = light;
    if (!Resolve()) return;
    Release();
    if (!apply || !light.follows_head || !player) return;

    const std::uintptr_t component = FindLight(player);
    if (!component) return;
    if (component != g_light.Component) {
        std::uint32_t index = 0;
        if (!ue::SafeReadU32(component + kInternalIndexOffset, index)) return;
        g_light = Light{};
        g_light.Component = component;
        g_light.Index = static_cast<std::int32_t>(index);
    }

    ue_call::Frame get(g_getWorldRotation);
    if (!ReadRelative(component, g_light.Base) || !get.Call(component)) return;
    const ue4::FRotator world = get.Get<ue4::FRotator>(0);
    const ue::FQuat4d turned = ue::QuatMul(ScaleTurn(headDelta, light.multiplier),
                                           ue::QuatFromEulerDeg(world.Pitch, world.Yaw, world.Roll));
    if (!SetRotation(g_setWorldRotation, component, ue4::FromCore(ue::QuatToRotator(turned)))) return;
    g_light.Turned = ReadRelative(component, g_light.Written);

    ue_call::Frame getLocation(g_getWorldLocation);
    if (!ReadLocation(component, g_light.BaseLocation) || !getLocation.Call(component)) return;
    const auto position = getLocation.Get<ue4::FVector>(0);
    const ue4::FVector moved = ue4::FromCore(ue::FVector{
        position.X + headOffset.X, position.Y + headOffset.Y, position.Z + headOffset.Z});
    if (!SetLocation(g_setWorldLocation, component, moved)) return;
    g_light.Moved = ReadLocation(component, g_light.WrittenLocation);
    g_light.Turned = ReadRelative(component, g_light.Written);
}

}  // namespace votv_ht::flashlight
