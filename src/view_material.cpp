// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "view_material.h"

#include "logging.h"
#include "ue_call.h"
#include "ue_reflect.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::view_material {
namespace {

namespace ue = cameraunlock::unreal;

struct LinearColor {
    float R, G, B, A;
};

struct StringView {
    const wchar_t* Data;
    std::int32_t Num, Max;
};

ue_call::Function g_setVector;
std::uint64_t g_parameterName = 0;
std::uintptr_t g_materialClass = 0;
std::uintptr_t g_playerClass = 0;
ue_reflect::FieldInfo g_materialField;
bool g_fieldReady = false;

bool Resolve() {
    ue_call::Function stringToName;
    if (!g_setVector.Resolve("MaterialInstanceDynamic", "SetVectorParameterValue",
                             {{"ParameterName", sizeof(g_parameterName)},
                              {"Value", sizeof(LinearColor)}}) ||
        !stringToName.Resolve("KismetStringLibrary", "Conv_StringToName",
                               {{"inString", sizeof(StringView)},
                                {"ReturnValue", sizeof(g_parameterName)}}))
        return false;

    const auto library = ue_call::DefaultObject("KismetStringLibrary");
    g_materialClass = ue::FindLiveObject("Class", "MaterialInstanceDynamic", nullptr);
    if (!library || !g_materialClass) return false;

    const StringView name{L"Param", 6, 6};
    ue_call::Frame frame(stringToName);
    frame.Set(0, name);
    if (!frame.Call(library)) return false;
    g_parameterName = frame.Get<std::uint64_t>(1);
    return g_parameterName != 0;
}

bool Write(std::uintptr_t player, const ue4::FVector& eye) {
    static const bool ready = Resolve();
    if (!ready) return false;

    const auto playerClass = ue_call::ClassOf(player);
    if (playerClass != g_playerClass) {
        g_playerClass = playerClass;
        g_fieldReady = ue_reflect::FindPropertyInChain(playerClass, "NewVar", g_materialField) &&
                       g_materialField.TypeName == "ObjectProperty" &&
                       g_materialField.Size == sizeof(std::uintptr_t);
    }
    if (!g_fieldReady) return false;

    std::uintptr_t material = 0;
    if (!ue::SafeReadPtr(player + g_materialField.Offset, material))
        return false;
    // The instance is absent in the main gameplay map.
    if (!material) return true;
    if (ue_call::ClassOf(material) != g_materialClass) return false;

    // mainPlayer_C.NewVar is its NewMaterial post-process instance. Param is the
    // world-space eye; leaving it at the clean camera position makes a leaned
    // tutorial view draw a checkerboard. Only this shader input follows the view.
    ue_call::Frame frame(g_setVector);
    frame.Set(0, g_parameterName);
    frame.Set(1, LinearColor{eye.X, eye.Y, eye.Z, 1.0f});
    return frame.Call(material);
}

}

bool Update(std::uintptr_t player, const ue4::FVector& eye) {
    if (!player) return true;
    const bool written = Write(player, eye);
    static bool failed = false;
    if (!written && !failed)
        Log::Line("view-material: could not update mainPlayer_C.NewVar.Param; "
                  "positional tracking cannot be applied safely");
    else if (written && failed)
        Log::Line("view-material: camera position parameter available again");
    failed = !written;
    return written;
}

}
