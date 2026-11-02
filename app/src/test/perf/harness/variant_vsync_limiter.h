// DESIGN VARIANT, not Canvas's code: included only by a patched scratch copy
// of androidbk.cpp that run_perf.sh builds inside the container. It replaces
// renderloop's "Limit FPS" sleep (usleep(41000)) with a wait for the next
// vsync, so together with the swap's own vsync wait each frame takes exactly
// two vsync periods: a steady 30 frames/s at 60 Hz. On a device this is what
// eglSwapInterval(2) on the current surface, or Swappy's 30 fps mode, would
// provide (eglSwapInterval(2) is not supported on every device).
#pragma once

void FakeSleepToNextVsync();  // ../sanitizers/harness/app/fake_platform.cpp

namespace perf_limiter {
inline void wait_one_vsync() { FakeSleepToNextVsync(); }
}
