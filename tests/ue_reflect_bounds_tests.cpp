// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The guard that stands between the engine's reflection data and a fixed stack
// buffer.
//
// Every parameter frame this mod builds - the aim trace, the multiplayer gate,
// the ADS component lookup - is a byte array on the stack written at offsets
// read out of the running process. ResolveAll proves those offsets sit inside
// the struct at the width the ENGINE reports; FieldFits is the second question,
// which is whether the fixed-size type the mod is about to put there fits in the
// slot. The cases below are the ones a wrong ReflectionLayout produces: a slot
// narrower than the write, an offset at the very end of the frame, and offsets
// large enough that an addition-based bounds test would wrap past itself.

#include <cstddef>
#include <cstdint>
#include <limits>

#include "test_harness.h"
#include "ue_reflect.h"

namespace {

using votv_ht::ue_reflect::FieldFits;
using votv_ht::ue_reflect::FieldInfo;

FieldInfo Field(std::size_t offset, std::size_t size) {
    FieldInfo f;
    f.Offset = offset;
    f.Size = size;
    return f;
}

void TestAWellFormedSlotFits() {
    CHECK(FieldFits(Field(0, 8), 8, 64));
    CHECK(FieldFits(Field(16, 24), 24, 64));
}

// The exact end of the frame is in bounds - a return value is routinely the
// last thing in a UFunction's parameter frame.
void TestASlotEndingExactlyAtTheFrameEndFits() {
    CHECK(FieldFits(Field(56, 8), 8, 64));
    CHECK(FieldFits(Field(63, 1), 1, 64));
}

void TestASlotRunningOneBytePastTheFrameDoesNot() {
    CHECK(!FieldFits(Field(57, 8), 8, 64));
    CHECK(!FieldFits(Field(64, 1), 1, 64));
}

// The case the guard exists for: the field is inside the frame at the size the
// engine reports, and the mod is about to write something wider into it.
void TestASlotNarrowerThanTheWriteDoesNot() {
    CHECK_MSG(!FieldFits(Field(0, 4), 8, 64),
              "a 4-byte slot cannot take the 8-byte pointer the mod writes there");
    CHECK_MSG(!FieldFits(Field(0, 8), sizeof(double) * 3, 64),
              "a pointer-sized slot cannot take an LWC FVector");
}

// An offset read out of something that is not a property table can be enormous.
// `Offset + bytes <= frame_size` wraps on these and reports a fit; the guard
// subtracts instead, so it cannot.
void TestAnOffsetLargeEnoughToWrapAnAdditionIsRejected() {
    constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
    CHECK_MSG(!FieldFits(Field(kMax, kMax), 8, 64),
              "a huge offset must not wrap past the bounds test");
    CHECK_MSG(!FieldFits(Field(kMax - 4, kMax), 8, 64),
              "an offset just below the wrap point must not fit either");
    // The shape a 32-bit reflected offset can actually take, against the
    // frame sizes the mod allocates for.
    CHECK(!FieldFits(Field(0xFFFFFFFFu, 8), 8, 1024));
}

// A zero-size field is what an unresolved read leaves behind, and zero bytes
// is never what the mod is writing.
void TestAZeroSizedSlotNeverFits() {
    CHECK(!FieldFits(Field(0, 0), 1, 64));
    CHECK(!FieldFits(Field(0, 0), 8, 64));
}

// A frame that did not read at all rejects every field rather than admitting
// the zero-offset one.
void TestNothingFitsInAnEmptyFrame() {
    CHECK(!FieldFits(Field(0, 8), 8, 0));
    CHECK(!FieldFits(Field(0, 1), 1, 0));
}

}  // namespace

int main() {
    TestAWellFormedSlotFits();
    TestASlotEndingExactlyAtTheFrameEndFits();
    TestASlotRunningOneBytePastTheFrameDoesNot();
    TestASlotNarrowerThanTheWriteDoesNot();
    TestAnOffsetLargeEnoughToWrapAnAdditionIsRejected();
    TestAZeroSizedSlotNeverFits();
    TestNothingFitsInAnEmptyFrame();

    return gr_test::Report();
}
