// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

// One UFUNCTION, resolved by name once and dispatched through ProcessEvent.
//
// Every parameter slot is located through the engine's reflection data and
// checked to be at least as wide as the C++ type written into or read out of
// it, so a frame is never written at a guessed offset.
namespace votv_ht::ue_call {

struct Param {
    const char* Name;
    std::size_t Bytes;
};

class Function {
public:
    // Finds `outer`::`name` in the object table and checks every parameter.
    // Logs and returns false on a missing function or a slot that does not fit.
    // Every caller sits in a 250ms ResolveRetry loop, so the log line is written
    // once per function and the retries after it are silent.
    bool Resolve(const char* outer, const char* name, std::initializer_list<Param> params);

    bool Ready() const { return m_fn != 0; }
    std::size_t FrameSize() const { return m_size; }
    std::size_t Offset(std::size_t index) const { return m_offsets[index]; }

    // `frame` must be FrameSize() bytes, already filled. False when the
    // dispatch faulted.
    bool Call(std::uintptr_t self, unsigned char* frame) const;

    static constexpr std::size_t kMaxFrame = 1024;

private:
    std::uintptr_t m_fn = 0;
    std::size_t m_size = 0;
    std::vector<std::size_t> m_offsets;
    bool m_reported = false;
};

// A reusable, zeroed parameter frame for one Function.
//
// The WHOLE buffer is zeroed, not the FrameSize() prefix of it. ProcessEvent
// fills and reads the UFunction's own parameter frame whatever size the mod
// measured, so a function that measured short - or did not measure at all, which
// leaves FrameSize() at zero - would otherwise hand the engine whatever the
// stack last held as its arguments, with every pointer-typed parameter garbage.
// A memset of a kilobyte costs nothing against a script VM dispatch.
class Frame {
public:
    explicit Frame(const Function& fn) : m_fn(fn) { std::memset(m_buf, 0, sizeof(m_buf)); }

    template <typename T>
    void Set(std::size_t index, const T& value) {
        std::memcpy(m_buf + m_fn.Offset(index), &value, sizeof(T));
    }
    template <typename T>
    T Get(std::size_t index) const {
        T value{};
        std::memcpy(&value, m_buf + m_fn.Offset(index), sizeof(T));
        return value;
    }
    unsigned char* Data() { return m_buf; }
    const unsigned char* At(std::size_t index) const { return m_buf + m_fn.Offset(index); }
    bool Call(std::uintptr_t self) { return m_fn.Call(self, m_buf); }

private:
    const Function& m_fn;
    alignas(16) unsigned char m_buf[Function::kMaxFrame];
};

// Class default object of a native class, e.g. "KismetSystemLibrary".
std::uintptr_t DefaultObject(const char* className);

// True for a class default object. A CDO carries the same properties as a live
// instance and is never drawn or ticked, so a search for a live object skips it.
bool IsDefaultObject(std::uintptr_t obj);

// The class of a live object, or 0.
std::uintptr_t ClassOf(std::uintptr_t obj);

// The UWorld a level actor lives in: the actor's outer is its ULevel, and the
// level's outer is the world. 0 when either link does not read.
std::uintptr_t WorldOf(std::uintptr_t actor);

// True when `className` is the object's class or one of its super classes.
// Dispatching a UFUNCTION on an object of another class runs its exec thunk on
// the wrong layout, so a caller that cannot be sure of the class asks this first.
bool IsA(std::uintptr_t obj, const char* className);

// The same test against a class already resolved to its UClass. No names are
// read, so it is the one to use across a walk of the whole object table.
bool IsInstanceOf(std::uintptr_t obj, std::uintptr_t cls);

// The FUObjectItem for a global object index, or 0 when the index is outside
// the table or the walk to it does not read. The object itself is the first
// pointer in the item.
std::uintptr_t ObjectItem(std::int32_t objectIndex);

// The object a TWeakObjectPtr {ObjectIndex, SerialNumber} names, or 0 when the
// slot is empty or reused.
std::uintptr_t ResolveWeak(std::int32_t objectIndex, std::int32_t serialNumber);

}  // namespace votv_ht::ue_call
