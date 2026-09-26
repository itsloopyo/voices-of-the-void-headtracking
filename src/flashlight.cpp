// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "flashlight.h"

#include <cmath>

#include "logging.h"
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
std::size_t g_relativeRotationOffset = 0;

// mainPlayer_C::light_R, resolved against the class it was first seen on.
std::uintptr_t g_playerClass = 0;
ue_reflect::FieldInfo g_lightField;

// The light being turned, and what the game had as its relative rotation before
// the turn. `Written` is what the turn left there: finding anything else next
// frame means the game set the light itself in between, and that is kept.
struct Light {
    std::uintptr_t Component = 0;
    std::int32_t Index = -1;
    bool Turned = false;
    ue4::FRotator Base{0.0f, 0.0f, 0.0f};
    ue4::FRotator Written{0.0f, 0.0f, 0.0f};
};
Light g_light;

bool Resolve() {
    if (g_resolved) return true;
    if (!g_retry.Due() || !ue_vm::Ready()) return false;
    const std::uintptr_t sceneComponent = ue::FindLiveObject("Class", "SceneComponent", nullptr);
    ue_reflect::FieldInfo relative;
    if (!sceneComponent ||
        !ue_reflect::FindPropertyInChain(sceneComponent, "RelativeRotation", relative) ||
        relative.Size != sizeof(ue4::FRotator))
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
                                        {"SweepHitResult", 1}, {"bTeleport", 1}}))
        return false;
    g_relativeRotationOffset = relative.Offset;
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

// Hands the light back to the game: its own relative rotation, unless the game
// has written a new one since the turn.
void Release() {
    if (!g_light.Turned) return;
    g_light.Turned = false;
    if (!Alive(g_light)) return;
    ue4::FRotator now{};
    if (!ReadRelative(g_light.Component, now) || !Same(now, g_light.Written)) return;
    SetRotation(g_setRelativeRotation, g_light.Component, g_light.Base);
}

}  // namespace

void Update(std::uintptr_t player, bool apply, const ue::FQuat4d& headDelta,
            const cameraunlock::effects::HeadFollowLightSettings& light) {
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
}

}  // namespace votv_ht::flashlight
