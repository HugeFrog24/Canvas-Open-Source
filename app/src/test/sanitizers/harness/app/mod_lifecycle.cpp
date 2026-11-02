// Mod lifecycle: the real registration and late-init entry points, extracted
// verbatim by run.sh from main.cpp (crash, UserThread, onModLibrary,
// completeModInitialization, lateInitUserLibs), with the real Canvas.cpp
// (pushUserLib, userLibs) and InitializationGate, against the fake JNIEnv and
// two stand-in mod libraries (mod_stub.cpp): one with a menu, one patch-only.
// Both export InitLate().
//
// Oracle: what a mod relies on from the lifecycle. Every loaded mod that
// exports InitLate() has it called exactly once, after initialization
// completes, and never concurrently with itself.
//
//   once    settle -> two onModLibrary -> completeModInitialization ->
//           lateInitUserLibs (GameActivity.fadeoutLogos's animation end)
//   twice   the same, with lateInitUserLibs delivered twice (fadeoutLogos
//           running again, e.g. for a recreated activity)
#include <dlfcn.h>
#include <unistd.h>
#include <jni.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "Canvas/Canvas.h"
#include "Canvas/InitializationGate.h"
#include "include/misc/Logger.h"
#include "include/misc/visibility.h"

// Coverage builds (stage_coverage_all): _exit skips the profile runtime's atexit write.
extern "C" int __llvm_profile_write_file(void) __attribute__((weak));

// Platform stand-in (linked with -Wl,--wrap=dlopen): main.cpp passes
// RTLD_LOCAL alone; bionic's dlopen accepts flags without RTLD_NOW/RTLD_LAZY
// (it always binds now), glibc rejects them with EINVAL.
extern "C" void *__real_dlopen(const char *file, int flags);
extern "C" void *__wrap_dlopen(const char *file, int flags) {
    if (!(flags & (RTLD_NOW | RTLD_LAZY))) flags |= RTLD_NOW;
    return __real_dlopen(file, flags);
}

static Canvas::InitializationGate g_modInitialization;  // main.cpp:35
typedef void (*func)();                                 // main.cpp, before UserThread

#include "crash.inc"
#include "UserThread.inc"
#include "onModLibrary.inc"
#include "completeModInitialization.inc"
#include "lateInitUserLibs.inc"

namespace {

struct Mod {
    const char *path;
    bool ui;
};

int *counter(const char *path, const char *name) {
    void *h = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
    return h ? static_cast<int *>(dlsym(h, name)) : nullptr;
}

}  // namespace

int main(int argc, char **argv) {
    const std::string mode = argc > 1 ? argv[1] : "once";
    const std::string dir = argc > 2 ? argv[2] : ".";
    const Mod mods[] = {{"mod_ui.so", true}, {"mod_patch.so", false}};

    JNIEnv *env = FakeJniEnv();
    static _jobject clazz;
    if (!g_modInitialization.begin()) return 2;  // what settle() does first (main.cpp:262)

    for (const Mod &m : mods) {
        const std::string path = dir + "/" + m.path;
        Java_git_artdeell_skymodloader_LibrarySelectorListener_onModLibrary(
            env, &clazz, FakeJniString(path.c_str()), m.ui ? JNI_TRUE : JNI_FALSE, FakeJniString(m.path),
            FakeJniString("author"), FakeJniString("description"), FakeJniString("1.0"), JNI_FALSE);
    }
    Java_git_artdeell_skymodloader_MainActivity_completeModInitialization(env, &clazz);

    const int deliveries = mode == "twice" ? 2 : 1;
    for (int i = 0; i < deliveries; ++i)
        Java_git_artdeell_skymodloader_MainActivity_lateInitUserLibs(env, &clazz);
    // The late-init threads are detached. Wait until the completed-call counts
    // stop changing (acquire loads in the mods), at most 5 s.
    int last = -1, stable = 0;
    for (int waited = 0; waited < 5000 && stable < 500; waited += 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        int sum = 0;
        for (const Mod &m : mods) {
            const std::string path = dir + "/" + m.path;
            void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
            auto done = h ? reinterpret_cast<int (*)()>(dlsym(h, "ModInitLateDone")) : nullptr;
            sum += done ? done() : 0;
        }
        stable = sum == last ? stable + 50 : 0;
        last = sum;
    }

    int failures = 0;
    for (const Mod &m : mods) {
        const std::string path = dir + "/" + m.path;
        int *calls = counter(path.c_str(), "g_initLateCalls");
        int *overlapped = counter(path.c_str(), "g_initLateOverlapped");
        const int n = calls ? *calls : -1;
        const bool ok = n == 1 && overlapped && !*overlapped;
        failures += !ok;
        std::printf("%s %s (%s): InitLate called %d time(s)%s\n", ok ? "ok      " : "MISMATCH", m.path,
                    m.ui ? "menu" : "patch-only", n, overlapped && *overlapped ? ", two calls overlapped" : "");
    }
    std::printf("registered with Canvas: %zu of %zu mods; lateInitUserLibs delivered %d time(s); failures=%d\n",
                Canvas::userLibs.size(), sizeof(mods) / sizeof(mods[0]), deliveries, failures);
    std::fflush(stdout);
    // _exit: skip static destructors. Destroying Canvas::userLibs at exit is
    // unordered with the detached late-init thread's reads; that exit-path
    // hazard is noted in FINDINGS (F17's class), not measured here.
    if (__llvm_profile_write_file) __llvm_profile_write_file();
    _exit(0);  // contract observations, recorded rather than failed
}
