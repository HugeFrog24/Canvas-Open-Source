// Real IconLoader.cpp with libktx/GL/AAsset stand-ins (fake_ktx_assets.cpp).
//   upload <code>:          getImage() on a present asset whose GL upload
//                           returns KTX_error_code <code>
//   shared-missing-atlas:   two icons that name the same missing atlas, as
//                           IconLoader.findIcons() can register; LeakSanitizer
//   race:                   getImage from two threads (F20)
//   icon-api:               getUIIcon/icon/iconButton in headless ImGui frames (pass 36)
//   icon-race:              getUIIcon from two threads on unresolved icons (pass 36)
#include <GLES3/gl3.h>
#include <jni.h>
#include <sanitizer/lsan_interface.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "Iconloader/IconLoader.h"
#include "imgui/imgui.h"
#include "ktx/ktx.h"

extern KTX_error_code g_uploadResult;
unsigned FakeKtxTexturesCreated();
namespace Canvas {
AAssetManager *aAssetManager = nullptr;
}
extern "C" void Java_git_artdeell_skymodloader_iconloader_IconLoader_addIcon(JNIEnv *, jclass, jstring, jstring, jfloat,
                                                                           jfloat, jfloat, jfloat);

static const char *const kCodes[] = {
    "KTX_SUCCESS", "KTX_FILE_DATA_ERROR", "KTX_FILE_ISPIPE", "KTX_FILE_OPEN_FAILED", "KTX_FILE_OVERFLOW",
    "KTX_FILE_READ_ERROR", "KTX_FILE_SEEK_ERROR", "KTX_FILE_UNEXPECTED_EOF", "KTX_FILE_WRITE_ERROR", "KTX_GL_ERROR",
    "KTX_INVALID_OPERATION", "KTX_INVALID_VALUE", "KTX_NOT_FOUND", "KTX_OUT_OF_MEMORY", "KTX_TRANSCODE_FAILED",
    "KTX_UNKNOWN_FILE_FORMAT", "KTX_UNSUPPORTED_TEXTURE_TYPE", "KTX_UNSUPPORTED_FEATURE", "KTX_LIBRARY_NOT_LINKED",
};

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "upload";
    if (!std::strcmp(mode, "upload")) {
        const int code = argc > 2 ? std::atoi(argv[2]) : 0;
        g_uploadResult = static_cast<KTX_error_code>(code);
        std::printf("%-30s ", kCodes[code]);
        std::fflush(stdout);
        SkyImage &image = IconLoader::getImage("present_image");
        std::printf("returned, textureId=%ld\n", static_cast<long>(reinterpret_cast<intptr_t>(image.textureId)));
        return 0;
    }
    if (!std::strcmp(mode, "shared-missing-atlas")) {
        _jobject clazz;
        JNIEnv *env = FakeJniEnv();
        Java_git_artdeell_skymodloader_iconloader_IconLoader_addIcon(env, &clazz, FakeJniString("icon_a"),
                                                                      FakeJniString("missing_atlas"), 0, 0, 1, 1);
        Java_git_artdeell_skymodloader_iconloader_IconLoader_addIcon(env, &clazz, FakeJniString("icon_b"),
                                                                      FakeJniString("missing_atlas"), 0, 0, 1, 1);
        UIIcon icon{};
        IconLoader::getUIIcon("icon_a", &icon);
        IconLoader::getUIIcon("icon_b", &icon);
        std::puts("both icons resolved");
        std::fflush(stdout);
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
        __lsan_do_leak_check();  // while the global maps are still alive (ASan builds; TSan has no LSan)
#endif
#endif
        return 0;
    }
    if (!std::strcmp(mode, "race")) {
        // IconLoader::getImage is exported (no visibility attribute, and the app
        // build has no -fvisibility=hidden), so mods can call it. One mod calls it
        // from Draw() on the render thread while another calls it from InitLate()
        // or its own thread; both insert into the unguarded IconLoader::images
        // (L13). Oracle: every call returns an image named as asked for, and
        // TSan/ASan stay quiet.
        g_uploadResult = KTX_SUCCESS;
        auto load = [](const char *prefix, int *bad) {
            for (int i = 0; i < 300; ++i) {
                char name[64];
                std::snprintf(name, sizeof(name), "present_%s_%d", prefix, i);
                SkyImage &image = IconLoader::getImage(name);
                if (image.textureId == IL_NO_TEXTURE) ++*bad;
            }
        };
        int badRender = 0, badLate = 0;
        std::thread render(load, "render", &badRender);
        std::thread late(load, "initlate", &badLate);
        render.join();
        late.join();
        std::printf("race: 600 getImage calls on two threads, %d without a texture\n", badRender + badLate);
        return 0;
    }
    if (!std::strcmp(mode, "icon-api")) {
        // Pass 36: the mod-facing API (IconLoader.h: getUIIcon, icon, iconButton) after
        // registrations through the real addIcon, as IconLoader.findIcons() makes them,
        // with ImGui frames run headless (as imgui's example_null). Oracles: the
        // registered texture and uv come back; icon() draws with that texture and a
        // placeholder otherwise; both iconButton paths report a click; ImGui's asserts
        // stay quiet. Leaks are attributed by allocation site at the end.
        _jobject clazz;
        JNIEnv *env = FakeJniEnv();
        auto add = [&](const char *name, const char *atlas, float u0, float v0, float u1, float v1) {
            Java_git_artdeell_skymodloader_iconloader_IconLoader_addIcon(env, &clazz, FakeJniString(name),
                                                                          FakeJniString(atlas), u0, v0, u1, v1);
        };
        g_uploadResult = KTX_SUCCESS;
        add("icon_ok", "present_atlas", 0.25f, 0.5f, 0.75f, 1.0f);
        add("icon_dup", "present_atlas", 0.1f, 0.1f, 0.2f, 0.2f);
        add("icon_dup", "present_atlas", 0.9f, 0.9f, 1.0f, 1.0f);  // the same name registered again
        add("icon_missing_atlas", "missing_atlas", 0, 0, 1, 1);

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800, 600);
        unsigned char *pixels;
        int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        int failures = 0;
        auto check = [&](bool ok, const char *what) {
            std::printf("%s %s\n", ok ? "ok      " : "MISMATCH", what);
            failures += !ok;
        };
        UIIcon ui{};
        IconLoader::getUIIcon("icon_ok", &ui);
        const ImTextureID atlasTexture = ui.textureId;
        check(reinterpret_cast<intptr_t>(ui.textureId) > 0 && ui.uv0.x == 0.25f && ui.uv0.y == 0.5f && ui.uv1.x == 0.75f &&
                  ui.uv1.y == 1.0f,
              "getUIIcon(icon_ok): the atlas texture and the registered uv");
        IconLoader::getUIIcon("icon_dup", &ui);
        std::printf("         icon_dup registered twice: getUIIcon returns uv0=(%.1f, %.1f), the %s registration\n", ui.uv0.x,
                    ui.uv0.y, ui.uv0.x == 0.1f ? "first" : "second");
        IconLoader::getUIIcon("no_such_icon", &ui);
        check(ui.textureId == IL_NO_TEXTURE, "getUIIcon(unknown name): IL_NO_TEXTURE");
        IconLoader::getUIIcon("icon_missing_atlas", &ui);
        check(ui.textureId == IL_NO_TEXTURE, "getUIIcon(icon whose atlas asset is missing): IL_NO_TEXTURE");

        bool clicked = false, clickedFallback = false, drewTexture = false, drewPlaceholder = false;
        ImVec2 button(-1, -1), fallback(-1, -1);
        for (int frame = 0; frame < 8; ++frame) {
            io.DeltaTime = 1.0f / 60.0f;
            if (frame == 3) { io.AddMousePosEvent(button.x, button.y); io.AddMouseButtonEvent(0, true); }
            if (frame == 4) io.AddMouseButtonEvent(0, false);
            if (frame == 5) { io.AddMousePosEvent(fallback.x, fallback.y); io.AddMouseButtonEvent(0, true); }
            if (frame == 6) io.AddMouseButtonEvent(0, false);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(800, 600));
            ImGui::Begin("mod window");
            IconLoader::icon("icon_ok", 32);
            IconLoader::icon("no_such_icon", 32);
            if (IconLoader::iconButton("icon_ok", 48)) clicked = true;
            button = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) / 2,
                            (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) / 2);
            if (IconLoader::iconButton("no_such_icon", 48)) clickedFallback = true;
            fallback = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) / 2,
                              (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) / 2);
            ImGui::End();
            ImGui::Render();
            const ImDrawData *dd = ImGui::GetDrawData();
            for (int n = 0; n < dd->CmdListsCount; ++n) {
                const ImDrawList *list = dd->CmdLists[n];
                for (const ImDrawCmd &cmd : list->CmdBuffer) drewTexture |= cmd.TextureId == atlasTexture;
                for (const ImDrawVert &v : list->VtxBuffer) drewPlaceholder |= v.col == 0xFFFF00FF;
            }
        }
        check(drewTexture, "icon(icon_ok) draws with the atlas texture");
        check(drewPlaceholder, "icon(unknown) draws the magenta placeholder");
        check(clicked, "iconButton(icon_ok) reports the click");
        check(clickedFallback, "iconButton(unknown) falls back to a text Button that reports the click");
        ImGui::DestroyContext();
        std::printf("failures=%d\n", failures);
        std::fflush(stdout);
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
        __lsan_do_recoverable_leak_check();  // the global maps are still alive here
#endif
#endif
        return failures ? 1 : 0;
    }
    if (!std::strcmp(mode, "icon-race")) {
        // Pass 36: F20's class through the other exported entry, getUIIcon. Two threads
        // (say a mod's Draw on the render thread and another mod's InitLate) resolve
        // the same 200 icons, each in its own atlas, for the first time. Oracle:
        // every call returns a texture, and each atlas is uploaded once.
        _jobject clazz;
        JNIEnv *env = FakeJniEnv();
        g_uploadResult = KTX_SUCCESS;
        for (int i = 0; i < 200; ++i) {
            char name[32], atlas[48];
            std::snprintf(name, sizeof(name), "icon_%d", i);
            std::snprintf(atlas, sizeof(atlas), "present_atlas_%d", i);
            Java_git_artdeell_skymodloader_iconloader_IconLoader_addIcon(env, &clazz, FakeJniString(name),
                                                                          FakeJniString(atlas), 0, 0, 1, 1);
        }
        auto resolve = [](int *bad) {
            for (int i = 0; i < 200; ++i) {
                char name[32];
                std::snprintf(name, sizeof(name), "icon_%d", i);
                UIIcon ui{};
                IconLoader::getUIIcon(name, &ui);
                if (ui.textureId == IL_NO_TEXTURE) ++*bad;
            }
        };
        int badA = 0, badB = 0;
        std::thread a(resolve, &badA), b(resolve, &badB);
        a.join();
        b.join();
        const unsigned uploads = FakeKtxTexturesCreated();
        std::printf("icon-race: 400 getUIIcon calls on two threads, %d without a texture; %u textures created for 200 atlases"
                    " (%u duplicate uploads)\n", badA + badB, uploads, uploads > 200 ? uploads - 200 : 0);
        return badA + badB || uploads != 200 ? 1 : 0;
    }
    return 2;
}
