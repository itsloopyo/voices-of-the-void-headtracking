// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Read the engine's own reflection data for a UStruct - a UFunction's parameter
// list, or a UScriptStruct's fields.
//
// Why this exists rather than a table of hardcoded struct offsets: the mod calls
// UKismetSystemLibrary::LineTraceSingle through the script VM and reads
// FHitResult::ImpactPoint out of the result. Both layouts are long, both moved
// between engine versions, and getting one field
// wrong produces an aim point that is plausible-looking garbage - the exact
// failure the reticle doctrine says costs days. The engine already stores the
// answer, keyed by name, in data this mod can read. So it asks.
//
// The only layout constants in the mod are the ones needed to read THAT data,
// and they live in the build profile's ReflectionLayout. Every one of them is
// checked before use: a UFunction whose parameter names do not resolve to
// plausible ascending offsets inside its own PropertiesSize is reported and the
// feature that needed it stands down, rather than writing into a buffer at
// offsets read out of noise.
namespace votv_ht::ue_reflect {

struct FieldInfo {
    std::uintptr_t Field = 0;
    std::string    Name;
    std::string    TypeName;   // FFieldClass name: "StructProperty", "BoolProperty", ...
    std::size_t    Offset = 0;
    std::size_t    Size = 0;   // ElementSize * ArrayDim
    // BoolProperty only: the mask within the byte at Offset. 0 when the field is
    // not a bool or its mask did not read.
    std::uint8_t   BoolMask = 0;
};

// Read a BoolProperty through its field mask, so a bitfield bool is not
// confused with its neighbours. False when the field is not a bool or the
// object does not read.
bool ReadBool(std::uintptr_t obj, const FieldInfo& f, bool& out);

// Check the FBoolProperty layout against APlayerController's bitfield, whose
// masks are fixed by declaration order (bShowMouseCursor 1, bEnableClickEvents
// 2, both in one byte). Logs and returns false when the layout is wrong.
bool VerifyBoolLayout(std::uintptr_t playerControllerClass);

// True when a fixed-size read or write of `bytes` at `f.Offset` stays inside a
// frame of `frame_size` AND the engine's own size for the field is at least
// that wide.
//
// ResolveAll answers a different question: it proves a named field sits inside
// its own struct at the size THE ENGINE reports. That says nothing about the
// fixed-size type a caller is about to memcpy into the slot. A parameter frame
// is written into a stack buffer at these offsets, so a field the caller treats
// as a pointer or an FVector while the engine calls it something narrower puts
// the tail of that write past the end of the buffer. Ask this before every such
// write, and stand the feature down rather than perform it.
//
// Subtraction rather than `Offset + bytes <= frame_size`: the offsets come from
// process memory that may not be a property table at all, and the sum of two
// values read out of noise can wrap past the check.
inline bool FieldFits(const FieldInfo& f, std::size_t bytes, std::size_t frame_size) {
    if (f.Size < bytes) return false;
    if (f.Offset > frame_size) return false;
    return frame_size - f.Offset >= bytes;
}

// Total size of a UStruct's instance data (UStruct::PropertiesSize). For a
// UFunction this covers the whole parameter frame including any locals, so it
// is the size to allocate for a ProcessEvent call.
std::size_t StructSize(std::uintptr_t ustruct);

// The same read, with the failure told apart from the answer. A UFunction with
// no parameters legitimately measures zero, so a caller that sizes a buffer
// from this cannot use zero alone to mean "the read did not work" - and
// ProcessEvent fills the function's real frame whatever the caller measured.
bool TryStructSize(std::uintptr_t ustruct, std::size_t& out);

// Every reflected property of a UStruct, in declaration order. Empty when the
// struct pointer or the reflection layout does not read.
std::vector<FieldInfo> Properties(std::uintptr_t ustruct);

// One named property. False when it is absent.
bool FindProperty(std::uintptr_t ustruct, const char* name, FieldInfo& out);

// One named property, searching the SuperStruct chain as well. A live player
// controller is some game subclass, and the fields the mod reads off it
// (AController::Pawn) are declared several classes up, so a search of the
// leaf class alone finds nothing.
bool FindPropertyInChain(std::uintptr_t ustruct, const char* name, FieldInfo& out);

// The UScriptStruct a StructProperty holds, or 0 when `f` is not a struct
// property or the pointer does not read.
std::uintptr_t StructOf(const FieldInfo& f);

// Resolve several named properties at once and sanity-check the lot: every name
// present, every offset plus size inside StructSize(). Logs the whole table and
// returns false when any check fails, so a caller can stand its feature down
// with a diagnostic instead of writing into a buffer at guessed offsets.
bool ResolveAll(const char* label, std::uintptr_t ustruct,
                const std::vector<std::string>& names,
                std::vector<FieldInfo>& out);

// Log a struct's whole property table. Used by the discovery pass and when
// ResolveAll fails.
void DumpProperties(const char* label, std::uintptr_t ustruct);

}  // namespace votv_ht::ue_reflect
