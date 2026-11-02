// Real JNI entry points, extracted verbatim by run.sh from main.cpp,
// IconLoader.cpp and CipherUtils.cpp into $GEN/*.inc, compiled against the fake
// JNIEnv. Storage they touch is declared here with Canvas.h's layout.
//   leaks: call each entry point like a session does, exit -> LeakSanitizer
//   race:  UI thread delivers keyboard completions while a mod thread
//          registers a listener (CipherUtils::addOnKeyboardCompleteListener)
#include <jni.h>
#include <sanitizer/lsan_interface.h>

#include <cstdint>
#include <cstdio>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Cipher/CipherUtils.h"
#include "Iconloader/IconLoader.h"
#include "include/misc/Logger.h"

namespace Canvas {
struct DeviceInfo {
    float xdpi;
    float ydpi;
    float density;
    std::string deviceName;
    std::string deviceManufacturer;
    std::string deviceModel;
};
DeviceInfo deviceInfo;
std::vector<void (*)(std::string)> onKeyboardCompleteListeners;
std::mutex onKeyboardCompleteListenersMutex;
jobject systemUI = nullptr;
}

std::unordered_map<std::string, PrivateUIIcon *> icons;
static std::string g_skyBuildKey;

#include "onKeyboardCompleteNative.inc"
#include "setDeviceInfoNative.inc"
#include "nativeSetSkyBuildKey.inc"
#include "addIcon.inc"
#include "addOnKeyboardCompleteListener.inc"
#include "getSysetemUI.inc"

static void listener(std::string message) { (void)message.size(); }

// interleave mode: the UI thread is inside this listener while a mod thread
// registers another one.
static std::mutex g_handshake;
static std::condition_variable g_handshakeCv;
static bool g_inside = false, g_proceed = false;
static void blocking_listener(std::string) {
    std::unique_lock<std::mutex> lock(g_handshake);
    g_inside = true;
    g_handshakeCv.notify_all();
    g_handshakeCv.wait(lock, [] { return g_proceed; });
}

int main(int argc, char **argv) {
    JNIEnv *env = FakeJniEnv();
    _jobject clazz;
    const char *mode = argc > 1 ? argv[1] : "leaks";

    if (!std::strcmp(mode, "leaks")) {
        Java_git_artdeell_skymodloader_MainActivity_setDeviceInfoNative(
            env, &clazz, 420.0f, 420.0f, 2.625f, FakeJniString("Pixel 8"), FakeJniString("Google"), FakeJniString("shiba"));
        Java_git_artdeell_skymodloader_MainActivity_nativeSetSkyBuildKey(env, &clazz, FakeJniString("build-key"));
        for (int i = 0; i < 25; ++i)
            Java_git_artdeell_skymodloader_MainActivity_onKeyboardCompleteNative(env, &clazz, FakeJniString("hello from the IME"));
        for (int i = 0; i < 40; ++i) {
            const std::string name = "icon_" + std::to_string(i);
            Java_git_artdeell_skymodloader_iconloader_IconLoader_addIcon(
                env, &clazz, FakeJniString(name.c_str()), FakeJniString("UIPackedAtlas"), 0.0f, 0.0f, 0.1f, 0.1f);
        }
        // Independent count from the fake JNIEnv, then LSan while globals are
        // still alive: a killed Android process never runs static destructors,
        // so objects only reachable from them (icons, deviceInfo) are not leaks.
        std::printf("GetStringUTFChars without ReleaseStringUTFChars: %ld\n", FakeJniOutstandingUtf());
        std::fflush(stdout);
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
        __lsan_do_leak_check();
#endif
#endif
        return 0;
    }

    if (!std::strcmp(mode, "race")) {
        CipherUtils::addOnKeyboardCompleteListener(&listener);
        std::thread mod([] {
            for (int i = 0; i < 4000; ++i)
                CipherUtils::addOnKeyboardCompleteListener(&listener);
        });
        for (int i = 0; i < 400; ++i)
            Java_git_artdeell_skymodloader_MainActivity_onKeyboardCompleteNative(env, &clazz, FakeJniString("x"));
        mod.join();
        std::puts("survived");
    }

    if (!std::strcmp(mode, "systemui")) {
        // GameActivity.onCreate passes a fresh SystemUI_android (which holds
        // the activity) each time the activity is created in this process.
        _jobject activities[3];
        for (auto &systemUI : activities)
            Java_git_artdeell_skymodloader_MainActivity_getSysetemUI(env, &clazz, &systemUI);
        std::printf("GameActivity created 3 times: live global refs %ld (expected 1, the current SystemUI)\n",
                    FakeJniLiveGlobalRefs());
        return FakeJniLiveGlobalRefs() == 1 ? 0 : 1;
    }

    if (!std::strcmp(mode, "interleave")) {
        // Deterministic form of the race: a keyboard completion is being
        // delivered (UI thread inside a listener) when a mod registers a
        // listener and the vector has to grow. The handshake lives only in the
        // harness; the iteration and the push_back are Canvas's own code.
        CipherUtils::addOnKeyboardCompleteListener(&blocking_listener);
        CipherUtils::addOnKeyboardCompleteListener(&listener);
        Canvas::onKeyboardCompleteListeners.shrink_to_fit();  // capacity == size: the next add reallocates
        std::thread mod([] {
            std::unique_lock<std::mutex> lock(g_handshake);
            g_handshakeCv.wait(lock, [] { return g_inside; });
            CipherUtils::addOnKeyboardCompleteListener(&listener);
            g_proceed = true;
            g_handshakeCv.notify_all();
        });
        Java_git_artdeell_skymodloader_MainActivity_onKeyboardCompleteNative(env, &clazz, FakeJniString("x"));
        mod.join();
        std::puts("survived");
    }
    return 0;
}
