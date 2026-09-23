// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Compiled only into a development build - see VOTV_DEV_COMMANDS in
// CMakeLists.txt. This is the engine toolbox the dev verbs are written
// against: it spawns actors, moves the pawn and runs console commands, none of
// which belongs in the binary a player installs for a co-op game.
#ifdef VOTV_DEV_COMMANDS

#include "dev/dev_ue.h"

#include <cstring>

#include <windows.h>

#include "logging.h"
#include "ue_call.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::dev_ue {

namespace {

namespace ue = ::cameraunlock::unreal;

std::uintptr_t g_controller = 0;

}  // namespace

void SetContext(std::uintptr_t controller) { g_controller = controller; }
std::uintptr_t Context() { return g_controller; }

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

bool FrameFits(const char* what, std::uintptr_t fn, std::size_t bytes) {
    const std::size_t size = ue_reflect::StructSize(fn);
    if (size != 0 && size <= bytes) return true;
    Log::Line("dev: %s has a %zu-byte parameter frame, over the %zu this command "
              "allocates", what, size, bytes);
    return false;
}

std::uintptr_t ReadPtrProp(std::uintptr_t obj, const char* name) {
    ue_reflect::FieldInfo f;
    if (!ue_reflect::FindPropertyInChain(ue_call::ClassOf(obj), name, f)) return 0;
    std::uintptr_t v = 0;
    ue::SafeReadPtr(obj + f.Offset, v);
    return v;
}

std::uintptr_t Target(const std::string& word) {
    if (word == "controller") return g_controller;
    if (word == "pawn") return ReadPtrProp(g_controller, "Pawn");
    if (word == "pcm") return ReadPtrProp(g_controller, "PlayerCameraManager");
    if (word == "weapon") return ReadPtrProp(ReadPtrProp(g_controller, "Pawn"), "CurrentWeapon");
    if (word == "world") return ue_call::WorldOf(g_controller);
    if (word == "gamestate") return ReadPtrProp(ue_call::WorldOf(g_controller), "GameState");
    if (word == "gamemode") return ReadPtrProp(ue_call::WorldOf(g_controller), "AuthorityGameMode");
    if (word == "playerstate") return ReadPtrProp(g_controller, "PlayerState");
    if (word == "hud") return ReadPtrProp(g_controller, "MyHUD");
    return std::strtoull(word.c_str(), nullptr, 16);
}

std::uintptr_t FindLocalController() {
    std::uintptr_t found = 0;
    ue::ForEachUObject([&](std::uintptr_t obj) {
        const std::string cls = ue::ClassName(obj);
        if (!ue::ContainsCI(cls, "PlayerController")) return false;
        if (ue_call::IsDefaultObject(obj)) return false;
        const std::uintptr_t outer = ue::OuterObject(obj);
        if (!outer || ue::ClassName(outer) != "Level") return false;
        found = obj;
        return true;
    });
    return found;
}

std::uintptr_t CallObjectGetter(std::uintptr_t self, const char* outer, const char* fnName) {
    const std::uintptr_t fn = ue::FindLiveObject("Function", fnName, outer);
    if (!fn || !self) return 0;
    std::vector<ue_reflect::FieldInfo> p;
    if (!ue_reflect::ResolveAll(fnName, fn, {"ReturnValue"}, p)) return 0;
    alignas(16) unsigned char buf[512];
    // ProcessEvent fills the frame from the UFunction's real size whatever we
    // measured, so a failed read must not be taken as a zero-size frame.
    std::size_t size = 0;
    if (!ue_reflect::TryStructSize(fn, size) || size > sizeof(buf)) return 0;
    std::memset(buf, 0, size);
    if (!ue_vm::Dispatch(reinterpret_cast<void*>(self), reinterpret_cast<void*>(fn), buf)) return 0;
    std::uintptr_t v = 0;
    if (p[0].Size >= 8) std::memcpy(&v, buf + p[0].Offset, 8);
    return v;
}

bool CallWithArgs(std::uintptr_t self, const char* outer, const char* fnName,
                  std::initializer_list<Arg> args, std::vector<unsigned char>& frame,
                  std::vector<ue_reflect::FieldInfo>* fieldsOut) {
    const std::uintptr_t fn = std::strncmp(fnName, "0x", 2) == 0 ? std::strtoull(fnName, nullptr, 16)
                                                                 : ue::FindLiveObject("Function", fnName, outer);
    if (!fn || !self) { Log::Line("dev: %s::%s unavailable", outer, fnName); return false; }
    // The read has to be told apart from the answer: a parameterless function
    // legitimately measures zero, but so does a failed read, and ProcessEvent
    // fills the function's real frame either way - so dispatching on a failed
    // read hands it a 16-byte buffer to write the real frame into.
    std::size_t size = 0;
    if (!ue_reflect::TryStructSize(fn, size) || size > 4096) {
        Log::Line("dev: %s::%s parameter frame unreadable or too large - not called", outer, fnName);
        return false;
    }
    const auto fields = ue_reflect::Properties(fn);
    frame.assign(size + 16, 0);
    for (const Arg& a : args) {
        bool placed = false;
        for (const auto& f : fields) {
            if (f.Name != a.Name) continue;
            if (!ue_reflect::FieldFits(f, a.Bytes, size)) break;
            std::memcpy(frame.data() + f.Offset, a.Data, a.Bytes);
            placed = true;
            break;
        }
        if (!placed) { Log::Line("dev: %s::%s has no slot for %s", outer, fnName, a.Name); return false; }
    }
    if (fieldsOut) *fieldsOut = fields;
    return ue_vm::Dispatch(reinterpret_cast<void*>(self), reinterpret_cast<void*>(fn), frame.data());
}

std::uint64_t ToFName(const std::string& text) {
    const std::wstring wide = Widen(text);
    const FStringHeader str{wide.c_str(), static_cast<std::int32_t>(wide.size() + 1),
                            static_cast<std::int32_t>(wide.size() + 1)};
    std::vector<unsigned char> conv;
    std::vector<ue_reflect::FieldInfo> convFields;
    const std::uintptr_t strLib = ue_call::DefaultObject("KismetStringLibrary");
    if (!CallWithArgs(strLib, "KismetStringLibrary", "Conv_StringToName", {{"inString", &str, sizeof(str)}},
                      conv, &convFields))
        return 0;
    std::uint64_t name = 0;
    // The offsets come from Properties(), which reports what the property table
    // says without checking it against the frame. Every other frame read in this
    // mod asks FieldFits first; these two did not, so a ReturnValue landing past
    // the frame read past the end of the vector.
    for (const auto& f : convFields)
        if (f.Name == "ReturnValue" && ue_reflect::FieldFits(f, sizeof(name), conv.size()))
            std::memcpy(&name, conv.data() + f.Offset, sizeof(name));
    return name;
}

bool ActorLocation(std::uintptr_t actor, ue4::FVector& out) {
    std::vector<unsigned char> frame;
    std::vector<ue_reflect::FieldInfo> fields;
    if (!CallWithArgs(actor, "Actor", "K2_GetActorLocation", {}, frame, &fields)) return false;
    for (const auto& f : fields)
        if (f.Name == "ReturnValue" && ue_reflect::FieldFits(f, sizeof(out), frame.size())) {
            std::memcpy(&out, frame.data() + f.Offset, sizeof(out));
            return true;
        }
    return false;
}

bool ExecuteConsoleCommand(std::uintptr_t worldContext, const std::wstring& command) {
    static std::uintptr_t fn = 0, cdo = 0;
    static std::size_t size = 0, ctxOff = 0, cmdOff = 0, playerOff = 0;
    if (!ue_vm::Ready()) return false;
    if (!fn) {
        cdo = ue_call::DefaultObject("KismetSystemLibrary");
        const std::uintptr_t f = ue::FindLiveObject("Function", "ExecuteConsoleCommand", "KismetSystemLibrary");
        std::vector<ue_reflect::FieldInfo> p;
        if (!cdo || !f ||
            !ue_reflect::ResolveAll("ExecuteConsoleCommand", f,
                                    {"WorldContextObject", "Command", "SpecificPlayer"}, p))
            return false;
        size = ue_reflect::StructSize(f);
        // Every slot at the width written into it, not just the string: a
        // pointer written into a slot the engine reports as narrower runs the
        // tail of that write off the end of `buf`.
        const std::size_t ptrBytes = sizeof(std::uintptr_t);
        if (size == 0 || size > 256 || !ue_reflect::FieldFits(p[0], ptrBytes, size) ||
            !ue_reflect::FieldFits(p[1], sizeof(FStringHeader), size) ||
            !ue_reflect::FieldFits(p[2], ptrBytes, size))
            return false;
        ctxOff = p[0].Offset; cmdOff = p[1].Offset; playerOff = p[2].Offset;
        fn = f;
    }
    alignas(16) unsigned char buf[256];
    std::memset(buf, 0, size);
    std::memcpy(buf + ctxOff, &worldContext, sizeof(worldContext));
    const FStringHeader str{command.c_str(), static_cast<std::int32_t>(command.size() + 1),
                            static_cast<std::int32_t>(command.size() + 1)};
    std::memcpy(buf + cmdOff, &str, sizeof(str));
    std::memcpy(buf + playerOff, &worldContext, sizeof(worldContext));
    return ue_vm::Dispatch(reinterpret_cast<void*>(cdo), reinterpret_cast<void*>(fn), buf);
}

}  // namespace votv_ht::dev_ue

#endif  // VOTV_DEV_COMMANDS
