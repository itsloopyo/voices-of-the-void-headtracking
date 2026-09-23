// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "kismet_trace.h"

#include <cstring>
#include <string>
#include <vector>

#include "logging.h"
#include "ue_reflect.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::kismet_trace {

namespace {

namespace ue = ::cameraunlock::unreal;

// TArray<AActor*> as the frame carries it. LineTraceSingle takes ActorsToIgnore
// by const reference and never reallocates it, so the caller's own storage is
// enough and there is nothing for the engine to free.
struct alignas(8) TArrayHeader {
    const void*  Data;
    std::int32_t Num;
    std::int32_t Max;
};

// The parameters, in the order they are resolved and then indexed.
const std::vector<std::string>& ParameterNames() {
    static const std::vector<std::string> kNames = {
        "WorldContextObject", "Start", "End", "TraceChannel", "bTraceComplex",
        "ActorsToIgnore", "DrawDebugType", "OutHit", "bIgnoreSelf",
    };
    return kNames;
}

// ResolveAll proves every parameter sits inside the frame at the width the
// ENGINE reports. These are a different claim: each write puts a fixed-size C++
// type into its slot, and a slot narrower than the type runs the tail of that
// write off the end of the caller's buffer.
struct ExpectedWidth { const char* Name; std::size_t Index; std::size_t Bytes; };
constexpr ExpectedWidth kWidths[] = {
    {"WorldContextObject", 0, sizeof(std::uintptr_t)},
    {"Start",              1, sizeof(ue4::FVector)},
    {"End",                2, sizeof(ue4::FVector)},
    {"TraceChannel",       3, 1},
    {"bTraceComplex",      4, 1},
    {"ActorsToIgnore",     5, sizeof(TArrayHeader)},
    {"DrawDebugType",      6, 1},
    {"bIgnoreSelf",        8, 1},
};

}  // namespace

Resolution Resolve(const char* label, std::uintptr_t& cdo, std::uintptr_t& function,
                   Layout& out) {
    cdo = ue::FindLiveObject("KismetSystemLibrary", "Default__KismetSystemLibrary", nullptr);
    function = ue::FindLiveObject("Function", "LineTraceSingle", "KismetSystemLibrary");
    if (!cdo || !function) {
        // The object table is walked from the render caller, so the first frames
        // can run before these are visible. Only repeated failure is a fault,
        // and the caller retries on its own interval.
        return Resolution::NotYet;
    }

    std::vector<ue_reflect::FieldInfo> p;
    if (!ue_reflect::ResolveAll("LineTraceSingle", function, ParameterNames(), p))
        return Resolution::Unusable;

    const std::size_t paramsSize = ue_reflect::StructSize(function);
    if (paramsSize > kMaxParams) {
        Log::Line("%s: LineTraceSingle's parameter frame is %zu bytes, which is not a "
                  "Kismet trace frame", label, paramsSize);
        return Resolution::Unusable;
    }

    for (const ExpectedWidth& w : kWidths) {
        if (ue_reflect::FieldFits(p[w.Index], w.Bytes, paramsSize)) continue;
        Log::Line("%s: LineTraceSingle.%s is %zu bytes at +0x%zx in a %zu-byte frame, "
                  "too narrow for the %zu this mod writes there",
                  label, w.Name, p[w.Index].Size, p[w.Index].Offset, paramsSize, w.Bytes);
        return Resolution::Unusable;
    }

    out = Layout{};
    out.ParamsSize     = paramsSize;
    out.WorldContext   = p[0].Offset;
    out.Start          = p[1].Offset;
    out.End            = p[2].Offset;
    out.TraceChannel   = p[3].Offset;
    out.TraceComplex   = p[4].Offset;
    out.ActorsToIgnore = p[5].Offset;
    out.DrawDebugType  = p[6].Offset;
    out.OutHit         = p[7].Offset;
    out.IgnoreSelf     = p[8].Offset;
    out.OutHitSize     = p[7].Size;
    return Resolution::Ok;
}

bool OutHitHolds(const Layout& layout, std::size_t hitSize) {
    ue_reflect::FieldInfo outHit;
    outHit.Offset = layout.OutHit;
    outHit.Size = layout.OutHitSize;
    return ue_reflect::FieldFits(outHit, hitSize, layout.ParamsSize);
}

void FillFrame(const Layout& layout, const Shot& shot, unsigned char* buf) {
    std::memset(buf, 0, layout.ParamsSize);
    std::memcpy(buf + layout.WorldContext, &shot.WorldContext, sizeof(shot.WorldContext));
    std::memcpy(buf + layout.Start, &shot.Start, sizeof(shot.Start));
    std::memcpy(buf + layout.End, &shot.End, sizeof(shot.End));
    buf[layout.TraceChannel] = static_cast<unsigned char>(shot.Channel);
    buf[layout.TraceComplex] = shot.Complex ? 1 : 0;
    buf[layout.DrawDebugType] = 0;  // EDrawDebugTrace::None
    buf[layout.IgnoreSelf] = 1;
    const TArrayHeader ignore{shot.Ignore, shot.IgnoreCount, shot.IgnoreCount};
    std::memcpy(buf + layout.ActorsToIgnore, &ignore, sizeof(ignore));
}

}  // namespace votv_ht::kismet_trace
