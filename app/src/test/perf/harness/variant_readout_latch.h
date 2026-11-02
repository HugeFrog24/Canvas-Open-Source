// DESIGN VARIANT, not Canvas's code: included only by a patched scratch copy
// of the CanvasMenu extraction that run_perf.sh's `readout` stage builds inside
// the container. Canvas prints io.Framerate as "%.3f ms/frame (%.1f FPS)"
// every frame; io.Framerate is a rolling average of io.DeltaTime, so the
// text (and with it the draw data) changes on most frames and keeps the
// skip-unchanged variant presenting while the menu is open. Here the readout
// shows a value latched twice a second instead.
#pragma once
#include "imgui/imgui.h"

namespace perf_readout {
inline float latched() {
    static float value = 0.0f;
    static double last = -1.0;
    const double now = ImGui::GetTime();  // fixed within a frame (set in NewFrame)
    if (last < 0.0 || now - last >= 0.5) {
        value = ImGui::GetIO().Framerate;
        last = now;
    }
    return value;
}
}
