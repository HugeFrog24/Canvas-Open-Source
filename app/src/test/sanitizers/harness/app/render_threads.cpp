// Runs the real androidbk.cpp render loop with ImGUI.java's thread layout:
//   - "imgui dispatch thread": runs queued init/resurface jobs, each of which
//     blocks in renderloop() until `run` drops (LinkedBlockingQueue in Java);
//   - UI thread (this main thread): delivers touch/key input, polls
//     wantsMouse()/wantsKeyboard(), and fires surfaceDestroyed -> shutdown()
//     and surfaceCreated -> queue resurface, as GameActivity/ImGUI.java do.
#include <jni.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui/imgui.h"

extern "C" {
void Java_git_artdeell_skymodloader_ImGUI_init(JNIEnv *, jclass, jobject, jfloat, jfloat, jobject, jboolean);
void Java_git_artdeell_skymodloader_ImGUI_resurface(JNIEnv *, jclass, jobject);
void Java_git_artdeell_skymodloader_ImGUI_shutdown(JNIEnv *, jclass);
void Java_git_artdeell_skymodloader_ImGUI_submitPositionEvent(JNIEnv *, jclass, jfloat, jfloat);
void Java_git_artdeell_skymodloader_ImGUI_submitButtonEvent(JNIEnv *, jclass, jint, jboolean);
void Java_git_artdeell_skymodloader_ImGUI_submitScrollEvent(JNIEnv *, jclass, jfloat, jfloat);
void Java_git_artdeell_skymodloader_ImGUI_submitUnicodeEvent(JNIEnv *, jclass, jchar);
void Java_git_artdeell_skymodloader_ImGUI_submitKeyEvent(JNIEnv *, jclass, jint, jboolean);
void Java_git_artdeell_skymodloader_ImGUI_clearTextFocus(JNIEnv *, jclass);
jboolean Java_git_artdeell_skymodloader_ImGUI_wantsMouse(JNIEnv *, jclass);
jboolean Java_git_artdeell_skymodloader_ImGUI_wantsKeyboard(JNIEnv *, jclass);
}
int FakeWindowRefs();
int FakeEglSwapIntervalCalls();
int FakeEglSwapIntervalWithoutCurrent();
int FakeEglSwapWithoutCurrent();
long FakeSwapCount();
void FakeSetSurfaceReleased(bool released);
void FakeArmSurfacePause();
void FakeWaitSurfacePaused();
void FakeReleaseSurfacePause();
namespace Canvas {
extern std::atomic<bool> g_fakeFocusMenuText;
const char *FakeMenuText();
extern std::atomic<void (*)()> g_fakeMenuProbe;
}
long FakeJavaClipboardGets();
long FakeJavaClipboardSets();
std::string FakeJavaClipboardUtf8();
void FakeJavaClipboardSetUtf16(const std::u16string &s);
long FakeJavaClipboardSetThrows();
void FakeJavaClipboardThrowAbove(size_t utf16Units);
bool FakeJniExceptionPending();
void FakeJniClearPending();
long FakeJniCallsWithPendingException();
std::string FakeJniFirstCallWithPendingException();

namespace {

class JobQueue {
public:
    void push(std::function<void()> job) {
        { std::lock_guard<std::mutex> lock(m_); jobs_.push_back(std::move(job)); }
        cv_.notify_one();
    }
    std::function<void()> take() {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&] { return !jobs_.empty(); });
        auto job = std::move(jobs_.front());
        jobs_.pop_front();
        return job;
    }
private:
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
};

_jobject g_surface, g_assets, g_class;

void input_burst(JNIEnv *env, int n) {
    for (int i = 0; i < n; ++i) {
        const float x = 40.0f + (i % 300), y = 40.0f + (i % 200);
        Java_git_artdeell_skymodloader_ImGUI_submitPositionEvent(env, &g_class, x, y);
        if (i % 20 == 0) Java_git_artdeell_skymodloader_ImGUI_submitButtonEvent(env, &g_class, 0, JNI_TRUE);
        if (i % 20 == 10) Java_git_artdeell_skymodloader_ImGUI_submitButtonEvent(env, &g_class, 0, JNI_FALSE);
        (void)Java_git_artdeell_skymodloader_ImGUI_wantsMouse(env, &g_class);
        (void)Java_git_artdeell_skymodloader_ImGUI_wantsKeyboard(env, &g_class);
        if (i % 7 == 0) Java_git_artdeell_skymodloader_ImGUI_submitUnicodeEvent(env, &g_class, 'a' + i % 26);
        if (i % 11 == 0) {
            Java_git_artdeell_skymodloader_ImGUI_submitKeyEvent(env, &g_class, 1, JNI_TRUE);
            Java_git_artdeell_skymodloader_ImGUI_submitKeyEvent(env, &g_class, 1, JNI_FALSE);
        }
        if (i % 97 == 0) Java_git_artdeell_skymodloader_ImGUI_clearTextFocus(env, &g_class);
        if (i % 50 == 0) Java_git_artdeell_skymodloader_ImGUI_submitScrollEvent(env, &g_class, 0.0f, -1.0f);
        if (i % 64 == 0) std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
}

// "clipboard" mode (pass 34): ImGui's clipboard API, as menu code calls it on
// the render thread, through androidbk_set_clipboard/androidbk_get_clipboard and
// the fake JNI's model of ImGUI.java's clipboard and ART's string conversions.
// Oracle: what GetClipboardText returns equals what that model predicts, and the
// helpers release every GetStringUTFChars.
struct ClipCase {
    const char *name;
    bool fromJava;             // the clip was set on the Java side (another app)
    std::string set;           // UTF-8 handed to SetClipboardText
    std::u16string javaSet;    // or the Java String, for fromJava cases
    std::string expect;        // what GetClipboardText should return (ART model)
};
std::vector<ClipCase> g_clipCases;
std::vector<std::string> g_clipGot;
std::atomic<bool> g_clipDone{false};

void clipboard_probe() {
    for (const ClipCase &c : g_clipCases) {
        if (c.fromJava) FakeJavaClipboardSetUtf16(c.javaSet);
        else ImGui::SetClipboardText(c.set.c_str());
        const char *got = ImGui::GetClipboardText();
        g_clipGot.push_back(got ? got : "(null)");
    }
    g_clipDone = true;
}

// "clipboard-throw" / "clipboard-throw-set" (pass 35, L20): a copy too large for
// the binder transaction makes ImGUI.setClipboard throw (the fake's threshold).
// The menu then pastes (clipboard-throw) or does nothing more with the clipboard
// (clipboard-throw-set). Oracles: no JNI call outside the allowed list is made
// while the exception is pending, and none is pending when init()/resurface()
// return to Java.
bool g_clipThrowPaste = false;
std::string g_clipAfterThrow;
std::atomic<bool> g_pendingAtJavaReturn{false};

void clipboard_throw_probe() {
    ImGui::SetClipboardText("small clip");                     // fits
    static const std::string big(600 * 1024, 'x');             // ~1.2 MB as a UTF-16 parcel
    ImGui::SetClipboardText(big.c_str());                      // throws in Java
    if (g_clipThrowPaste) {
        const char *got = ImGui::GetClipboardText();
        g_clipAfterThrow = got ? got : "(null)";
    }
    g_clipDone = true;
}

std::string hex(const std::string &s, size_t max = 12) {
    std::string out;
    char b[4];
    for (size_t i = 0; i < s.size() && i < max; ++i) { std::snprintf(b, sizeof b, "%02X ", (unsigned char)s[i]); out += b; }
    if (s.size() > max) out += "... (" + std::to_string(s.size()) + " bytes)";
    return out.empty() ? "(empty)" : out;
}

bool valid_utf8(const std::string &str) {  // RFC 3629: no surrogates, no overlongs
    const unsigned char *p = reinterpret_cast<const unsigned char *>(str.c_str());
    while (*p) {
        const int n = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : (*p >> 3) == 30 ? 4 : 0;
        if (n == 0 || (n == 2 && *p < 0xC2)) return false;
        for (int i = 1; i < n; ++i) if ((p[i] & 0xC0) != 0x80) return false;
        if (n == 3 && p[0] == 0xED && p[1] >= 0xA0) return false;
        p += n;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    const std::string mode = argc > 1 ? argv[1] : "stress";
    JNIEnv *env = FakeJniEnv();
    JobQueue queue;
    bool initialized = false;  // ImGUIThread.hasInitialized, touched only on the dispatch thread
    std::mutex doneMutex;
    bool done = false;

    std::thread dispatch([&] {
        for (;;) {
            auto job = queue.take();
            if (!job) break;
            job();
            // Back in ImGUIThread.run: Java now throws whatever is pending.
            if (FakeJniExceptionPending()) {
                g_pendingAtJavaReturn = true;
                FakeJniClearPending();
            }
        }
        std::lock_guard<std::mutex> lock(doneMutex);
        done = true;
    });

    auto surfaceCreated = [&] {
        queue.push([&] {
            if (initialized) {
                Java_git_artdeell_skymodloader_ImGUI_resurface(env, &g_class, &g_surface);
            } else {
                initialized = true;
                Java_git_artdeell_skymodloader_ImGUI_init(env, &g_class, &g_surface, 28.0f, 1.2f, &g_assets, JNI_FALSE);
            }
        });
    };

    auto finish = [&] {
        queue.push(nullptr);
        int shutdowns = 0;
        for (;;) {
            Java_git_artdeell_skymodloader_ImGUI_shutdown(env, &g_class);
            ++shutdowns;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            std::lock_guard<std::mutex> lock(doneMutex);
            if (done) break;
        }
        dispatch.join();
        return shutdowns;
    };
    auto waitForFrames = [](long n) {
        const long start = FakeSwapCount();
        while (FakeSwapCount() - start < n)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };

    if (mode == "lost-shutdown") {
        // surfaceDestroyed arriving while the queued resurface job is between
        // creating the new EGL surface and setting run = true (a quick
        // background/foreground/background). The pause sits in the fake
        // eglCreateWindowSurface; Canvas's ordering is untouched.
        surfaceCreated();
        waitForFrames(10);
        Java_git_artdeell_skymodloader_ImGUI_shutdown(env, &g_class);  // surfaceDestroyed #1
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        FakeArmSurfacePause();
        surfaceCreated();                                               // queues resurface
        FakeWaitSurfacePaused();                                        // inside init_sfc
        Java_git_artdeell_skymodloader_ImGUI_shutdown(env, &g_class);  // surfaceDestroyed #2 returns here
        const long atReturn = FakeSwapCount();
        FakeReleaseSurfacePause();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const long after = FakeSwapCount() - atReturn;
        std::printf("frames presented in the 300 ms after surfaceDestroyed's shutdown() returned: %ld -> %s\n",
                    after, after > 0 ? "SHUTDOWN LOST, rendering continued" : "ok");
        finish();
        return after > 0 ? 1 : 0;
    }

    if (mode == "unicode") {
        // L19: ImGUITextInput.java:215 sends text one UTF-16 code unit at a
        // time through submitUnicodeEvent. Type "a", "é" (U+00E9) and an emoji
        // (U+1F600 = D83D DE00) into the focused menu text field, through the
        // real JNI entry point and Drain. Oracle: the field holds valid UTF-8.
        // This build has 16-bit ImWchar (imconfig.h: IMGUI_USE_WCHAR32 off), so
        // the best it can store for the emoji is U+FFFD (EF BF BD), which is
        // what io.AddInputCharacterUTF16 would give.
        surfaceCreated();
        waitForFrames(10);
        Canvas::g_fakeFocusMenuText = true;
        waitForFrames(10);
        const jchar units[] = {'a', 0x00E9, 0xD83D, 0xDE00, 'b'};
        for (jchar u : units) Java_git_artdeell_skymodloader_ImGUI_submitUnicodeEvent(env, &g_class, u);
        waitForFrames(20);
        finish();  // render loop stopped: the buffer is no longer written
        const unsigned char *t = reinterpret_cast<const unsigned char *>(Canvas::FakeMenuText());
        std::printf("field bytes:");
        for (const unsigned char *p = t; *p; ++p) std::printf(" %02X", *p);
        std::printf("\n");
        // Well-formed UTF-8 check (RFC 3629): no encoded surrogates (ED A0..BF), no overlongs.
        bool valid = true;
        for (const unsigned char *p = t; *p && valid;) {
            int n = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : (*p >> 3) == 30 ? 4 : 0;
            if (n == 0) { valid = false; break; }
            for (int i = 1; i < n; ++i) if ((p[i] & 0xC0) != 0x80) valid = false;
            if (n == 3 && p[0] == 0xED && p[1] >= 0xA0) valid = false;  // U+D800..U+DFFF
            p += n;
        }
        const char *best = "a\xC3\xA9\xEF\xBF\xBD" "b";
        std::printf("valid UTF-8: %s; matches the 16-bit ImWchar best case (61 C3 A9 EF BF BD 62): %s\n",
                    valid ? "yes" : "NO (encoded surrogates)", std::strcmp(reinterpret_cast<const char *>(t), best) ? "no" : "yes");
        return valid ? 0 : 1;
    }

    if (mode == "clipboard") {
        const std::string big(1 << 20, 'x');
        g_clipCases = {
            {"empty", false, "", u"", ""},
            {"ascii", false, "hello", u"", "hello"},
            {"latin1 (U+00E9)", false, "caf\xC3\xA9", u"", "caf\xC3\xA9"},
            {"CJK (U+4F60 U+597D)", false, "\xE4\xBD\xA0\xE5\xA5\xBD", u"", "\xE4\xBD\xA0\xE5\xA5\xBD"},
            {"emoji, 4-byte UTF-8 (U+1F600)", false, "\xF0\x9F\x98\x80", u"", "\xF0\x9F\x98\x80"},
            // F22's field bytes: a typed emoji stored as two encoded surrogates.
            {"F22 field text (encoded surrogate pair)", false, "a\xC3\xA9\xED\xA0\xBD\xED\xB8\x80" "b", u"", "a\xC3\xA9\xF0\x9F\x98\x80" "b"},
            {"lone high surrogate, 3-byte form", false, "x\xED\xA0\xBDy", u"", "x\xED\xA0\xBDy"},
            {"truncated sequence at the end (U+4F60 cut short)", false, "ab\xE4\xBD", u"", "ab?"},
            {"1 MiB of ASCII (buffer grows)", false, big, u"", big},
            {"short again (buffer shrinks)", false, "ok", u"", "ok"},
            {"Java side: U+0000 inside the text", true, "", std::u16string(u"a") + char16_t(0) + u"b", "a\xC0\x80" "b"},
            {"Java side: surrogate pair (U+1F600)", true, "", u"\xD83D\xDE00", "\xF0\x9F\x98\x80"},
            {"Java side: lone low surrogate", true, "", u"\xDE00", "\xED\xB8\x80"},
            {"Java side: empty clip (getClipboard returns \"\")", true, "", u"", ""},
        };
        surfaceCreated();
        waitForFrames(5);
        const long outstandingBefore = FakeJniOutstandingUtf();
        Canvas::g_fakeMenuProbe = &clipboard_probe;
        while (!g_clipDone) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        finish();
        int mismatches = 0;
        for (size_t i = 0; i < g_clipCases.size(); ++i) {
            const ClipCase &c = g_clipCases[i];
            const std::string &got = g_clipGot[i];
            const bool ok = got == c.expect;
            mismatches += !ok;
            std::printf("%s %-50s got %s%s%s\n", ok ? "ok      " : "MISMATCH", c.name, hex(got).c_str(),
                        valid_utf8(got) ? "" : "  [not valid UTF-8]",
                        !c.fromJava && got != c.set ? "  [differs from the text set]" : "");
            if (!ok) std::printf("         expected %s\n", hex(c.expect).c_str());
        }
        const long outstanding = FakeJniOutstandingUtf() - outstandingBefore;
        std::printf("Java calls: getClipboard %ld, setClipboard %ld; GetStringUTFChars without Release: %ld; mismatches=%d\n",
                    FakeJavaClipboardGets(), FakeJavaClipboardSets(), outstanding, mismatches);
        return mismatches || outstanding ? 1 : 0;
    }

    if (mode == "clipboard-throw" || mode == "clipboard-throw-set") {
        g_clipThrowPaste = mode == "clipboard-throw";
        FakeJavaClipboardThrowAbove(500 * 1024);  // UTF-16 units, about a 1 MB parcel
        surfaceCreated();
        waitForFrames(5);
        Canvas::g_fakeMenuProbe = &clipboard_throw_probe;
        while (!g_clipDone) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        waitForFrames(10);  // the render loop keeps running with the exception pending
        finish();           // shutdown: renderloop exits and init() returns to Java
        const long calls = FakeJniCallsWithPendingException();
        std::printf("setClipboard threw: %ld time(s)\n", FakeJavaClipboardSetThrows());
        std::printf("JNI calls outside the allowed list made with the exception pending: %ld%s%s\n", calls,
                    calls ? " (first: " : "", calls ? (FakeJniFirstCallWithPendingException() + "; CheckJNI aborts here)").c_str() : "");
        if (g_clipThrowPaste) std::printf("paste after the failed copy returned: %s\n", hex(g_clipAfterThrow).c_str());
        std::printf("exception pending when init() returned to Java: %s\n",
                    g_pendingAtJavaReturn ? "YES -> ImGUIThread.run rethrows it (catches only InterruptedException)" : "no");
        return calls || g_pendingAtJavaReturn ? 1 : 0;
    }

    if (mode == "quiet-shutdown") {
        // TSan only flags the run-flag race when nothing orders shutdown()'s
        // write before renderloop's next read. In the stress mode the UI
        // thread's next input push (CanvasInput's queue mutex, taken again by
        // Drain) can supply that order. Here the UI thread does nothing that
        // synchronises after shutdown() until the loop has stopped.
        surfaceCreated();
        waitForFrames(20);
        input_burst(env, 500);
        Java_git_artdeell_skymodloader_ImGUI_shutdown(env, &g_class);  // surfaceDestroyed returns here
        // SurfaceHolder.Callback: no access to the surface after
        // surfaceDestroyed returns. With no resurface to swallow the flag
        // (F10), count the frames the render thread still presents.
        const long atReturn = FakeSwapCount();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const long after = FakeSwapCount() - atReturn;
        std::printf("swaps started after shutdown() returned (no swallow): %ld%s\n", after,
                    after > 0 ? " -> surface used after surfaceDestroyed returned" : "");
        finish();
        std::puts("survived");
        return 0;
    }

    if (mode == "released-surface") {
        // surfaceCreated queues a resurface job, but the surface is destroyed
        // again before the dispatch thread gets to it (quick app switch). The
        // barrier job stands in for the dispatch thread still being busy; the
        // job then runs with the Surface SurfaceView has already destroyed.
        surfaceCreated();
        waitForFrames(10);
        Java_git_artdeell_skymodloader_ImGUI_shutdown(env, &g_class);  // surfaceDestroyed #1
        std::mutex barrierMutex;
        std::condition_variable barrierCv;
        bool barrierOpen = false;
        queue.push([&] {
            std::unique_lock<std::mutex> lock(barrierMutex);
            barrierCv.wait(lock, [&] { return barrierOpen; });
        });
        surfaceCreated();                                               // queues resurface
        Java_git_artdeell_skymodloader_ImGUI_shutdown(env, &g_class);  // surfaceDestroyed #2
        FakeSetSurfaceReleased(true);                                   // SurfaceView destroyed its Surface
        std::puts("resurface job now runs against the destroyed Surface");
        std::fflush(stdout);
        { std::lock_guard<std::mutex> lock(barrierMutex); barrierOpen = true; }
        barrierCv.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        FakeSetSurfaceReleased(false);
        finish();
        std::puts("survived");
        return 0;
    }

    surfaceCreated();
    for (int cycle = 0; cycle < 3; ++cycle) {
        input_burst(env, 4000);
        Java_git_artdeell_skymodloader_ImGUI_shutdown(env, &g_class);  // surfaceDestroyed
        surfaceCreated();
    }
    input_burst(env, 2000);

    // Final teardown. A shutdown() that lands before resurface() sets run=true
    // is lost; keep re-sending it and count how many it took.
    const int shutdowns = finish();
    std::printf("render loop exited after %d final shutdown() call(s)\n", shutdowns);
    // Four surfaces were created (init + 3 resurface); only the live one should
    // still hold a reference.
    std::printf("ANativeWindow references held after 4 surfaces: %d (expected 1)\n", FakeWindowRefs());
    std::printf("EGL: eglSwapInterval calls %d, made with no current context/surface %d (fails: EGL_BAD_CONTEXT); "
                "eglSwapBuffers with no current surface %d\n",
                FakeEglSwapIntervalCalls(), FakeEglSwapIntervalWithoutCurrent(), FakeEglSwapWithoutCurrent());
    return 0;
}
