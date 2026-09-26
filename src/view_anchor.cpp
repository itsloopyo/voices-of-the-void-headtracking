// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "view_anchor.h"

#include "logging.h"
#include "ue_call.h"
#include "ue_reflect.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::view_anchor {
namespace {

namespace ue = cameraunlock::unreal;

ue_call::Function g_getLocation;
ue_call::Function g_setLocation;
ue_call::Function g_getCollision;
std::uintptr_t g_world = 0;
std::uintptr_t g_director = 0;
std::uintptr_t g_component = 0;
ue_reflect::FieldInfo g_anchorField;
bool g_fieldReady = false;
bool g_collisionFree = false;
bool g_moved = false;
ue4::FVector g_base{}, g_written{};

bool Resolve() {
    return g_getLocation.Resolve("SceneComponent", "K2_GetComponentLocation",
                                  {{"ReturnValue", sizeof(ue4::FVector)}}) &&
           g_setLocation.Resolve("SceneComponent", "K2_SetWorldLocation",
                                  {{"NewLocation", sizeof(ue4::FVector)}, {"bSweep", 1},
                                   {"SweepHitResult", 1}, {"bTeleport", 1}}) &&
           g_getCollision.Resolve("PrimitiveComponent", "GetCollisionEnabled",
                                   {{"ReturnValue", 1}});
}

bool Move(std::uintptr_t controller, const ue4::FVector& offset) {
    const auto world = ue_call::WorldOf(controller);
    if (!world) return false;
    if (world != g_world) {
        g_world = world;
        g_director = g_component = 0;
        g_fieldReady = g_moved = false;
        ue::ForEachUObject([&](std::uintptr_t object) {
            if (ue::ClassName(object) != "tutorial3_C" || ue_call::IsDefaultObject(object) ||
                ue_call::WorldOf(object) != world)
                return false;
            g_director = object;
            g_fieldReady = ue_reflect::FindPropertyInChain(ue_call::ClassOf(object), "renderThing",
                                                          g_anchorField) &&
                           g_anchorField.TypeName == "ObjectProperty" &&
                           g_anchorField.Size == sizeof(std::uintptr_t);
            return true;
        });
    }
    if (!g_director) return true;
    if (!g_fieldReady) return false;

    static const bool ready = Resolve();
    if (!ready) return false;
    std::uintptr_t component = 0;
    if (!ue::SafeReadPtr(g_director + g_anchorField.Offset, component) || !component)
        return false;
    if (component != g_component) {
        g_component = component;
        g_moved = false;
        g_collisionFree = false;
        if (!ue_call::IsA(component, "PrimitiveComponent")) return false;
        ue_call::Frame collision(g_getCollision);
        if (!collision.Call(component)) return false;
        g_collisionFree = collision.Get<std::uint8_t>(0) == 0;
    }
    if (!g_collisionFree) return false;

    ue_call::Frame location(g_getLocation);
    if (!location.Call(component)) return false;
    const auto current = location.Get<ue4::FVector>(0);
    // The tutorial resets this invisible, collision-free mesh in front of the
    // clean eye each tick and checks that it rendered. A forward lean otherwise
    // passes it, making that check fail. Preserve a new game-authored position;
    // repeated view queries must not add the same lean a second time.
    if (!g_moved || current.X != g_written.X || current.Y != g_written.Y ||
        current.Z != g_written.Z)
        g_base = current;
    const ue4::FVector wanted{g_base.X + offset.X, g_base.Y + offset.Y, g_base.Z + offset.Z};
    ue_call::Frame frame(g_setLocation);
    frame.Set(0, wanted);
    frame.Set(3, static_cast<std::uint8_t>(1));
    if (!frame.Call(component)) return false;
    g_written = wanted;
    g_moved = true;
    return true;
}

}

bool Update(std::uintptr_t controller, const ue4::FVector& offset) {
    const bool moved = Move(controller, offset);
    static bool failed = false;
    if (!moved && !failed)
        Log::Line("view-anchor: could not align the tutorial's collision-free camera mesh; "
                  "positional tracking cannot be applied safely");
    else if (moved && failed)
        Log::Line("view-anchor: camera mesh available again");
    failed = !moved;
    return moved;
}

}
