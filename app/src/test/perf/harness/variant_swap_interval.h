// DESIGN VARIANT, not Canvas's code: included only by a patched scratch copy
// of androidbk.cpp that run_perf.sh's `limiter` stage builds inside the
// container. It replaces renderloop's "Limit FPS" sleep (usleep(41000)) with
// eglSwapInterval(display, 2) on the current context and surface while the
// option is on (1 while it is off). In the fake display that holds each frame
// for at least two vsync periods: a steady 30 frames/s at 60 Hz, whether or
// not eglSwapBuffers itself blocks.
//
// It stands for two-vsync pacing, not for this exact call on Android: AOSP's
// Surface sets maxSwapInterval = 1 and setSwapInterval() clamps to it
// (frameworks/native/libs/gui/Surface.cpp), so eglSwapInterval(2) acts as 1
// there. On a device the same pacing comes from Swappy (Choreographer plus
// presentation timestamps). Unlike Canvas's only eglSwapInterval call
// (sanitizer finding F15), this one runs with the context current.
#pragma once
#include <EGL/egl.h>

namespace perf_limiter {
inline void apply_swap_interval(EGLDisplay display, bool limited) {
    static int current = 1;
    const int wanted = limited ? 2 : 1;
    if (wanted != current && eglSwapInterval(display, wanted)) current = wanted;
}
}
