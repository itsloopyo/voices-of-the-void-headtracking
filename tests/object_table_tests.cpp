// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The walk from an object index to its slot in the engine's object table.
//
// The indices that reach it are not the mod's own: they come out of a
// TWeakObjectPtr inside an FHitResult the physics filled, and out of a widget's
// InternalIndex. So the walk is a boundary, and it has two ways to go wrong
// that this suite pins:
//
//   - An index past the end of the table read a pointer from past the end of
//     the chunk array and handed back an item address built out of whatever was
//     there. The header has always documented the bounded behaviour; the code
//     did not have it.
//   - The chunk size and item size come from a build profile as plain numbers,
//     and both are divisors. A profile that leaves one zero faulted with an
//     integer divide by zero, which no SafeRead guard covers.
//
// The table here is a synthetic one in this process's own memory: SetRuntime
// takes a base address and a layout, and the fault-guarded reads underneath work
// against any readable address, so no game is needed.

#include <cstdint>
#include <cstring>
#include <vector>

#include "test_harness.h"
#include "ue_call.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace {

namespace ue = ::cameraunlock::unreal;

constexpr std::size_t kObjObjectsRva = 0x10;
// NumElements sits inside ObjObjects, so it is read at kObjObjectsRva + this.
constexpr std::size_t kNumOffset = 0x18;
constexpr std::size_t kItemSize = 0x18;
constexpr std::size_t kChunkElems = 4;
constexpr std::size_t kSerialOffset = 0x10;   // FUObjectItem::SerialNumber
constexpr std::uint32_t kObjectCount = 6;     // two chunks, the second half full

// One synthetic FUObjectArray: a header standing in for the game module's
// .data, a chunk pointer array, and two chunks of FUObjectItem.
struct FakeTable {
    std::vector<unsigned char> header{std::vector<unsigned char>(0x80, 0)};
    std::vector<std::uintptr_t> chunkPointers{std::vector<std::uintptr_t>(2, 0)};
    std::vector<unsigned char> chunkA{std::vector<unsigned char>(kChunkElems * kItemSize, 0)};
    std::vector<unsigned char> chunkB{std::vector<unsigned char>(kChunkElems * kItemSize, 0)};

    FakeTable() {
        chunkPointers[0] = reinterpret_cast<std::uintptr_t>(chunkA.data());
        chunkPointers[1] = reinterpret_cast<std::uintptr_t>(chunkB.data());
        const std::uintptr_t chunks = reinterpret_cast<std::uintptr_t>(chunkPointers.data());
        std::memcpy(header.data() + kObjObjectsRva, &chunks, sizeof(chunks));
        const std::uint32_t num = kObjectCount;
        std::memcpy(header.data() + kObjObjectsRva + kNumOffset, &num, sizeof(num));
    }

    std::uintptr_t Base() const { return reinterpret_cast<std::uintptr_t>(header.data()); }

    std::uintptr_t Item(std::uint32_t index) const {
        const unsigned char* chunk = index / kChunkElems == 0 ? chunkA.data() : chunkB.data();
        return reinterpret_cast<std::uintptr_t>(chunk) + (index % kChunkElems) * kItemSize;
    }

    // Put an object pointer and a serial number in a slot, the way the engine
    // would.
    void Fill(std::uint32_t index, std::uintptr_t object, std::int32_t serial) {
        auto* slot = reinterpret_cast<unsigned char*>(Item(index));
        std::memcpy(slot, &object, sizeof(object));
        std::memcpy(slot + kSerialOffset, &serial, sizeof(serial));
    }
};

ue::UObjectGlobalsLayout Layout(std::size_t chunkElems = kChunkElems,
                                std::size_t itemSize = kItemSize) {
    ue::UObjectGlobalsLayout layout{};
    layout.kObjObjects = kObjObjectsRva;
    layout.kObjObjects_Num = kNumOffset;
    layout.kFUObjectItemSize = itemSize;
    layout.kChunkNumElems = chunkElems;
    layout.kFNamePool = 0;
    layout.kFNamePoolBlocks = 0;
    layout.kClassPrivate = 0x10;
    layout.kNamePrivate = 0x18;
    layout.kOuterPrivate = 0x20;
    return layout;
}

void Install(const FakeTable& table, const ue::UObjectGlobalsLayout& layout) {
    ue::SetRuntime(table.Base(), table.Base() + 0x80, layout);
}

void TestAnIndexInsideTheTableResolvesToItsSlot() {
    FakeTable table;
    Install(table, Layout());
    CHECK(votv_ht::ue_call::ObjectItem(0) == table.Item(0));
    CHECK(votv_ht::ue_call::ObjectItem(3) == table.Item(3));
    CHECK_MSG(votv_ht::ue_call::ObjectItem(4) == table.Item(4),
              "the first index of the second chunk comes from the second chunk pointer");
    CHECK(votv_ht::ue_call::ObjectItem(kObjectCount - 1) == table.Item(kObjectCount - 1));
}

void TestAnIndexPastTheEndOfTheTableIsRefused() {
    FakeTable table;
    Install(table, Layout());
    CHECK_MSG(votv_ht::ue_call::ObjectItem(kObjectCount) == 0,
              "one past the last live object is outside the table");
    CHECK(votv_ht::ue_call::ObjectItem(kObjectCount + 1) == 0);
    CHECK_MSG(votv_ht::ue_call::ObjectItem(0x7FFFFFFF) == 0,
              "a garbage index must not be turned into an item address");
}

void TestANegativeIndexIsRefused() {
    FakeTable table;
    Install(table, Layout());
    CHECK(votv_ht::ue_call::ObjectItem(-1) == 0);
    CHECK(votv_ht::ue_call::ObjectItem(-0x7FFFFFFF) == 0);
}

// Both of these are divisors in the walk. A build profile that leaves one zero
// used to fault here rather than answer.
void TestAZeroDivisorInTheLayoutIsRefusedRatherThanDividedBy() {
    FakeTable table;
    Install(table, Layout(0, kItemSize));
    CHECK_MSG(votv_ht::ue_call::ObjectItem(0) == 0, "a zero chunk size must not be divided by");
    Install(table, Layout(kChunkElems, 0));
    CHECK_MSG(votv_ht::ue_call::ObjectItem(0) == 0, "a zero item size is not a table layout");
}

void TestNoRuntimeMeansNoTable() {
    ue::SetRuntime(0, 0, Layout());
    CHECK(votv_ht::ue_call::ObjectItem(0) == 0);
}

// The weak pointer is what the traces and the reticle actually hold: an index
// and the serial number the slot carried when it was taken.
void TestAWeakPointerResolvesOnlyWhileItsSlotHoldsTheSameObject() {
    FakeTable table;
    Install(table, Layout());
    const std::uintptr_t object = reinterpret_cast<std::uintptr_t>(&table);
    table.Fill(2, object, 7);

    CHECK(votv_ht::ue_call::ResolveWeak(2, 7) == object);
    CHECK_MSG(votv_ht::ue_call::ResolveWeak(2, 8) == 0,
              "a slot reused since the pointer was taken resolves to nothing");
    CHECK_MSG(votv_ht::ue_call::ResolveWeak(2, 0) == 0, "serial 0 is an empty weak pointer");
    CHECK_MSG(votv_ht::ue_call::ResolveWeak(kObjectCount, 7) == 0,
              "a weak pointer past the end of the table resolves to nothing");
}

}  // namespace

int main() {
    TestAnIndexInsideTheTableResolvesToItsSlot();
    TestAnIndexPastTheEndOfTheTableIsRefused();
    TestANegativeIndexIsRefused();
    TestAZeroDivisorInTheLayoutIsRefusedRatherThanDividedBy();
    TestNoRuntimeMeansNoTable();
    TestAWeakPointerResolvesOnlyWhileItsSlotHoldsTheSameObject();

    return gr_test::Report();
}
