// A stand-in mod library for mod_lifecycle.cpp, built twice as a shared
// object: with MOD_UI=1 its Start() returns a Draw function (a mod with a
// menu), with MOD_UI=0 it returns nullptr (a patch-only mod). Both export
// InitLate(), which counts its calls; the harness reads the counter with
// dlsym. Not Canvas code.
#include <atomic>

namespace {
std::atomic<int> g_threadsInside{0};
std::atomic<int> g_done{0};  // completed InitLate calls, published with release
}

extern "C" {

__attribute__((visibility("default"))) int g_initLateCalls = 0;      // plain int: TSan sees concurrent calls
__attribute__((visibility("default"))) int g_initLateOverlapped = 0;  // set if two InitLate calls overlap

static void Draw(bool *) {}

// Declared with the type Canvas calls it through (main.cpp `func (*Start)()`,
// `typedef void (*func)();`), so UBSan's function-type check reports only
// mismatches in Canvas, not in this stand-in.
typedef void (*func)();
__attribute__((visibility("default"))) func Start() {
#if MOD_UI
    return reinterpret_cast<func>(&Draw);
#else
    return nullptr;
#endif
}

__attribute__((visibility("default"))) void InitLate() {
    if (g_threadsInside.fetch_add(1) > 0) g_initLateOverlapped = 1;
    ++g_initLateCalls;
    for (volatile int i = 0; i < 2000000; ++i) {}  // a mod doing some setup work
    g_threadsInside.fetch_sub(1);
    g_done.fetch_add(1, std::memory_order_release);
}

// For the harness: an acquire load, so its later reads of the counters are
// ordered after every completed InitLate (no harness-made TSan report).
__attribute__((visibility("default"))) int ModInitLateDone() { return g_done.load(std::memory_order_acquire); }
}
