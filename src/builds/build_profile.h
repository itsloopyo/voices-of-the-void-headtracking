// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <cstddef>
#include <cstdint>

#include <cameraunlock/memory/pe_fingerprint.h>
#include <cameraunlock/unreal/ue_runtime.h>

#include "inject_mode.h"

// One shipped build of Voices of the Void: the PE fingerprint that identifies it and
// every per-build RVA and field offset the mod reads. No matching profile leaves
// the mod dormant. Append a profile for a new build; never edit an existing one.

namespace votv_ht
{
    using PeFingerprint = ::cameraunlock::memory::PeFingerprint;

    // The offsets needed to read the engine's own reflection data. Everything
    // else the mod touches (UFUNCTION frames, component fields) is asked for by
    // name through that data, so these are the only layout constants it holds.
    struct ReflectionLayout
    {
        std::size_t kFField_ClassPrivate;
        std::size_t kFField_Next;
        std::size_t kFField_NamePrivate;

        std::size_t kFProperty_ArrayDim;
        std::size_t kFProperty_ElementSize;
        std::size_t kFProperty_Offset;

        std::size_t kFFieldClass_Name;

        std::size_t kUStruct_SuperStruct;
        std::size_t kUStruct_ChildProperties;
        std::size_t kUStruct_PropertiesSize;

        // FBoolProperty's FieldSize/ByteOffset/ByteMask/FieldMask bytes,
        // which follow FProperty.
        std::size_t kFBoolProperty_FieldSize;

        // FStructProperty::Struct, which follows FProperty.
        std::size_t kFStructProperty_Struct;
    };

    struct OffsetTable
    {
        // APlayerController::GetPlayerViewPoint. Zero = profile incomplete.
        std::uintptr_t kGetPlayerViewPointRva;

        // Return-address RVAs of the GetPlayerViewPoint call sites, observed in
        // game with inject mode 0. Only the caller the inject mode selects gets
        // the head pose; every other caller reads the clean rotation, which is
        // what keeps the weapon, AI and replication on the mouse aim.
        inject::CallerRvas kKnownCallerRvas;

        int kDefaultInjectMode;

        // FMinimalViewInfo: the FOV offset from Location, and the
        // Location->Rotation gap the render caller's out-params must have.
        struct {
            std::size_t kFovOffset;
            std::size_t kRotationStride;
        } MinimalViewInfoLayout;

        ::cameraunlock::unreal::UObjectGlobalsLayout UObjectGlobals;

        // UObject::ProcessEvent.
        std::uintptr_t kProcessEventRva;

        ReflectionLayout Reflection;
    };

    struct BuildProfile
    {
        const char*   Name;
        PeFingerprint Fingerprint;
        OffsetTable   Offsets;
    };
}
