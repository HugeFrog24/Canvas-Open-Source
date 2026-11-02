// Thread attachment through Canvas's mod-facing JNI helpers: the real
// fileselector.cpp, plus CipherUtils::performHapticFeedback extracted verbatim
// by run.sh. The fake JavaVM tracks attachment per thread and, at thread exit,
// applies ART's ThreadExitCallback rule (fatal if still attached).
//   java-thread:    requestFile from an attached thread (the render thread case)
//   mod-thread:     requestFile from a mod's own std::thread
//   haptic-valid:   performHapticFeedback(TYPE_SUCCESS) from a mod thread
//   haptic-invalid: performHapticFeedback with an out-of-range type from a mod thread
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "Canvas/Canvas.h"
#include "Cipher/CipherUtils.h"
#include "fileselector/fileselector.h"

namespace Canvas {
JavaVM *javaVM = nullptr;
jobject systemUI = nullptr;
}

void fsel_setup(JNIEnv *env);

#include "performHapticFeedback.inc"

static void on_file(int) {}
static _jobject g_systemUI;

extern "C" void Java_git_artdeell_skymodloader_FileSelector_callbackFunctionCall(JNIEnv *, jclass, jlong, jint);
extern "C" void Java_git_artdeell_skymodloader_FileSelector_multiCallbackFunctionCall(JNIEnv *, jclass, jlong, jintArray);
static int g_singleFd = -1;
static std::vector<int> g_files;
static int g_multiCalls = 0;
static void on_single(int fd) { g_singleFd = fd; }
static void on_files(size_t count, const int *fds) {  // copies, as a mod must: fds is valid only during the call
    ++g_multiCalls;
    g_files.assign(fds, fds + count);
}

static void report(const char *where) {
    std::printf("%-15s attaches=%ld detaches=%ld\n", where, FakeJniAttaches(), FakeJniDetaches());
    std::fflush(stdout);
}

int main(int argc, char **argv) {
    const std::string mode = argc > 1 ? argv[1] : "java-thread";
    Canvas::javaVM = FakeJavaVM();
    Canvas::systemUI = &g_systemUI;
    FakeJniTrackAttachment();  // this thread plays a Java thread
    fsel_setup(FakeJniEnv());

    auto onModThread = [&](void (*body)()) {
        std::thread t([body] {
            body();
            report("before exit");
        });
        t.join();
        report("after join");
        std::puts("survived");
    };

    if (mode == "java-thread") {
        requestFile("text/plain", &on_file, false);
        report("java thread");
        std::puts("survived");
    } else if (mode == "mod-thread") {
        onModThread([] { requestFile("text/plain", &on_file, false); });
    } else if (mode == "haptic-valid") {
        onModThread([] { CipherUtils::performHapticFeedback(HapticFeedbackType::TYPE_SUCCESS); });
    } else if (mode == "haptic-invalid") {
        onModThread([] { CipherUtils::performHapticFeedback(static_cast<HapticFeedbackType>(99)); });
    } else if (mode == "callbacks") {
        // Java delivering picker results back to a mod's callback (FileSelector.java).
        JNIEnv *env = FakeJniEnv();
        _jobject clazz;
        Java_git_artdeell_skymodloader_FileSelector_callbackFunctionCall(env, &clazz, reinterpret_cast<jlong>(&on_single), 42);
        const jint fds[] = {7, 8, 9};
        Java_git_artdeell_skymodloader_FileSelector_multiCallbackFunctionCall(
            env, &clazz, reinterpret_cast<jlong>(&on_files), FakeJniIntArray(fds, 3));
        const bool multiOk = g_files == std::vector<int>{7, 8, 9};
        Java_git_artdeell_skymodloader_FileSelector_multiCallbackFunctionCall(env, &clazz, reinterpret_cast<jlong>(&on_files), nullptr);
        const bool nullOk = g_files.empty();
        Java_git_artdeell_skymodloader_FileSelector_multiCallbackFunctionCall(
            env, &clazz, reinterpret_cast<jlong>(&on_files), FakeJniIntArray(fds, 0));
        const bool emptyOk = g_files.empty();
        std::printf("single fd=%d (%s), three fds %s, null array %s, empty array %s, callbacks=%d, "
                    "int elements outstanding=%ld\n",
                    g_singleFd, g_singleFd == 42 ? "ok" : "WRONG", multiOk ? "ok" : "WRONG", nullOk ? "ok" : "WRONG",
                    emptyOk ? "ok" : "WRONG", g_multiCalls, FakeJniOutstandingIntElements());
        const bool ok = g_singleFd == 42 && multiOk && nullOk && emptyOk && g_multiCalls == 3 &&
                        FakeJniOutstandingIntElements() == 0;
        std::puts(ok ? "survived" : "CALLBACK CONTRACT BROKEN");
        return ok ? 0 : 1;
    }
    return 0;
}
