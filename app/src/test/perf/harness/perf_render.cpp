// Times Canvas's overlay frame on a desktop host, without Sky, mods or a phone.
//
// Real code: androidbk.cpp's render loop and input bridge, scroll.cpp, ImGui,
// Canvas.cpp, and main.cpp's HelpMarker / ModStyleScope / DrawMods / CanvasMenu
// (extracted verbatim by run_perf.sh). Stand-ins: EGL/GLES/window/JNI
// (../sanitizers/harness/app/fake_*.cpp) and the mods, which are small panels
// of ordinary ImGui widgets. Built like the app: -O3, LTO, NDEBUG, no sanitizers.
//
// Per frame, on the render thread, it records wall time (steady_clock) and the
// thread's own CPU time (CLOCK_THREAD_CPUTIME_ID) at five points:
//   0 frame start (GL backend NewFrame)      1 ImGui NewFrame done (NewFramePost hook)
//   2 menu + mods drawn (EndFramePre hook)   3 Render + snapshot done (GL submission)
//   4 swap
// Stages: input = 0->1, menu = 1->2, render = 2->3, submit = 3->4.
//
//   perf_render <scenario> <mods> <frames>
//   scenarios: menu (Canvas menu shown, frames back to back), hidden (menu
//              hidden, mods still drawn), touch (menu shown, UI thread
//              delivering touch input), vsync (menu shown, frames 16.7 ms
//              apart), limited ("Limit FPS" on: Canvas's 41 ms sleep),
//              taps (frames from a tap or typed text to the widget; one
//              stand-in mod, menu hidden; counts, not timings)
#include <android/keycodes.h>  // run_perf.sh generates it from androidbk.cpp
#include <jni.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>  // no-ops unless running under callgrind
#else
#define CALLGRIND_ZERO_STATS
#define CALLGRIND_DUMP_STATS_AT(label)
#endif

#include "Canvas/Canvas.h"
#include "Canvas/InitializationGate.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui_androidbk/androidbk.h"
#include "include/misc/visibility.h"

// main.cpp's file-static gate; the extracted CanvasMenu checks it.
static Canvas::InitializationGate g_modInitialization;
#include "HelpMarker.inc"
#include "ModStyleScope.inc"
#include "DrawMods.inc"
#include "CanvasMenu.inc"

extern int g_fakeSwapSleepUs, g_fakeWindowW, g_fakeWindowH, g_fakeVsyncAligned, g_fakeSwapQueued;
extern long long g_fakePresentNs;  // queued-swap model: vsync the pushed frame will be latched at
// Defined only by design-variant builds (patched scratch copies of androidbk.cpp).
extern "C" __attribute__((weak)) void perf_variant_report(double seconds);
extern void (*g_fakeOnGlInit)();
long FakeSwapCount();
extern void (*g_fakeFramePoint)(int);

extern "C" {
void Java_git_artdeell_skymodloader_ImGUI_init(JNIEnv *, jclass, jobject, jfloat, jfloat, jobject, jboolean);
void Java_git_artdeell_skymodloader_ImGUI_shutdown(JNIEnv *, jclass);
void Java_git_artdeell_skymodloader_ImGUI_submitPositionEvent(JNIEnv *, jclass, jfloat, jfloat);
void Java_git_artdeell_skymodloader_ImGUI_submitButtonEvent(JNIEnv *, jclass, jint, jboolean);
void Java_git_artdeell_skymodloader_ImGUI_submitUnicodeEvent(JNIEnv *, jclass, jchar);
void Java_git_artdeell_skymodloader_ImGUI_submitKeyEvent(JNIEnv *, jclass, jint, jboolean);
jboolean Java_git_artdeell_skymodloader_ImGUI_wantsMouse(JNIEnv *, jclass);
jboolean Java_git_artdeell_skymodloader_ImGUI_wantsKeyboard(JNIEnv *, jclass);
}

namespace {

// A ~420 dpi phone in portrait, as on the device in the Codex audit:
// ImGUI.java passes sp2px(14) and dp2px(1.2) for font size and scale.
constexpr float kFontPx = 36.75f;
constexpr float kScale = 3.15f;

struct Frame {
    long long wall[5];
    long long cpu[5];
    int vertices;
    int drawLists;
    int drawCalls;      // as imgui_impl_opengl3 issues them: one per non-clipped ImDrawCmd
    long uploadBytes;   // glBufferData per draw list: full vertex + index arrays, every frame
    unsigned long long drawHash;  // of everything the GL backend would submit
    long swapsAtStart;            // eglSwapBuffers calls so far, sampled at frame start
    long long analysisCpu;        // the harness's own counting/hashing at point 3,
    long long analysisWall;       // subtracted from the frame's timings
    long long presentNs;          // vsync at which the frame was latched (PERF_VSYNC_ALIGNED), else 0
};

// FNV-1a over the submitted geometry and commands. Equal hashes on consecutive
// frames mean the frame drew exactly what the previous one did, i.e. its clear,
// render and present were redundant.
unsigned long long hash_draw_data(const ImDrawData *dd) {
    unsigned long long h = 1469598103934665603ULL;
    auto mix = [&h](const void *data, size_t size) {
        const unsigned char *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 1099511628211ULL;
    };
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList *list = dd->CmdLists[n];
        mix(list->VtxBuffer.Data, sizeof(ImDrawVert) * list->VtxBuffer.Size);
        mix(list->IdxBuffer.Data, sizeof(ImDrawIdx) * list->IdxBuffer.Size);
        for (const ImDrawCmd &cmd : list->CmdBuffer) {
            mix(&cmd.ClipRect, sizeof(cmd.ClipRect));
            mix(&cmd.TextureId, sizeof(cmd.TextureId));
            mix(&cmd.VtxOffset, sizeof(cmd.VtxOffset));
            mix(&cmd.IdxOffset, sizeof(cmd.IdxOffset));
            mix(&cmd.ElemCount, sizeof(cmd.ElemCount));
        }
    }
    return h;
}

// What the vendored GL backend would submit for this frame
// (imgui_impl_opengl3.cpp RenderDrawData: lines 545-620).
void count_gl_submission(const ImDrawData *dd, Frame &out) {
    out.drawLists = dd->CmdListsCount;
    out.drawCalls = 0;
    out.uploadBytes = 0;
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList *list = dd->CmdLists[n];
        out.uploadBytes += static_cast<long>(list->VtxBuffer.Size) * sizeof(ImDrawVert) +
                           static_cast<long>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        for (const ImDrawCmd &cmd : list->CmdBuffer) {
            if (cmd.UserCallback != nullptr) continue;
            const float x0 = (cmd.ClipRect.x - dd->DisplayPos.x) * dd->FramebufferScale.x;
            const float y0 = (cmd.ClipRect.y - dd->DisplayPos.y) * dd->FramebufferScale.y;
            const float x1 = (cmd.ClipRect.z - dd->DisplayPos.x) * dd->FramebufferScale.x;
            const float y1 = (cmd.ClipRect.w - dd->DisplayPos.y) * dd->FramebufferScale.y;
            if (x1 <= x0 || y1 <= y0) continue;  // the backend's own skip rule
            ++out.drawCalls;
        }
    }
}

std::vector<Frame> g_frames;
Frame g_current{};
std::atomic<bool> g_measuring{false};
std::atomic<long> g_frameCount{0};
std::atomic<long> g_recorded{0};
long g_capacity = 0;

long long wall_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
long long cpu_ns() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

long long g_initStartNs = 0;  // set just before ImGUI_init (startup timing, pass 47)
bool g_firstFrameSeen = false;
bool g_inWindow = false;  // render thread only: this frame started inside the measured window
bool g_awaitPresent = false;
bool g_noAnalysis = false;  // PERF_NO_ANALYSIS: skip the harness's GL counting and hashing

bool g_tapScenario = false;
void tap_script();

// "taplatency" (pass 61): start and present time of each frame, by frame number.
bool g_tapLatency = false;
constexpr int kMaxTapFrames = 1 << 16;
std::atomic<long long> g_frameStartNs[kMaxTapFrames];
std::atomic<long long> g_framePresentNs[kMaxTapFrames];
std::atomic<int> g_framesStarted{0};

void mark(int point) {  // render thread only
    if (point == 4 && g_tapScenario) tap_script();  // after this frame's swap, before the next Drain
    if (g_tapLatency && (point == 0 || point == 5)) {
        const int fn = ImGui::GetFrameCount() + (point == 0 ? 1 : 0);  // point 0 precedes NewFrame
        if (fn > 0 && fn < kMaxTapFrames) {
            if (point == 0) {
                g_frameStartNs[fn] = wall_ns();
                g_framesStarted = fn;
            } else {
                g_framePresentNs[fn] = g_fakeSwapQueued ? g_fakePresentNs : wall_ns();
            }
        }
    }
    if (point == 5) {  // the fake display latched the frame just pushed
        if (g_awaitPresent && !g_frames.empty()) g_frames.back().presentNs = g_fakeSwapQueued ? g_fakePresentNs : wall_ns();
        g_awaitPresent = false;
        return;
    }
    if (point == 0 && !g_firstFrameSeen) {
        g_firstFrameSeen = true;
        std::printf("  startup: first frame began %.1f ms after ImGUI_init\n", (wall_ns() - g_initStartNs) / 1e6);
        std::fflush(stdout);
    }
    if (point == 0 && g_measuring && g_recorded < g_capacity) {
        // Callgrind zero/dump happen here, on the render thread, so the dump
        // holds exactly the recorded frames whatever the scheduler does.
        if (g_recorded == 0 && !g_inWindow) CALLGRIND_ZERO_STATS;
        g_inWindow = true;
    }
    g_current.wall[point] = wall_ns();
    g_current.cpu[point] = cpu_ns();
    if (point == 0) g_current.swapsAtStart = FakeSwapCount();
    if (point == 3) {
        g_current.analysisCpu = g_current.analysisWall = 0;
        if (ImGui::GetDrawData() && !g_noAnalysis) {
            g_current.vertices = ImGui::GetDrawData()->TotalVtxCount;
            count_gl_submission(ImGui::GetDrawData(), g_current);
            g_current.drawHash = hash_draw_data(ImGui::GetDrawData());
            g_current.analysisCpu = cpu_ns() - g_current.cpu[3];  // harness work, not Canvas's
            g_current.analysisWall = wall_ns() - g_current.wall[3];
        }
    }
    if (point == 4) {
        ++g_frameCount;
        if (g_inWindow) {
            g_current.presentNs = 0;
            g_frames.push_back(g_current);
            g_awaitPresent = true;
            g_inWindow = false;
            if (++g_recorded == g_capacity) CALLGRIND_DUMP_STATS_AT("measured frames");
        }
    }
}

void install_hooks() {
    // Runs in the fake ImGui_ImplOpenGL3_Init, after loadFonts() built the atlas.
    {
        ImFontAtlas *atlas = ImGui::GetIO().Fonts;
        int glyphs = 0;
        for (ImFont *font : atlas->Fonts) glyphs += font->Glyphs.Size;
        std::printf("  startup: fonts loaded and atlas built %.1f ms after ImGUI_init; atlas %dx%d, %d glyphs, "
                    "%.1f MB as RGBA32 (the GL3 backend uploads RGBA32)\n",
                    (wall_ns() - g_initStartNs) / 1e6, atlas->TexWidth, atlas->TexHeight, glyphs,
                    atlas->TexWidth * 4.0 * atlas->TexHeight / (1024.0 * 1024.0));
        std::fflush(stdout);
    }
    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_NewFramePost;
    hook.Callback = [](ImGuiContext *, ImGuiContextHook *) { mark(1); };
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
    hook.Type = ImGuiContextHookType_EndFramePre;
    hook.Callback = [](ImGuiContext *, ImGuiContextHook *) { mark(2); };
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
}

// A typical small mod panel: status text, six option rows, a wrapped note.
void stand_in_mod(bool *) {
    static bool flags[6] = {};
    static float values[6] = {0.25f, 0.5f, 0.75f, 0.1f, 0.9f, 0.6f};
    ImGui::Text("Status: running");
    for (int i = 0; i < 6; ++i) {
        ImGui::PushID(i);
        ImGui::Checkbox("Option", &flags[i]);
        ImGui::SameLine();
        ImGui::SliderFloat("##value", &values[i], 0.0f, 1.0f);
        ImGui::SameLine();
        ImGui::Button("Apply");
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextWrapped("Stand-in mod panel used to measure Canvas's per-frame overlay cost on the host.");
}

// PERF_DYNAMIC_MODS=k: the first k stand-in mods also show a live value, as a
// mod displaying a timer or a game value would. PERF_DYNAMIC_TEXT=frame (the
// default) changes it every frame; =tenth shows seconds to one decimal (10 Hz).
bool g_dynamicTenth = false;
void stand_in_mod_dynamic(bool *open) {
    stand_in_mod(open);
    if (g_dynamicTenth) ImGui::Text("Live: %.1f s", ImGui::GetTime());
    else ImGui::Text("Live: frame %d", ImGui::GetFrameCount());
}

// "taps" scenario (pass 58, PERF.md queue 10): how many overlay frames a tap or
// typed text takes to reach a widget. Counted in frames, not timed. Real code:
// the JNI entry points, CanvasInput's queue and Drain, ImGui's input queue and
// widgets. Stand-in: one mod window holding a button and a text field (Canvas
// menu hidden, so that window is the only one under the touch). The script runs
// on the render thread after a frame's swap, so every event it submits lands in
// the gap before the next frame's Drain, as UI-thread events between two frames
// would.
std::vector<int> g_tapClickFrames;  // frames in which the button reported a click
std::atomic<int> g_tapClickCount{0}, g_tapLastClickFrame{0};  // the same, for the UI thread
std::atomic<float> g_tapX{-1}, g_tapY{-1};  // button centre, display coordinates
bool g_tapFocus = false;
char g_tapText[64] = "";
std::vector<int> g_tapTextLen;      // text length at the end of each frame, by frame number
std::atomic<bool> g_tapDone{false};

void stand_in_tap(bool *) {
    if (ImGui::Button("Tap target", ImVec2(400, 160))) {
        g_tapClickFrames.push_back(ImGui::GetFrameCount());
        g_tapLastClickFrame = ImGui::GetFrameCount();
        ++g_tapClickCount;
    }
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    g_tapX = (a.x + b.x) / 2;
    g_tapY = (a.y + b.y) / 2;
    if (g_tapFocus) {
        ImGui::SetKeyboardFocusHere();
        g_tapFocus = false;
    }
    ImGui::InputText("##tap text", g_tapText, sizeof(g_tapText));
    const int f = ImGui::GetFrameCount();
    if (std::getenv("TAPS_DEBUG")) {  // per-frame input state as ImGui sees it
        const ImGuiIO &io = ImGui::GetIO();
        std::printf("  frame %d: pos (%.0f, %.0f) down=%d clicked=%d released=%d active=%s hovered=%s\n", f, io.MousePos.x,
                    io.MousePos.y, io.MouseDown[0], io.MouseClicked[0], io.MouseReleased[0],
                    ImGui::GetCurrentContext()->ActiveId ? "yes" : "no",
                    ImGui::GetCurrentContext()->HoveredWindow ? ImGui::GetCurrentContext()->HoveredWindow->Name : "-");
    }
    if (static_cast<int>(g_tapTextLen.size()) <= f) g_tapTextLen.resize(f + 1, -1);
    g_tapTextLen[f] = static_cast<int>(std::strlen(g_tapText));
}

void tap_report_click(const char *name, int after) {
    int clicked = -1;
    for (int c : g_tapClickFrames) if (c > after) { clicked = c; break; }
    std::printf("  taps: %-62s events after frame %d, click reported in frame %d -> %d frame(s)\n", name, after, clicked,
                clicked < 0 ? -1 : clicked - after);
}

void tap_report_text(const char *name, int after, int target) {
    std::printf("  taps: %-62s events after frame %d, text length by frame:", name, after);
    int reached = -1;
    for (int f = after + 1; f < static_cast<int>(g_tapTextLen.size()) && f <= after + 8; ++f) {
        std::printf(" %d", g_tapTextLen[f]);
        if (reached < 0 && g_tapTextLen[f] >= target) reached = f;
    }
    std::printf(" -> all applied after %d frame(s)\n", reached < 0 ? -1 : reached - after);
}

void tap_script() {  // render thread, after a swap
    static _jobject cls;
    JNIEnv *env = FakeJniEnv();
    auto pos = [&](float x, float y) { Java_git_artdeell_skymodloader_ImGUI_submitPositionEvent(env, &cls, x, y); };
    auto button = [&](bool down) { Java_git_artdeell_skymodloader_ImGUI_submitButtonEvent(env, &cls, 0, down); };
    auto chars = [&](const char *text) {
        for (const char *c = text; *c; ++c) Java_git_artdeell_skymodloader_ImGUI_submitUnicodeEvent(env, &cls, *c);
    };
    auto key = [&](int code, bool down) { Java_git_artdeell_skymodloader_ImGUI_submitKeyEvent(env, &cls, code, down); };
    const int f = ImGui::GetFrameCount();  // the frame that just ended
    static int step = 0, at = 0;
    const int wait = 12;                    // frames to let each case settle
    if (f < 60) return;                     // warm-up: fonts, layout
    switch (step) {
    case 0:  // GameActivity: ACTION_DOWN -> position + press; ACTION_UP -> position + release
        std::printf("  taps: button centre (%.0f, %.0f) in a %dx%d display\n", g_tapX.load(), g_tapY.load(), g_fakeWindowW, g_fakeWindowH);
        pos(g_tapX, g_tapY); button(true); pos(g_tapX, g_tapY); button(false);
        at = f; ++step; break;
    case 1:
        if (f < at + wait) break;
        tap_report_click("quick tap, down+up in one gap, release at the press position", at);
        pos(g_tapX - 30, g_tapY); button(true); pos(g_tapX - 28, g_tapY); button(false);
        at = f; ++step; break;
    case 2:
        if (f < at + wait) break;
        tap_report_click("quick tap, down+up in one gap, release 2 px away", at);
        pos(g_tapX, g_tapY); button(true);
        at = f; ++step; break;
    case 3:  // the release one frame later: the tap spans a frame boundary
        pos(g_tapX, g_tapY); button(false);
        at = f; ++step; break;
    case 4:
        if (f < at + wait) break;
        tap_report_click("tap spanning a frame boundary (counted from the release)", at);
        g_tapFocus = true;  // the field becomes active over the next frames
        at = f; ++step; break;
    case 5:
        if (f < at + wait) break;
        chars("abcdefgh");
        at = f; ++step; break;
    case 6:
        if (f < at + wait) break;
        tap_report_text("8 characters in one gap (IME text, submitUnicodeEvent)", at, 8);
        chars("ij"); key(AKEYCODE_SHIFT_LEFT, true); key(AKEYCODE_SHIFT_LEFT, false); chars("kl");
        at = f; ++step; break;
    case 7:
        if (f < at + wait) break;
        tap_report_text("'i','j', Shift down, Shift up, 'k','l' in one gap", at, 12);
        g_tapDone = true;
        ++step; break;
    default: break;
    }
}

double pct(std::vector<long long> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * (v.size() - 1))] / 1000.0;  // ns -> us
}

}  // namespace

int main(int argc, char **argv) {
    const std::string scenario = argc > 1 ? argv[1] : "menu";
    const int mods = argc > 2 ? std::atoi(argv[2]) : 0;
    g_capacity = argc > 3 ? std::atol(argv[3]) : 3000;
    g_frames.reserve(g_capacity);

    // Frames back to back by default (hot caches: a lower bound). "vsync" spaces
    // them 16.7 ms apart, as at 60 Hz, so caches and clocks cool between frames.
    // PERF_SWAP_QUEUED: swaps queue and return (non-async BufferQueue) and are
    // latched one per vsync; implies the vsync clock and the gap histogram.
    g_fakeSwapQueued = std::getenv("PERF_SWAP_QUEUED") ? 1 : 0;
    g_fakeSwapSleepUs = (scenario == "vsync" || scenario == "taps" || std::getenv("PERF_VSYNC") || std::getenv("PERF_VSYNC_ALIGNED") || g_fakeSwapQueued) ? 16667 : 0;
    g_fakeVsyncAligned = (std::getenv("PERF_VSYNC_ALIGNED") || g_fakeSwapQueued) ? 1 : 0;  // presents land on vsync boundaries
    g_noAnalysis = std::getenv("PERF_NO_ANALYSIS") != nullptr;
    g_fakeWindowW = 1080;
    g_fakeWindowH = 2376;
    g_fakeOnGlInit = install_hooks;
    g_fakeFramePoint = mark;
    g_tapScenario = scenario == "taps";
    g_tapLatency = scenario == "taplatency";

    // Canvas state after a normal start: mods registered, initialization published.
    g_modInitialization.begin();
    const int dynamicMods = std::getenv("PERF_DYNAMIC_MODS") ? std::atoi(std::getenv("PERF_DYNAMIC_MODS")) : 0;
    g_dynamicTenth = std::getenv("PERF_DYNAMIC_TEXT") && !std::strcmp(std::getenv("PERF_DYNAMIC_TEXT"), "tenth");
    for (int i = 0; i < mods; ++i) {
        Canvas::UserLib lib;
        lib.Name = "Mod " + std::to_string(i + 1);
        lib.Author = "Harness";
        lib.Description = "Stand-in mod";
        lib.Version = "1.0";
        lib.UIEnabled = true;
        lib.UISelfManaged = false;
        lib.DisplaysUI = true;
        lib.Draw = g_tapScenario || g_tapLatency ? stand_in_tap : i < dynamicMods ? stand_in_mod_dynamic : stand_in_mod;
        lib.Start = nullptr;
        lib.InitLate = nullptr;
        Canvas::pushUserLib(lib);
    }
    Canvas::hideCanvasMenu = scenario == "hidden" || g_tapScenario || g_tapLatency;
    Canvas::frameRateLimited = scenario == "limited" || (g_tapLatency && std::getenv("PERF_TAP_LIMITED"));
    g_modInitialization.publish();

    JNIEnv *env = FakeJniEnv();
    static _jobject clazz, surface, assets;
    // PERF_DROID_SANS=1: ImGUI.init's enableDroidSans (the CJK fallback merge),
    // with FAKE_ASSET_DIR pointing at the app's assets so the font loads.
    const jboolean droidSans = std::getenv("PERF_DROID_SANS") ? JNI_TRUE : JNI_FALSE;
    g_initStartNs = wall_ns();
    std::thread dispatch([env, droidSans] {
        Java_git_artdeell_skymodloader_ImGUI_init(env, &clazz, &surface, kFontPx, kScale, &assets, droidSans);
    });

    if (g_tapLatency) {
        // UI thread (this thread), as GameActivity: ACTION_DOWN -> position + press,
        // ACTION_UP after PERF_TAP_MS -> position + release. Taps start at a random
        // phase (0-100 ms) so the release falls anywhere in the frame cycle.
        while (g_framesStarted < 60) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const int taps = std::getenv("PERF_TAPS") ? std::atoi(std::getenv("PERF_TAPS")) : 100;
        const int tapMs = std::getenv("PERF_TAP_MS") ? std::atoi(std::getenv("PERF_TAP_MS")) : 0;
        std::mt19937 rng(12345);
        std::uniform_int_distribution<int> phaseUs(0, 100000);
        std::vector<long long> toStart, toPresent, framesAfter;
        int missed = 0;
        for (int t = 0; t < taps; ++t) {
            std::this_thread::sleep_for(std::chrono::microseconds(phaseUs(rng)));
            const float x = g_tapX, y = g_tapY;
            Java_git_artdeell_skymodloader_ImGUI_submitPositionEvent(env, &clazz, x, y);
            Java_git_artdeell_skymodloader_ImGUI_submitButtonEvent(env, &clazz, 0, JNI_TRUE);
            if (tapMs) std::this_thread::sleep_for(std::chrono::milliseconds(tapMs));
            const int clicksBefore = g_tapClickCount;
            Java_git_artdeell_skymodloader_ImGUI_submitPositionEvent(env, &clazz, x, y);
            const long long up = wall_ns();
            const int startedAtUp = g_framesStarted;
            Java_git_artdeell_skymodloader_ImGUI_submitButtonEvent(env, &clazz, 0, JNI_FALSE);
            while (g_tapClickCount == clicksBefore && wall_ns() - up < 1000000000LL)
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            if (g_tapClickCount == clicksBefore) { ++missed; continue; }
            const int f = g_tapLastClickFrame;
            while (g_framePresentNs[f] == 0 && wall_ns() - up < 2000000000LL)
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            toStart.push_back(g_frameStartNs[f] - up);
            toPresent.push_back(g_framePresentNs[f] - up);
            framesAfter.push_back((f - startedAtUp) * 1000LL);  // pct() divides by 1000
        }
        Java_git_artdeell_skymodloader_ImGUI_shutdown(env, &clazz);
        dispatch.join();
        auto ms = [](const std::vector<long long> &v, double p) { return pct(v, p) / 1000.0; };
        std::printf("taplatency limited=%d tap_ms=%d taps=%d missed=%d | release->acting frame start ms p50=%.1f p95=%.1f"
                    " p99=%.1f max=%.1f | release->present ms p50=%.1f p95=%.1f p99=%.1f max=%.1f"
                    " | frames started after release p50=%.0f max=%.0f\n",
                    Canvas::frameRateLimited ? 1 : 0, tapMs, taps, missed, ms(toStart, 0.5), ms(toStart, 0.95),
                    ms(toStart, 0.99), ms(toStart, 1.0), ms(toPresent, 0.5), ms(toPresent, 0.95), ms(toPresent, 0.99),
                    ms(toPresent, 1.0), pct(framesAfter, 0.5), pct(framesAfter, 1.0));
        return 0;
    }
    if (g_tapScenario) {  // frame counts only; the script ends the run
        for (int ms = 0; !g_tapDone && ms < 30000; ms += 5) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        Java_git_artdeell_skymodloader_ImGUI_shutdown(env, &clazz);
        dispatch.join();
        std::printf("  taps: %s after %ld frames\n", g_tapDone ? "done" : "TIMED OUT", g_frameCount.load());
        return g_tapDone ? 0 : 1;
    }
    while (g_frameCount < 300)  // warm-up: font atlas, window layout, caches
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    g_measuring = true;  // the render thread opens the window at its next frame start
    int i = 0;
    std::vector<long long> wantsMouseNs, wantsKeyboardNs;  // uilatency: UI-thread time inside each query
    // PERF_UI_CALLS: keep the UI thread querying until it has made this many
    // calls, even after the frame window is full (unpaced runs end after a few
    // hundred calls otherwise; p99.9 wants about 10,000).
    const size_t minUiCalls = std::getenv("PERF_UI_CALLS") ? std::strtoul(std::getenv("PERF_UI_CALLS"), nullptr, 10) : 0;
    while (g_recorded < g_capacity || (scenario == "uilatency" && wantsMouseNs.size() < minUiCalls)) {
        if (scenario == "uilatency") {
            // GameActivity asks wantsMouse()/wantsKeyboard() on the UI thread for
            // every touch; both take mutexes the render thread also takes
            // (CanvasInput's queue and snapshot locks).
            const float x = 200.0f + (i % 400), y = 300.0f + (i % 600);
            Java_git_artdeell_skymodloader_ImGUI_submitPositionEvent(env, &clazz, x, y);
            long long t0 = wall_ns();
            (void)Java_git_artdeell_skymodloader_ImGUI_wantsMouse(env, &clazz);
            long long t1 = wall_ns();
            (void)Java_git_artdeell_skymodloader_ImGUI_wantsKeyboard(env, &clazz);
            long long t2 = wall_ns();
            wantsMouseNs.push_back(t1 - t0);
            wantsKeyboardNs.push_back(t2 - t1);
            ++i;
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        } else if (scenario == "touch") {  // ~250 Hz touch stream over the menu, as GameActivity forwards it
            const float x = 200.0f + (i % 400), y = 300.0f + (i % 600);
            Java_git_artdeell_skymodloader_ImGUI_submitPositionEvent(env, &clazz, x, y);
            if (i % 50 == 0) Java_git_artdeell_skymodloader_ImGUI_submitButtonEvent(env, &clazz, 0, JNI_TRUE);
            if (i % 50 == 25) Java_git_artdeell_skymodloader_ImGUI_submitButtonEvent(env, &clazz, 0, JNI_FALSE);
            (void)Java_git_artdeell_skymodloader_ImGUI_wantsMouse(env, &clazz);
            ++i;
            std::this_thread::sleep_for(std::chrono::microseconds(4000));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    g_measuring = false;
    const long callgrindFrames = g_recorded;  // the callgrind dump covers exactly these frames
    Java_git_artdeell_skymodloader_ImGUI_shutdown(env, &clazz);  // no resurface here, so F10's swallow cannot occur
    dispatch.join();

    std::vector<long long> input, menu, render, submit, cpu, wall, interval, drawCalls, uploadBytes;
    long long vertices = 0;
    int drawLists = 0;
    long unchanged = 0, streak = 0, longestStreak = 0;
    for (size_t f = 0; f < g_frames.size(); ++f) {
        const Frame &fr = g_frames[f];
        input.push_back(fr.cpu[1] - fr.cpu[0]);
        menu.push_back(fr.cpu[2] - fr.cpu[1]);
        render.push_back(fr.cpu[3] - fr.cpu[2]);
        submit.push_back(fr.cpu[4] - fr.cpu[3] - fr.analysisCpu);
        cpu.push_back(fr.cpu[4] - fr.cpu[0] - fr.analysisCpu);
        wall.push_back(fr.wall[4] - fr.wall[0] - fr.analysisWall);
        if (f > 0) interval.push_back(fr.wall[0] - g_frames[f - 1].wall[0]);
        vertices += fr.vertices;
        drawCalls.push_back(fr.drawCalls * 1000LL);    // pct() divides by 1000
        uploadBytes.push_back(fr.uploadBytes);         // pct() -> KB-ish units below
        drawLists = std::max(drawLists, fr.drawLists);
        if (f > 0 && fr.drawHash == g_frames[f - 1].drawHash) {
            ++unchanged;
            longestStreak = std::max(longestStreak, ++streak);
        } else {
            streak = 0;
        }
    }
    std::printf("scenario=%s mods=%d frames=%zu vertices/frame=%lld | cpu_us p50=%.1f p95=%.1f p99=%.1f max=%.1f"
                " | stages_p50_us input=%.1f menu=%.1f render=%.1f submit=%.1f | wall_us p50=%.1f"
                " | interval_ms p50=%.2f p95=%.2f | gl draw_lists=%d draw_calls p50=%.0f max=%.0f"
                " upload_KB p50=%.1f max=%.1f | unchanged_frames=%ld (%.1f%%) longest_run=%ld | callgrind_frames=%ld\n",
                scenario.c_str(), mods, g_frames.size(), g_frames.empty() ? 0 : vertices / (long long)g_frames.size(),
                pct(cpu, 0.5), pct(cpu, 0.95), pct(cpu, 0.99), pct(cpu, 1.0), pct(input, 0.5), pct(menu, 0.5),
                pct(render, 0.5), pct(submit, 0.5), pct(wall, 0.5), pct(interval, 0.5) / 1000.0,
                pct(interval, 0.95) / 1000.0, drawLists, pct(drawCalls, 0.5), pct(drawCalls, 1.0),
                pct(uploadBytes, 0.5) * 1000.0 / 1024.0, pct(uploadBytes, 1.0) * 1000.0 / 1024.0, unchanged,
                g_frames.size() > 1 ? 100.0 * unchanged / (g_frames.size() - 1) : 0.0, longestStreak, callgrindFrames);
    if (g_frames.size() > 1) {
        const double seconds = (g_frames.back().wall[4] - g_frames.front().wall[0]) / 1e9;
        long long cpuTotal = 0;
        for (long long c : cpu) cpuTotal += c;
        const double span = (g_frames.back().wall[0] - g_frames.front().wall[0]) / 1e9;
        const long presents = g_frames.back().swapsAtStart - g_frames.front().swapsAtStart;
        std::printf("  per second: iterations=%.1f presents=%.1f render-thread CPU=%.2f ms\n",
                    g_frames.size() / seconds, span > 0 ? presents / span : 0.0, cpuTotal / 1e6 / seconds);
        // Raw counts for the measured window (between the first and last
        // recorded frame starts), e.g. to split callgrind totals per frame type.
        // "presented=", not "presents=": stage_variant and stage_readout grep for
        // "presents=" and take one match per log (pass 44).
        std::printf("  window: frames=%zu presented=%ld\n", g_frames.size() - 1, presents);
        if (perf_variant_report) perf_variant_report(seconds);
    }
    if (!wantsMouseNs.empty()) {
        std::printf("  UI thread inside wantsMouse (us): p50=%.2f p99=%.2f p99.9=%.2f max=%.2f (%zu calls);"
                    " wantsKeyboard: p50=%.2f p99=%.2f max=%.2f\n",
                    pct(wantsMouseNs, 0.5), pct(wantsMouseNs, 0.99), pct(wantsMouseNs, 0.999), pct(wantsMouseNs, 1.0),
                    wantsMouseNs.size(), pct(wantsKeyboardNs, 0.5), pct(wantsKeyboardNs, 0.99), pct(wantsKeyboardNs, 1.0));
    }
    if (g_fakeVsyncAligned) {  // gaps between latched frames, in vsync periods
        long gaps[5] = {0, 0, 0, 0, 0};  // 1, 2, 3, 4, 5+ periods
        long long prev = 0;
        for (const Frame &fr : g_frames) {
            if (fr.presentNs == 0) continue;
            if (prev) {
                const long periods = std::lround((fr.presentNs - prev) / (g_fakeSwapSleepUs * 1000.0));
                gaps[std::min(4L, std::max(1L, periods) - 1)]++;
            }
            prev = fr.presentNs;
        }
        const long total = gaps[0] + gaps[1] + gaps[2] + gaps[3] + gaps[4];
        std::printf("  present gaps (vsync periods): 1=%ld 2=%ld 3=%ld 4=%ld 5+=%ld (of %ld)\n", gaps[0], gaps[1],
                    gaps[2], gaps[3], gaps[4], total);
    }
    return 0;
}
