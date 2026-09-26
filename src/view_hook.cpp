// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Inject the head pose into the render path, and nowhere else.
//
// GetPlayerViewPoint fires from several call sites per frame. Only the one the
// caller gate names - ULocalPlayer::GetViewPoint, which builds the scene view
// and every world-to-screen projection the HUD asks for - gets the pose written
// back. Every other caller keeps the clean rotation, which is what keeps the
// weapon trace, the projectile spawn, the laser pointer, the terrain scanner,
// enemy perception and replication on the mouse aim. Those all resolve their
// direction from PlayerCharacter::FirstPersonCamera's world transform, and the
// mod never writes to that component.
//
// What a reader is told about all this lives in hook_log.h, over the
// FrameReport this file fills.

#include "view_hook.h"

#include <atomic>
#include <cstdint>

#include <windows.h>
#include <psapi.h>
#include <intrin.h>

#include "aim_projection.h"
#include "aim_trace.h"
#include "builds/build_registry.h"
#include "camera_boundary.h"
#include "camera_fov.h"
#include "dev_console.h"
#include "flashlight.h"
#include "footage_view.h"
#include "frame_report.h"
#include "game_state.h"
#include "hook_log.h"
#include "inject_mode.h"
#include "lean_trace.h"
#include "logging.h"
#include "player_rig.h"
#include "pose_shaping.h"
#include "reticle.h"
#include "tracking.h"
#include "tracking_gate.h"
#include "ue4_types.h"
#include "ue_call.h"
#include "ue_vm.h"
#include "window_centering.h"

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/unreal/ue_math.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::view_hook {

namespace {

namespace ue = ::cameraunlock::unreal;

using ue::FQuat4d;
using ue::FRotator;
using ue::FVector;
using cameraunlock::time::FrameClock;

using GetPlayerViewPoint_t =
    void(__fastcall*)(void* self, ue4::FVector* outLocation, ue4::FRotator* outRotation);

Dependencies g_deps{};

std::atomic<bool> g_trackingEnabled{true};
std::atomic<bool> g_worldSpaceYaw{true};
std::atomic<int>  g_injectMode{inject::kFirstCaller};

GetPlayerViewPoint_t g_origGetPlayerViewPoint = nullptr;
void* g_hookTarget = nullptr;
std::atomic<std::uint64_t> g_hookCallCount{0};

FrameClock g_frameClock;
cameraunlock::camera::LeanClamp g_leanClamp;

// The render caller runs more than once per engine frame (the scene view, and
// every projection the HUD asks the local player for). All of them must see the
// same pose, so the first call of a frame does the work and the rest replay it.
// A repeat is recognised by the engine's own frame number and the identical
// clean view. A millisecond clock cannot tell two frames apart at high frame
// rates, and a false repeat would hold the previous frame's head pose.
struct FrameCache {
    bool Valid = false;
    std::int64_t Frame = 0;
    ue4::FVector CleanLocation{0.0f, 0.0f, 0.0f};
    ue4::FRotator CleanRotation{0.0f, 0.0f, 0.0f};
    ue4::FVector OutLocation{0.0f, 0.0f, 0.0f};
    ue4::FRotator OutRotation{0.0f, 0.0f, 0.0f};
};
FrameCache g_frameCache;
AimSample g_lastAim;
bool g_markOverride = false;
FVector g_markOverridePoint{0.0, 0.0, 0.0};

ue_vm::ResolveRetry g_frameCountRetry;
ue_call::Function g_getFrameCount;   // KismetSystemLibrary::GetFrameCount
std::uintptr_t g_kismetSystemCdo = 0;

// GFrameCounter, through UKismetSystemLibrary::GetFrameCount. False until the
// function resolves, and then the cache simply never replays.
bool EngineFrame(std::int64_t& out) {
    if (!g_getFrameCount.Ready()) {
        if (!g_frameCountRetry.Due() || !ue_vm::Ready()) return false;
        g_kismetSystemCdo = ue_call::DefaultObject("KismetSystemLibrary");
        if (!g_kismetSystemCdo ||
            !g_getFrameCount.Resolve("KismetSystemLibrary", "GetFrameCount",
                                     {{"ReturnValue", sizeof(std::int64_t)}}))
            return false;
    }
    ue_call::Frame frame(g_getFrameCount);
    if (!frame.Call(g_kismetSystemCdo)) return false;
    out = frame.Get<std::int64_t>(0);
    return true;
}

bool SameView(const ue4::FVector& a, const ue4::FVector& b) { return a.X == b.X && a.Y == b.Y && a.Z == b.Z; }
bool SameView(const ue4::FRotator& a, const ue4::FRotator& b) {
    return a.Pitch == b.Pitch && a.Yaw == b.Yaw && a.Roll == b.Roll;
}

// The tick the pose started applying, 0 while it is not applied.
std::uint64_t g_poseSinceMs = 0;

// A clean eye that moves further than this between two frames has been cut to,
// not moved. Flying in noclip with Shift held covered 152 m in 3 s, which is
// well under this a frame.
constexpr double kCameraCutCm = 300.0;

// How far the aim ray is cast. Past this the mark is projected from the
// direction alone, which is what a target at infinity looks like anyway.
constexpr double kMaxTraceCm = 100000.0;

std::uintptr_t ReturnRva(const void* returnAddress) {
    const auto addr = reinterpret_cast<std::uintptr_t>(returnAddress);
    return ue::ModuleBase() != 0 ? addr - ue::ModuleBase() : addr;
}

float Clamp1(float ndc) { return ndc < -1.0f ? -1.0f : (ndc > 1.0f ? 1.0f : ndc); }

// The render window, kept between frames. Finding it enumerates every top-level
// window on the desktop and holds the window-list lock while it does, which is
// not something to repeat once a frame in a hook the HUD drives; the handle only
// stops being valid when the engine tears the window down, and IsWindow says so
// without enumerating anything.
HWND RenderWindow() {
    static HWND s_window = nullptr;
    if (!s_window || !IsWindow(s_window)) s_window = window_centering::FindRenderWindow();
    return s_window;
}

// Half-field tangents of the frame as the engine will draw it.
bool FrameTangents(float fov, float& tanX, float& tanY) {
    RECT rc{};
    const HWND wnd = RenderWindow();
    if (!wnd || !GetClientRect(wnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return false;
    const float aspect = static_cast<float>(rc.right) / static_cast<float>(rc.bottom);
    return camera_fov::HalfFieldTangents(camera_fov::AspectConstraint(), fov, aspect, tanX, tanY);
}

AimSample Sample(const FVector& origin, const FVector& dir, const FrameReport& r,
                 const ue4::FVector& eye) {
    return AimSample{true, origin.X, origin.Y, origin.Z, dir.X, dir.Y, dir.Z, r.TraceHit,
                     r.AimPoint.X, r.AimPoint.Y, r.AimPoint.Z, eye.X, eye.Y, eye.Z,
                     r.Mark.X, r.Mark.Y, r.Mark.Valid};
}

// The lean clamp as the heartbeat reads it. Taken after everything that could
// move it this frame, because a clamp that has quietly stopped clamping looks
// exactly like one that never engaged.
void NoteLeanState(FrameReport& report) {
    report.CollisionEnabled = g_deps.config->collision_enabled;
    // Both halves of "the sweep is not answering": the resolve that gave up for
    // the session, and a query that could not run on the lean just asked for.
    // The second is the one that moves during play, and reporting only the first
    // left a clamp that had quietly stopped clamping reading as one on a clear
    // path.
    report.LeanQueryFailed = lean_trace::Failed() || g_leanClamp.LastQueryFailed();
    report.LeanInContact = g_leanClamp.InContact();
}

// As much of the wanted lean as the level leaves room for, swept from the CLEAN
// eye. Passed through untouched when CollisionEnabled switches the clamp off.
FVector ClampLean(const FVector& cleanLocation, const FVector& wanted, float dt,
                  std::uintptr_t pawn) {
    if (!g_deps.config->collision_enabled) return wanted;
    lean_trace::SetPawn(pawn);
    const cameraunlock::math::Vec3 from{static_cast<float>(cleanLocation.X),
                                        static_cast<float>(cleanLocation.Y),
                                        static_cast<float>(cleanLocation.Z)};
    const cameraunlock::math::Vec3 want{static_cast<float>(wanted.X),
                                        static_cast<float>(wanted.Y),
                                        static_cast<float>(wanted.Z)};
    const cameraunlock::math::Vec3 allowed =
        g_leanClamp.Apply(from, want, dt, &lean_trace::Query, nullptr);
    hook_log::LeanClamp(want, allowed, g_leanClamp.LastQueryFailed(), g_leanClamp.InContact());
    return FVector{allowed.x, allowed.y, allowed.z};
}

// The mark: the point the shot's ray stops on, projected from the eye the frame
// is drawn from, through the rotation the frame is drawn with - both read back
// from the floats actually written, so the projection describes the camera the
// player is looking through.
aim_projection::Ndc ProjectMark(const ue4::FVector& eye, const ue4::FRotator& drawn,
                                const aim_trace::Result& hit, const FVector& aimDir,
                                float tanX, float tanY) {
    const aim_projection::View view{
        ue4::ToCore(eye), ue::QuatFromEulerDeg(drawn.Pitch, drawn.Yaw, drawn.Roll), tanX, tanY};
    if (g_markOverride) return aim_projection::ProjectPoint(view, g_markOverridePoint);
    return hit.Hit ? aim_projection::ProjectPoint(view, hit.Point)
                   : aim_projection::ProjectDirection(view, aimDir);
}

// True when this call is the render caller repeating inside one engine frame, in
// which case it is handed that frame's view back rather than a second pose.
bool ReplayFrame(bool haveFrame, std::int64_t engineFrame, const ue4::FVector& cleanLocation,
                 const ue4::FRotator& cleanRotation, ue4::FVector* outLocation,
                 ue4::FRotator* outRotation) {
    if (!haveFrame || !g_frameCache.Valid || engineFrame != g_frameCache.Frame ||
        !SameView(cleanLocation, g_frameCache.CleanLocation) ||
        !SameView(cleanRotation, g_frameCache.CleanRotation))
        return false;
    *outLocation = g_frameCache.OutLocation;
    *outRotation = g_frameCache.OutRotation;
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        Log::Line("hook: the render caller repeats within engine frame %lld, so the rest of "
                  "the frame replays that frame's view",
                  static_cast<long long>(engineFrame));
    }
    return true;
}

// The aim's own ray. It starts at PlayerCharacter::FirstPersonCamera and runs
// along that component's forward, which is what the weapon trace, the
// projectile spawn, the laser pointer and the terrain scanner all resolve their
// direction from - not the drawn view, which carries the camera manager's
// shakes on top.
struct AimRay {
    FVector Origin;
    FVector Direction;
};

AimRay ReadAimRay(const player_rig::Snapshot& rig, const FVector& cleanLocation,
                  const FQuat4d& cleanQ) {
    if (rig.Camera.Valid) return AimRay{rig.Camera.Position, rig.Camera.Forward};
    return AimRay{cleanLocation, ue::QuatRotateVec(cleanQ, FVector{1.0, 0.0, 0.0})};
}

// This frame's head rotation, in degrees. False leaves `pose` untouched: the
// session has published nothing, or what it published stopped being a number.
bool ReadHeadRotation(Session* session, float dt, Pose& pose) {
    if (!session || !session->Update(dt)) return false;
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    if (!session->GetRotation(yaw, pitch, roll)) return false;
    if (!Finite3(yaw, pitch, roll)) {
        hook_log::NonFinitePose(true);
        return false;
    }
    pose.yaw = yaw;
    pose.pitch = pitch;
    pose.roll = roll;
    return true;
}

// This frame's head position, in the processor's metres. What the session
// published lands in `pose` whatever the answer, because that is what the
// heartbeat reports as applied; false only stops it reaching the eye.
bool ReadHeadPosition(Session* session, Pose& pose) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    const bool published = session->GetPositionOffset(x, y, z);
    pose.x = x;
    pose.y = y;
    pose.z = z;
    if (!published) return false;
    if (!Finite3(x, y, z)) {
        hook_log::NonFinitePose(false);
        return false;
    }
    return true;
}

// Where the frame is drawn from: the clean eye plus as much of the lean as the
// level leaves room for.
FVector LeanedEye(const FVector& cleanLocation, const FQuat4d& cleanQ, const Pose& pose,
                  bool havePosition, float dt, std::uintptr_t pawn, FrameReport& report) {
    // A camera cut: a teleport, or the rider getting on or off the ATV. The
    // allowance belongs to the wall the eye was next to before it, so it starts
    // over rather than easing out of that wall in the new place.
    static FVector s_lastEye{0.0, 0.0, 0.0};
    const double cx = cleanLocation.X - s_lastEye.X, cy = cleanLocation.Y - s_lastEye.Y,
                 cz = cleanLocation.Z - s_lastEye.Z;
    s_lastEye = cleanLocation;
    if (cx * cx + cy * cy + cz * cz > kCameraCutCm * kCameraCutCm) g_leanClamp.Reset();
    if (!havePosition) {
        // Rotation-only mode applies a pose but no lean, so the clamp never runs
        // and never releases. Without this the allowance stays frozen at the last
        // 6DOF frame's, and cycling back to full tracking rations the first lean
        // against a wall the player walked away from several rooms ago.
        g_leanClamp.Reset();
        return cleanLocation;
    }
    const FVector offset =
        ClampLean(cleanLocation, camera_boundary::PositionOffset(cleanQ, pose.x, pose.y, pose.z),
                  dt, pawn);
    report.PositionOffset = offset;
    return FVector{cleanLocation.X + offset.X, cleanLocation.Y + offset.Y,
                   cleanLocation.Z + offset.Z};
}

// The game's crosshair follows the aim point whenever the pose is applied.
// Clamped to the frame edge rather than dropped. The mark leaves the frame
// at about 25 degrees of head pitch on a 16:9 display at the game's default
// field of view, which is ordinary head movement, and handing the reticle
// `false` there restores the game's own layout - a crosshair drawn dead
// centre, the one place the rounds are certainly not going. A mark with no
// direction at all still restores: with no projection there is nothing
// better to say than what the game already drew.
void PublishMark(std::uintptr_t controller, std::uintptr_t pawn, FrameReport& report) {
    report.MarkOnScreen = aim_projection::OnScreen(report.Mark);
    reticle::Publish(controller, pawn, report.Mark.Valid, Clamp1(report.Mark.X),
                     Clamp1(report.Mark.Y));
}

// One render frame, from the clean view the engine handed back to the view the
// player sees. Everything it changes goes through outLocation / outRotation.
void ApplyFrame(std::uintptr_t controller, std::uintptr_t retRva, ue4::FVector* outLocation,
                ue4::FRotator* outRotation, const ue4::FVector& cleanLocationRaw,
                const ue4::FRotator& cleanRaw, std::uint64_t tick) {
    FrameReport report;
    const FRotator clean = ue4::ToCore(cleanRaw);
    const FVector cleanLocation = ue4::ToCore(cleanLocationRaw);
    const FQuat4d cleanQ = ue::QuatFromEulerDeg(clean.Pitch, clean.Yaw, clean.Roll);

    const player_rig::Snapshot rig = player_rig::Read(controller);

    camera_fov::RefreshAspectConstraint(controller);
    report.RenderFov = camera_fov::ReadRenderFov(outLocation, outRotation);
    // The base is mainPlayer_C's CameraComponent::FieldOfView, sampled while
    // its Zoom timeline sits at the start, which is the FOV the player's own
    // setting puts on the camera. The render caller is handed GetFOVAngle(),
    // which is that same number until the zoom fades the camera away from it,
    // so the factor reads 1.0000 in ordinary play and only moves while a zoom
    // is running.
    report.BaseFov = rig.HaveFov ? rig.BaseFov : 0.0f;
    report.ZoomFactor = pose_shaping::ZoomFactor(report.RenderFov, report.BaseFov);

    report.Gate = game_state::Evaluate(controller, rig, cleanLocation, cleanQ);
    game_state::LogTransitions(report.Gate);

    Session* session = tracking::Get();
    const float dt = g_frameClock.Tick();
    Pose pose;
    report.HavePose = ReadHeadRotation(session, dt, pose);

    hook_log::ZoomTerms(report, tick);
    hook_log::FovReadable(report);

    report.TrackingEnabled = g_trackingEnabled.load(std::memory_order_relaxed);
    report.Verdict = DecideTracking(report.Gate, report.TrackingEnabled, report.HavePose);

    // Cast in every gameplay frame, not only tracked ones, so the log compares
    // like with like.
    const AimRay aim = ReadAimRay(rig, cleanLocation, cleanQ);
    aim_trace::Result hit;
    if (report.Gate.InGameplay) {
        hit = aim_trace::Cast(rig.Pawn, aim.Origin, aim.Direction, kMaxTraceCm);
        report.TraceValid = hit.Valid;
        report.TraceHit = hit.Hit;
        report.TraceDistance = hit.Distance;
        report.AimPoint = hit.Point;
        hook_log::AimTrace(hit, kMaxTraceCm);
    }

    if (!PoseApplies(report.Verdict)) {
        g_poseSinceMs = 0;
        g_leanClamp.Reset();
        reticle::Publish(controller, rig.Pawn, false, 0.0f, 0.0f);
        flashlight::Update(rig.Player, false, FQuat4d{0.0, 0.0, 0.0, 1.0}, g_deps.config->light);
        g_lastAim = Sample(aim.Origin, aim.Direction, report, *outLocation);
        NoteLeanState(report);
        hook_log::Heartbeat(report, retRva);
        return;
    }

    bool havePosition = ReadHeadPosition(session, pose);
    // Zeroed rather than just left unapplied, so the heartbeat's pose and the
    // offset it reports agree with what the frame was drawn from.
    if (havePosition && footage_view::BlocksLean(controller)) {
        pose.x = 0.0f;
        pose.y = 0.0f;
        pose.z = 0.0f;
        havePosition = false;
    }

    if (g_poseSinceMs == 0) g_poseSinceMs = tick;
    pose_shaping::ShapePose(pose, pose_shaping::EntryEase(tick - g_poseSinceMs),
                            report.ZoomFactor);
    report.Applied = pose;

    // The ATV's camera hangs off two spring arms on the vehicle's own physics
    // body, and both inherit its pitch and roll, so while riding the world's up
    // axis has no fixed relation to where the rider is looking and head yaw turns
    // about the camera's own.
    FRotator rotation = clean;
    camera_boundary::ApplyHeadPose(rotation, pose.yaw, pose.pitch, pose.roll,
                                   g_worldSpaceYaw.load(std::memory_order_relaxed) &&
                                       !rig.Riding);
    const FVector eye =
        LeanedEye(cleanLocation, cleanQ, pose, havePosition, dt, rig.Pawn, report);
    *outRotation = ue4::FromCore(rotation);
    *outLocation = ue4::FromCore(eye);
    // Riding, the flashlight is on a player whose camera is not the one drawn.
    flashlight::Update(rig.Player, !rig.Riding,
                       ue::QuatMul(ue::QuatFromEulerDeg(rotation.Pitch, rotation.Yaw, rotation.Roll),
                                   ue::QuatInv(cleanQ)),
                       g_deps.config->light);

    if (hit.Valid && FrameTangents(report.RenderFov, report.TanX, report.TanY))
        report.Mark =
            ProjectMark(*outLocation, *outRotation, hit, aim.Direction, report.TanX, report.TanY);

    g_lastAim = Sample(aim.Origin, aim.Direction, report, *outLocation);
    PublishMark(controller, rig.Pawn, report);

    NoteLeanState(report);
    hook_log::Heartbeat(report, retRva);
}

// The pass's own cost and the gap since the last one, for hook_log::FrameCost.
void NoteFrameCost(std::int64_t before, std::int64_t after) {
    static std::int64_t s_frequency = 0;
    static std::int64_t s_lastStart = 0;
    if (s_frequency == 0) {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        s_frequency = f.QuadPart;
    }
    const auto us = [](std::int64_t ticks) {
        return static_cast<std::uint64_t>(ticks * 1000000 / s_frequency);
    };
    const std::uint64_t gapUs = s_lastStart != 0 ? us(before - s_lastStart) : 0;
    s_lastStart = before;
    hook_log::FrameCost(us(after - before), gapUs);
}

void __fastcall GetPlayerViewPoint_Hook(void* self, ue4::FVector* outLocation, ue4::FRotator* outRotation) {
    const std::uintptr_t retRva = ReturnRva(_ReturnAddress());
    const auto controller = reinterpret_cast<std::uintptr_t>(self);

    g_origGetPlayerViewPoint(self, outLocation, outRotation);

    const int mode = g_injectMode.load(std::memory_order_relaxed);

    dev_console::Poll(controller);

    if (mode == inject::kAllCallers) {
        // Counted here rather than on the way in: the number is only the
        // summary's own interval and nothing outside discovery reads it, so the
        // atomic has no business on the path every ordinary frame takes - and
        // GetPlayerViewPoint runs from several call sites per frame.
        const auto call = g_hookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
        hook_log::CountCaller(retRva, call,
                    static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(outRotation)) -
                    static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(outLocation)));
        return;
    }
    if (!inject::ShouldInject(retRva, mode, Offsets().kKnownCallerRvas)) return;

    const ue4::FRotator cleanRaw = *outRotation;
    const ue4::FVector cleanLocationRaw = *outLocation;
    std::int64_t engineFrame = 0;
    const bool haveFrame = EngineFrame(engineFrame);
    if (ReplayFrame(haveFrame, engineFrame, cleanLocationRaw, cleanRaw, outLocation, outRotation))
        return;

    LARGE_INTEGER before{}, after{};
    QueryPerformanceCounter(&before);
    ApplyFrame(controller, retRva, outLocation, outRotation, cleanLocationRaw, cleanRaw,
               GetTickCount64());
    QueryPerformanceCounter(&after);
    NoteFrameCost(before.QuadPart, after.QuadPart);

    g_frameCache = FrameCache{haveFrame, engineFrame,  cleanLocationRaw,
                              cleanRaw,  *outLocation, *outRotation};
}

}  // namespace

bool Install(const Dependencies& deps) {
    g_deps = deps;
    g_trackingEnabled.store(deps.config->enable_on_startup);
    g_worldSpaceYaw.store(deps.config->world_space_yaw);
    g_injectMode.store(Offsets().kDefaultInjectMode);

    const HMODULE host = GetModuleHandleW(nullptr);
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), host, &mi, sizeof(mi))) {
        Log::Line("FATAL: GetModuleInformation on the host module failed (%lu) - no "
                  "hook installed", GetLastError());
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    ue::SetRuntime(base, base + mi.SizeOfImage, Offsets().UObjectGlobals);

    game_state::SetSoloOnly(deps.config->disable_in_multiplayer);
    aim_trace::SetTraceChannel(deps.config->aim_trace_channel);
    lean_trace::SetMargin(deps.config->lean_clamp.skin);
    // The canonical row passes the channel through unchecked, and the trace
    // writes it into a one-byte ETraceTypeQuery, whose channels are 0 to 31.
    int channel = deps.config->collision_channel;
    if (channel < 0 || channel > 31) {
        Log::Line("config: CollisionChannel=%d is not one of the game's trace channels (0 to 31) - "
                  "the wall check uses channel 0", channel);
        channel = 0;
    }
    lean_trace::SetChannel(channel);
    cameraunlock::camera::LeanClampSettings clamp;
    clamp.skin = 0.0f;  // lean_trace carries the margin along the surface normal
    clamp.release_smoothing = deps.config->lean_clamp.release_smoothing;
    g_leanClamp.SetSettings(clamp);

    auto& hm = cameraunlock::hooks::HookManager::Instance();
    if (auto s = hm.Initialize(); s != cameraunlock::hooks::HookStatus::Ok &&
                                  s != cameraunlock::hooks::HookStatus::ErrorAlreadyInitialized) {
        Log::Line("FATAL: MinHook Initialize failed: %s",
                  cameraunlock::hooks::HookStatusToString(s));
        return false;
    }

    g_hookTarget = reinterpret_cast<void*>(base + Offsets().kGetPlayerViewPointRva);
    if (auto s = hm.CreateHook(g_hookTarget, reinterpret_cast<void*>(&GetPlayerViewPoint_Hook),
                               reinterpret_cast<void**>(&g_origGetPlayerViewPoint));
        s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: CreateHook(GetPlayerViewPoint) failed: %s",
                  cameraunlock::hooks::HookStatusToString(s));
        g_hookTarget = nullptr;
        return false;
    }
    if (auto s = hm.EnableHook(g_hookTarget); s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: EnableHook(GetPlayerViewPoint) failed: %s",
                  cameraunlock::hooks::HookStatusToString(s));
        g_hookTarget = nullptr;
        return false;
    }

    Log::Line("hook: GetPlayerViewPoint at RVA 0x%08llx, module base 0x%llx, inject mode %d, "
              "ProcessEvent %s",
              static_cast<unsigned long long>(Offsets().kGetPlayerViewPointRva),
              static_cast<unsigned long long>(base), g_injectMode.load(),
              ue_vm::Ready() ? "resolved" : "UNAVAILABLE");
    if (g_injectMode.load() == inject::kAllCallers)
        Log::Line("hook: caller-discovery mode - nothing is written to the camera");
    return true;
}

void Shutdown() {
    if (!g_hookTarget) return;
    cameraunlock::hooks::HookManager::Instance().DisableHook(g_hookTarget);
    g_hookTarget = nullptr;
}

bool TrackingEnabled() { return g_trackingEnabled.load(); }
void SetTrackingEnabled(bool enabled) { g_trackingEnabled.store(enabled); }

bool WorldSpaceYaw() { return g_worldSpaceYaw.load(); }
void SetWorldSpaceYaw(bool worldSpaceYaw) { g_worldSpaceYaw.store(worldSpaceYaw); }

AimSample LastAim() { return g_lastAim; }

void RequestReport() { hook_log::RequestReport(); }

void SetMarkOverride(bool valid, double x, double y, double z) {
    g_markOverridePoint = FVector{x, y, z};
    g_markOverride = valid;
}

int  InjectMode() { return g_injectMode.load(); }
void SetInjectMode(int mode) { g_injectMode.store(mode); }

}  // namespace votv_ht::view_hook
