// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What a parameter frame presents to the script VM before anything is written
// into it.
//
// Every engine call this mod makes hands ue_call::Frame's buffer straight to
// UObject::ProcessEvent, which fills and reads the UFunction's OWN parameter
// frame whatever size the mod measured for it. So the bytes the engine sees
// have to be zero all the way to the end of the buffer, not only as far as
// FrameSize(): a function whose PropertiesSize does not read leaves FrameSize()
// at zero, and every byte past it would then be whatever the stack last held -
// which is a pointer-typed parameter made of noise.
//
// Constructed over a poisoned buffer rather than on a bare stack, so the check
// fails on a frame that is not zeroed instead of passing on a stack that
// happened to be clean.

#include <cstddef>
#include <cstring>
#include <new>

#include "test_harness.h"
#include "ue_call.h"

namespace {

using votv_ht::ue_call::Frame;
using votv_ht::ue_call::Function;

constexpr unsigned char kPoison = 0xCC;

// Placement-new a Frame over `storage`, every byte of which is kPoison.
Frame* FrameOverPoison(unsigned char* storage, std::size_t bytes, const Function& fn) {
    std::memset(storage, kPoison, bytes);
    return new (storage) Frame(fn);
}

bool AllZero(const unsigned char* at, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i)
        if (at[i] != 0) return false;
    return true;
}

// A Function that never resolved measures a zero-byte frame. This is the case
// the whole-buffer zeroing exists for: the old constructor zeroed FrameSize()
// bytes, so all 1024 stayed poisoned.
void TestAnUnresolvedFunctionYieldsAFullyZeroedFrame() {
    const Function fn;
    CHECK_MSG(!fn.Ready(), "a default-constructed Function has not resolved");
    CHECK_MSG(fn.FrameSize() == 0, "and measures a zero-byte parameter frame");

    alignas(16) unsigned char storage[sizeof(Frame)];
    Frame* frame = FrameOverPoison(storage, sizeof(storage), fn);
    CHECK_MSG(AllZero(frame->Data(), Function::kMaxFrame),
              "ProcessEvent reads the function's real frame whatever the mod measured, so "
              "every byte of the buffer must be zero even when FrameSize() is 0");
}

// The same claim stated against the buffer's own end, which is where a short
// memset leaves the stack showing through.
void TestTheTailOfTheBufferIsZeroedToo() {
    const Function fn;
    alignas(16) unsigned char storage[sizeof(Frame)];
    Frame* frame = FrameOverPoison(storage, sizeof(storage), fn);

    const unsigned char* last = frame->Data() + Function::kMaxFrame - 64;
    CHECK_MSG(AllZero(last, 64), "the last 64 bytes of the frame are zero");
}

}  // namespace

int main() {
    TestAnUnresolvedFunctionYieldsAFullyZeroedFrame();
    TestTheTailOfTheBufferIsZeroedToo();

    return gr_test::Report();
}
