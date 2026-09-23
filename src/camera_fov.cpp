// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_fov.h"

#include <atomic>

#include "builds/build_registry.h"
#include "logging.h"
#include "ue_call.h"
#include "ue_reflect.h"

#include "cameraunlock/memory/safe_memory.h"
#include "cameraunlock/unreal/ue_runtime.h"

// ULocalPlayer::GetViewPoint, the render caller, fills its FMinimalViewInfo as
//
//     OutViewInfo      = CameraManager->GetCameraCacheView();
//     OutViewInfo.FOV  = CameraManager->GetFOVAngle();
//     PC->GetPlayerViewPoint(&OutViewInfo.Location, &OutViewInfo.Rotation);
//
// so the FOV this frame is drawn with is already in the struct when the hook
// runs, a fixed distance past the Location pointer it is handed.
namespace votv_ht::camera_fov {

namespace {

namespace ue = ::cameraunlock::unreal;

std::atomic<int> g_constraint{kConstraintUnknown};

std::uintptr_t g_playerClass = 0;
ue_reflect::FieldInfo g_playerField;
std::uintptr_t g_localPlayerClass = 0;
ue_reflect::FieldInfo g_constraintField;

}  // namespace

float ReadRenderFov(const void* outLocation, const void* outRotation) {
    const auto& mvi = Offsets().MinimalViewInfoLayout;
    const auto locAddr = reinterpret_cast<std::uintptr_t>(outLocation);
    const auto rotAddr = reinterpret_cast<std::uintptr_t>(outRotation);
    float fov = 0.0f;
    if (rotAddr - locAddr != mvi.kRotationStride ||
        !ue::SafeReadFloat(locAddr + mvi.kFovOffset, fov) || !Plausible(fov))
        return 0.0f;
    return fov;
}

void RefreshAspectConstraint(std::uintptr_t controller) {
    const std::uintptr_t controllerClass = ue_call::ClassOf(controller);
    if (!controllerClass) return;
    if (controllerClass != g_playerClass) {
        g_playerClass = controllerClass;
        if (!ue_reflect::FindPropertyInChain(controllerClass, "Player", g_playerField) ||
            g_playerField.Size != sizeof(std::uintptr_t))
            g_playerField = ue_reflect::FieldInfo{};
    }
    if (g_playerField.Size != sizeof(std::uintptr_t)) return;

    std::uintptr_t localPlayer = 0;
    if (!ue::SafeReadPtr(controller + g_playerField.Offset, localPlayer) || !localPlayer) return;
    const std::uintptr_t playerClass = ue_call::ClassOf(localPlayer);
    if (playerClass != g_localPlayerClass) {
        g_localPlayerClass = playerClass;
        if (!ue_reflect::FindPropertyInChain(playerClass, "AspectRatioAxisConstraint", g_constraintField) ||
            g_constraintField.Size != 1)
            g_constraintField = ue_reflect::FieldInfo{};
    }
    if (g_constraintField.Size != 1) return;

    // One byte, because the field is one byte. A uint16 read of a field on the
    // last byte of a committed page faults, and the constraint then reads as
    // unresolved - which stands the mark down on an ultrawide display.
    std::uint8_t raw = 0;
    if (!cameraunlock::memory::SafeReadU8(localPlayer + g_constraintField.Offset, raw)) return;
    const int value = static_cast<int>(raw);
    const int next = (value == kMaintainYFOV || value == kMaintainXFOV || value == kMajorAxisFOV)
                         ? value : kConstraintUnknown;
    const int previous = g_constraint.exchange(next, std::memory_order_relaxed);
    if (previous != next)
        Log::Line("fov: AspectRatioAxisConstraint=%d (%s)", next,
                  next == kMaintainYFOV ? "MaintainYFOV" : next == kMaintainXFOV ? "MaintainXFOV"
                  : next == kMajorAxisFOV ? "MajorAxisFOV" : "unreadable");
}

int AspectConstraint() { return g_constraint.load(std::memory_order_relaxed); }

}  // namespace votv_ht::camera_fov
