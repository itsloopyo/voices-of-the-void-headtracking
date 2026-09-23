// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ue4_types.h"

// The parameter frame of UKismetSystemLibrary::LineTraceSingle, resolved out of
// the engine's own reflection data and filled.
//
// Two features cast that ray: the aim trace, which finds the point the shot
// stops on, and the lean trace, which finds how far the eye may travel before it
// meets geometry. They ask for different things out of the FHitResult and act on
// the answers differently, but the nine parameters going IN are the same nine
// bytes-in-a-buffer either way, and a slot resolved at the wrong offset writes
// past the end of a stack buffer whichever caller did it. That resolution and
// that fill live here, once.
//
// What stays with each caller: which extra parameters it reads back, which
// FHitResult fields it resolves, what it ignores and how it retries.
namespace votv_ht::kismet_trace {

// The frame of a Kismet trace is a couple of hundred bytes. This is the ceiling
// the resolver checks PropertiesSize against, so a garbage size cannot turn into
// a stack overflow, and the size every caller's buffer is declared at.
constexpr std::size_t kMaxParams = 1024;

// Where the parameters both traces write sit in the frame.
struct Layout {
    std::size_t ParamsSize = 0;
    std::size_t WorldContext = 0;
    std::size_t Start = 0;
    std::size_t End = 0;
    std::size_t TraceChannel = 0;
    std::size_t TraceComplex = 0;
    std::size_t ActorsToIgnore = 0;
    std::size_t DrawDebugType = 0;
    std::size_t OutHit = 0;
    std::size_t IgnoreSelf = 0;
    // The engine's own size for the OutHit slot, for OutHitHolds().
    std::size_t OutHitSize = 0;
};

enum class Resolution {
    // The function or the library's default object is not in the object table
    // yet. The caller retries on its own interval.
    NotYet,
    Ok,
    // The frame is not the shape this mod writes into it. Permanent for the
    // session; the caller stands its feature down.
    Unusable,
};

// Find UKismetSystemLibrary::LineTraceSingle and its default object, resolve
// every parameter above, and check each slot against the width this mod writes
// there. `label` prefixes the log lines with the caller's name.
Resolution Resolve(const char* label, std::uintptr_t& cdo, std::uintptr_t& function,
                   Layout& out);

// Whether the OutHit slot can hold a whole FHitResult of `hitSize` bytes. Every
// field is read at OutHit + its own offset, so the slot has to hold the struct
// rather than merely start inside the frame.
bool OutHitHolds(const Layout& layout, std::size_t hitSize);

// One cast, in the terms the frame is written in.
struct Shot {
    // The pawn. With bIgnoreSelf set this excludes the player's own capsule,
    // which both rays start inside.
    std::uintptr_t WorldContext = 0;
    ue4::FVector Start{0.0f, 0.0f, 0.0f};
    ue4::FVector End{0.0f, 0.0f, 0.0f};
    int Channel = 0;
    // Per-triangle rather than against the collision hull: what the aim trace
    // needs so its point is on the surface drawn, and what the lean trace does
    // not.
    bool Complex = false;
    const std::uintptr_t* Ignore = nullptr;
    std::int32_t IgnoreCount = 0;
};

// Zero `buf` over the frame and write `shot` into it. `buf` must be kMaxParams
// bytes and 16-byte aligned: ProcessEvent hands it to engine code that reads its
// fields at their natural alignment.
void FillFrame(const Layout& layout, const Shot& shot, unsigned char* buf);

// FHitResult::Actor and ::Component as the frame carries them: a
// TWeakObjectPtr, which is an object index and a serial number side by side.
// ue_call::ResolveWeak turns one into the object it names.
struct WeakObject {
    std::int32_t Index = -1;
    std::int32_t Serial = 0;
};

// The width both traces require of those fields before reading one as a pair.
// A narrower field is not that pair, and reading one as if it were takes the
// bytes that follow it for an object index.
constexpr std::size_t kWeakObjectBytes = 2 * sizeof(std::int32_t);

inline WeakObject ReadWeak(const unsigned char* at) {
    WeakObject w;
    std::memcpy(&w.Index, at, sizeof(w.Index));
    std::memcpy(&w.Serial, at + sizeof(w.Index), sizeof(w.Serial));
    return w;
}

}  // namespace votv_ht::kismet_trace
