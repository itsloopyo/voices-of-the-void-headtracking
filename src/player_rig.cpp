// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "player_rig.h"

#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "logging.h"
#include "ue4_types.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/memory/safe_memory.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::player_rig {

namespace {

namespace ue = ::cameraunlock::unreal;

ue_vm::ResolveRetry g_retry;
bool g_resolved = false;

ue_call::Function g_componentToWorld;    // SceneComponent::K2_GetComponentToWorld
ue_call::Function g_playbackPosition;    // TimelineComponent::GetPlaybackPosition
std::size_t g_transformRotation = 0;
std::size_t g_transformTranslation = 0;
std::size_t g_pawnOffset = 0;

// The ATV is a pawn of its own: holding use on it possesses ATV_C, and the
// frame is then drawn from ATV_C::Camera while ATV_C::Player names the rider.
// Its zoom (the atv_zoom key) is the `Timeline` timeline, which takes that
// camera from the rider's FOV setting to 44 degrees and back.
constexpr const char* kRideClass = "ATV_C";

enum class PawnKind { Other, OnFoot, Riding };

// The fields of the possessed pawn's class, resolved against the class they
// were first seen on; a different pawn class re-resolves.
struct PawnFields {
    std::uintptr_t Class = 0;
    PawnKind Kind = PawnKind::Other;
    ue_reflect::FieldInfo Camera;    // the CameraComponent the frame is drawn from
    ue_reflect::FieldInfo Zoom;      // the TimelineComponent that narrows its FOV
    ue_reflect::FieldInfo Rider;     // ATV_C::Player
};
PawnFields g_pawn;

// mainPlayer_C's state flags, resolved against the rider's class.
struct PlayerFields {
    std::uintptr_t Class = 0;
    ue_reflect::FieldInfo Dead;
    ue_reflect::FieldInfo Ragdoll;
    ue_reflect::FieldInfo WakingUp;
    ue_reflect::FieldInfo MouseOff;
};
PlayerFields g_player;

// CameraComponent::FieldOfView, resolved against the component's class.
struct CameraFields {
    std::uintptr_t Class = 0;
    ue_reflect::FieldInfo FieldOfView;
};
CameraFields g_camera;

bool Resolve(std::uintptr_t controller) {
    if (g_resolved) return true;
    if (!g_retry.Due() || !ue_vm::Ready()) return false;

    const std::uintptr_t controllerClass = ue_call::ClassOf(controller);
    if (!controllerClass) return false;
    ue_reflect::FieldInfo pawn;
    if (!ue_reflect::FindPropertyInChain(controllerClass, "Pawn", pawn) ||
        pawn.Size != sizeof(std::uintptr_t)) {
        // Once: this runs again every 250ms until the whole rig resolves.
        static bool s_said = false;
        if (!s_said) {
            s_said = true;
            Log::Line("player-rig: AController::Pawn is not in %s's property chain",
                      ue::ObjectName(controllerClass).c_str());
        }
        return false;
    }
    if (!ue_reflect::VerifyBoolLayout(controllerClass)) return false;

    const std::uintptr_t transform = ue::FindLiveObject("ScriptStruct", "Transform", nullptr);
    if (!transform) return false;
    std::vector<ue_reflect::FieldInfo> t;
    if (!ue_reflect::ResolveAll("Transform", transform, {"Rotation", "Translation"}, t) ||
        t[0].Size != sizeof(ue4::FQuat) || t[1].Size != sizeof(ue4::FVector)) {
        static bool s_said = false;
        if (!s_said) {
            s_said = true;
            Log::Line("player-rig: FTransform's layout did not resolve");
        }
        return false;
    }
    if (!g_componentToWorld.Resolve("SceneComponent", "K2_GetComponentToWorld",
                                    {{"ReturnValue", ue_reflect::StructSize(transform)}}))
        return false;
    // Not fatal: without it there is no un-zoomed reference and the pose is
    // never scaled, which is the right answer when the zoom state cannot be read.
    g_playbackPosition.Resolve("TimelineComponent", "GetPlaybackPosition",
                               {{"ReturnValue", sizeof(float)}});

    g_pawnOffset = pawn.Offset;
    g_transformRotation = t[0].Offset;
    g_transformTranslation = t[1].Offset;
    g_resolved = true;
    Log::Line("player-rig: resolved (Pawn=+0x%zx, FTransform Rotation=+0x%zx "
              "Translation=+0x%zx, zoom position %s)",
              g_pawnOffset, g_transformRotation, g_transformTranslation,
              g_playbackPosition.Ready() ? "readable" : "UNAVAILABLE");
    return true;
}

// A bool field that need not exist: an absent one reads as false rather than
// standing the whole rig down, so a build that renames one state flag loses
// that one check and keeps the rest.
bool OptionalBool(std::uintptr_t cls, const char* name, ue_reflect::FieldInfo& out) {
    if (!ue_reflect::FindPropertyInChain(cls, name, out) || out.TypeName != "BoolProperty")
        out = ue_reflect::FieldInfo{};
    return out.BoolMask != 0;
}

bool PointerField(std::uintptr_t cls, const char* name, ue_reflect::FieldInfo& out) {
    if (ue_reflect::FindPropertyInChain(cls, name, out) && out.Size == sizeof(std::uintptr_t))
        return true;
    out = ue_reflect::FieldInfo{};
    return false;
}

void RefreshPawnFields(std::uintptr_t pawn) {
    const std::uintptr_t cls = ue_call::ClassOf(pawn);
    if (!cls || cls == g_pawn.Class) return;
    g_pawn = PawnFields{};
    g_pawn.Class = cls;
    // Camera is the component the first-person view and the interaction trace
    // are both built from, and Zoom is the timeline that narrows its field of
    // view. Both are declared on mainPlayer_C, so a pawn that has neither is one
    // of the game's other pawns - the main menu's, an arcade cabinet - and the
    // gate closes on it without needing a flag of its own.
    const std::string name = ue::ObjectName(cls);
    const bool camera = PointerField(cls, "Camera", g_pawn.Camera);
    if (name == kRideClass) {
        if (camera && PointerField(cls, "Player", g_pawn.Rider)) g_pawn.Kind = PawnKind::Riding;
        PointerField(cls, "Timeline", g_pawn.Zoom);
    } else if (camera && PointerField(cls, "Zoom", g_pawn.Zoom)) {
        g_pawn.Kind = PawnKind::OnFoot;
    }
    Log::Line("player-rig: pawn class %s is %s", name.c_str(),
              g_pawn.Kind == PawnKind::OnFoot  ? "the player character"
              : g_pawn.Kind == PawnKind::Riding ? "the ATV, ridden"
              : name == kRideClass              ? "the ATV, but its Camera or Player did NOT resolve"
                                                : "not the player character");
}

void RefreshPlayerFields(std::uintptr_t player) {
    const std::uintptr_t cls = ue_call::ClassOf(player);
    if (!cls || cls == g_player.Class) return;
    g_player = PlayerFields{};
    g_player.Class = cls;
    std::string missing;
    const std::pair<const char*, ue_reflect::FieldInfo*> flags[] = {
        {"dead", &g_player.Dead},
        {"isRagdoll", &g_player.Ragdoll},
        {"isWakingUp", &g_player.WakingUp},
        {"deactivateMouseInput", &g_player.MouseOff},
    };
    for (const auto& f : flags)
        if (!OptionalBool(cls, f.first, *f.second)) missing += std::string(" ") + f.first;
    Log::Line("player-rig: state flags on %s%s", ue::ObjectName(cls).c_str(),
              missing.empty() ? " all found" : (" MISSING:" + missing).c_str());
}

void RefreshCameraFields(std::uintptr_t component) {
    const std::uintptr_t cls = ue_call::ClassOf(component);
    if (!cls || cls == g_camera.Class) return;
    g_camera = CameraFields{};
    g_camera.Class = cls;
    if (!ue_reflect::FindPropertyInChain(cls, "FieldOfView", g_camera.FieldOfView) ||
        g_camera.FieldOfView.TypeName != "FloatProperty")
        g_camera.FieldOfView = ue_reflect::FieldInfo{};
    Log::Line("player-rig: camera component %s (FieldOfView %s)", ue::ObjectName(cls).c_str(),
              g_camera.FieldOfView.Field ? "found" : "MISSING");
}

Transform ComponentTransform(std::uintptr_t component) {
    Transform out;
    if (!component) return out;
    ue_call::Frame frame(g_componentToWorld);
    if (!frame.Call(component)) return out;
    const unsigned char* ret = frame.At(0);
    ue4::FQuat q{};
    ue4::FVector p{};
    std::memcpy(&q, ret + g_transformRotation, sizeof(q));
    std::memcpy(&p, ret + g_transformTranslation, sizeof(p));
    out.Position = ue4::ToCore(p);
    out.Forward = ue::QuatRotateVec(ue4::ToCore(q), ue::FVector{1.0, 0.0, 0.0});
    const double len2 = out.Forward.X * out.Forward.X + out.Forward.Y * out.Forward.Y +
                        out.Forward.Z * out.Forward.Z;
    out.Valid = std::isfinite(out.Position.X) && std::isfinite(out.Position.Y) &&
                std::isfinite(out.Position.Z) && len2 > 0.98 && len2 < 1.02;
    return out;
}

std::uintptr_t ReadPointer(std::uintptr_t obj, const ue_reflect::FieldInfo& f) {
    std::uintptr_t v = 0;
    if (!f.Field || !ue::SafeReadPtr(obj + f.Offset, v)) return 0;
    return v;
}

// The un-zoomed field of view: the camera's own FieldOfView, sampled while the
// zoom timeline sits at its start. Held between samples, so a frame taken mid
// zoom still scales against the value the player's setting put there. Zero
// until the first resting sample, which leaves the zoom factor at 1.0.
//
// Held per camera. The ATV's camera rests at 100 degrees until its first zoom
// and at the rider's setting after it, so carrying the on-foot value onto it
// would scale every ride by the ratio of the two.
std::uintptr_t g_baseFovCamera = 0;
float g_baseFov = 0.0f;

bool RestingZoom(std::uintptr_t zoom) {
    if (!zoom || !g_playbackPosition.Ready()) return false;
    ue_call::Frame frame(g_playbackPosition);
    if (!frame.Call(zoom)) return false;
    return frame.Get<float>(0) <= 0.0001f;
}

void RefreshBaseFov(std::uintptr_t pawn, std::uintptr_t camera) {
    if (!camera || !g_camera.FieldOfView.Field) return;
    if (camera != g_baseFovCamera) {
        g_baseFovCamera = camera;
        g_baseFov = 0.0f;
    }
    if (!RestingZoom(ReadPointer(pawn, g_pawn.Zoom))) return;
    float fov = 0.0f;
    if (!ue::SafeReadFloat(camera + g_camera.FieldOfView.Offset, fov)) return;
    if (!(fov >= 10.0f && fov <= 170.0f) || fov == g_baseFov) return;
    Log::Line("player-rig: un-zoomed field of view is %.2f degrees (was %.2f)", fov, g_baseFov);
    g_baseFov = fov;
}

}  // namespace

Snapshot Read(std::uintptr_t controller) {
    Snapshot s;
    if (!Resolve(controller)) return s;

    std::uintptr_t pawn = 0;
    if (!ue::SafeReadPtr(controller + g_pawnOffset, pawn) || !pawn) return s;
    RefreshPawnFields(pawn);
    if (g_pawn.Kind == PawnKind::Other) return s;

    const std::uintptr_t player =
        g_pawn.Kind == PawnKind::Riding ? ReadPointer(pawn, g_pawn.Rider) : pawn;
    if (!player) return s;
    RefreshPlayerFields(player);

    s.Pawn = pawn;
    s.Player = player;
    s.Riding = g_pawn.Kind == PawnKind::Riding;
    const std::uintptr_t camera = ReadPointer(pawn, g_pawn.Camera);
    if (camera) RefreshCameraFields(camera);
    s.Camera = ComponentTransform(camera);

    RefreshBaseFov(pawn, camera);
    if (g_baseFov > 0.0f) {
        s.HaveFov = true;
        s.BaseFov = g_baseFov;
    }

    s.HaveState = true;
    ue_reflect::ReadBool(player, g_player.Dead, s.Dead);
    ue_reflect::ReadBool(player, g_player.Ragdoll, s.Ragdoll);
    ue_reflect::ReadBool(player, g_player.WakingUp, s.WakingUp);
    ue_reflect::ReadBool(player, g_player.MouseOff, s.MouseInputOff);
    return s;
}

}  // namespace votv_ht::player_rig
