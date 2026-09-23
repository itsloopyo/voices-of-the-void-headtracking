// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Every build of Voices of the Void this mod knows about. The game ships as a
// direct download (itch.io / the YeetPatch updater), so there is one store and
// one file. Append-only: a patch gets a new kItchProfile_<date> at the top of
// kKnownProfiles in build_registry.cpp, and the profiles below stay for players
// who have not updated.

namespace votv_ht::builds
{
    // VotV-Win64-Shipping.exe, Unreal Engine 4.27.2. PE TimeDateStamp
    // 0x6a08597d is 2026-05-16.
    extern const BuildProfile kItchProfile_20260516 = {
        "itch-win64-20260516",
        { 0x6A08597Du, 0x0538F000u, 0x050DB020u },
        {
            // APlayerController::GetPlayerViewPoint, slot +0x718 of the
            // APlayerController vtable (0x04100478). Reached from
            // execGetActorEyesViewPoint's dispatch of slot +0x608, then the
            // class-registration chain: APlayerController::GetPrivateStaticClass
            // (0x030c9f60) passes its class constructor, whose tail jump lands
            // on the constructor that stores the vtable. The body is stock:
            // NAME_Spectating (0x142), Role at +0xf0, PlayerCameraManager at
            // +0x2b8, CameraCachePrivate.TimeStamp at PCM+0x1ae0,
            // GetCameraViewPoint at PCM vtable +0x708. Nothing calls it
            // directly - it is only ever dispatched through the vtable.
            0x02dc4010ULL,

            // Slot 1 is ULocalPlayer::GetViewPoint, the render path. Its call
            // site stores PlayerCameraManager->GetFOVAngle() (PCM vtable
            // +0x6d0) into OutViewInfo+0x18 and then passes OutViewInfo+0x00 /
            // +0x0c as the out-params, which is the only caller whose rotation
            // sits 0x0c after its location. The rest are captured in game with
            // inject mode 0.
            {{
                0x02c22212ULL,  // 1: ULocalPlayer::GetViewPoint - render path
                // 2..6 are the other five callers inject mode 0 counted in
                // game, so a session can aim the injection at each in turn.
                // Only slot 1 passes an FMinimalViewInfo - its rotation sits
                // 0x0c after its location - and only slot 1 draws the frame:
                // injecting any of the others leaves the view where the game
                // put it. The out-params they pass are 0x70, 0x10, 0x40 and
                // 0x10 apart, and slot 6's are not a pair at all.
                0x02cccb42ULL, 0x02dbf98bULL, 0x02c0c09fULL, 0x02b424a0ULL,
                0x00b5359bULL, 0x0ULL, 0x0ULL,
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            inject::kFirstCaller,

            // FMinimalViewInfo: Location 0x00, Rotation 0x0c, FOV 0x18, read
            // off APlayerCameraManager::GetCameraViewPoint (which copies the
            // cached POV's two halves) and the GetFOVAngle store above it.
            { 0x18, 0x0c },

            {
                // UObject::ProcessEvent (0x01465930) opens by comparing
                // this->InternalIndex (+0x0c) against NumElements at
                // 0x04d8f934 and loading the chunk table from 0x04d8f920,
                // indexed (idx >> 16) * 8 then (idx & 0xffff) * 0x18.
                // FUObjectArray::AllocateUObjectIndex confirms the layout:
                // ObjObjects is GUObjectArray+0x10 and its NumElements
                // GUObjectArray+0x24.
                0x04d8f920ULL,
                0x14,
                0x18,
                0x10000,
                // The pool every FName accessor lea's into rcx before calling
                // the pool constructor at 0x01270dd0, with Blocks[] at +0x10.
                0x04d535c0ULL,
                0x10,
                0x10,
                0x18,
                0x20,
            },

            // UObject::ProcessEvent. Identified by the InternalIndex/NumElements
            // compare above, the FUNC_Net test on Function->FunctionFlags
            // (+0xb0), and being the callee of AActor::ProcessEvent
            // (0x028d4530).
            0x01465930ULL,

            // UE 4.27 FField / FProperty layout: Owner at +0x10 is a 16-byte
            // FFieldVariant, so Next sits at +0x20 and Offset_Internal at +0x4c.
            // UStruct carries FStructBaseChain after UField, which is what puts
            // SuperStruct at +0x40 rather than +0x30. Verified in game by
            // ue_reflect::VerifyBoolLayout.
            {
                0x08,   // kFField_ClassPrivate
                0x20,   // kFField_Next
                0x28,   // kFField_NamePrivate
                0x38,   // kFProperty_ArrayDim
                0x3c,   // kFProperty_ElementSize
                0x4c,   // kFProperty_Offset
                0x00,   // kFFieldClass_Name
                0x40,   // kUStruct_SuperStruct
                0x50,   // kUStruct_ChildProperties
                0x58,   // kUStruct_PropertiesSize
                0x78,   // kFBoolProperty_FieldSize
                0x78,   // kFStructProperty_Struct
            },
        },
    };
}
