// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "ue4_types.h"
#include "ue_reflect.h"

// The engine toolbox the dev verbs are written against: name a target, call a
// UFUNCTION with arguments supplied at run time, convert a string to an FName.
//
// Separate from the verbs because the two answer different questions - this one
// is how to reach into the engine, dev_commands.cpp is what each verb prints -
// and because every verb that dispatches a UFUNCTION has to go through the one
// bounds-checked fill below rather than its own.
//
// Everything here is game thread only, and only reachable while
// [Dev] DevCommands=true.
namespace votv_ht::dev_ue {

// The player controller the last command ran against. Poll() falls back to it
// when the view hook is quiet, so it is remembered rather than re-found.
void SetContext(std::uintptr_t controller);
std::uintptr_t Context();

// A command's target word: one of the named objects below, or a hex address.
//
//   controller pawn pcm weapon world gamestate gamemode playerstate hud
std::uintptr_t Target(const std::string& word);

// An ObjectProperty's value by name, or 0.
std::uintptr_t ReadPtrProp(std::uintptr_t obj, const char* name);

// This process's local player controller, found by walking the object table.
// Only for the states where the view hook is not handing one over.
std::uintptr_t FindLocalController();

std::wstring Widen(const std::string& s);

// An FString as the engine's parameter frames carry it.
struct alignas(8) FStringHeader {
    const wchar_t* Data;
    std::int32_t   Num;
    std::int32_t   Max;
};

// ProcessEvent fills a UFunction's WHOLE parameter frame - every parameter and
// every local - so a frame bigger than the buffer handed to it writes off the
// end of that buffer. Every dispatch through a fixed-size stack buffer asks
// this first. `what` names the caller in the log line.
bool FrameFits(const char* what, std::uintptr_t fn, std::size_t bytes);

// One named argument, at the width the caller writes it.
struct Arg { const char* Name; const void* Data; std::size_t Bytes; };

// Dispatch a UFUNCTION with named arguments and hand the filled frame back for
// reading. `fnName` may be a hex UFunction address instead of a name. False
// when the function, its frame or any named slot did not resolve - which is
// logged, never guessed past.
bool CallWithArgs(std::uintptr_t self, const char* outer, const char* fnName,
                  std::initializer_list<Arg> args, std::vector<unsigned char>& frame,
                  std::vector<ue_reflect::FieldInfo>* fieldsOut = nullptr);

// A parameterless getter that returns an object pointer, or 0.
std::uintptr_t CallObjectGetter(std::uintptr_t self, const char* outer, const char* fnName);

// UKismetStringLibrary::Conv_StringToName, for the FName arguments the engine's
// own functions take. 0 when the conversion did not run.
std::uint64_t ToFName(const std::string& text);

// AActor::K2_GetActorLocation.
bool ActorLocation(std::uintptr_t actor, ue4::FVector& out);

// UKismetSystemLibrary::ExecuteConsoleCommand on the game thread.
bool ExecuteConsoleCommand(std::uintptr_t worldContext, const std::wstring& command);

}  // namespace votv_ht::dev_ue
