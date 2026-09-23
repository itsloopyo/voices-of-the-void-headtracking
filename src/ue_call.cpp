// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ue_call.h"

#include "builds/build_registry.h"
#include "logging.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::ue_call {

namespace ue = ::cameraunlock::unreal;

// FUObjectItem is Object, Flags, ClusterRootIndex, SerialNumber.
constexpr std::size_t kFUObjectItem_SerialNumber = 0x10;

constexpr const char* kDefaultObjectPrefix = "Default__";

bool Function::Resolve(const char* outer, const char* name, std::initializer_list<Param> params) {
    const std::uintptr_t fn = ue::FindLiveObject("Function", name, outer);
    if (!fn) {
        if (!m_reported) {
            m_reported = true;
            Log::Line("ue-call: %s::%s is not in the object table", outer, name);
        }
        return false;
    }
    std::vector<std::string> names;
    for (const Param& p : params) names.emplace_back(p.Name);
    std::vector<ue_reflect::FieldInfo> fields;
    if (!names.empty() && !ue_reflect::ResolveAll(name, fn, names, fields)) return false;

    // TryStructSize rather than StructSize: the latter answers 0 both for a
    // parameterless function and for a PropertiesSize that did not read, and
    // ProcessEvent fills this function's real frame whatever the mod measured.
    // A frame that did not measure is not one to dispatch into.
    std::size_t size = 0;
    const bool measured = ue_reflect::TryStructSize(fn, size);
    if (!measured || size > kMaxFrame) {
        if (!m_reported) {
            m_reported = true;
            if (!measured)
                Log::Line("ue-call: %s::%s's PropertiesSize did not read - not dispatching "
                          "into a frame of unknown size", outer, name);
            else
                Log::Line("ue-call: %s::%s has a %zu-byte frame, over the %zu this mod allocates",
                          outer, name, size, kMaxFrame);
        }
        return false;
    }
    std::size_t i = 0;
    for (const Param& p : params) {
        if (!ue_reflect::FieldFits(fields[i], p.Bytes, size)) {
            if (!m_reported) {
                m_reported = true;
                Log::Line("ue-call: %s::%s.%s is %zu bytes at +0x%zx, too narrow for %zu",
                          outer, name, p.Name, fields[i].Size, fields[i].Offset, p.Bytes);
            }
            return false;
        }
        ++i;
    }
    m_offsets.clear();
    for (const auto& f : fields) m_offsets.push_back(f.Offset);
    m_size = size;
    m_fn = fn;
    return true;
}

bool Function::Call(std::uintptr_t self, unsigned char* frame) const {
    if (!m_fn || !self) return false;
    return ue_vm::Dispatch(reinterpret_cast<void*>(self), reinterpret_cast<void*>(m_fn), frame);
}

std::uintptr_t DefaultObject(const char* className) {
    const std::string cdo = std::string(kDefaultObjectPrefix) + className;
    return ue::FindLiveObject(className, cdo.c_str(), nullptr);
}

bool IsDefaultObject(std::uintptr_t obj) {
    return ue::ObjectName(obj).rfind(kDefaultObjectPrefix, 0) == 0;
}

std::uintptr_t ClassOf(std::uintptr_t obj) {
    std::uintptr_t cls = 0;
    if (!obj || !ue::SafeReadPtr(obj + ue::Layout().kClassPrivate, cls)) return 0;
    return cls;
}

std::uintptr_t WorldOf(std::uintptr_t actor) {
    return ue::OuterObject(ue::OuterObject(actor));
}

bool IsA(std::uintptr_t obj, const char* className) {
    std::uintptr_t cls = ClassOf(obj);
    for (int depth = 0; cls && depth < 32; ++depth) {
        if (ue::ObjectName(cls) == className) return true;
        std::uintptr_t super = 0;
        if (!ue::SafeReadPtr(cls + Offsets().Reflection.kUStruct_SuperStruct, super)) return false;
        cls = super;
    }
    return false;
}

bool IsInstanceOf(std::uintptr_t obj, std::uintptr_t cls) {
    std::uintptr_t cur = ClassOf(obj);
    for (int depth = 0; cur && depth < 32; ++depth) {
        if (cur == cls) return true;
        std::uintptr_t super = 0;
        if (!ue::SafeReadPtr(cur + Offsets().Reflection.kUStruct_SuperStruct, super)) return false;
        cur = super;
    }
    return false;
}

std::uintptr_t ObjectItem(std::int32_t objectIndex) {
    if (objectIndex < 0) return 0;
    const auto& layout = ue::Layout();
    // The runtime has to be set and the layout's two divisors non-zero: both are
    // plain fields a build profile fills, and a zero one faults with an integer
    // divide by zero, outside every SafeRead guard.
    if (ue::ModuleBase() == 0 || layout.kChunkNumElems == 0 || layout.kFUObjectItemSize == 0)
        return 0;
    const auto index = static_cast<std::uintptr_t>(objectIndex);
    const std::uintptr_t objArr = ue::ModuleBase() + layout.kObjObjects;
    std::uintptr_t chunks = 0, chunk = 0;
    std::uint32_t num = 0;
    if (!ue::SafeReadPtr(objArr, chunks) || !chunks) return 0;
    // The index comes out of engine memory - a TWeakObjectPtr in an FHitResult,
    // a widget's InternalIndex - so it is bounded against the table's own
    // element count rather than trusted. Without this an out-of-range index
    // reads a pointer from past the end of the chunk array and hands back an
    // item address built out of whatever was there.
    if (!ue::SafeReadU32(objArr + layout.kObjObjects_Num, num) || index >= num) return 0;
    if (!ue::SafeReadPtr(chunks + (index / layout.kChunkNumElems) * sizeof(std::uintptr_t), chunk) ||
        !chunk)
        return 0;
    return chunk + (index % layout.kChunkNumElems) * layout.kFUObjectItemSize;
}

std::uintptr_t ResolveWeak(std::int32_t objectIndex, std::int32_t serialNumber) {
    if (serialNumber == 0) return 0;
    const std::uintptr_t item = ObjectItem(objectIndex);
    if (!item) return 0;
    std::uintptr_t obj = 0;
    std::uint32_t serial = 0;
    if (!ue::SafeReadPtr(item, obj) || !ue::SafeReadU32(item + kFUObjectItem_SerialNumber, serial))
        return 0;
    return static_cast<std::int32_t>(serial) == serialNumber ? obj : 0;
}

}  // namespace votv_ht::ue_call
