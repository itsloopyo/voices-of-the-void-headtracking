// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The parameter frame both traces are cast through.
//
// The aim trace and the lean trace write the same nine slots into a fixed stack
// buffer at offsets read out of the running process, and a byte in the wrong
// place there is a trace that answers about somewhere else - or a write off the
// end of the buffer. The fill is pure given a resolved layout, so what it puts
// where is locked here, with no game and no engine.

#include <cstdint>
#include <cstring>

#include "kismet_trace.h"
#include "test_harness.h"

using namespace votv_ht;

namespace {

// Deliberately not in declaration order and not packed: the fill must follow the
// engine's offsets rather than any layout of its own.
kismet_trace::Layout TestLayout() {
    kismet_trace::Layout l;
    l.ParamsSize = 200;
    l.WorldContext = 0;
    l.Start = 8;
    l.End = 20;
    l.TraceChannel = 32;
    l.TraceComplex = 33;
    l.ActorsToIgnore = 40;
    l.DrawDebugType = 56;
    l.OutHit = 64;
    l.IgnoreSelf = 190;
    l.OutHitSize = 120;
    return l;
}

struct FilledFrame {
    alignas(16) unsigned char Buf[kismet_trace::kMaxParams];
};

// Poisoned first, so a slot the fill does not write shows up as poison rather
// than as a zero that happens to be right.
constexpr unsigned char kPoison = 0xAB;

void Fill(const kismet_trace::Layout& layout, const kismet_trace::Shot& shot, FilledFrame& out) {
    std::memset(out.Buf, kPoison, sizeof(out.Buf));
    kismet_trace::FillFrame(layout, shot, out.Buf);
}

template <typename T>
T ReadAt(const FilledFrame& frame, std::size_t offset) {
    T value{};
    std::memcpy(&value, frame.Buf + offset, sizeof(T));
    return value;
}

const std::uintptr_t kPawn = 0x1234'5678'9abc'0000ull;
const std::uintptr_t kIgnore[2] = {0x1111ull, 0x2222ull};

kismet_trace::Shot TestShot() {
    kismet_trace::Shot shot;
    shot.WorldContext = kPawn;
    shot.Start = ue4::FVector{1.0f, 2.0f, 3.0f};
    shot.End = ue4::FVector{10.0f, 20.0f, 30.0f};
    shot.Channel = 7;
    shot.Complex = true;
    shot.Ignore = kIgnore;
    shot.IgnoreCount = 2;
    return shot;
}

void TestEveryParameterLandsAtItsOwnOffset() {
    const kismet_trace::Layout layout = TestLayout();
    FilledFrame frame;
    Fill(layout, TestShot(), frame);

    CHECK(ReadAt<std::uintptr_t>(frame, layout.WorldContext) == kPawn);
    const ue4::FVector start = ReadAt<ue4::FVector>(frame, layout.Start);
    CHECK(start.X == 1.0f && start.Y == 2.0f && start.Z == 3.0f);
    const ue4::FVector end = ReadAt<ue4::FVector>(frame, layout.End);
    CHECK(end.X == 10.0f && end.Y == 20.0f && end.Z == 30.0f);
    CHECK(frame.Buf[layout.TraceChannel] == 7);
    CHECK_MSG(frame.Buf[layout.TraceComplex] == 1, "a complex trace asks for per-triangle");
    CHECK_MSG(frame.Buf[layout.DrawDebugType] == 0, "EDrawDebugTrace::None - nothing is drawn");
    CHECK_MSG(frame.Buf[layout.IgnoreSelf] == 1,
              "bIgnoreSelf, which is what excludes the player's own capsule");
}

// The array the engine reads the ignore list out of: a pointer and two counts,
// pointing at the caller's own storage rather than at anything allocated here.
void TestTheIgnoreListPointsAtTheCallersStorage() {
    const kismet_trace::Layout layout = TestLayout();
    FilledFrame frame;
    Fill(layout, TestShot(), frame);

    CHECK(ReadAt<const void*>(frame, layout.ActorsToIgnore) ==
          static_cast<const void*>(kIgnore));
    CHECK(ReadAt<std::int32_t>(frame, layout.ActorsToIgnore + 8) == 2);
    CHECK_MSG(ReadAt<std::int32_t>(frame, layout.ActorsToIgnore + 12) == 2,
              "Max equals Num: the engine must not treat the storage as growable");
}

// A simple trace and an empty ignore list are the other half of the two flags,
// and both have to reach the frame as zero rather than as whatever was there.
void TestTheFrameIsZeroedBeforeItIsWritten() {
    const kismet_trace::Layout layout = TestLayout();
    kismet_trace::Shot shot = TestShot();
    shot.Complex = false;
    shot.Ignore = nullptr;
    shot.IgnoreCount = 0;
    FilledFrame frame;
    Fill(layout, shot, frame);

    CHECK(frame.Buf[layout.TraceComplex] == 0);
    CHECK(ReadAt<const void*>(frame, layout.ActorsToIgnore) == nullptr);
    CHECK(ReadAt<std::int32_t>(frame, layout.ActorsToIgnore + 8) == 0);
    // OutHit is an output: the engine fills it, so it must arrive cleared.
    for (std::size_t i = layout.OutHit; i < layout.OutHit + 16; ++i) CHECK(frame.Buf[i] == 0);
}

// The buffer is bigger than the frame, and the bytes past the frame are none of
// the fill's business.
void TestNothingPastTheFrameIsTouched() {
    const kismet_trace::Layout layout = TestLayout();
    FilledFrame frame;
    Fill(layout, TestShot(), frame);

    for (std::size_t i = layout.ParamsSize; i < sizeof(frame.Buf); ++i)
        CHECK(frame.Buf[i] == kPoison);
}

// Every FHitResult field is read at OutHit + its own offset, so the slot has to
// hold the whole struct rather than merely start inside the frame.
void TestOutHitMustHoldAWholeHitResult() {
    kismet_trace::Layout layout = TestLayout();
    CHECK(kismet_trace::OutHitHolds(layout, 120));
    CHECK_MSG(!kismet_trace::OutHitHolds(layout, 121),
              "a struct wider than the engine's own slot is refused");

    // A slot the engine reports as wide enough, but which runs off the end of
    // the frame, is refused too.
    layout.OutHit = 150;
    layout.OutHitSize = 120;
    CHECK(!kismet_trace::OutHitHolds(layout, 120));
}

// The index comes first and the serial second, so a swapped read would resolve
// a serial number as an object index.
void TestAWeakPointerReadsIndexThenSerial() {
    unsigned char bytes[kismet_trace::kWeakObjectBytes];
    const std::int32_t index = 0x00012345;
    const std::int32_t serial = 77;
    std::memcpy(bytes, &index, sizeof(index));
    std::memcpy(bytes + sizeof(index), &serial, sizeof(serial));

    const kismet_trace::WeakObject w = kismet_trace::ReadWeak(bytes);
    CHECK(w.Index == index);
    CHECK(w.Serial == serial);
    CHECK_MSG(kismet_trace::kWeakObjectBytes == 8, "a TWeakObjectPtr is two int32s");
}

}  // namespace

int main() {
    TestEveryParameterLandsAtItsOwnOffset();
    TestTheIgnoreListPointsAtTheCallersStorage();
    TestTheFrameIsZeroedBeforeItIsWritten();
    TestNothingPastTheFrameIsTouched();
    TestOutHitMustHoldAWholeHitResult();
    TestAWeakPointerReadsIndexThenSerial();
    return gr_test::Report();
}
