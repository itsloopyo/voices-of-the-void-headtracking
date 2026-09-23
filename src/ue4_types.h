// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/unreal/ue_math.h>

// Unreal Engine 4 stores FVector, FRotator and FQuat as floats. cameraunlock-core's
// Unreal math is written for UE5's double-precision types, so the engine's own
// values are read and written through the float layouts below and converted to
// core's types at that edge. All the maths above the edge runs in doubles.
namespace votv_ht::ue4 {

struct FVector  { float X, Y, Z; };
struct FRotator { float Pitch, Yaw, Roll; };
struct FQuat    { float X, Y, Z, W; };
struct FVector2D { float X, Y; };

inline cameraunlock::unreal::FVector ToCore(const FVector& v) {
    return {v.X, v.Y, v.Z};
}
inline cameraunlock::unreal::FRotator ToCore(const FRotator& r) {
    return {r.Pitch, r.Yaw, r.Roll};
}
inline cameraunlock::unreal::FQuat4d ToCore(const FQuat& q) {
    return {q.X, q.Y, q.Z, q.W};
}
inline FVector FromCore(const cameraunlock::unreal::FVector& v) {
    return {static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)};
}
inline FRotator FromCore(const cameraunlock::unreal::FRotator& r) {
    return {static_cast<float>(r.Pitch), static_cast<float>(r.Yaw), static_cast<float>(r.Roll)};
}

}  // namespace votv_ht::ue4
