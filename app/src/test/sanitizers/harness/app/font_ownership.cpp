// L18: the real loadFonts (verbatim run.sh extraction from androidbk.cpp) with
// the CJK fallback font enabled, as ImGUI.java:68 always passes. loadFonts hands
// its malloc'ed DroidSansFallback buffer to AddFontFromMemoryTTF with ImGui's
// default ownership (the atlas frees it) and then free()s it itself after
// Build(). Today nothing clears, rebuilds or destroys the atlas afterwards;
// these modes do what any such change would:
//   clear     io.Fonts->Clear()           (what ImGui::DestroyContext does to its atlas)
//   rebuild   ClearTexData() + Build()    (e.g. a font size or scale change)
//   destroy   ImGui::DestroyContext()
//   control   the same clear, with enableDroidSans = false (no handed-over buffer)
// Oracle: each mode completes ("survived") with no sanitizer report.
// The assets come from FAKE_ASSET_DIR (fake_platform.cpp).
#include <android/asset_manager.h>
#include <android/log.h>
#include <jni.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui/imgui.h"
#include "include/misc/visibility.h"

#define g_LogTag "imgui4ca"  // androidbk.cpp:28
#include "loadFonts.inc"

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "clear";
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    static int token;
    const bool droidSans = std::strcmp(mode, "control") != 0;
    loadFonts(io, 36.75f, reinterpret_cast<AAssetManager *>(&token), droidSans);
    std::printf("loadFonts(enableDroidSans=%d): %d font config(s), atlas %dx%d\n", droidSans,
                io.Fonts->ConfigData.Size, io.Fonts->TexWidth, io.Fonts->TexHeight);
    std::fflush(stdout);
    if (!std::strcmp(mode, "clear") || !std::strcmp(mode, "control")) {
        io.Fonts->Clear();
    } else if (!std::strcmp(mode, "rebuild")) {
        io.Fonts->ClearTexData();
        io.Fonts->Build();
    } else if (!std::strcmp(mode, "destroy")) {
        ImGui::DestroyContext();
    } else {
        return 2;
    }
    std::puts("survived");
    return 0;
}
