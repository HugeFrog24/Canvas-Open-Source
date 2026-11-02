// DESIGN VARIANT, not Canvas's code: included only by a patched scratch copy
// of androidbk.cpp that run_perf.sh builds inside the container. When the
// frame's draw data equals the previous frame's, the patched renderloop skips
// glClear, GL submission and eglSwapBuffers and waits one vsync period instead
// (on a device that wait would come from vsync, e.g. AChoreographer). ImGui
// itself still runs every frame (NewFrame/Render), so its invariants hold.
#pragma once
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#include "imgui/imgui.h"

extern int g_fakeSwapSleepUs, g_fakeVsyncAligned;
void FakeSleepToNextVsync();  // ../sanitizers/harness/app/fake_platform.cpp
extern void (*g_fakeFramePoint)(int);

namespace perf_variant {
inline std::atomic<long> g_presented{0}, g_skipped{0};
inline unsigned long long g_lastHash = 0;
inline bool g_haveLast = false;

inline unsigned long long hash(const ImDrawData *dd) {
    unsigned long long h = 1469598103934665603ULL;
    auto mix = [&h](const void *data, size_t size) {
        const unsigned char *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 1099511628211ULL;
    };
    for (int n = 0; dd && n < dd->CmdListsCount; ++n) {
        const ImDrawList *list = dd->CmdLists[n];
        mix(list->VtxBuffer.Data, sizeof(ImDrawVert) * list->VtxBuffer.Size);
        mix(list->IdxBuffer.Data, sizeof(ImDrawIdx) * list->IdxBuffer.Size);
        for (const ImDrawCmd &cmd : list->CmdBuffer) {
            mix(&cmd.ClipRect, sizeof(cmd.ClipRect));
            mix(&cmd.TextureId, sizeof(cmd.TextureId));
            mix(&cmd.ElemCount, sizeof(cmd.ElemCount));
        }
    }
    return h;
}

#ifdef VARIANT_MEMCMP
// Change test by comparison with a kept copy of the previous frame's lists:
// memcmp runs at memory speed and is exact (no hash collisions); a list is
// re-copied only when it changed.
struct CmdKey {
    ImVec4 clip;
    ImTextureID texture;
    unsigned int vtxOffset, idxOffset, elemCount;
    unsigned int pad;  // fills the tail padding so memcmp never compares indeterminate bytes
};
static_assert(sizeof(CmdKey) == 16 + sizeof(ImTextureID) + 4 * sizeof(unsigned int), "CmdKey must have no padding");
struct ListCopy {
    std::vector<ImDrawVert> vtx;
    std::vector<ImDrawIdx> idx;
    std::vector<CmdKey> cmds;
};
inline std::vector<ListCopy> g_prevLists;
inline std::vector<CmdKey> g_scratchCmds;

inline bool frame_changed(const ImDrawData *dd) {
    const int lists = dd ? dd->CmdListsCount : 0;
    bool changed = !g_haveLast || lists != static_cast<int>(g_prevLists.size());
    g_prevLists.resize(lists);
    for (int n = 0; n < lists; ++n) {
        const ImDrawList *list = dd->CmdLists[n];
        ListCopy &prev = g_prevLists[n];
        g_scratchCmds.clear();
        for (const ImDrawCmd &c : list->CmdBuffer)
            g_scratchCmds.push_back({c.ClipRect, c.TextureId, c.VtxOffset, c.IdxOffset, c.ElemCount, 0u});
        const bool same =
            prev.vtx.size() == static_cast<size_t>(list->VtxBuffer.Size) &&
            prev.idx.size() == static_cast<size_t>(list->IdxBuffer.Size) && prev.cmds.size() == g_scratchCmds.size() &&
            std::memcmp(prev.vtx.data(), list->VtxBuffer.Data, sizeof(ImDrawVert) * prev.vtx.size()) == 0 &&
            std::memcmp(prev.idx.data(), list->IdxBuffer.Data, sizeof(ImDrawIdx) * prev.idx.size()) == 0 &&
            std::memcmp(prev.cmds.data(), g_scratchCmds.data(), sizeof(CmdKey) * prev.cmds.size()) == 0;
        if (!same) {
            changed = true;
            prev.vtx.assign(list->VtxBuffer.Data, list->VtxBuffer.Data + list->VtxBuffer.Size);
            prev.idx.assign(list->IdxBuffer.Data, list->IdxBuffer.Data + list->IdxBuffer.Size);
            prev.cmds = g_scratchCmds;
        }
    }
    g_haveLast = true;
    return changed;
}
#else
// True when the frame differs from the last presented one (always true first).
// Byte-wise FNV-1a: simple but slow on ~90 KB per frame (perf run 7).
inline bool frame_changed(const ImDrawData *dd) {
    const unsigned long long h = hash(dd);
    const bool changed = !g_haveLast || h != g_lastHash;
    g_lastHash = h;
    g_haveLast = true;
    return changed;
}
#endif

// Skipped frame: close the perf driver's frame record, then wait as a present would.
inline void wait_next_vsync() {
    ++g_skipped;
    if (g_fakeFramePoint) {
        g_fakeFramePoint(3);
        g_fakeFramePoint(4);
    }
    // On the vsync clock (aligned or queued display model) a skipped frame waits
    // for the next vsync boundary, as a Choreographer callback would; otherwise
    // it sleeps one period (runs 6-8).
    if (g_fakeVsyncAligned) FakeSleepToNextVsync();
    else if (g_fakeSwapSleepUs > 0) usleep(g_fakeSwapSleepUs);
}
}  // namespace perf_variant

extern "C" void perf_variant_report(double) {
    std::printf("  VARIANT skip-unchanged (whole run incl. warm-up): presented=%ld skipped=%ld\n",
                perf_variant::g_presented.load(), perf_variant::g_skipped.load());
}
