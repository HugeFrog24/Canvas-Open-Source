// EGL / GLES / ANativeWindow / AAsset / ImGui GL backend stand-ins for driving
// androidbk.cpp's render loop on a desktop host. No real drawing happens; the
// draw data is still walked so the render thread touches what it produced.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Canvas/Canvas.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/imgui.h"

namespace {
int g_token;
std::atomic<GLuint> g_nextTexture{1};
std::atomic<long> g_swaps{0};

// One-shot pause inside eglCreateWindowSurface, i.e. inside init_sfc(), which
// resurface() runs before it sets run = true.
std::mutex g_pauseMutex;
std::condition_variable g_pauseCv;
bool g_pauseArmed = false, g_paused = false, g_release = false;
}

// Perf-track knobs (app/src/test/perf). Defaults keep the sanitizer stages'
// behaviour: a 0.5 ms stand-in for vsync and no frame hooks.
int g_fakeSwapSleepUs = 500;
int g_fakeWindowW = 1280, g_fakeWindowH = 720;
// Vsync model (perf track): when g_fakeVsyncAligned is set, eglSwapBuffers
// returns at the next boundary of a virtual vsync clock (period
// g_fakeSwapSleepUs) instead of after a fixed sleep, and reports frame point 5
// ("presented") there.
int g_fakeVsyncAligned = 0;
long long g_fakeVsyncOriginNs = 0;
long long FakeNowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
void FakeSleepToNextVsync() {
    const long long period = g_fakeSwapSleepUs * 1000LL;
    if (period <= 0) return;
    if (g_fakeVsyncOriginNs == 0) g_fakeVsyncOriginNs = FakeNowNs();
    const long long now = FakeNowNs();
    const long long next = g_fakeVsyncOriginNs + ((now - g_fakeVsyncOriginNs) / period + 1) * period;
    const timespec until = {static_cast<time_t>(next / 1000000000LL), static_cast<long>(next % 1000000000LL)};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, nullptr);
}
// Queued-swap model (perf track; takes precedence over g_fakeVsyncAligned): a
// non-async BufferQueue. eglSwapBuffers queues the buffer and returns at once;
// the display latches one buffer per vsync and keeps each for at least the
// current swap interval; the producer blocks (as it would in dequeueBuffer)
// only while g_fakeSwapQueueDepth buffers are already waiting. Frame point 5
// is reported with g_fakePresentNs = the vsync the buffer will be latched at.
int g_fakeSwapQueued = 0;
int g_fakeSwapQueueDepth = 2;
long long g_fakePresentNs = 0;
static int g_fakeSwapInterval = 1;  // set by a successful eglSwapInterval
static std::deque<long long> s_pendingLatch;  // render thread only
static long long s_lastLatch = 0;
static void FakeSleepUntilNs(long long t) {
    const timespec until = {static_cast<time_t>(t / 1000000000LL), static_cast<long>(t % 1000000000LL)};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, nullptr);
}
// The vsync a buffer pushed at `now` is latched at: the first boundary after
// now that also keeps the previous frame on screen for the swap interval.
static long long FakeNextLatch(long long now, long long period) {
    if (g_fakeVsyncOriginNs == 0) g_fakeVsyncOriginNs = now;
    long long earliest = now;
    if (s_lastLatch) earliest = std::max(earliest, s_lastLatch + (std::max(1, g_fakeSwapInterval) - 1) * period);
    s_lastLatch = g_fakeVsyncOriginNs + ((earliest - g_fakeVsyncOriginNs) / period + 1) * period;
    return s_lastLatch;
}
static void FakeQueueSwap() {
    const long long period = g_fakeSwapSleepUs * 1000LL;
    if (period <= 0) return;
    long long now = FakeNowNs();
    while (!s_pendingLatch.empty() && s_pendingLatch.front() <= now) s_pendingLatch.pop_front();
    if (static_cast<int>(s_pendingLatch.size()) >= g_fakeSwapQueueDepth) {  // no free buffer: wait for a latch
        FakeSleepUntilNs(s_pendingLatch.front());
        now = s_pendingLatch.front();
        s_pendingLatch.pop_front();
    }
    g_fakePresentNs = FakeNextLatch(now, period);
    s_pendingLatch.push_back(g_fakePresentNs);
}
// Blocking model (g_fakeVsyncAligned): the swap returns when its buffer is
// latched. With swap interval 1 this is FakeSleepToNextVsync().
static void FakeBlockingSwap() {
    const long long period = g_fakeSwapSleepUs * 1000LL;
    if (period <= 0) return;
    FakeSleepUntilNs(FakeNextLatch(FakeNowNs(), period));
}
void (*g_fakeOnGlInit)() = nullptr;          // after ImGui::CreateContext, before the first frame
void (*g_fakeFramePoint)(int point) = nullptr;  // 0 frame start, 3 GL submission, 4 swap

// EGL contract tracking (Khronos man pages): eglSwapInterval acts on the
// current context's draw surface and fails with EGL_BAD_CONTEXT /
// EGL_BAD_SURFACE (returning EGL_FALSE) when the calling thread has none.
static thread_local EGLContext t_currentContext = EGL_NO_CONTEXT;
static thread_local EGLSurface t_currentDraw = EGL_NO_SURFACE;
static std::atomic<int> g_swapIntervalCalls{0}, g_swapIntervalNoCurrent{0}, g_swapNoCurrent{0};
int FakeEglSwapIntervalCalls() { return g_swapIntervalCalls.load(); }
int FakeEglSwapIntervalWithoutCurrent() { return g_swapIntervalNoCurrent.load(); }
int FakeEglSwapWithoutCurrent() { return g_swapNoCurrent.load(); }

long FakeSwapCount() { return g_swaps.load(); }
void FakeArmSurfacePause() {
    std::lock_guard<std::mutex> lock(g_pauseMutex);
    g_pauseArmed = true;
    g_paused = g_release = false;
}
void FakeWaitSurfacePaused() {
    std::unique_lock<std::mutex> lock(g_pauseMutex);
    g_pauseCv.wait(lock, [] { return g_paused; });
}
void FakeReleaseSurfacePause() {
    std::lock_guard<std::mutex> lock(g_pauseMutex);
    g_release = true;
    g_pauseCv.notify_all();
}

EGLDisplay eglGetDisplay(void *) { return &g_token; }
EGLBoolean eglInitialize(EGLDisplay, EGLint *, EGLint *) { return EGL_TRUE; }
EGLBoolean eglSwapInterval(EGLDisplay, EGLint interval) {
    ++g_swapIntervalCalls;
    if (t_currentContext == EGL_NO_CONTEXT || t_currentDraw == EGL_NO_SURFACE) {
        ++g_swapIntervalNoCurrent;
        return EGL_FALSE;  // EGL_BAD_CONTEXT / EGL_BAD_SURFACE
    }
    g_fakeSwapInterval = interval;
    return EGL_TRUE;
}
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint *, EGLConfig *configs, EGLint, EGLint *num) {
    if (configs)
        *configs = &g_token;
    *num = 1;
    return EGL_TRUE;
}
EGLBoolean eglGetConfigAttrib(EGLDisplay, EGLConfig, EGLint, EGLint *value) { *value = 1; return EGL_TRUE; }
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint *) { return &g_token; }
EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *) {
    std::unique_lock<std::mutex> lock(g_pauseMutex);
    if (g_pauseArmed) {
        g_pauseArmed = false;
        g_paused = true;
        g_pauseCv.notify_all();
        g_pauseCv.wait(lock, [] { return g_release; });
    }
    return &g_token;
}
EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface) { return EGL_TRUE; }
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface draw, EGLSurface, EGLContext context) {
    t_currentContext = context;
    t_currentDraw = draw;
    return EGL_TRUE;
}
EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface) {
    if (t_currentContext == EGL_NO_CONTEXT || t_currentDraw == EGL_NO_SURFACE) ++g_swapNoCurrent;
    if (g_fakeFramePoint) g_fakeFramePoint(4);
    ++g_swaps;
    if (g_fakeSwapQueued) {
        FakeQueueSwap();  // returns at once unless the queue is full
        if (g_fakeFramePoint) g_fakeFramePoint(5);
    } else if (g_fakeVsyncAligned) {
        FakeBlockingSwap();  // the buffer is latched at the next vsync (or later, for swap interval 2+)
        if (g_fakeFramePoint) g_fakeFramePoint(5);
    } else if (g_fakeSwapSleepUs > 0) {
        usleep(g_fakeSwapSleepUs);  // stand-in for vsync
    }
    return EGL_TRUE;
}

void glBindTexture(GLenum, GLuint) {}
void glClear(GLbitfield) {}
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glDeleteTextures(GLsizei, const GLuint *) {}
void glGenTextures(GLsizei n, GLuint *textures) { for (GLsizei i = 0; i < n; ++i) textures[i] = g_nextTexture++; }
GLenum glGetError() { return GL_NO_ERROR; }
void glTexParameteri(GLenum, GLenum, GLint) {}
void glViewport(GLint, GLint, GLsizei, GLsizei) {}

// Reference counting as the NDK documents it: ANativeWindow_fromSurface returns
// a window with one reference already acquired, which the caller must release.
// AOSP behaviour this models: SurfaceView.releaseSurfaces() destroys its Surface,
// which zeroes the Surface's native object; ANativeWindow_fromSurface then
// returns NULL (frameworks/base native_window_jni.cpp), and
// ANativeWindow_acquire(NULL) dereferences it (window->incStrong, frameworks/native
// libs/nativewindow/ANativeWindow.cpp).
struct ANativeWindow { std::atomic<int> refs{0}; };
static ANativeWindow g_window;
static std::atomic<bool> g_surfaceReleased{false};
int FakeWindowRefs() { return g_window.refs.load(); }
void FakeSetSurfaceReleased(bool released) { g_surfaceReleased = released; }
ANativeWindow *ANativeWindow_fromSurface(JNIEnv *, jobject) {
    if (g_surfaceReleased) return nullptr;
    ++g_window.refs;
    return &g_window;
}
void ANativeWindow_acquire(ANativeWindow *w) { ++w->refs; }
void ANativeWindow_release(ANativeWindow *w) { --w->refs; }
int32_t ANativeWindow_getWidth(ANativeWindow *) { return g_fakeWindowW; }
int32_t ANativeWindow_getHeight(ANativeWindow *) { return g_fakeWindowH; }
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *, int32_t, int32_t, int32_t) { return 0; }

AAssetManager *AAssetManager_fromJava(JNIEnv *, jobject) { return reinterpret_cast<AAssetManager *>(&g_token); }
// Assets: none by default (the sanitizer stages). With FAKE_ASSET_DIR set
// (perf track), AAssetManager_open serves files from that directory, e.g. the
// app's own assets/DroidSansFallback.ttf.
struct FakeAssetFile {
    std::vector<unsigned char> data;
    size_t pos = 0;
};
AAsset *AAssetManager_open(AAssetManager *, const char *name, int) {
    const char *dir = std::getenv("FAKE_ASSET_DIR");
    if (!dir || !name) return nullptr;
    const std::string path = std::string(dir) + "/" + name;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    auto *a = new FakeAssetFile;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) a->data.insert(a->data.end(), buf, buf + n);
    std::fclose(f);
    return reinterpret_cast<AAsset *>(a);
}
int64_t AAsset_getLength64(AAsset *a) { return a ? static_cast<int64_t>(reinterpret_cast<FakeAssetFile *>(a)->data.size()) : 0; }
int AAsset_read(AAsset *a, void *out, size_t count) {
    if (!a) return 0;
    auto *f = reinterpret_cast<FakeAssetFile *>(a);
    const size_t n = std::min(count, f->data.size() - f->pos);
    std::memcpy(out, f->data.data() + f->pos, n);
    f->pos += n;
    return static_cast<int>(n);
}
void AAsset_close(AAsset *a) { delete reinterpret_cast<FakeAssetFile *>(a); }

bool ImGui_ImplOpenGL3_Init(const char *) {
    if (g_fakeOnGlInit) g_fakeOnGlInit();
    return true;
}
void ImGui_ImplOpenGL3_Shutdown() {}
void ImGui_ImplOpenGL3_NewFrame() {
    if (g_fakeFramePoint) g_fakeFramePoint(0);
    ImGuiIO &io = ImGui::GetIO();
    if (io.Fonts->TexID == 0) {
        unsigned char *pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(1)));
    }
}
void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData *data) {
    if (g_fakeFramePoint) g_fakeFramePoint(3);
    volatile unsigned sum = 0;
    for (int n = 0; data && n < data->CmdListsCount; ++n) {
        const ImDrawList *list = data->CmdLists[n];
        for (int i = 0; i < list->VtxBuffer.Size; ++i)
            sum += list->VtxBuffer[i].col;
        for (int i = 0; i < list->IdxBuffer.Size; ++i)
            sum += list->IdxBuffer[i];
    }
}

// The two Canvas symbols androidbk.cpp uses. CanvasMenu stands in for main.cpp's
// menu with a text field, so WantTextInput / focus clearing are exercised. The
// perf track links main.cpp's real CanvasMenu and Canvas.cpp instead.
#ifndef FAKE_PLATFORM_NO_CANVAS_MENU
namespace Canvas {
bool frameRateLimited = false;
// render_threads "unicode" mode (L19): focus the field, read it back after shutdown.
std::atomic<bool> g_fakeFocusMenuText{false};
static char g_menuText[64] = "";
const char *FakeMenuText() { return g_menuText; }
// render_threads "clipboard" mode: run once on the render thread, inside a frame,
// where a mod's or Canvas's menu code would call ImGui's clipboard API.
std::atomic<void (*)()> g_fakeMenuProbe{nullptr};
void CanvasMenu() {
    static bool check = false;
    if (auto probe = g_fakeMenuProbe.exchange(nullptr)) probe();
    ImGui::Begin("Canvas Menu");
    if (g_fakeFocusMenuText.load()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##text", g_menuText, sizeof(g_menuText));
    ImGui::Checkbox("Limit FPS", &check);
    for (int i = 0; i < 40; ++i)
        ImGui::Text("row %d", i);
    ImGui::End();
}
}
#endif
