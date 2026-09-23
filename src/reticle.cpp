// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "reticle.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include <windows.h>

#include "logging.h"
#include "ue4_types.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace votv_ht::reticle {

namespace {

namespace ue = ::cameraunlock::unreal;

// Voices of the Void draws its crosshair from one widget. The HUD holds a
// CanvasPanel `curs` with a WidgetSwitcher `switcher_crosshair` inside it, and
// the switcher picks between the four cursor panels the game uses -
// canvas_cursor_class, canvas_cursor_tri, canvas_cursor_cross and
// canvas_cursor_no - as what the player is looking at changes. Moving the
// switcher moves whichever of them is being drawn, so there is one widget to
// bind and no guess about which cursor is live.
//
// Matched on the exact widget name rather than a substring: `curs` and
// `canvas_cursor_cross` are different widgets in the same tree, and a substring
// would take whichever the object table reached first.
constexpr const char* kCrosshairName = "switcher_crosshair";
constexpr std::size_t kInternalIndexOffset = 0x0c;

// The HUD widget blueprint's own tree carries a template copy of every widget
// alongside the live one, and a save-game preview or a second HUD instance adds
// more. All of them bind; the paint test is what decides which are moved.
constexpr std::size_t kMaxPanels = 8;

ue_vm::ResolveRetry g_resolveRetry;
bool g_resolved = false;
std::uintptr_t g_layoutLib = 0;
std::uintptr_t g_widgetClass = 0;
ue_call::Function g_setTranslation;   // Widget::SetRenderTranslation
ue_call::Function g_viewportScale;    // WidgetLayoutLibrary::GetViewportScale
ue_call::Function g_viewportSize;     // WidgetLayoutLibrary::GetViewportSize

// What a readback found. Unpainted is its own answer rather than a
// misplacement: a HUD that is not the active one sits collapsed for a whole
// session, and Slate leaves an unpainted widget's cached geometry at whatever
// it last was, so calling that "not where it was put" cries wolf every time.
enum class Paint { Unknown, Unpainted, Placed, Misplaced };

struct Panel {
    std::uintptr_t Widget = 0;      // the crosshair switcher, which is what moves
    std::int32_t Index = -1;        // its object index, for the liveness test
    // What this mod last wrote to the widget. NaN until the first write, so that
    // write always goes out: a widget re-bound after another panel died keeps
    // whatever translation it was last given, and assuming 0 there would skip
    // the move that puts it back in the centre.
    float LastX = std::numeric_limits<float>::quiet_NaN();
    float LastY = std::numeric_limits<float>::quiet_NaN();
    Paint LastPaint = Paint::Unknown;
    // What the sample before last said. A readback compares this frame's
    // requested position against the geometry Slate cached when it last
    // PAINTED, which is a frame behind, so a head that is moving disagrees by
    // however far the mark travelled in that frame - every time. Reporting only
    // a verdict that has survived two samples a second apart drops that
    // transient and keeps a widget that has genuinely stopped following.
    Paint PendingPaint = Paint::Unknown;
    bool Drawn = false;
};
Panel g_panels[kMaxPanels];
std::size_t g_panelCount = 0;

bool g_enabled = true;

ue_vm::ResolveRetry g_findRetry;
// Whether the panels have been moved at least once, so the first move writes
// the viewport and DPI it was made against and the rest stay silent.
bool g_bound = false;

bool Resolve() {
    if (g_resolved) return true;
    if (!g_resolveRetry.Due() || !ue_vm::Ready()) return false;
    g_layoutLib = ue_call::DefaultObject("WidgetLayoutLibrary");
    g_widgetClass = ue::FindLiveObject("Class", "Widget", nullptr);
    const std::size_t ptr = sizeof(std::uintptr_t);
    const std::size_t v2 = sizeof(ue4::FVector2D);
    if (!g_widgetClass || !g_layoutLib ||
        !g_setTranslation.Resolve("Widget", "SetRenderTranslation", {{"Translation", v2}}) ||
        !g_viewportScale.Resolve("WidgetLayoutLibrary", "GetViewportScale",
                                 {{"WorldContextObject", ptr}, {"ReturnValue", sizeof(float)}}) ||
        !g_viewportSize.Resolve("WidgetLayoutLibrary", "GetViewportSize",
                                {{"WorldContextObject", ptr}, {"ReturnValue", v2}}))
        return false;
    g_resolved = true;
    return true;
}

// ---- readback: where Slate last drew the crosshair ----------------------
// The move is a request; the paint is the fact. Once a second the bound
// widget's render translation and its last painted geometry are read back and
// compared with the position that was asked for, so "the crosshair is not where
// the mod put it" is a line in the log rather than a guess.
//
// Only a CHANGE in that comparison is written. Logging every sample buries the
// build match, the link and the gate transitions a report is read for.
constexpr std::uint64_t kReadbackMs = 1000;
// Whole-pixel rounding on the way out and the DPI divide on the way back leave
// well under a pixel of round-trip error; anything past this is the widget not
// following.
constexpr float kDrawTolerancePx = 2.0f;
ue_vm::ResolveRetry g_readbackRetry;
bool g_readbackResolved = false;
std::uintptr_t g_slateLib = 0;
ue_call::Function g_cachedGeometry;   // Widget::GetCachedGeometry
ue_call::Function g_localToViewport;  // SlateBlueprintLibrary::LocalToViewport
ue_call::Function g_localSize;        // SlateBlueprintLibrary::GetLocalSize
ue_call::Function g_isVisible;        // Widget::IsVisible
std::size_t g_geometrySize = 0;
std::size_t g_renderTransformOffset = 0;
std::uint64_t g_lastReadbackMs = 0;

// How many viewport pixels one unit of render translation moves the crosshair.
//
// NOT the DPI scale. WidgetLayoutLibrary::GetViewportScale answers for the
// viewport - 0.666 at 720p on the stock curve - but a render translation is
// scaled by EVERY transform between the widget and the viewport, and this
// game's HUD sits under one of its own. Asking the DPI alone put the crosshair
// 23% short of where it was sent: Slate reported a drawn centre of (301, 561)
// px for a translation the mod expected to land at (365, 523).
//
// So it is measured from the widget's own painted geometry, through the
// engine's own SlateBlueprintLibrary::LocalToViewport: the pixel distance
// between two local points a known distance apart. 0 until the first
// measurement, which leaves the DPI as the fallback.
float g_pixelsPerUnit = 0.0f;

bool ResolveReadback() {
    if (g_readbackResolved) return true;
    if (!g_readbackRetry.Due()) return false;
    const std::uintptr_t geometry = ue::FindLiveObject("ScriptStruct", "Geometry", nullptr);
    const std::uintptr_t widgetClass = ue::FindLiveObject("Class", "Widget", nullptr);
    g_slateLib = ue_call::DefaultObject("SlateBlueprintLibrary");
    ue_reflect::FieldInfo transform;
    if (!geometry || !widgetClass || !g_slateLib ||
        !ue_reflect::FindPropertyInChain(widgetClass, "RenderTransform", transform))
        return false;
    g_geometrySize = ue_reflect::StructSize(geometry);
    // A zero size would satisfy every FieldFits below, and the readback would
    // then hand the engine a geometry of no bytes and report "has not been
    // drawn" for the rest of the session with nothing saying why.
    if (g_geometrySize == 0 || g_geometrySize > ue_call::Function::kMaxFrame) return false;
    const std::size_t v2 = sizeof(ue4::FVector2D);
    if (!g_cachedGeometry.Resolve("Widget", "GetCachedGeometry", {{"ReturnValue", g_geometrySize}}) ||
        !g_localToViewport.Resolve("SlateBlueprintLibrary", "LocalToViewport",
                                   {{"WorldContextObject", sizeof(std::uintptr_t)}, {"Geometry", g_geometrySize},
                                    {"LocalCoordinate", v2}, {"PixelPosition", v2}, {"ViewportPosition", v2}}) ||
        !g_localSize.Resolve("SlateBlueprintLibrary", "GetLocalSize",
                             {{"Geometry", g_geometrySize}, {"ReturnValue", v2}}) ||
        !g_isVisible.Resolve("Widget", "IsVisible", {{"ReturnValue", 1}}))
        return false;
    g_renderTransformOffset = transform.Offset;
    g_readbackResolved = true;
    return true;
}

// Whether Slate is currently drawing this widget. False for every item the
// game is not drawing - the blueprint's own template copy - and for the live
// one while the HUD is hidden.
bool Painted(std::uintptr_t widget) {
    if (!widget || !ResolveReadback()) return false;
    ue_call::Frame vis(g_isVisible);
    if (!vis.Call(widget)) return false;
    return (vis.Get<std::uint8_t>(0) & 1u) != 0;
}

struct Drawn {
    bool Valid = false;
    bool Visible = false;
    ue4::FVector2D Translation{0.0f, 0.0f};
    ue4::FVector2D Centre{0.0f, 0.0f};   // viewport pixels
    ue4::FVector2D Size{0.0f, 0.0f};     // local units
};

// `widget` is the crosshair that was moved: its own render translation and its
// own painted geometry.
Drawn ReadDrawn(std::uintptr_t controller, std::uintptr_t widget) {
    Drawn d;
    if (!widget || !ResolveReadback()) return d;
    ue::SafeReadFloat(widget + g_renderTransformOffset, d.Translation.X);
    ue::SafeReadFloat(widget + g_renderTransformOffset + 4, d.Translation.Y);
    d.Visible = Painted(widget);
    ue_call::Frame geo(g_cachedGeometry);
    if (!geo.Call(widget)) return d;
    ue_call::Frame size(g_localSize);
    std::memcpy(size.Data() + g_localSize.Offset(0), geo.At(0), g_geometrySize);
    if (!size.Call(g_slateLib)) return d;
    d.Size = size.Get<ue4::FVector2D>(1);
    ue_call::Frame toViewport(g_localToViewport);
    toViewport.Set(0, controller);
    std::memcpy(toViewport.Data() + g_localToViewport.Offset(1), geo.At(0), g_geometrySize);
    toViewport.Set(2, ue4::FVector2D{d.Size.X * 0.5f, d.Size.Y * 0.5f});
    if (!toViewport.Call(g_slateLib)) return d;
    d.Centre = toViewport.Get<ue4::FVector2D>(3);
    d.Valid = true;
    return d;
}

// An object the engine has destroyed leaves its slot to be reused by something
// else, so the index has to still hold the same object.
bool StillAlive(std::uintptr_t obj, std::int32_t index) {
    const std::uintptr_t item = ue_call::ObjectItem(index);
    std::uintptr_t live = 0;
    return obj != 0 && item != 0 && ue::SafeReadPtr(item, live) && live == obj;
}

bool PanelsAlive() {
    if (g_panelCount == 0) return false;
    for (std::size_t i = 0; i < g_panelCount; ++i)
        if (!StillAlive(g_panels[i].Widget, g_panels[i].Index)) return false;
    return true;
}

// Every live widget named as the crosshair switcher. The set only changes when
// the HUD itself is rebuilt - a level load, a save reload - which the liveness
// test above catches.
bool Bind() {
    if (PanelsAlive()) return true;
    for (Panel& panel : g_panels) panel = Panel{};
    g_panelCount = 0;
    if (!g_findRetry.Due()) return false;

    ue::ForEachUObject([&](std::uintptr_t obj) {
        if (g_panelCount >= kMaxPanels) return true;
        if (!ue::EqualsCI(ue::ObjectName(obj), kCrosshairName)) return false;
        if (!ue_call::IsInstanceOf(obj, g_widgetClass)) return false;
        std::uint32_t index = 0;
        if (!ue::SafeReadU32(obj + kInternalIndexOffset, index)) return false;
        g_panels[g_panelCount].Widget = obj;
        g_panels[g_panelCount].Index = static_cast<std::int32_t>(index);
        ++g_panelCount;
        return false;
    });
    if (g_panelCount == 0) return false;
    for (std::size_t i = 0; i < g_panelCount; ++i)
        Log::Line("reticle: bound %s %s (0x%llx) under %s",
                  ue::ClassName(g_panels[i].Widget).c_str(),
                  ue::ObjectName(g_panels[i].Widget).c_str(),
                  static_cast<unsigned long long>(g_panels[i].Widget),
                  ue::OuterName(g_panels[i].Widget).c_str());
    return true;
}

// Only the crosshair on screen is moved. The blueprint's template tree carries
// a copy of the switcher that Slate never paints, and a widget nothing is
// drawing does not need to be in the right place - so the paint test is what
// separates the live HUD from the templates, and a widget is moved on the frame
// it comes back.
//
// True when at least one crosshair was written to this call - not when the pass
// merely ran. A frame where every crosshair already sits where it is being
// asked to sit wrote nothing, and saying otherwise is what would date the
// "first move" line below to a frame no widget moved on.
bool Move(float x, float y) {
    bool wrote = false;
    for (std::size_t i = 0; i < g_panelCount; ++i) {
        Panel& panel = g_panels[i];
        // The position test comes before the paint test: IsVisible is a
        // dispatch through the script VM, once per bound crosshair, and a mark
        // that has not moved needs no write whatever Slate is drawing.
        if (!panel.Widget || (x == panel.LastX && y == panel.LastY)) continue;
        if (!Painted(panel.Widget)) continue;
        ue_call::Frame frame(g_setTranslation);
        frame.Set(0, ue4::FVector2D{x, y});
        if (!frame.Call(panel.Widget)) {
            g_panelCount = 0;
            return false;
        }
        panel.LastX = x;
        panel.LastY = y;
        wrote = true;
    }
    return wrote;
}

// The viewport size and DPI scale, or false when they do not read.
bool Viewport(std::uintptr_t controller, ue4::FVector2D& size, float& dpi) {
    ue_call::Frame sizeFrame(g_viewportSize);
    sizeFrame.Set(0, controller);
    ue_call::Frame scale(g_viewportScale);
    scale.Set(0, controller);
    if (!sizeFrame.Call(g_layoutLib) || !scale.Call(g_layoutLib)) return false;
    size = sizeFrame.Get<ue4::FVector2D>(1);
    dpi = scale.Get<float>(1);
    return size.X > 0.0f && size.Y > 0.0f && dpi > 0.0f;
}

// What the last readback said about one panel, against the centre the move
// asked for. Only a CHANGE is written: see kReadbackMs.
void LogPanelPaint(Panel& panel, const Drawn& d, float askedX, float askedY, float expectedX,
                   float expectedY, const ue4::FVector2D& viewport, float dpi) {
    // A widget Slate is not drawing has no position to check. The size is NOT
    // part of that test: a WidgetSwitcher has no size of its own - its children
    // carry it - so requiring one here reported the crosshair as never drawn
    // while Slate was reporting a perfectly good centre for it.
    const Paint paint =
        !d.Valid || !d.Visible ? Paint::Unpainted
        : std::fabs(d.Centre.X - expectedX) <= kDrawTolerancePx &&
          std::fabs(d.Centre.Y - expectedY) <= kDrawTolerancePx ? Paint::Placed
                                                                : Paint::Misplaced;
    if (paint != panel.PendingPaint) {
        panel.PendingPaint = paint;
        return;
    }
    if (paint == panel.LastPaint && d.Visible == panel.Drawn) return;
    panel.LastPaint = paint;
    panel.Drawn = d.Visible;
    Log::Line("reticle: %s %s %s - asked translation (%.1f,%.1f) -> expected centre (%.0f,%.0f) px | "
              "translation (%.1f,%.1f) visible=%d drawn centre (%.0f,%.0f) px size (%.1f,%.1f) valid=%d | "
              "viewport %.0fx%.0f dpi %.3f",
              ue::ClassName(panel.Widget).c_str(), ue::ObjectName(panel.Widget).c_str(),
              paint == Paint::Unpainted ? "has not been drawn"
                  : paint == Paint::Placed ? "is drawn where it was put"
                                           : "is NOT drawn where it was put",
              askedX, askedY, expectedX, expectedY,
              d.Translation.X, d.Translation.Y, d.Visible ? 1 : 0, d.Centre.X, d.Centre.Y, d.Size.X,
              d.Size.Y, d.Valid ? 1 : 0, viewport.X, viewport.Y, dpi);
}

// Viewport pixels per unit of render translation, measured off `widget`'s own
// painted geometry. False when the widget is not painted or the transform did
// not read, which leaves the previous measurement in place.
bool MeasurePixelsPerUnit(std::uintptr_t controller, std::uintptr_t widget, float& out) {
    if (!widget || !ResolveReadback()) return false;
    ue_call::Frame geo(g_cachedGeometry);
    if (!geo.Call(widget)) return false;
    constexpr float kProbe = 100.0f;
    ue4::FVector2D at[2] = {{0.0f, 0.0f}, {kProbe, 0.0f}};
    ue4::FVector2D px[2] = {};
    for (int i = 0; i < 2; ++i) {
        ue_call::Frame frame(g_localToViewport);
        frame.Set(0, controller);
        std::memcpy(frame.Data() + g_localToViewport.Offset(1), geo.At(0), g_geometrySize);
        frame.Set(2, at[i]);
        if (!frame.Call(g_slateLib)) return false;
        px[i] = frame.Get<ue4::FVector2D>(3);
    }
    const float scale = (px[1].X - px[0].X) / kProbe;
    if (!(scale > 0.05f && scale < 20.0f)) return false;
    out = scale;
    return true;
}

// Once a second, where Slate actually drew each panel against where it was put.
void ReadBackPanels(std::uintptr_t controller, float askedX, float askedY, float pixelX,
                    float pixelY, const ue4::FVector2D& viewport, float dpi) {
    const std::uint64_t now = GetTickCount64();
    if (now - g_lastReadbackMs < kReadbackMs) return;
    g_lastReadbackMs = now;

    for (std::size_t i = 0; i < g_panelCount; ++i) {
        if (!Painted(g_panels[i].Widget)) continue;
        float scale = 0.0f;
        if (!MeasurePixelsPerUnit(controller, g_panels[i].Widget, scale)) continue;
        // The measurement lands on the same number to a few decimal places but
        // not to the last bit, so an equality test writes this line once a
        // second for the rest of the session. Half a percent is far below what
        // would move the crosshair a pixel and far above that jitter.
        if (g_pixelsPerUnit > 0.0f && std::fabs(scale - g_pixelsPerUnit) < g_pixelsPerUnit * 0.005f)
            break;
        Log::Line("reticle: %.4f viewport pixels per unit of render translation "
                  "(DPI scale says %.4f)", scale, dpi);
        g_pixelsPerUnit = scale;
        break;
    }

    const float expectedX = viewport.X * 0.5f + pixelX;
    const float expectedY = viewport.Y * 0.5f + pixelY;
    for (std::size_t i = 0; i < g_panelCount; ++i)
        LogPanelPaint(g_panels[i], ReadDrawn(controller, g_panels[i].Widget),
                      askedX, askedY, expectedX, expectedY, viewport, dpi);
}

}  // namespace

void SetEnabled(bool enabled) { g_enabled = enabled; }

void Publish(std::uintptr_t controller, std::uintptr_t pawn, bool valid, float ndcX, float ndcY) {
    if (!g_enabled || !pawn || !Resolve() || !Bind()) return;
    if (!valid) {
        Move(0.0f, 0.0f);
        return;
    }

    ue4::FVector2D viewport{};
    float dpi = 1.0f;
    if (!Viewport(controller, viewport, dpi)) {
        Move(0.0f, 0.0f);
        return;
    }
    // Render translation is in the widget's own units; the pixels it moves are
    // that times every scale between the widget and the viewport. The DPI scale
    // is only the first of those, so it is the fallback until the measurement
    // below has run once. Screen y runs down.
    const float pixelX = std::round(ndcX * viewport.X * 0.5f);
    const float pixelY = std::round(-ndcY * viewport.Y * 0.5f);
    const float perUnit = g_pixelsPerUnit > 0.0f ? g_pixelsPerUnit : dpi;
    const float tx = pixelX / perUnit;
    const float ty = pixelY / perUnit;
    if (Move(tx, ty) && !g_bound) {
        g_bound = true;
        Log::Line("reticle: first move (viewport %.0fx%.0f, %.4f px per unit, DPI scale %.3f)",
                  viewport.X, viewport.Y, perUnit, dpi);
    }

    ReadBackPanels(controller, tx, ty, pixelX, pixelY, viewport, dpi);
}

}  // namespace votv_ht::reticle
