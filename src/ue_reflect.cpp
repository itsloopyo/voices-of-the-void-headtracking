// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ue_reflect.h"

#include <cstdint>
#include <cstdio>

#include "builds/build_registry.h"
#include "logging.h"

#include "cameraunlock/memory/safe_memory.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::ue_reflect {

namespace {

namespace ue = ::cameraunlock::unreal;

// A property chain longer than this is not a property chain. UE's largest
// reflected structs are a few hundred fields; anything past this means the
// Next offset is wrong and the walk is following heap noise, so it stops
// rather than reading until it faults.
constexpr int kMaxChain = 4096;

std::string FieldName(std::uintptr_t field) {
    std::uint32_t id = 0;
    if (!ue::SafeReadU32(field + Offsets().Reflection.kFField_NamePrivate, id)) return {};
    return ue::ResolveFName(id);
}

std::string FieldTypeName(std::uintptr_t field) {
    std::uintptr_t cls = 0;
    if (!ue::SafeReadPtr(field + Offsets().Reflection.kFField_ClassPrivate, cls) || !cls)
        return {};
    std::uint32_t id = 0;
    if (!ue::SafeReadU32(cls + Offsets().Reflection.kFFieldClass_Name, id)) return {};
    return ue::ResolveFName(id);
}

// Everything recorded about one field: two FName resolves, so two strings off
// the heap, plus three dword reads. Filled for a field that has already MATCHED
// rather than for every field walked past - see FindProperty.
FieldInfo ReadField(std::uintptr_t field) {
    const auto& r = Offsets().Reflection;
    FieldInfo info;
    info.Field    = field;
    info.Name     = FieldName(field);
    info.TypeName = FieldTypeName(field);

    std::uint32_t offset = 0, elementSize = 0, arrayDim = 0;
    if (ue::SafeReadU32(field + r.kFProperty_Offset, offset) &&
        ue::SafeReadU32(field + r.kFProperty_ElementSize, elementSize) &&
        ue::SafeReadU32(field + r.kFProperty_ArrayDim, arrayDim)) {
        info.Offset = offset;
        info.Size   = static_cast<std::size_t>(elementSize) * (arrayDim ? arrayDim : 1);
    }
    if (info.TypeName == "BoolProperty") {
        // FieldSize, ByteOffset, ByteMask, FieldMask.
        std::uint32_t packed = 0;
        if (ue::SafeReadU32(field + r.kFBoolProperty_FieldSize, packed)) {
            const std::uint8_t fieldSize = packed & 0xFF;
            const std::uint8_t byteOffset = (packed >> 8) & 0xFF;
            const std::uint8_t byteMask = (packed >> 16) & 0xFF;
            const std::uint8_t fieldMask = (packed >> 24) & 0xFF;
            if (fieldSize >= 1 && fieldSize <= 8 && byteOffset < fieldSize && byteMask != 0 &&
                (fieldMask == 0xFF || fieldMask == byteMask)) {
                info.Offset += byteOffset;
                info.BoolMask = byteMask;
            }
        }
    }
    return info;
}

// The FField chain hanging off a UStruct, in declaration order. `visit(field)`
// returns true to stop early.
template <typename Fn>
void ForEachField(std::uintptr_t ustruct, Fn&& visit) {
    if (!ustruct) return;
    const auto& r = Offsets().Reflection;

    std::uintptr_t field = 0;
    if (!ue::SafeReadPtr(ustruct + r.kUStruct_ChildProperties, field)) return;

    for (int i = 0; field && i < kMaxChain; ++i) {
        if (visit(field)) return;
        std::uintptr_t next = 0;
        if (!ue::SafeReadPtr(field + r.kFField_Next, next)) return;
        field = next;
    }
}

}  // namespace

bool TryStructSize(std::uintptr_t ustruct, std::size_t& out) {
    if (!ustruct) return false;
    std::uint32_t size = 0;
    if (!ue::SafeReadU32(ustruct + Offsets().Reflection.kUStruct_PropertiesSize, size))
        return false;
    out = size;
    return true;
}

std::size_t StructSize(std::uintptr_t ustruct) {
    std::size_t size = 0;
    return TryStructSize(ustruct, size) ? size : 0;
}

std::vector<FieldInfo> Properties(std::uintptr_t ustruct) {
    std::vector<FieldInfo> out;
    ForEachField(ustruct, [&out](std::uintptr_t field) {
        out.push_back(ReadField(field));
        return false;
    });
    return out;
}

// Walks the chain and reads only each field's NAME until one matches, rather
// than materialising the whole property table and searching it. The table is
// two heap strings per field, and the callers ask repeatedly against the same
// struct - ResolveAll once per name it wants, FindPropertyInChain once per
// class above the leaf - so building it per lookup was the whole cost of
// resolving a ten-parameter UFunction.
bool FindProperty(std::uintptr_t ustruct, const char* name, FieldInfo& out) {
    bool found = false;
    ForEachField(ustruct, [&](std::uintptr_t field) {
        if (!ue::EqualsCI(FieldName(field), name)) return false;
        out = ReadField(field);
        found = true;
        return true;
    });
    return found;
}

bool FindPropertyInChain(std::uintptr_t ustruct, const char* name, FieldInfo& out) {
    const auto& r = Offsets().Reflection;
    std::uintptr_t cur = ustruct;
    // Bounded rather than "until SuperStruct is null": a wrong kUStruct_SuperStruct
    // reads a pointer-shaped field that happens to point back into the object
    // graph, and the walk then never terminates.
    for (int depth = 0; cur && depth < 32; ++depth) {
        if (FindProperty(cur, name, out)) return true;
        std::uintptr_t super = 0;
        if (!ue::SafeReadPtr(cur + r.kUStruct_SuperStruct, super)) return false;
        cur = super;
    }
    return false;
}

std::uintptr_t StructOf(const FieldInfo& f) {
    if (f.TypeName != "StructProperty" || !f.Field) return 0;
    std::uintptr_t st = 0;
    if (!ue::SafeReadPtr(f.Field + Offsets().Reflection.kFStructProperty_Struct, st)) return 0;
    return st;
}

namespace {

// True once `label` has had a fault reported, and marks it as reported when it
// has not. Every resolve in this mod sits inside a 250ms ResolveRetry loop, so
// a layout fault that does not clear writes its line and the property dump
// under it four times a second for the rest of the session - burying the build
// match, the link and the gate transitions a report is read for. Callers all
// run on the game thread (the view hook and the ProcessEvent tick), so the
// static needs no lock.
bool AlreadyReported(const char* label) {
    static std::vector<std::string> reported;
    for (const std::string& seen : reported)
        if (seen == label) return true;
    reported.emplace_back(label);
    return false;
}

}  // namespace

void DumpProperties(const char* label, std::uintptr_t ustruct) {
    const std::size_t size = StructSize(ustruct);
    Log::Line("reflect: %s @0x%llx PropertiesSize=%zu", label,
        static_cast<unsigned long long>(ustruct), size);
    for (const FieldInfo& f : Properties(ustruct)) {
        Log::Line("reflect:   +0x%03zx size %-4zu %-18s %s",
            f.Offset, f.Size, f.TypeName.c_str(), f.Name.c_str());
    }
}

bool ResolveAll(const char* label, std::uintptr_t ustruct,
                const std::vector<std::string>& names,
                std::vector<FieldInfo>& out) {
    out.clear();
    // The fault is composed rather than written where it is found, so every way
    // out of here reports through the one gate below.
    char reason[256] = {};
    const std::size_t size = StructSize(ustruct);
    // A reflected struct with no size, or one bigger than any real UE struct,
    // says PropertiesSize is not where the profile claims. Everything below
    // would then be measured against nonsense.
    if (size == 0 || size > 0x10000) {
        std::snprintf(reason, sizeof(reason),
                      "%s has PropertiesSize=%zu, which is not a struct size - the "
                      "ReflectionLayout in this build profile does not fit this game build",
                      label, size);
    } else {
        for (const std::string& want : names) {
            FieldInfo f;
            if (!FindProperty(ustruct, want.c_str(), f)) {
                std::snprintf(reason, sizeof(reason), "%s has no property named %s", label,
                              want.c_str());
                break;
            }
            // FieldFits rather than `f.Offset + f.Size > size`: both numbers are
            // read out of process memory that may not be a property table, and the
            // sum of two such values can wrap past an addition-based check.
            if (f.Size == 0 || !FieldFits(f, f.Size, size)) {
                std::snprintf(reason, sizeof(reason),
                              "%s.%s lands at +0x%zx size %zu, outside its own "
                              "PropertiesSize %zu",
                              label, want.c_str(), f.Offset, f.Size, size);
                break;
            }
            out.push_back(f);
        }
    }

    if (reason[0] == '\0') return true;
    out.clear();
    if (!AlreadyReported(label)) {
        Log::Line("reflect: %s", reason);
        DumpProperties(label, ustruct);
    }
    return false;
}

bool ReadBool(std::uintptr_t obj, const FieldInfo& f, bool& out) {
    if (f.BoolMask == 0 || !obj) return false;
    // One byte, because the mask is a byte mask. A uint16 read of a flag sitting
    // on the last byte of a committed page faults, and every caller here treats
    // a failed read as the blocking answer - so the wider read stands tracking
    // down on a flag that was perfectly readable.
    std::uint8_t byte = 0;
    if (!cameraunlock::memory::SafeReadU8(obj + f.Offset, byte)) return false;
    out = (byte & f.BoolMask) != 0;
    return true;
}

bool VerifyBoolLayout(std::uintptr_t playerControllerClass) {
    FieldInfo cursor, click;
    const bool found = FindPropertyInChain(playerControllerClass, "bShowMouseCursor", cursor) &&
                       FindPropertyInChain(playerControllerClass, "bEnableClickEvents", click);
    const bool ok = found && cursor.BoolMask == 0x01 && click.BoolMask == 0x02 &&
                    cursor.Offset == click.Offset;
    // Said once. The caller retries every 250ms until every other part of the
    // rig resolves too, and this layout is the same on every attempt.
    if (!AlreadyReported("bool layout"))
        Log::Line("reflect: bool layout %s (bShowMouseCursor +0x%zx mask 0x%02x, bEnableClickEvents "
                  "+0x%zx mask 0x%02x)", ok ? "verified" : "DOES NOT MATCH this build",
                  cursor.Offset, cursor.BoolMask, click.Offset, click.BoolMask);
    return ok;
}

}  // namespace votv_ht::ue_reflect
