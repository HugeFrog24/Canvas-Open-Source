# Canvas overlay performance (host measurements)

Research log for one question: how much does Canvas's own overlay cost per
frame, and could that plausibly contribute to the sluggishness users report?
Measured on a desktop host in Docker, without Sky, mods or a phone. This track
explains; it does not fix.

## Summary so far (passes 1-58)

What the host measurements show, each with the run that supports it:

1. **The overlay's CPU work is small.** With five stand-in mod panels at
   60 Hz spacing a frame costs about 70 us of render-thread CPU, about 4 ms
   per second, under 1% of one host core (runs 2, 7). Instruction counts
   scale the same way, so this does not depend on machine load (run 3).
   The stable figure is instructions: about 405k per frame with five mods
   and the menu open (runs 3, 17, 19). Host CPU time for identical code
   varied 1.8x between sessions (60 Hz p50: 68 µs in run 2, 123 µs in
   pass 49), consistent with the hybrid-core placement noted in pass 35.
   So time figures compare only within one interleaved run.
2. **Most of that is not Canvas's own code.** About two thirds is the mods'
   widget drawing; Canvas's menu and per-mod wrapper are about a fifth
   (run 3). Touch input adds almost nothing (runs 1, 3).
3. **ImGui's GPU-facing output is light.** Five panels: 12 draw calls and
   about 93 KB of buffer uploads per frame (run 4).
4. **In the usual in-game state every overlay frame is redundant.** With the
   Canvas menu hidden, each frame is identical to the previous one, even
   when nothing is drawn at all, yet the loop still clears and presents a
   full-screen 1080x2376 translucent layer about 59 times a second (runs 5,
   6). That full-surface clear is over 100x ImGui's geometry in nominal
   bytes (run 4).
5. **That redundancy is avoidable cheaply.** A design variant that skips
   unchanged frames presents nothing while idle and costs about 25k
   instructions per changing frame, roughly 2 us (runs 6, 8, 9). This holds
   under both display models (run 14). With the menu open it only helps if
   the FPS readout is latched or left out of the change test: otherwise the
   readout changes on 50-90% of frames, depending on frame timing (runs 6,
   11, 14: 41, 30 and 56 presents/s). The design can only remove presents
   the content does not need. One visible mod showing a per-frame value keeps
   it presenting every vsync; one updating at 10 Hz leaves 10 presents/s
   (run 18). Its benefit on a device therefore depends on what the installed
   mods draw. In instructions, the designs differ by under 4% (run 17).
6. **"Limit FPS" paces unevenly by design, not because of cost.** Below the
   refresh rate a non-async BufferQueue lets the swap return without waiting.
   With that, the 41 ms sleep gives 24.2 frames/s, with 33 and 50 ms gaps in
   a 52/48 split. That matches the audit's device cadence (about 23.1/s,
   mostly 33/50 ms). Per-frame CPU is the same with and without the limiter.
   Pacing every second vsync gives a steady 30/s (runs 10, 13; pass 6
   analysis). On Android that pacing would come from Swappy, because
   `Surface` clamps `eglSwapInterval(2)` to 1.
7. **With the menu open, the FPS readout is what keeps frames changing.** It
   prints a 60-frame rolling average to three decimals every frame. Latched
   twice a second on top of the skip-unchanged variant, it cuts presents from
   about 30/s to about 2/s (run 11).
8. **The UI thread does not wait on Canvas's input locks.** The JNI queries
   GameActivity makes per touch take under 1.3 µs at p99.9, even with the
   render thread drawing 20 mods flat out. The worst call seen was 56 µs
   (run 12).

9. **The shipped font setup costs memory and startup time, not frame time.**
   The app always merges a CJK fallback font (`ImGUI.java:68`), giving a
   2048x4096 atlas with 3,352 glyphs instead of 512x1024 with 467. Per-frame
   instructions are unchanged (404.7k). Building it takes about 12x longer
   at startup (host). By source, about 70 MB of CPU and GPU memory is held
   (RGBA32 texture and both CPU copies), against about 4.5 MB without the
   merge (run 19).
10. **Overlay tap response depends on frame timing and buffering, not on
    per-frame CPU.** A near-instant tap, with press and release between
    two frames, reaches an ImGui button in 2 frames; a tap held across a
    frame boundary takes 1. Typed text arrives in 1 frame unless key events
    are mixed in (run 21). Canvas's input settings are already the fastest
    that still register taps (runs 22-23).
    - End to end, in the queued-swap model, release to present for taps
      held 50 ms or longer is about 26-28 ms p50 with "Limit FPS" and about
      59 ms without (run 24). Unlimited rendering fills the BufferQueue,
      about 3 vsyncs. Pass 57's estimate, "the limiter roughly triples it",
      counted frame intervals only and is superseded.

What it rules out and what is left for the device: Canvas's overlay CPU work
and ImGui's geometry are unlikely to explain a visible slowdown. If the
overlay contributes, the candidate is the cost of presenting an always-on,
full-screen translucent layer at display rate: GPU clear/resolve, memory
traffic, and composition. The AOSP Hardware Composer documentation notes that
devices typically have about four overlay planes, that layers beyond that
fall back to GPU composition, and that blending translucent layers on a
static screen can make overlays less efficient; the decision logic is vendor
specific. On the audited OnePlus both layers were composited in hardware, but
other OEMs may differ. The same page says the HWC can reuse a cached
composition when SurfaceFlinger asks it to composite the same set of
buffers again. An overlay that queues a new buffer every vsync never offers
that. This matters only when the game layer is also static, for example in a
paused menu. The tests that would settle it are on the device: the
audit's overlay-absent comparison, and the skip-unchanged variant, in a scene
that shows the slowdown, over sustained runs. Separately, the frozen thermal
status (sanitizer FINDINGS L10) remains a non-overlay lead.

Device answer (Galaxy A52, 2026-11-01, and OnePlus 13, 2026-11-02;
[device/DEVICE.md](device/DEVICE.md), "Results"):
- **Frame rate:** the game held its 30 fps target in every variant.
- **Hitches:** an overlay presenting every vsync about doubled the game's
  missed frames, 6.6-10% against 3.5-4.3% with no overlay presents.
- **Fix:** skip-unchanged plus the latched readout brought that to the
  no-overlay level with the menu shown.
- **Timing (run 2):** when the overlay draws matters more than its cost. The
  game finishes each frame 1-2 ms before the compositor's deadline, and overlay
  GPU work landing just before that deadline pushes the game's frame a vsync
  late. Presenting at 30/s with presentation times put 92-94% of overlay frames
  just after the game's. That matched no-overlay (3.7% long frames) at full
  per-present cost. Cheaper presents (partial redraw, low priority, no depth)
  only reached 6.2%.
- **Timing (run 3):**
  - **Matches no-overlay in every round:** drawing once per game frame,
    triggered by a hook on Sky's `vkQueuePresentKHR` (Sky presents through
    Vulkan on this phone).
  - **Long frames:** 4.9% against 5.3% for no-overlay and 9.0% for `base`.
    With skip-unchanged it was 4.3%, with a p95 of 33.3 ms in all rounds.
  - **No fence needed:** waiting for the game's frame to finish on the GPU
    did not help.
  - **Hook cost:** 11 µs per game frame on the game's thread.
  - **`pace30pt` was luck:** it held run 2's phase for only 30 s of one round
    (7.9% long frames), so pacing from vsync cannot be relied on.
- **Production design (run 4):** present hook, once per game frame,
  skip-unchanged, and a vsync fallback after 70 ms without a game frame.
  - **Long frames:** 4.7% against 4.8% without the overlay and 10.1% for
    `base`.
  - **With a large menu that changes every frame:** 5.7%, against 12.3% for
    `base` with the same menu.
  - **How late the draw can start:** on the A52, up to 16 ms after the
    game's present still keeps the benefit; at 24 ms long frames rose to
    8.0%.
  - **Loading:** the fallback keeps the menu at 60 frames/s while the game
    loads.
- **Second phone (run 5, OnePlus 13, Snapdragon 8 Elite):** Sky at 30 fps
  through Vulkan, panel at 60 Hz.
  - **Long frames:** `prod` 1.0% and `prod` with the large changing menu
    0.9%, against 1.0% without the overlay and 5.5% for `base`.
  - **16 ms lag:** 4.3% long frames, so most of the benefit is gone.
  - **4 and 8 ms lag (run 6):** 1.4% and 1.3%, against 1.2% with no lag and
    1.0% without the overlay, so within the spread. Starting the draw within
    8 ms of the game's present kept the no-overlay level on both phones.
  - **Game's frame:** already finished on the GPU when its present returns
    (14 µs fence wait, against 14 ms on the A52).
- **Slow state:** 20 fps for tens of seconds. It is a GPU and CPU governor
  equilibrium with no lowered limits, not the overlay; vanilla Sky was not
  measured. The overlay's every-vsync
  work shortens episodes, so skip-unchanged trades steadier frames for longer
  episodes. Drawing once per game frame did not lengthen them in two
  10-minute runs (0 and 7 s, against `base`'s 0 and 35 s).
- **Thermal:** the frozen status was confirmed, but Sky showed no reaction to
  SEVERE within 60 s.
- **CJK merge:** about 28 MB of graphics memory, no frame-time effect.

## Device test plan (for the owner; not run by this loop)

What would settle the overlay question on a phone, using tools already in
hand (the Codex audit's `capture-layer-cadence.py`, adb, Perfetto):

1. Setup, matched across runs: same Sky version and graphics setting, same
   scene that shows the slowdown, same mods enabled, display mode recorded,
   brightness fixed, not charging, starting cool (`dumpsys thermalservice`).
2. Builds: A, Canvas as is; B, the overlay absent (the ImGui `SurfaceView`
   never created); C, the skip-unchanged-frames variant (runs 6-9), which
   keeps the overlay but presents only when it changes; optionally C', C
   with the FPS readout latched twice a second (run 11), for the menu-open
   state.
   For "Limit FPS": A with the option on, against a build whose limiter
   paces every second vsync. Swappy at 30 fps would do; `eglSwapInterval(2)`
   would not, because `Surface` clamps it to 1. The host predicts A at about
   23-24 overlay presents/s, alternating 33 and 50 ms, and the paced build
   at a steady 30/s (run 13).
3. Per run, both short (about 2 minutes) and sustained (20-30 minutes):
   - game-layer presentation intervals and the overlay layer's presents per
     second (`capture-layer-cadence.py`, SurfaceFlinger latency per layer).
     Perfetto's FrameTimeline (Android 12+) gives jank types per frame but
     does not cover SurfaceViews yet. Use it for the game's frames only; the
     overlay is a SurfaceView, so its cadence comes from per-layer latency;
   - each layer's composition type (hardware or GPU) via Perfetto's
     `android.surfaceflinger.layers` data source with the composition-type
     trace flag (Android 15+; the audited phone had `traced` stopped, so it
     must be enabled first);
   - thermal status over time (`dumpsys thermalservice`), and CPU/GPU
     frequencies where the device exposes them;
   - the game process's memory with and without the CJK font merge (run 19:
     the shipped `enableDroidSans = true` builds a 2048x4096 atlas, about
     70 MB of CPU+GPU memory by source). Use `dumpsys meminfo <package>`:
     the Native Heap row, plus the Graphics rows (Gfx dev, EGL mtrack,
     GL mtrack; GL mtrack is the driver's GL usage, mostly textures). The
     memtrack rows depend on the vendor's HAL and are snapshots, so compare
     the two builds on the same device. A low-RAM device shows whether
     the difference costs anything. Look at `ApplicationExitInfo` reasons
     across sessions (low-memory kills of the game) and how often background
     apps are killed (pass 55);
   - CPU samples of the overlay's render thread (`simpleperf record -t
     <tid>`, or a Perfetto CPU profile). Look at the share of time inside
     `ImGui_ImplOpenGL3_RenderDrawData`, especially the driver's
     `glGetIntegerv`/`glIsEnabled` (about 20 per frame, pass 51 analysis).
     AGI times individual calls only for Vulkan, not GL ES, so sampling is
     the way to see this;
   - which installed mods draw values that change every frame. Build C's
     benefit depends on it (run 18): one such mod keeps it presenting at
     display rate;
   - on a variable-refresh display: the active refresh rate and the overlay
     layer's frame-rate vote, from SurfaceFlinger's scheduler state in
     `dumpsys SurfaceFlinger`. SurfaceFlinger estimates each layer's rate
     from its update history. Per AOSP's heuristic changes, a layer whose
     frame times do not settle on one known rate gets a Max vote, and small
     damage regions can be suppressed. The overlay presents full-surface
     damage every vsync (or at the uneven "Limit FPS" cadence), so whether
     it holds the display at a higher rate than the game alone would is a
     device question. Builds B and C answer it;
   - overlay tap latency with "Limit FPS" on and off (run 24). Film a tap on
     an overlay button with a high-speed camera (240 fps or more), and
     measure finger lift to the button's visual change. Or take a Perfetto
     trace with input events and the overlay layer's latch times. The model
     predicts about 3 vsyncs of buffering when unlimited and almost none
     under the limiter. The device's buffer count and GPU time decide the
     real figure.
4. Reading: if B or C closes most of the gap to vanilla, presenting the
   always-on overlay is the cost, and C is the low-risk change; if neither
   moves it, the overlay is ruled out and the next lead is the frozen
   thermal status (sanitizer FINDINGS L10) as an A/B of its own. Memory is
   a separate axis: if the low-RAM phone shows low-memory kills or heavy
   memory pressure with the shipped fonts and not without the CJK merge
   (build D: `enableDroidSans = false`), the font atlas is a cost
   independent of frame rate.
5. Across OEMs: repeat step 3's composition-type check on the Samsung and
   Xiaomi/POCO devices users reported. AOSP notes that devices typically have
   about four overlay planes and fall back to GPU composition beyond that, so
   the extra layer may cost more on some devices than on the OnePlus.

## What is measured

The real overlay frame, as the render thread runs it:
- real: `androidbk.cpp` render loop and UI-thread input bridge, `scroll.cpp`,
  ImGui, `Canvas.cpp`, and `main.cpp`'s `HelpMarker`, `ModStyleScope`,
  `DrawMods` and `CanvasMenu` (extracted verbatim by `run_perf.sh`);
- stand-ins: EGL/GLES, window and JNI (`../sanitizers/harness/app/fake_*.cpp`),
  and the mods, which are small panels of ordinary ImGui widgets (status text,
  six checkbox/slider/button rows, a wrapped note);
- build: as the app (`-O3`, LTO, `NDEBUG`, libc++), no sanitizers;
- screen: 1080x2376 (the audited phone's mode), font 36.75 px, scale 3.15
  (`ImGUI.java`'s `sp2px(14)`, `dp2px(1.2)` at ~420 dpi).

Per frame the render thread records its own CPU time
(`CLOCK_THREAD_CPUTIME_ID`, less sensitive to other load than wall time) at:
frame start -> ImGui NewFrame done (**input**: GL backend new frame, input
replay, ImGui NewFrame) -> menu and mods drawn (**menu**) -> Render and touch
snapshot done (**render**) -> swap (**submit**: walking the draw data, standing
in for GL submission). Results are percentiles over thousands of frames.

What host numbers cannot say: GPU time, compositor cost of the extra layer,
contention with the game's Vulkan rendering, thermal behaviour, ARM CPU speed.
They bound the overlay's CPU work and compare variants; they do not explain the
device symptom by themselves.

## How to run

From the repo root (Git Bash):

```sh
mkdir -p app/src/test/perf/out
MSYS_NO_PATHCONV=1 docker run --rm -e REPS=5 -e FRAMES=3000 \
  -v "$PWD/app/src:/repo:ro" -v "$PWD/app/src/test/perf/out:/out" \
  canvas-sanitizer-harness:local bash /repo/test/perf/run_perf.sh frame
```

Each scenario runs `REPS` times; the run with the median p50 is reported. Raw
lines for every run are in `out/frame_<scenario>_<mods>.log`.

## Results

### Run 1 (2026-10-31, 3 runs x 2000 frames, i9-14900HX, 32 CPUs, load 1.41 -> 2.21)

| Scenario | Mods | CPU/frame p50 | p95 | p99 | Vertices | input | menu | render | submit |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| menu | 0 | 4.2 us | 4.5 | 8.2 | 272 | 1.5 | 1.3 | 0.2 | 1.2 |
| menu | 1 | 10.7 us | 12.1 | 18.4 | 1072 | 1.4 | 5.0 | 0.4 | 3.9 |
| menu | 5 | 34.2 us | 38.1 | 51.9 | 4104 | 1.5 | 17.8 | 0.3 | 14.4 |
| menu | 20 | 121.9 us | 136.9 | 189.4 | 15564 | 1.6 | 65.1 | 0.6 | 54.3 |
| hidden | 5 | 29.1 us | 35.1 | 61.0 | 3540 | 1.5 | 14.8 | 0.3 | 12.4 |
| touch | 5 | 35.2 us | 40.6 | 56.6 | 4104 | 1.6 | 18.6 | 0.3 | 14.6 |
| limited | 5 | 78.0 us | 98.5 | 122.7 | 4112 | 18.1 | 41.3 | 3.2 | 14.8 |

Stage columns are p50 in microseconds. `limited` ran at 41.18 ms intervals
(p95 41.32 ms, about 24 frames/s), Canvas's 41 ms sleep.

Reading:
- Frames ran back to back (no vsync model) except `limited`, so caches stayed
  hot: these are lower bounds. `limited`'s per-frame cost is 2.3x `menu 5`
  because the CPU goes cold during the 41 ms sleep. A `vsync` scenario (frames
  16.7 ms apart) was added after this run to give a realistic 60 Hz figure.
- Cost scales with what is drawn: about 6 us and 800 vertices per stand-in mod
  panel, roughly half in ImGui drawing and half in walking the draw data.
- Touch input adds nothing measurable at the CPU level (input stage 1.5 to
  1.6 us), and hiding the Canvas menu saves little when mods still draw.
- The CPU-per-second comparison originally written here used the back-to-back
  figure and understated the realistic cost; see run 2 for the corrected one.

### Run 2 (2026-10-31, 5 runs x 1000 frames, same host, load 0.58 -> 0.28; only the peer's idle redis/db containers running)

| Scenario | Mods | CPU/frame p50 | p95 | p99 | max | Vertices | input | menu | render | submit | Interval p50 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| menu (back to back) | 5 | 34.2 us | 39.0 | 61.2 | 297.5 | 4104 | 1.5 | 17.9 | 0.3 | 14.4 | 0.03 ms |
| vsync (16.7 ms apart) | 5 | 68.2 us | 92.2 | 129.5 | 176.8 | 4100 | 14.1 | 36.6 | 2.3 | 14.5 | 16.84 ms |

Reading:
- `menu 5` reproduced run 1 exactly (34.2 us p50), so the harness is stable
  under low load.
- At 60 Hz spacing a frame costs 2x the back-to-back figure. The extra lands
  in the stages that run first after the idle gap (input 1.5 -> 14.1 us, menu
  17.9 -> 36.6 us); the draw-data walk is unchanged. That is the pattern of
  cold caches and a down-clocked core between frames, which is how the overlay
  actually runs (vsync-bound), so run 2's figure is the realistic host one.
- Overlay CPU per second, host: about 68 us x 59.4 = 4.0 ms/s at 60 Hz (0.4%
  of one core); with "Limit FPS", 78 us x 24.3 = 1.9 ms/s (run 1). So the
  limiter roughly halves the overlay's CPU use, but from a small base.
- What this rules in or out: on this host the overlay's CPU work is well under
  1% of a core with five mod panels. A phone core several times slower would
  still put it in the low single-digit percent of one core. That makes the
  overlay's CPU work an unlikely main cause of a visible slowdown; its GPU,
  buffer-upload and compositor costs remain unmeasured (queue 3) and are the
  more plausible overlay-side contributors.

### Run 3 (2026-10-31): instruction counts, callgrind, 500 frames per scenario

Load-independent (valgrind 3.22 callgrind in `canvas-perf-harness:local`, built
from `Dockerfile.perf`). The render thread zeroes the counters at the first
measured frame and dumps at the last, so each count covers exactly 500 frames.
Same build as runs 1-2 plus `-g`.

| Scenario | Mods | Instructions/frame | CPU p50 (runs 1-2) | Instructions per us |
|---|---:|---:|---:|---:|
| menu | 0 | 46,309 | 4.2 us | 11.0k |
| menu | 1 | 125,796 | 10.7 us | 11.8k |
| menu | 5 | 403,472 | 34.2 us | 11.8k |
| menu | 20 | 1,450,660 | 121.9 us | 11.9k |
| hidden | 5 | 347,915 | 29.1 us | 12.0k |
| touch | 5 | 405,075 | 35.2 us | 11.5k |

Inclusive share of the frame, `menu 5`: `CanvasMenu` 85.9%, of which
`DrawMods` 73.3%, of which the stand-in mods' own `Draw` 65.0%; ImGui
`NewFrame` about 5.5%.

Reading:
- The instruction ratio between scenarios matches the CPU-time ratio (about
  11-12k instructions per microsecond throughout), so runs 1-2 are confirmed
  by a method that load on the machine cannot distort.
- Where the work goes with five panels: about two thirds is the mods' own
  widget code (stand-ins here; real mods differ). Canvas's own share is the
  menu (window, mod table, help tooltips: about 13%, ~51k instructions) and
  `DrawMods`' wrapper around each mod (style swap via `ModStyleScope`,
  window begin/end: about 8%, ~6.7k instructions per mod).
- Hiding the Canvas menu saves 14% of instructions with five mods; touch input
  adds 0.4%.
- What this rules in or out: Canvas's own per-frame code is a minority of the
  overlay's CPU work, and the whole overlay's CPU work was already small (run
  2). If the overlay matters for the slowdown, it is through GPU, buffer
  upload or composition, not CPU instructions.
- Harness note: the first instruction-count attempt zeroed and dumped from the
  main thread, which valgrind schedules sluggishly, so the dumps covered
  uncounted extra frames and gave inconsistent per-frame numbers; it was
  discarded and the window moved onto the render thread.

### Run 4 (2026-10-31): GL submission per frame, 1 run x 500 frames per scenario

What ImGui's vendored GL backend would submit, counted from each frame's
`ImDrawData` with the backend's own rules (`imgui_impl_opengl3.cpp:545-620`:
one `glBufferData` per draw list carrying its full vertex and index arrays,
every frame; one scissor plus `glDrawElements` per non-clipped `ImDrawCmd`;
20-byte vertices, 16-bit indices). Exact for this backend and this geometry;
no GL driver involved. Host load 0.78 before and after; only the peer's idle
redis/db containers running.

| Scenario | Mods | Draw lists | Draw calls | Upload per frame | Upload per second at 60 Hz |
|---|---:|---:|---:|---:|---:|
| menu | 0 | 1 | 2 | 6.2 KB | 0.37 MB |
| menu | 1 | 2 | 4 | 24.3 KB | 1.4 MB |
| menu | 5 | 6 | 12 | 92.8 KB | 5.4 MB |
| menu | 20 | 21 | 42 | 351.8 KB | 20.6 MB |
| hidden | 5 | 5 | 10 | 79.9 KB | 4.7 MB |
| touch | 5 | 6 | 12 | 92.8 KB | 5.4 MB |

The per-frame CPU timings in this run (single repetition) matched runs 1-2
within 1 us for every scenario.

Reading:
- ImGui's geometry is light: with five mod panels, 12 draw calls and about
  93 KB of buffer uploads per frame.
- For scale (nominal arithmetic from Canvas's code, not a device
  measurement): `renderloop` clears the full overlay surface every frame
  (`androidbk.cpp:515-517`). At 1080x2376 RGBA8 that is about 10.3 MB of
  pixels per frame, about 616 MB per second at 60 Hz before any compression,
  over 100x the geometry uploads, and the resulting full-screen translucent
  layer is then composited every frame. On the audited phone the buffers were
  compressed (UBWC) and composited in hardware (Codex audit), so real traffic
  is lower; how much lower is a device question. On tile-based mobile GPUs a
  clear at the start of a render pass is close to free (Arm's and Meta's
  guidance: clear-on-load costs no memory read, and skipping it would force
  one). So the cost to watch is not the `glClear` call itself but writing
  the whole surface out and the compositor reading it back, once per
  present. Presenting less often (the skip-unchanged variant) removes that;
  dropping the clear does not.
- What this rules in or out: if the overlay costs the GPU or memory system
  anything noticeable, it comes from drawing and presenting a full-screen
  layer every frame whether or not anything changed, not from ImGui's
  geometry or Canvas's CPU work. The variant worth evaluating is therefore
  one that does not render or present unchanged frames.

### Analysis (pass 6): why "Limit FPS" paces unevenly, and what the device numbers imply

No new measurement; a model from Canvas's code, run 1's host interval, and the
Codex audit's device cadence (Codex's numbers, not re-measured here).

- With "Limit FPS" on, each `renderloop` iteration sleeps 41 ms first
  (`androidbk.cpp:506`), then builds and submits the frame, then swaps. A
  frame can only be shown at a vsync, every 16.67 ms at 60 Hz, so the gap
  between shown frames is a whole number of vsync periods. An iteration of
  41-43 ms spans 2.46-2.6 periods, so the gaps alternate between 2 periods
  (33.3 ms) and 3 periods (50 ms). That is the "predominantly 33/50 ms"
  pattern the audit recorded, produced by the limiter's design rather than by
  load.
- Host (run 1): 41.18 ms per iteration, so about 47% of gaps would be 50 ms.
- Device (Codex): 23.07-23.12 overlay presentations per second, i.e. about
  43.3 ms per iteration, about 60% 50 ms gaps and 40% 33 ms gaps.
- Inference: on that phone each limited iteration took about 2.3 ms beyond
  the 41 ms sleep (43.3 - 41.0). That figure bundles the overlay frame's
  work, sleep overshoot (timer slack and scheduling) and any wait in
  `eglSwapBuffers`; it cannot separate them. It is an upper bound for the
  overlay's per-frame time on that device in that light scene, and it is
  far above the host's 0.07 ms of frame work, which is expected for a phone
  core, a real GL driver and real scheduling.
- Design note for queue 5: a limiter aligned to vsync (presenting every
  second vsync for a steady 30 frames/s) would remove the 33/50 alternation.
  The natural tool is `eglSwapInterval(2)` on the current surface, but
  Canvas's only `eglSwapInterval` call is the misplaced one recorded as
  sanitizer finding F15, which fails before any context exists.

### Run 5 (2026-10-31): redundant frames, 1 run x 1000 frames per scenario

Each frame's submitted geometry and commands are hashed (FNV-1a over vertex
and index buffers and every draw command); a frame equal to the previous one
drew exactly the same thing, so its clear, render and present were redundant.
Counts do not depend on machine load. A peer Tibik campaign container was
running (load 1.56 -> 1.37): this run's CPU timings came out about 3x runs
1-2 (e.g. `menu 5` 110 us vs 34 us) and are not used.

| Scenario | Mods | Vertices/frame | Frames identical to the previous | Longest identical run |
|---|---:|---:|---:|---:|
| menu (back to back) | 0 | 272 | 3 (0.3%) | 1 |
| menu (back to back) | 5 | 4104 | 22 (2.2%) | 3 |
| touch | 5 | 4104 | 18 (1.8%) | 2 |
| vsync (16.7 ms apart) | 5 | 4100 | 401 (40.1%) | 8 |
| hidden (menu hidden, static mod panels) | 5 | 3540 | 999 (100%) | 999 |
| hidden (menu hidden, no mod windows) | 0 | 0 | 999 (100%) | 999 |

Reading:
- With the Canvas menu open, what changes from frame to frame is mainly its
  "Application average ... FPS" readout (`main.cpp:214-218`): back to back,
  almost every frame differs; at 60 Hz spacing 40% of frames repeat, because
  the one-decimal readout holds for a few frames at a time.
- With the menu hidden and mods showing static panels, every frame repeats.
  With the menu hidden and no mod windows open, the overlay draws nothing at
  all (0 vertices), yet `renderloop` still clears the full surface and
  presents it every frame. That matches the audit's device observation of
  about 60 overlay presentations per second whatever was on screen.
- What this rules in or out: the overlay's per-frame CPU work is small (runs
  2-3), but in the common in-game state (menu hidden) essentially all of its
  frames are redundant. The open question for the device is therefore not
  ImGui's cost but the cost of presenting a full-screen translucent layer at
  display rate while it shows nothing; the overlay-absent comparison the audit
  proposed is the test, and a skip-unchanged-frames variant (next queue item)
  is the design it would evaluate.

### Run 6 (2026-10-31): DESIGN VARIANT "skip unchanged frames", 1 run x 900 frames, 16.7 ms spacing

Variant, not Canvas's code: a patched scratch copy of `androidbk.cpp` built by
`stage_variant` (`app/src/main` untouched). After `ImGui::Render()` it hashes
the draw data; if nothing changed since the last presented frame it skips
`glClear`, GL submission and `eglSwapBuffers` and waits one vsync period
instead. ImGui itself still runs every frame. Header:
`harness/variant_skip_unchanged.h`.

| Scenario | Baseline presents/s | Variant presents/s | Frames skipped by variant |
|---|---:|---:|---:|
| menu, 5 mods | 59.0 | 40.7 | 31% |
| hidden, 5 static mod panels | 59.2 | 0.0 | all but the first |
| hidden, nothing drawn | 59.5 | 0.0 | all but the first |

Present counts do not depend on load. The CPU columns of this run are
**invalid** and not reported, for two reasons: a peer Tibik campaign container
started during the run (only redis/db at the start), and the harness's own
per-frame draw-data hash (added in pass 5) was being timed inside the
"submit" stage. That hash is a byte-wise FNV over about 90 KB per frame with
five mods, so it inflated every CPU figure since pass 5 (run 5's timings were
already discarded for load). Fixed in this pass: the driver now measures its
own counting and hashing at point 3 and subtracts it from the frame's CPU and
wall time. The variant's own hash is part of the variant and stays in its
numbers; a real implementation would want a cheaper change signal.

Reading:
- With the menu hidden, the variant presents nothing while nothing changes,
  where Canvas today presents a full-screen layer about 59 times a second.
  With the menu open, the FPS readout still forces about 41 presents per
  second; a slower-updating readout would cut that further.
- What this rules in or out: the design can remove essentially all overlay
  presents in the common in-game state. Whether that matters on a phone (the
  GPU, memory and compositor cost of the presents it removes) is outside what
  a host can show; this is the variant the overlay-absent device comparison
  would decide on.

### Run 7 (2026-10-31): DESIGN VARIANT CPU comparison, corrected driver, 4 interleaved rounds x 480 frames, 16.7 ms spacing

Baseline and variant alternate within each round (A,B then B,A) and both are
pinned to one CPU (`taskset -c 2`), so they absorb the same drift; medians and
[min-max] over rounds. Load 0.74 -> 1.03; a peer Tibik campaign container had
started by the end (only redis/db at the start); the interleaving spreads its
effect across both builds. The driver subtracts its own analysis (pass 8 fix).

| Scenario | Build | Render-thread CPU per second | Presents/s | Frame CPU p50 |
|---|---|---:|---:|---:|
| menu, 5 mods | baseline | 4.34 ms [4.17-4.38] | 59.1 | 71.6 us |
| menu, 5 mods | variant | 9.40 ms [8.42-10.46] | 38.3 [34.7-42.5] | 148.8 us |
| hidden, 5 mods | baseline | 3.48 ms [3.14-3.51] | 59.2 | 56.6 us |
| hidden, 5 mods | variant | 6.83 ms [6.31-7.75] | 0.0 | 112.0 us |
| hidden, 0 mods | baseline | 1.11 ms [0.90-1.22] | 59.6 | 17.8 us |
| hidden, 0 mods | variant | 1.01 ms [0.87-1.17] | 0.0 | 16.0 us |

Reading:
- The corrected baseline agrees with run 2 (71.6 us vs 68.2 us per frame at
  60 Hz spacing), so the pass-8 fix restored comparability.
- The variant removes every present when nothing changes, as in run 6, but as
  written it roughly doubles the overlay's CPU use when mod panels are drawn
  (+3 to +5 ms per second on this host). Its byte-wise FNV hash of about
  90 KB of geometry per frame costs more than the work it skips here, where
  that work is only a stand-in for GL submission. With nothing drawn the hash
  is free and the variant matches baseline.
- What this rules in or out: skipping presents works, but the change test
  must be cheap. A comparison against the previous frame's buffers (memcmp at
  memory bandwidth) or a word-wise hash should cost a small fraction of
  today's; that is the next variant. The CPU it saves on a device (real GL
  submission, driver work) is not visible on this host.

### Run 8 (2026-10-31): DESIGN VARIANT with a cheap change test (memcmp), 4 interleaved rounds x 480 frames, 16.7 ms spacing

Same protocol as run 7 (interleaved, pinned, medians [min-max]). The variant
now compares each draw list with a kept copy of the previous frame's (memcmp
of vertices, indices and command keys) instead of hashing every byte, and
re-copies only lists that changed (`-DVARIANT_MEMCMP`, now the default).
Load 0.73 -> 0.09; no other containers running during the run.

| Scenario | Build | Render-thread CPU per second | Presents/s | Frame CPU p50 |
|---|---|---:|---:|---:|
| menu, 5 mods | baseline | 5.37 ms [3.89-7.58] | 59.0 | 94.5 us |
| menu, 5 mods | variant | 6.55 ms [4.18-8.16] | 38.9 [31.0-45.8] | 108.1 us |
| hidden, 5 mods | baseline | 4.35 ms [3.31-5.71] | 59.1 | 75.4 us |
| hidden, 5 mods | variant | 4.64 ms [2.95-6.31] | 0.0 | 76.0 us |
| hidden, 0 mods | baseline | 1.11 ms [0.98-2.17] | 59.4 | 18.0 us |
| hidden, 0 mods | variant | 1.38 ms [1.01-2.31] | 0.0 | 21.3 us |

Reading:
- Presents: unchanged from run 6. Nothing presented while the menu is
  hidden; about 39 per second with it open (the FPS readout).
- CPU: this run was noisy (baseline spreads up to 2x between rounds although
  no container was running, so the cause is outside Docker), and the
  variant's ranges overlap baseline's in every scenario. With the cheap test
  the variant's overhead is within that noise, where the byte-wise hash
  cost +3 to +5 ms per second (run 7). An exact figure needs instruction
  counts, which load cannot move (next).
- A first attempt this pass presented on every frame: the command-key struct
  had 4 bytes of uninitialised tail padding, so memcmp never matched. Fixed
  with an explicit zeroed field and a `static_assert`; that attempt is not
  recorded.

### Run 9 (2026-10-31): DESIGN VARIANT overhead in instructions, callgrind, 300 frames per build

`stage_variant_instr` (canvas-perf-harness:local): baseline and the memcmp
variant built with `-g`, counted over exactly 300 measured frames each. Not
affected by machine load (a peer Tibik campaign and an image build were
running). Both builds include the perf driver's own per-frame analysis (GL
counting and a byte-wise hash, about 360k instructions with five mods, added
in passes 4-5), so absolute numbers are above run 3's; the difference between
the two builds is what this run measures.

| Scenario | Baseline instructions/frame | Variant | Difference |
|---|---:|---:|---:|
| menu, 5 mods (frames change) | 762,738 | 787,349 | +24,611 |
| hidden, 5 mods (nothing changes) | 657,514 | 643,595 | -13,919 |
| hidden, 0 mods (nothing drawn) | 26,208 | 26,233 | +25 |

Reading:
- When frames change, the variant's compare-and-copy costs about 25k
  instructions per frame, about 6% of Canvas's own frame (run 3: 403k) and
  roughly 2 us on this host, about 0.13 ms of CPU per second at 60 Hz.
- When nothing changes, it is cheaper than baseline, because the skipped
  frames do not submit; on a device the skipped work also includes the real
  GL driver, the GPU and composition, none of which this host sees.
- What this rules in or out: a skip-unchanged-frames design need not cost
  meaningful CPU. Its benefit (no presents while idle) and its cost are now
  both measured on the host; whether removing those presents helps on a phone
  remains the device question.
- Harness note: for absolute instruction numbers, the driver's own analysis
  should be switchable off in instruction runs (queue).

### Run 10 (2026-10-31): "Limit FPS" on a vsync-latched display, Canvas's 41 ms sleep vs DESIGN VARIANT (one-vsync wait), 1 run x 300 frames each

`stage_limiter`, `PERF_VSYNC_ALIGNED=1`, scenario `limited`, 5 stand-in mods,
menu open. Real: `renderloop` and everything it calls (Canvas's `usleep(41000)`
included). Stand-ins: the display (a virtual 60 Hz vsync clock) and EGL. In
this model **`eglSwapBuffers` blocks until the next vsync** and the frame is
shown there. The variant is a patched scratch copy of `androidbk.cpp` that
replaces the sleep with a wait for the next vsync
(`harness/variant_vsync_limiter.h`). Host: i9-14900HX, 32 CPUs. Load
4.56 -> 2.73. A peer Tibik campaign started during the run, so the CPU columns
are indicative only. Presents and gaps are counted on the virtual clock and do
not depend on load.

| Build | presents/s | present gaps (vsync periods) | frame CPU p50 / p95 / p99 / max (µs) |
|---|---:|---|---|
| Canvas (41 ms sleep) | 20.0 | 3: 299 of 299 | 73 / 95 / 119 / 171 |
| variant (one-vsync wait) | 30.0 | 2: 299 of 299 | 117 / 142 / 169 / 172 |

Reading:
- In this model every Canvas iteration is the 41 ms sleep plus a wait for the
  next vsync, so every gap is 3 periods (50 ms, 20 frames/s). The variant
  gives a steady 2 periods (30 frames/s).
- **The model does not match the device evidence for Canvas.** The Codex
  audit measured about 23.1 overlay presents/s, with mostly 33 and 50 ms
  gaps. That is what a swap that returns without waiting produces: 41-43 ms
  iterations latched at the next vsync give alternating 2- and 3-period gaps
  (pass 6 analysis). With a non-async BufferQueue the producer blocks only
  when no free buffer is left, in `dequeueBuffer` rather than necessarily
  in `eglSwapBuffers` (AOSP BufferQueueProducer change "queueBuffer() CPU
  throttling only in async mode"). A loop running below the refresh rate never
  fills the queue, so it does not block. So the 20/s here is an artefact of
  the blocking-swap model. The variant's steady 30/s also relies on it:
  without a blocking swap, a single vsync wait limits nothing.
- What this rules in or out: nothing new about the overlay's CPU cost. It
  shows that the limiter's pacing depends on swap semantics, so the next run
  uses a queued-swap model (swap returns at once, one latch per vsync, block
  only when the queue is full). It also compares a variant built on
  `eglSwapInterval(2)`, which holds each frame for two vsyncs either way.

### Run 11 (2026-10-31): DESIGN VARIANT FPS readout latched twice a second, menu open, 4 interleaved rounds x 480 frames, 16.7 ms spacing

`stage_readout`, `PERF_VSYNC=1`, scenario `menu`, 5 stand-in mods, rounds
interleaved (A,B,C then C,B,A), pinned to one CPU. Medians [min-max] over
rounds. Real: `renderloop`, `CanvasMenu` and ImGui. Stand-ins: the mods
(static panels), EGL and the display. Builds:
- baseline: Canvas as is;
- variant: skip clear/submit/present when the draw data is unchanged
  (memcmp, as in run 8);
- variant_readout: the same, plus a patched scratch copy of the `CanvasMenu`
  extraction whose "Application average ... FPS" text shows `io.Framerate`
  latched every 0.5 s (`harness/variant_readout_latch.h`).

Host: i9-14900HX. Load 2.19 -> 1.19. A peer Tibik campaign was running for
part of the run (it had ended by the next check), so the CPU columns need a
repeat. Presents are counted, not timed.

| Build | presents/s | render-thread CPU ms/s | frame CPU p50 (µs) |
|---|---:|---:|---:|
| baseline | 59.15 [59.10-59.20] | 4.17 [3.91-4.31] | 68.8 [64.5-70.6] |
| variant | 29.80 [29.10-31.20] | 3.91 [3.60-4.03] | 65.0 [60.3-67.5] |
| variant_readout | 1.85 [1.70-2.00] | 3.49 [3.16-3.76] | 57.4 [49.7-63.0] |

Reading:
- With the menu open and nothing else changing, the per-frame FPS text is
  what keeps the overlay presenting. Once it updates twice a second, presents
  drop from about 59/s (Canvas) to about 2/s, one per readout change.
- The plain variant's rate with the menu open depends on how often the
  `%.3f ms/frame` text repeats. That depends on frame-time jitter: 40.7/s in
  run 6, 29.8/s here. It is a host property, not a device estimate.
- CPU: the latched readout lowers render-thread CPU slightly (skipped frames
  do no GL submission). The differences are within about 0.7 ms/s, under
  partly changing load. This rules nothing in about CPU cost; the change it
  makes is in presents.

### Run 12 (2026-10-31): UI-thread time in `wantsMouse`/`wantsKeyboard` while mods draw, 5 runs per setting

Scenario `uilatency`, menu open. The UI thread submits a touch position every
250 µs (`submitPositionEvent`), then times one `wantsMouse` and one
`wantsKeyboard` call. These are the JNI queries GameActivity makes per touch.
Both take mutexes that the render thread also takes (CanvasInput's queue and
snapshot locks). Real: the JNI entry points, CanvasInput, `renderloop`,
`CanvasMenu`, ImGui. Stand-ins: mods (static panels), EGL, the display, and
the touch source. Each figure is the UI thread's wall time inside one call.
Host: i9-14900HX.

| Setting | calls per run | wantsMouse p50 / p99 / p99.9 / max (µs), over 5 runs | wantsKeyboard max (µs) |
|---|---:|---|---:|
| 20 mods, render thread unpaced (worst case contention; load 1.39 -> 1.36) | 1214-1320 | 0.08-0.09 / 0.46-0.49 / 0.56-1.39 / 1.3-12.3 | 0.6-1.1 |
| 5 mods, unpaced (same run) | 340-375 | 0.08-0.09 / 0.43-0.62 / 0.49-2.73 / 0.5-55.7 | 0.3-0.4 |
| 20 mods, render thread at 60 Hz (`PERF_VSYNC=1`; load 1.22 -> 1.22) | 15411-15958 | 0.07-0.08 / 0.46-0.51 / 0.66-1.24 / 1.5-33.7 | 1.2-28.7 |

Render thread in the 60 Hz runs: frame CPU p50 169-179 µs, p99 209-295 µs,
max 312-472 µs. The input stage is about 15 µs, because each frame drains
about 66 queued touch events.

Load: a peer Tibik campaign started during the 60 Hz runs, so their maxima
need a repeat. In the unpaced runs few calls happen before the run ends, so
p99.9 there is close to the top two or three samples.

Reading:
- The UI thread spends under 1.3 µs at p99.9 in either query, even while the
  render thread runs flat out with 20 mods. The worst single call seen was
  56 µs, three orders of magnitude below a 16.7 ms frame and five below the
  5 s input-dispatch timeout.
- What this rules in or out: on this host, Canvas's input locks do not hold up
  the UI thread. It does not cover a phone's scheduler: a preempted render
  thread holding the lock, priority inversion, or little cores. Those would
  show as long `wantsMouse` slices in a device trace (Device test plan).

### Run 13 (2026-10-31): "Limit FPS" pacing on two display models, Canvas vs two DESIGN VARIANTS, 1 run x 300 frames each

`stage_limiter`, scenario `limited`, 5 stand-in mods, menu open. Real: as in
run 10. Two display models (`fake_platform.cpp`):
- blocking (`PERF_VSYNC_ALIGNED=1`): the swap returns when its buffer is
  latched, as in run 10;
- queued (`PERF_SWAP_QUEUED=1`): a non-async BufferQueue. The swap returns at
  once, the display latches one buffer per vsync, and the producer blocks only
  while 2 buffers are waiting.

Both models honour the swap interval. Builds:
- canvas: as is (41 ms sleep);
- variant: the sleep replaced by a wait for the next vsync;
- si2: the sleep replaced by `eglSwapInterval(2)` while the option is on.
  It stands for two-vsync pacing: AOSP's `Surface` clamps the swap interval
  to 1, so on Android this pacing would come from Swappy, not from this call.

Host: i9-14900HX. Load 1.69 -> 0.96. A peer Tibik campaign ran for part of
the run, which affects only the CPU column. Presents and gaps are counted on
the virtual clock.

| Model | Build | presents/s | gaps in vsync periods (of 299) | iteration p50 | frame CPU p50 / p95 / p99 / max (µs) |
|---|---|---:|---|---:|---|
| blocking | canvas | 20.0 | 3: 299 | 50.00 ms | 74 / 96 / 102 / 167 |
| blocking | variant | 30.0 | 2: 299 | 33.33 ms | 75 / 98 / 111 / 158 |
| blocking | si2 | 30.0 | 2: 299 | 33.34 ms | 75 / 105 / 174 / 345 |
| queued | canvas | 24.2 | 2: 156, 3: 143 | 41.25 ms | 74 / 96 / 126 / 168 |
| queued | variant | 60.0 | 1: 299 | 16.67 ms | 72 / 98 / 119 / 139 |
| queued | si2 | 30.0 | 2: 299 | 33.33 ms | 75 / 97 / 113 / 166 |

Reading:
- The queued model reproduces the device pattern for Canvas's limiter. The
  41 ms sleep plus frame work gives 41.25 ms iterations latched at the next
  vsync, so gaps alternate 33 ms (52%) and 50 ms (48%), at 24.2 presents/s.
  The Codex audit measured about 23.1/s on the phone, with mostly 33/50 ms
  gaps; its longer iterations (about 43.3 ms) shift the mix toward 50 ms.
  The blocking model (run 10) does not reproduce it.
- Check against arithmetic (pass 25). An iteration lasting k vsync periods,
  with 2 < k < 3, gives a 3-period gap in a fraction k - 2 of frames.
  - Host: 41.25 ms = 2.475 periods predicts 47.5% 50 ms gaps and 24.24
    presents/s; measured 47.8% (143 of 299) and 24.2/s.
  - Device: the audit's 43.3 ms = 2.598 periods predicts 59.8% 50 ms gaps
    and 23.09/s; the audit measured about 23.1/s.
  The limiter targets about 24 frames/s on a 60 Hz panel, the case that
  needs 3:2 pulldown for 24 fps film. Its unevenness is the same alternating
  2- and 3-refresh hold.
- So "Limit FPS" as written gives uneven pacing (judder between 30 and 20
  frames/s) by design, not because of load. The unevenness is in when the
  overlay's frames are shown, not in how much CPU they cost: per-frame CPU is
  the same in every row (p50 72-75 µs).
- Two-vsync pacing (si2) gives a steady 30/s in both models, as Swappy would
  on a device. Waiting for one vsync before the frame gives 30/s only if the
  swap blocks; with a queued swap it limits nothing (60/s). That design is
  model-dependent and is dropped.
- What this rules in or out: the limiter's judder is explained without any
  CPU cost from the overlay, which rules out "the overlay is too slow" as
  its cause. Whether the judder is visible in game depends on how the
  overlay layer is composed with the game's own frames, which only the
  device can show (Device test plan).

### Run 14 (2026-10-31): DESIGN VARIANT skip-unchanged under the queued-swap model, 4 interleaved rounds x 480 frames

`MODEL=queued run_perf.sh variant` (queue 9). As runs 7-8, but on the queued
display model of run 13: the swap returns at once, one latch per vsync, and
it blocks only with 2 buffers waiting. A skipped frame waits for the next
vsync boundary, as a Choreographer callback would. Real: `renderloop`,
`CanvasMenu`, ImGui. Stand-ins: mods (static panels), EGL, the display.
Pinned to one CPU. Host: i9-14900HX. Load 1.63 -> 0.17. A Tibik campaign ran
for part of the run, so the CPU columns need a repeat; presents are counted.

| Scenario | Build | presents/s | render-thread CPU ms/s | frame CPU p50 (µs) |
|---|---|---:|---:|---:|
| menu, 5 mods | baseline | 60.00 [60.00-60.00] | 6.30 [4.60-6.90] | 104.6 [74.3-126.7] |
| menu, 5 mods | variant | 55.60 [53.00-56.50] | 5.28 [5.12-5.87] | 73.6 [70.2-78.9] |
| hidden, 5 mods | baseline | 60.00 [60.00-60.00] | 3.58 [3.21-4.42] | 58.1 [50.1-64.4] |
| hidden, 5 mods | variant | 0.00 [0.00-0.00] | 3.11 [3.03-3.37] | 50.3 [49.0-54.3] |
| hidden, nothing drawn | baseline | 60.00 [60.00-60.00] | 1.38 [1.29-1.52] | 21.2 [19.8-21.4] |
| hidden, nothing drawn | variant | 0.00 [0.00-0.00] | 1.45 [1.36-1.67] | 21.2 [20.3-23.3] |

Reading:
- Menu hidden: as predicted (pass 29) and as in run 6. Canvas presents a
  full-screen layer every vsync; the variant presents nothing while nothing
  changes. The display model does not change that.
- Menu open: **the prediction was wrong.** The variant presents 55.6/s here,
  against 29.8/s under the sleep model (run 11). In the queued model a
  presented frame returns at once, so the next iteration starts right away
  with a very short `io.DeltaTime`; a skipped frame then waits a full vsync.
  That uneven mix moves the 60-frame average on most frames, so the
  `%.3f ms/frame` text changes and the frame presents.
- How often the menu-open overlay presents under a skip-unchanged design
  depends on the frame-time pattern. Runs 6, 11 and 14 give 41, 30 and 56
  per second. Only a readout that updates on its own clock (run 11's latched
  readout: about 2/s) makes the result independent of timing. For a design,
  that means the FPS text has to be latched, or excluded from the change
  test.
- CPU: within the layout and load noise noted in pass 31; no CPU claim.

### Run 15 (2026-10-31): run 12 repeated on a quiet machine, 20 mods, 5 runs per setting

Queue 8. Same scenario and stand-ins as run 12. The unpaced setting now
uses `PERF_UI_CALLS=10000` (pass 26), so every run has at least 10,000 UI
calls and p99.9 rests on about 10 samples above it. No other containers
apart from Tibik's idle redis/db. Load 0.39 -> 0.56 (unpaced) and
0.69 -> 0.27 (60 Hz).

| Setting | calls per run | wantsMouse p50 / p99 / p99.9 / max (µs), over 5 runs | wantsKeyboard max (µs) |
|---|---:|---|---:|
| render thread unpaced | 10,000 | 0.06-0.07 / 0.42-0.46 / 0.65-1.30 / 2.1-49.5 | 1.0-10.2 |
| render thread at 60 Hz | 15,744-15,995 | 0.07-0.08 / 0.42-0.45 / 0.70-0.98 / 27-181 | 6.1-197 |

Render thread at 60 Hz: frame CPU p50 172-176 µs, p95 204-413 µs, p99
343-551 µs, max 393-840 µs. Unpaced: p50 123-125 µs, max 412-537 µs.

Reading:
- The medians and p99 match run 12. The p99.9 is now well sampled: under
  about 1.3 µs in every run.
- The 60 Hz maxima are larger than in the unpaced runs: one call per run,
  out of about 16,000, takes 0.03-0.2 ms. The cause is not established here.
  The UI thread may meet the render thread inside its locks while a frame
  is drained, or it may be preempted. Even the worst, 0.2 ms, is about 1% of
  a 16.7 ms frame.
- What this rules in or out: unchanged from run 12. Canvas's input locks do
  not hold up the UI thread on this host. Lock waits behind a preempted
  render thread on a phone are a device question (trace `wantsMouse` slices).

### Run 16 (2026-10-31): run 11 repeated (FPS readout latched twice a second), 4 interleaved rounds x 480 frames

Same builds and setup as run 11 (`stage_readout`, `PERF_VSYNC=1`, menu
open, 5 stand-in mods, pinned to one virtual CPU). Load 2.31 -> 0.52. A
Tibik campaign started just before the run and ended during it, so this is
not the quiet repeat queue 8 asked for. The pass 35 caveat about hybrid
cores also applies.

| Build | presents/s | render-thread CPU ms/s | frame CPU p50 (µs) |
|---|---:|---:|---:|
| baseline | 59.10 [58.90-59.20] | 4.28 [4.00-5.46] | 71.1 [65.8-81.8] |
| variant | 33.30 [32.40-36.60] | 4.17 [4.01-4.25] | 69.0 [63.6-70.7] |
| variant_readout | 1.95 [1.60-2.00] | 3.73 [3.53-4.67] | 60.4 [57.2-68.9] |

Reading: presents reproduce run 11 (59.15 / 29.8 / 1.85). The CPU ordering
also repeats: the latched-readout build uses about 0.5 ms/s less, because
it skips GL submission on almost every frame. That difference sits inside
the min-max ranges, and two runs under changing load with core placement
unknown cannot settle it, so no CPU claim. The presents result is the one
that stands: about 2/s with the readout latched, against 59/s for Canvas.

### Run 17 (2026-10-31): instructions per presented and per skipped frame, menu open, 5 mods, callgrind, 300 frames per build

`stage_readout_instr` (`canvas-perf-harness:local`), pass 37's method. The
builds are those of runs 11/16, with `-g`, and the driver's own analysis is
off (`PERF_NO_ANALYSIS`). Instruction counts do not depend on load, frequency
or core type (pass 35). Two earlier attempts died with a clang segfault
during the build; the third built and ran cleanly.

| Build | instructions/frame | window frames presented |
|---|---:|---:|
| baseline | 404,677 | 299 of 299 |
| variant (skip unchanged) | 428,186 | 289 of 299 |
| variant_readout (latched FPS text) | 388,310 | 0 of 299 |

Split: a presented frame costs 429,566 instructions and a skipped frame
388,310. That assumes each kind costs the same in both variant builds;
variant_readout presented nothing, so its figure is the skipped-frame cost
directly. Against baseline, a presented frame in the variant costs +24.9k
(the compare and copy, as in run 9's +24.6k) and a skipped frame -16.4k.
Weighted by run 16's 60 Hz mix (59.1 frames/s):

| Build | presents/s (run 16) | instructions/s |
|---|---:|---:|
| baseline | 59.1 | 23.9M |
| variant | 33.3 | 24.3M |
| variant_readout | 1.95 | 23.0M |

Reading:
- The baseline matches run 3 (403,472/frame), which had no analysis either.
- In instructions, the three designs are within 4% of each other: the
  latched readout saves 0.9M/s (about 0.08 ms/s at run 3's ~11.8k
  instructions per µs), and the plain variant costs 0.4M/s more.
  Runs 11 and 16 timed a larger gap (about 0.5 ms/s). The instruction count
  misses memory traffic from the draw-data copy and compare, and those timed
  runs carry the layout and load noise noted in passes 31 and 35. So the CPU
  difference is small either way and not a reason to pick a design.
- The host's GL calls are stand-ins that cost almost nothing, so this
  understates what a skipped frame saves where a real driver runs. What
  separates the designs is presents: 59/s against 2/s. Their cost is GPU and
  compositor work, which only the device can measure.
- What this rules in or out: it settles the open CPU question of runs 11/16
  (no meaningful CPU difference on the host) and confirms run 3's per-frame
  cost by an independent run.

### Run 18 (2026-10-31): DESIGN VARIANT skip-unchanged with one mod showing a live value, 2 interleaved rounds x 480 frames

`stage_variant` (sleep model, `PERF_VSYNC=1`, pinned to one virtual CPU)
with `PERF_DYNAMIC_MODS=1`. One of the five stand-in mods also shows a live
value: the frame number (changes every frame) or seconds to one decimal
(changes 10 times a second). Real: `renderloop`, `CanvasMenu`, `DrawMods`,
ImGui. Stand-ins: the mods, EGL, the display. No source was found that Sky
mods show live values (pass 40), so this models possible mods, not known
ones. Load 1.53 -> 0.10 and 0.61 -> 0.06; Tibik's idle redis/db only. The
frame-CPU column was lost to a harness bug (pass 44), so CPU is not
reported.

| Live value | Scenario | baseline presents/s | variant presents/s |
|---|---|---:|---:|
| every frame | menu open, 5 mods | 58.95 | 58.60 |
| every frame | menu hidden, 5 mods | 58.90 | 58.95 |
| 10 times a second | menu open, 5 mods | 58.70 | 41.00 [34.2-47.8] |
| 10 times a second | menu hidden, 5 mods | 59.05 | 10.00 [10.0-10.0] |
| (any) | menu hidden, no mods | 59.45 | 0.00 |

Reading:
- The variant presents exactly as often as the fastest-changing visible
  content changes: once per change of the 10 Hz value, every frame for a
  per-frame value, and with the menu open as often as the FPS readout moves
  (run 11).
- So the skip-unchanged result of runs 6 and 14 (0 presents while idle) holds
  only while no visible mod panel shows a changing value. One mod with a
  per-frame value brings the overlay back to presenting every vsync. A mod
  updating at 10 Hz still cuts presents from 59/s to 10/s.
- What this rules in or out: it bounds the design. It cannot remove presents
  that the content itself demands, so its benefit on a device depends on
  what the installed mods draw. That is a property of the mods and is not
  known here. For content that changes, partial updates (damage regions,
  pass 42) are the matching technique.

### Run 19 (2026-10-31): font atlas in the shipped configuration (CJK fallback merged), startup and per-frame cost

Found while looking for an unmeasured cost. `ImGUI.java:68` calls
`init(..., enableDroidSans = true)`, so the app always merges
`DroidSansFallback.ttf` with `GetGlyphRangesChineseSimplifiedCommon()` into
the atlas (`androidbk.cpp:568-631`). Every earlier run passed `JNI_FALSE`.
ImGui's font documentation advises against that range unless oversampling
is 1 and the size is small. Canvas uses the default oversampling at
`Utils.sp2px(14)`, which is 36.75 px on the audited phone, as in the harness.
Real: `loadFonts`, `Build()` and the font files (Roboto stands in as the
bundled Lato; the CJK font is the app's own asset, served through the
harness's new `FAKE_ASSET_DIR`). Host: i9-14900HX; a Tibik campaign ran
during part of the timing runs.

| Fonts | atlas | glyphs | as RGBA32 | atlas built after `ImGUI_init` (3 runs) | instructions/frame, menu + 5 mods (callgrind) |
|---|---|---:|---:|---|---:|
| without the CJK merge (earlier runs) | 512x1024 | 467 | 2.0 MB | 3.4-3.6 ms | 404,701 |
| shipped (`enableDroidSans=true`) | 2048x4096 | 3,352 | 32.0 MB | 41.3-49.3 ms | 404,718 |

Reading:
- Per-frame cost does not change: 404,701 vs 404,718 instructions. Every
  earlier per-frame result holds for the shipped configuration.
- Startup and memory do change. Building the atlas takes about 12x longer
  (host, one-off). The atlas is 16x larger: 8 MB as alpha.
- From source, not measured on the host, whose GL backend is a stand-in:
  the real GL3 backend converts the atlas with `GetTexDataAsRGBA32` and
  uploads it (`imgui_impl_opengl3.cpp:668`, whose comment notes that 75% of
  that memory is wasted). ImGui keeps both CPU-side copies until
  `ClearTexData()`, which Canvas never calls. So the shipped overlay holds
  about 8 MB + 32 MB of CPU memory plus a 32 MB texture. Without the CJK
  merge that would be about 0.5 MB + 2 MB plus 2 MB.
- What this rules in or out: not a per-frame CPU cause. It is a fixed memory
  cost of roughly 70 MB that every user pays whether or not they read
  Chinese, and a one-off startup cost. On a device with little RAM, memory
  pressure can show up as slowness elsewhere; whether it does is a device
  question (Device test plan: compare process memory with and without the
  merge).

### Run 20 (2026-10-31): host speed calibration across all 32 virtual CPUs (pass 50)

`harness/calib.c`: a dependent chain of 200M integer adds, five repetitions,
fastest kept, pinned with `taskset` to each virtual CPU in turn, two rounds.
It reports adds per ns, about the effective clock in GHz. From now on
`environment()` prints it for CPU 2 (the CPU the interleaved stages pin to)
before every stage. Host: i9-14900HX in the Docker Desktop VM; Tibik's idle
redis/db only.

| Round | min | max | values below 5.1 |
|---|---:|---:|---:|
| 1 | 5.04 | 5.54 | 3 of 32 |
| 2 | 5.11 | 5.55 | 0 of 32 |

Reading:
- A busy single thread runs at about 5.0-5.5 GHz on every virtual CPU, in
  both rounds; none looked like an E-core (those run around 4 GHz or less
  on this part). So pass 49's 1.8x swing in the 60 Hz frame time (68 vs
  123 µs for the same code) does not come from busy threads landing on slow
  cores.
- The 60 Hz scenario differs: the render thread wakes for about 0.1 ms
  every 16.7 ms and sleeps the rest. The clock a thread meets right after
  waking, and where a mostly idle thread is placed, are power-management
  decisions that a busy calibration cannot see. Back-to-back runs were about
  34 µs (run 2), against 68 µs and now 123 µs paced, so the pacing alone
  costs 2-3.6x on this host, and that factor varies by session.
- Consequence for the method: 60 Hz CPU-time figures on this host measure
  the host's wake-up behaviour as much as the code. Instruction counts
  (about 405k per frame, runs 3/17/19) are the code's cost. The calibration
  line flags sessions whose busy clock is off, but it cannot normalise paced
  timings.
- Not a device claim. A phone's governor has its own version of this for a
  thread that wakes briefly at 60 Hz; how it treats Canvas's render thread
  is a device question (Device test plan: CPU frequency while only the
  overlay runs).

### Analysis (pass 51): GL state save and restore in the real GL3 backend

From source, not measured: the harness replaces the backend with a stand-in.
Canvas builds the vendored `imgui_impl_opengl3.cpp` (CMakeLists.txt:29), on
an ES3 context on Android. Every `ImGui_ImplOpenGL3_RenderDrawData` call
first saves GL state: by reading the 1.90.6 source for an ES3 context, about
15 `glGetIntegerv` and 5 `glIsEnabled` queries. They cover active texture,
program, texture, sampler, array/element buffers, VAO, viewport, scissor
box, six blend factors and equations, and blend/cull/depth/stencil/scissor
enables. It restores the same state after drawing, about 25 state-setting
calls. Canvas renders on its own EGL context (`androidbk.cpp:173`), so
nothing else's state is there to protect. These calls exist to coexist
with other renderers on a shared context.

Reading:
- Per-frame CPU on the host: negligible; the instruction counts include
  the stand-in only.
- On a device, the cost of `glGet*` depends on the driver. Forum reports
  range from microseconds to milliseconds when a query forces the CPU to
  wait for the command stream. The common advice is to cache state rather
  than query it every frame. Whether Canvas's 20 queries per frame cost
  anything on the audited phone is a device question: a CPU trace of the
  render thread inside `RenderDrawData` (Perfetto, Android GPU Inspector)
  would show it. If they cost anything, an overlay on its own context could
  skip them.

### Analysis (pass 57): a tap's response in overlay frames (ImGui input trickling)

Read from source; not measured. Real code: `Drain` (`androidbk.cpp:335-377`)
and the vendored ImGui 1.90.6 `UpdateInputEvents` (`imgui.cpp:9353-9450`).

The input path:
- Java's touch handler (`GameActivity.java:680-690`) queues one position
  event per `ACTION_DOWN`/`MOVE`/`UP`, plus a button event on `DOWN` and
  `UP`.
- The render thread replays the queue into ImGui once per frame, just
  before `NewFrame` (`androidbk.cpp:509`).
- `io.ConfigInputTrickleEventQueue` is on (ImGui's default;
  `imgui.cpp:1325`), and Canvas does not change it.

Trickling rules that apply:
- A second change to the same mouse button in one frame is deferred to the
  next frame.
- A position event after a button change is deferred too.
- `AddMousePosEvent` drops a position equal to the last queued one.
- ImGui buttons act on release by default.

So for a quick tap whose `DOWN` and `UP` both arrive between two overlay
frames:
- the first frame applies the position and the press;
- the release is applied one frame later, and the widget responds in that
  frame.
- A tap that spans a frame boundary has no extra frame. Its release is
  applied at the next frame's `Drain`, like any event.

Canvas already avoids a second deferral. The comment at `androidbk.cpp:341-353`
explains why it does not tag touches as `ImGuiMouseSource_TouchScreen`. That
tag adds ImGui's "#2702: TouchScreen have no initial hover" rule, which
would also split the position from the press. A tap would then take three
frames.

What this means for sluggishness:
- **The cost is counted in frames.** Response time to a quick tap is the
  wait for the next `Drain`, plus one extra frame, plus presentation. One
  frame is 16.7 ms at 60 Hz. Under "Limit FPS", run 13's queued-swap model
  gives 33/50 ms gaps.
- **The limiter roughly triples it.** By arithmetic, the extra frame costs
  about 17 ms unlimited and 33-50 ms with the limiter on. The total wait,
  release to the frame that acts on it, is about 17-33 ms unlimited and
  33-100 ms limited.
- **CPU per frame plays no part.** The overlay's CPU (runs 2-3) does not
  enter; the frame interval does.
- What it rules in: with "Limit FPS" on, overlay buttons can feel slow to
  respond even though each frame is cheap.
- What it rules out: per-frame overlay CPU as the cause of that delay.
- Presentation latency (BufferQueue depth, composition) comes on top and is
  a device question.
- One public ImGui issue (#7889) reports trickling adding a frame to
  character input. That case is key events interleaved with text: a text
  event after a key change is deferred while a text field is active.
  Consecutive text events never defer each other. Canvas's IME path sends
  characters alone (`submitUnicodeEvent`), so plain typing should apply
  them all in one frame. A key event in between, such as backspace through
  `submitKeyEvent`, would split them.

### Run 21 (2026-10-31, pass 58): frames from a tap or typed text to the widget, 3 runs

Scenario `taps` (`stage_taps`). Frame counts, not timings, so host speed and
load do not enter. Containers at the time: the peer's idle redis/db only, no
Canvas container. Pacing: 16.7 ms spacing, which does not affect the counts.

What is real:
- the JNI entry points (`submitPositionEvent`, `submitButtonEvent`,
  `submitUnicodeEvent`, `submitKeyEvent`);
- CanvasInput's queue and `Drain`;
- the vendored ImGui 1.90.6 (input queue with trickling on, `Button`,
  `InputText`);
- Canvas.cpp, androidbk's render loop.

Stand-ins:
- one mod window holding a 400x160 button and a text field, with the Canvas
  menu hidden so that window is the only one under the touch;
- the fake platform.

The script runs on the render thread after a frame's swap, so its events land
in the gap before the next `Drain`, as events from the UI thread between two
frames would. Events follow GameActivity's order: `ACTION_DOWN` sends a
position and a press, `ACTION_UP` a position and a release.

| Case | Frames until the widget responds |
|---|---|
| Quick tap, down and up in one gap, release at the press position | 2 |
| Quick tap, release 2 px away | 2 |
| Tap spanning a frame boundary (counted from the release) | 1 |
| 8 characters in one gap (`submitUnicodeEvent`) | 1 (length 8 in the first frame) |
| `i`, `j`, Shift down, Shift up, `k`, `l` in one gap | 4 (lengths 10, 10, 10, 12) |

Runs 2 and 3 gave the same counts. Every case is what pass 57 predicted from
`UpdateInputEvents`. A release at the press position sends a duplicate
position, which `AddMousePosEvent`'s filter drops. A release 2 px away has
its new position deferred together with the release. Both quick taps
therefore take 2 frames.

Reading:
- **Rules in:** a quick tap on an overlay widget costs one more frame than
  the frame rate alone implies. With "Limit FPS" on, that frame is 33-50 ms
  (run 13). So overlay buttons respond about 33-100 ms after release,
  against 17-33 ms unlimited, before presentation latency.
- **Rules out:** per-frame CPU as a factor in that delay; it depends only on
  the frame interval.
- Typing through the IME is not slowed by trickling. A key event between
  characters adds a frame on each side, here 4 frames for one Shift press
  and release.
- Not measured here: the 3-frame case Canvas avoids by not tagging touches
  as `ImGuiMouseSource_TouchScreen` (`androidbk.cpp:341-353`). See run 22.

### Run 22 (2026-10-31, pass 59): DESIGN VARIANT, touches tagged as touchscreen input, 3 runs each

Same scenario, harness and conditions as run 21 (`stage_taps`; frame counts;
no Canvas container, only the peer's idle redis/db). The variant is a patched
scratch copy of `androidbk.cpp` built in the container. Its `Drain` calls
`io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen)` before replaying
events, as upstream backends that discriminate touch do. Everything else is
real as in run 21. Canvas's build ran in the same invocation and reproduced
run 21 exactly.

| Case | Canvas (frames) | DESIGN VARIANT, touchscreen-tagged (frames) |
|---|---|---|
| Quick tap, release at the press position | 2 | 3 |
| Quick tap, release 2 px away | 2 | 4 |
| Tap spanning one frame boundary (from the release) | 1 | 2 |
| 8 IME characters | 1 | 1 |
| Characters with Shift interleaved | 4 | 4 |

All runs agreed. The variant's extra frames come from ImGui's "#2702:
TouchScreen have no initial hover" rule: a touch press that follows a
position change in the same frame is deferred to the next frame.
- Quick tap: position | press | release.
- Release 2 px away: position | press | new position | release.
- One-frame spanning tap: the release finds the deferred press still
  queued ahead of it.

Reading:
- This verifies, from the other side, the design note at
  `androidbk.cpp:341-353`. Leaving touches untagged saves 1-2 frames per
  quick tap against the upstream-style backend: about 33-100 ms under
  "Limit FPS" by run 13's gaps. The cost, per that note, is ImGui's 3 px
  stationary threshold.
- It rules out the input-source setting as a cause of extra overlay latency
  in the shipped code. The 2-frame quick tap of run 21 is the floor with
  trickling on. Removing that last extra frame would need
  `io.ConfigInputTrickleEventQueue = false`, at the cost of losing down/up
  pairs within one frame. Run 23 measures it.

### Run 23 (2026-10-31, pass 60): DESIGN VARIANT, input trickling off, 3 runs

Same scenario and harness as runs 21-22 (`stage_taps`, frame counts). The
variant is a patched scratch copy of `androidbk.cpp` whose `Drain` sets
`io.ConfigInputTrickleEventQueue = false` before replaying events. ImGui's
default is on, and Canvas leaves it on. Load: a peer Tibik campaign
container (`tibik-asan-harness`) was running, plus the idle redis/db.
Counts don't depend on load. Canvas's build and the touchscreen variant
reproduced runs 21 and 22 in the same invocation.

| Case | Canvas | DESIGN VARIANT, trickling off |
|---|---|---|
| Quick tap, release at the press position | 2 frames | no click |
| Quick tap, release 2 px away | 2 frames | no click |
| Tap spanning a frame boundary | 1 frame | no click |
| 8 IME characters | 1 frame | 1 frame |
| Characters with Shift interleaved | 4 frames | 1 frame (Shift press flattened away) |

All 3 runs agreed. A per-frame trace (`TAPS_DEBUG=1`) shows why even the
spanning tap is lost. On the release, Canvas queues an off-screen pointer
position (-FLT_MAX) right after the button-up (`androidbk.cpp:273-276`).
Its comment says this keeps a lifted finger from hovering.
- Trickling on: ImGui defers that position to the frame after the release.
  The release is therefore applied while the pointer is still over the
  button, and the click registers.
- Trickling off: the release and the off-screen position are applied in
  the same frame. ImGui requires the item to be hovered at release, so no
  tap ever clicks. Quick taps are also lost because their press and
  release flatten in one frame.

Trace, spanning tap (variant):
```
frame 85: pos (285, 219) down=1 clicked=1 released=0 active=yes hovered=Mod 1
frame 86: pos (-FLT_MAX, -FLT_MAX) down=0 clicked=0 released=1 active=no hovered=-
```

Reading:
- **Trickling is load-bearing for Canvas's input design.** The 2-frame
  quick tap in runs 21-22 is the cheapest setting that registers taps at
  all with Canvas's lift-off invalidation. It is not avoidable overhead.
- **The input path is ruled out as an extra source of latency** beyond the
  frame interval. With "Limit FPS" the remaining lever is the frame rate
  itself (run 13), not ImGui's input configuration.

### Run 24 (2026-10-31, pass 61): release-to-response time by tap duration, queued-swap model, 100 taps per row

Scenario `taplatency` (`stage_taplatency`), on host time under the fake
vsync clock (`PERF_SWAP_QUEUED`).

What is real and what is a stand-in:
- Real: the JNI entry points, CanvasInput and `Drain`, ImGui (trickling
  on), androidbk's render loop with Canvas's 41 ms "Limit FPS" sleep when
  limited, and Canvas.cpp.
- Stand-in: one mod window with a 400x160 button (menu hidden), and the
  queued-swap display: a non-async BufferQueue of depth 2, latched one
  buffer per 16.7 ms vsync. That is the model that matched the device's
  limiter cadence in run 13.

Method:
- The UI thread taps at a random phase (0-100 ms), as GameActivity does:
  position + press, then after `tap_ms` position + release.
- Measured from the release's submission to (a) the start of the frame
  whose widget reports the click, and (b) that frame's present, the vsync
  at which the model latches it.

Conditions: host i9-14900HX, 32 vCPUs. Load 0.35 before and 0.04 after.
Calibration on CPU 2: 5.33 adds/ns. Containers: the peer's idle redis/db
only, unchanged during the run. No taps were missed.

| Limit FPS | tap_ms | release -> acting frame start, ms (p50 / p95 / p99 / max) | release -> present, ms (p50 / p95 / p99 / max) | frames |
|---|---|---|---|---|
| off | 0 | 26.4 / 32.7 / 33.2 / 33.3 | 76.3 / 82.6 / 83.1 / 83.2 | 2 |
| off | 50 | 9.4 / 15.7 / 16.5 / 16.6 | 59.4 / 65.6 / 66.4 / 66.4 | 1 |
| off | 100 | 9.7 / 15.5 / 16.5 / 16.6 | 59.5 / 65.4 / 66.4 / 66.5 | 1 |
| off | 150 | 9.5 / 15.7 / 16.3 / 16.6 | 59.2 / 65.6 / 66.2 / 66.4 | 1 |
| on | 0 | 64.0 / 80.0 / 81.4 / 81.5 | 72.2 / 91.9 / 95.8 / 97.5 | 2 |
| on | 50 | 21.8 / 37.5 / 39.2 / 40.0 | 27.7 / 46.9 / 50.9 / 51.6 | 1 |
| on | 100 | 19.1 / 38.5 / 40.9 / 41.1 | 27.9 / 49.1 / 52.9 / 53.4 | 1 |
| on | 150 | 17.6 / 38.3 / 39.8 / 40.9 | 26.0 / 49.6 / 54.8 / 55.8 | 1 |

Reading:
- **Taps held 50 ms or longer span a frame boundary**, so the trickle
  frame of runs 21-23 does not apply: 1 frame at 60 Hz and under the
  limiter. Only near-instant taps (0 ms here) take 2. No study of tap
  duration was found. The only figure is an informal Chrome remark that
  about half of single-finger gestures end within 150 ms. So most taps
  probably take the 1-frame path, especially at 60 Hz.
- **In this model the limiter lowers release-to-present for ordinary
  taps**: about 26-28 ms p50 against about 59 ms unlimited. Limit FPS adds
  waiting for the next `Drain`: p50 about 18-22 ms against 9.5 ms.
  Unlimited, however, the loop runs ahead and fills the BufferQueue, and
  each frame is latched about 3 vsyncs (50 ms) after it starts. Under the
  limiter the queue stays empty and a frame is latched at the next vsync.
  This matches the frame-pacing guidance: submitting frames as fast as the
  API allows adds input latency, and unpaced producers block in
  `dequeueBuffer`.
- Near-instant taps: 72 ms p50 to present with the limiter against 76 ms
  without. Similar, for opposite reasons.
- Rules in: the unlimited overlay's own buffering as a source of
  input-to-display delay for its widgets, about 3 frames in this model.
- Rules out: "Limit FPS" making overlay taps slower end to end, in this
  model. It corrects pass 57's arithmetic, which counted frame intervals
  only.
- Model-dependent: buffer count, GPU completion time and SurfaceFlinger
  latching on a device change the absolute numbers. Input-to-display
  latency on a device needs a device measurement; see the Device test plan.
  This is the overlay's own widgets only, not the game's input.

### Run 25 (2026-10-31, pass 62): run 24 under a blocking-swap model, 100 taps per row

Same scenario and harness as run 24 (`stage_taplatency`, now
`TAP_MODELS="queued blocking"`). The blocking model (`PERF_VSYNC_ALIGNED`, as
in run 10) makes every swap wait for the next vsync and present at it, so
nothing queues. The queued model is run 24's.

Conditions: host i9-14900HX, 32 vCPUs.
- First run: two peer Tibik campaign containers (`tibik-asan-harness`) were
  running at the start and exited during the run; load 2.60 -> 0.97.
  Calibration 4.98 adds/ns.
- Because load changed, the blocking rows were repeated on a quiet machine
  (load 0.82 -> 0.30, only the idle redis/db). They agreed within 0.3 ms;
  the repeat is shown.
- The queued rows of the first run reproduced run 24 within 1 ms (100 ms
  taps: 59.6 against 59.4 unlimited, 28.8 against 27.9 limited).

Release to present, ms (p50 / p95 / p99 / max):

| Limit FPS | tap_ms | queued (run 24 model) | blocking |
|---|---|---|---|
| off | 0 | 76.1 / 82.3 / 82.9 / 82.9 | 42.8 / 49.0 / 49.8 / 49.9 |
| off | 100 | 59.6 / 65.7 / 66.3 / 66.5 | 26.2 / 32.3 / 33.0 / 33.2 |
| on | 0 | 74.1 / 90.7 / 96.3 / 98.0 | 81.1 / 106.8 / 107.9 / 108.6 |
| on | 100 | 28.8 / 47.7 / 53.7 / 57.7 | 31.0 / 57.1 / 58.3 / 58.6 |

Release to the acting frame's start was the same in both models when
unlimited: 9.5 ms p50 for 100 ms taps. With the limiter it was longer under
blocking (22.4 against 19.6 ms p50), because the limited loop then runs at
20/s instead of 24/s (runs 10, 13).

Reading:
- **The queue explains about 33 ms (2 vsyncs) of run 24's unlimited 59 ms.**
  With no queueing, an unlimited overlay shows a tap's result about 26 ms
  after release (p50).
- **Whether "Limit FPS" helps or hurts tap latency depends on buffering.**
  - Queued: the limiter shows a tap 31 ms sooner at p50 (28.8 against
    59.6).
  - Blocking: it shows it about 5 ms later at p50 and 25 ms later at p99.
  - Run 13 found the queued model matches the audited device's limiter
    cadence (33/50 ms gaps, about 23/s), which the blocking model does not
    (steady 20/s). AOSP commits put a default BufferQueue at three slots.
    Both point to the queued case on that device; that is an inference, not
    a device measurement.
- **Rules out:** per-frame CPU in every row; differences come only from
  frame timing and buffering.
- **What would settle it:** the Device test plan's tap-latency step, with
  a high-speed camera or Perfetto latch times.

## Queue

1. Done in pass 2: `vsync` scenario.
2. Done in pass 3: instruction counts (`stage_instr`, `Dockerfile.perf`).
3. Done in pass 4, differently: GL submission counted from `ImDrawData` with
   the backend's rules (exact for this backend). A Mesa software-GL run was
   dropped: it would mostly time a CPU rasterizer, which says nothing about a
   phone GPU.
4. Done in passes 7-11: redundant frames (run 5); skip-unchanged variant
   presents (run 6), CPU (runs 7-8) and instruction overhead (run 9).
   FPS readout latched twice a second: run 11 (pass 19).
5. Done in passes 19-20: `stage_limiter`. Under a blocking-swap model the limiter
   gives 20/s (run 10); under a queued-swap model it gives 24.2/s with
   alternating 33/50 ms gaps, matching the device (run 13). Two-vsync pacing
   variant: steady 30/s under both models.
6. Done in pass 19: `uilatency` (run 12).
8. Repeat on a quiet machine (no Tibik campaign): run 11 CPU columns and
   run 12 at 60 Hz (maxima), with more UI calls in the unpaced setting.
   Run 12 part done in pass 33 (run 15). Run 11 repeated as run 16, not
   quiet (a Tibik campaign overlapped); presents reproduce, CPU stays
   unsettled. Done as far as this host allows.
9. Done in pass 32: skip-unchanged under the queued-swap model (run 14).
7. arm64 instruction counts under QEMU (relative, not timing). Needs QEMU
   built with `--enable-plugins` and its `libinsn` plugin (Ubuntu packaging
   not confirmed); deprioritised: counts on an emulated core say little about
   time on a phone core.
10. Done in pass 58 (run 21): all four expectations confirmed. Pass 57
    analysis on the host, counted in frames rather than time. Use the
    real `Drain` and ImGui with the fake menu's button. Submit `pos, down,
    pos, up` within one frame gap, and count frames until the button reports
    a click. Repeat with the tap spanning a frame boundary, with eight
    characters typed into the focused field, and with the same characters
    interleaved with key events. Expected: 2 frames, 1 frame, all eight
    characters in one frame, and a split at each key event. It is
    deterministic, so it needs the container but not a quiet machine.

## Pass log

- Perf pass 1 (2026-10-31): harness built (real overlay frame, stand-in mods,
  app optimisation); scenarios menu 0/1/5/20, hidden 5, touch 5, limited 5.
  Results above. Added `vsync` scenario. Next: queue 1, then 2.
- Perf pass 2 (2026-10-31): searched frequency scaling under alternating
  busy/idle load (cpufreq-bench method). Added `SCENARIOS` subset selection.
  Checked `docker ps` (only the peer's idle redis/db). Ran `menu 5` and
  `vsync 5`, 5 x 1000 frames: realistic 60 Hz cost 68.2 us/frame p50 (2x back
  to back), about 4 ms of CPU per second; "Limit FPS" about halves it.
  Corrected run 1's CPU-per-second reading. Next: queue 2 (instruction counts,
  Dockerfile.perf with valgrind), then queue 3 (real GL path for upload sizes).
- Perf pass 3 (2026-10-31): searched callgrind client requests for region
  measurement (zero at start, labelled dump at end). Added `Dockerfile.perf`
  (valgrind) and `stage_instr`; fixed the measurement window after a first
  attempt gave inconsistent counts. Ran menu 0/1/5/20, hidden 5, touch 5 x 500
  frames: instruction ratios match CPU time (about 11-12k instructions/us);
  Canvas's own code is about 21% of a five-mod frame. `docker ps` showed a
  peer Tibik campaign container running (irrelevant to instruction counts).
  Next: queue 3 (real GL path via Mesa software GLES: upload bytes and draw
  calls per frame).
- Perf pass 4 (2026-10-31): searched how ImGui's OpenGL3 backend submits
  draw data, then confirmed it in the vendored source (full vertex and index
  upload per draw list every frame, one draw call per non-clipped command).
  Added GL-submission counting from `ImDrawData` to the perf driver. Ran menu
  0/1/5/20, hidden 5, touch 5 (1 x 500 frames): five mods = 12 draw calls and
  93 KB uploaded per frame; the full-surface clear is over 100x that in
  nominal bytes. Queue 3 closed (Mesa path dropped, reason recorded). Next:
  queue 4, redundant-frame share via draw-data hashing.
- Perf pass 5 (2026-10-31): a Canvas sanitizer container was running, so no
  timing. Searched how ImGui users avoid idle redraws (maintainers recommend
  idling in the application loop with a low background rate, not skipping
  render calls inside ImGui). Added a draw-data hash per frame to the perf
  driver: it reports frames identical to the previous one and the longest run
  of them (compiles; not yet run). Expectation to test: with the Canvas menu
  open, its "Application average ... FPS" text (`main.cpp:214-218`) changes
  every frame, so no frame repeats. Next: run queue 4 on a quiet machine.
- Perf pass 6 (2026-10-31): a Canvas sanitizer container was running, so no
  timing. Searched swap blocking and buffer behaviour under vsync (results
  covered Linux/Mesa/SDL, not Android specifically). Wrote the "Limit FPS"
  pacing model: the 41 ms sleep against 16.67 ms vsync gives alternating
  33/50 ms gaps by design; Codex's device cadence implies about 2.3 ms per
  limited iteration beyond the sleep (an upper bound, bundling work,
  oversleep and swap wait). Linked queue 5 to sanitizer finding F15. Next:
  queue 4 (redundant frames) when the machine is quiet.
- Perf pass 7 (2026-10-31): searched redraw-on-change approaches in
  immediate-mode UIs (iced reactive rendering, ImGui refresh-policy
  discussion, damage tracking). `docker ps`: no Canvas sanitizer container,
  a peer Tibik campaign running (recorded as load; timings discarded). Ran
  the redundant-frame count: menu open, 0.3-2.2% of frames repeat (40% at
  60 Hz spacing, because of the FPS readout); menu hidden, 100% repeat, even
  with nothing drawn. Next: the skip-unchanged-frames variant (queue 4
  follow-up) on a quiet machine.
- Perf pass 8 (2026-10-31): searched precedent for skipping presents of
  unchanged frames (Chromium skips SwapBuffers when nothing is damaged, for
  power; Cobalt stops swapping once its surface is gone). Built the
  skip-unchanged-frames variant from a patched scratch copy and ran it
  against baseline at 60 Hz spacing: presents/s 59 -> 41 with the menu open,
  59 -> 0 with it hidden. CPU columns invalid (a peer Tibik campaign started
  mid-run; the harness's own hash was being timed); harness fixed to subtract
  its analysis time. Next: repeat the CPU comparison on a quiet machine.
- Perf pass 9 (2026-10-31): searched A/B benchmarking practice (interleaved
  rounds, medians with spread, pinning). `stage_variant` now runs
  interleaved, pinned rounds. Ran 4 rounds x 480 frames at 60 Hz spacing on a
  mostly quiet host (a Tibik campaign started near the end): corrected
  baseline matches run 2; the variant removes all idle presents but its naive
  hash doubles CPU when panels are drawn. Next: a variant with a cheap change
  test (memcmp against the previous frame's buffers), same protocol.
- Perf pass 10 (2026-10-31): searched memcmp vs FNV-1a throughput (FNV-1a
  about 0.27 bytes/cycle, which accounts for run 7's overhead). Added a
  memcmp change test to the variant (default) and selection via
  `VARIANT_CHANGE`. First attempt never skipped (uninitialised struct
  padding); fixed. Ran the interleaved comparison: presents 59 -> 0 hidden,
  59 -> 39 with menu; CPU overhead within run-to-run noise (spreads
  overlapping). Next: instruction counts for baseline vs memcmp variant
  (`stage_instr`-style, load-independent) to put an exact number on it.
- Perf pass 11 (2026-10-31): searched instruction-count comparisons between
  builds (same toolchain and valgrind, keep symbols). Factored the variant
  build into `build_variant_pair`; added `stage_variant_instr`. Ran it: the
  memcmp variant costs +24.6k instructions/frame when frames change, -13.9k
  when nothing changes, +25 with nothing drawn. Next: make the driver's own
  analysis switchable off for instruction runs, then queue 5 (vsync-aligned
  limiter variant) and queue 6 (UI-thread latency in wantsMouse).
- Perf pass 12 (2026-10-31): a Canvas sanitizer container (HWASan run) was
  up, so no timing. Searched 30 fps pacing on Android (Swappy recommended;
  eglSwapInterval(2) not supported on every device; sleep limiters are the
  device-independent fallback). Built, not yet run: a vsync-latched display
  model in the fake EGL (`PERF_VSYNC_ALIGNED`: swap returns at the next
  vsync boundary, frame point 5 marks the latch), a present-gap histogram in
  vsync periods, `PERF_NO_ANALYSIS` to drop the driver's own analysis, and
  the limiter design variant (`variant_vsync_limiter.h`, `stage_limiter`:
  41 ms sleep replaced by a one-vsync wait). Next: run `limiter`.
- Perf pass 13 (2026-10-31): the HWASan container and a Tibik campaign were
  running, so no timing. Searched the AOSP Hardware Composer documentation
  on overlay planes and translucent layers. Wrote the "Summary so far"
  section at the top: the overlay's CPU work and ImGui geometry are small;
  in the common in-game state every overlay frame is redundant but still
  presented full-screen; a cheap skip-unchanged variant removes that; the
  remaining question (presenting an always-on full-screen translucent layer)
  needs the device. Next: run `limiter` when the machine is free.
- Perf pass 14 (2026-10-31): the HWASan container was still running, so no
  timing. Searched input-dispatch latency on Android (5 s input ANR; lock
  contention on the main thread is a documented cause; frames budget about
  16.7 ms). Added a `uilatency` scenario: the UI thread submits a touch and
  times each `wantsMouse`/`wantsKeyboard` call (they take mutexes the render
  thread also takes) while mods draw, reporting p50/p99/p99.9/max. Compiles;
  not yet run. Next: run `limiter` and `uilatency` (e.g. 20 mods) when free.
- Perf pass 15 (2026-10-31): the HWASan container and a Tibik campaign were
  running, so no timing. Searched Perfetto/Winscope layer tracing (Android
  15+: `android.surfaceflinger.layers` with composition-type flags). Wrote
  the "Device test plan" section: builds A (as is), B (overlay absent), C
  (skip-unchanged variant); short and sustained runs; layer cadence,
  composition type per layer, thermal status; and how to read the outcome.
  Next: run `limiter` and `uilatency` when the machine is free.
- Perf pass 16 (2026-10-31): the HWASan container was still running, so no
  timing. Searched QEMU TCG plugins (libinsn counts instructions; needs a
  plugin-enabled QEMU build). Refreshed the queue: items 4 (done apart from a
  low-priority readout variant), 5 and 6 (built, waiting for a free machine),
  7 (deprioritised, with the reason). Next: run `limiter` and `uilatency`.
- Perf pass 17 (2026-10-31): the HWASan container was still running (its F9
  step had stalled writing a QEMU core dump and was stopped so the run could
  finish), so no timing. No new search beyond pass 16's; nothing measured.
  Next: run `limiter` and `uilatency` once no Canvas container is running.
- Perf pass 18 (2026-10-31): a Canvas HWASan container was running (plus a
  Tibik campaign), so no timing. Searched how ImGui computes `io.Framerate`
  (a rolling average of `io.DeltaTime`; the vendored 1.90.6 averages the
  last 60 deltas, `imgui.cpp:4599-4603`). Canvas prints it every frame as
  `"%.3f ms/frame (%.1f FPS)"` (`main.cpp:214-218`), so the menu's text
  changes whenever the 60-frame mean moves by 1 µs or more, which is why the
  skip-unchanged variant still presents about 41/s with the menu open
  (run 6). How often the text repeats depends on the host's frame-time
  jitter, so run 6's 31% skipped is not an estimate for a device. Built
  `stage_readout` (DESIGN VARIANT): the skip-unchanged build plus a patched
  scratch copy of the `CanvasMenu` extraction whose readout shows a value
  latched every 0.5 s (`harness/variant_readout_latch.h`; the patched copy
  wins the include lookup via `-I$OUT/readout`, `$GEN` untouched). Built
  with `BUILD_ONLY=1`; nothing timed. Next: run `limiter`, `uilatency` and
  `readout` once no Canvas container is running.
- Perf pass 19 (2026-10-31): no Canvas container at the start; only Tibik's
  idle redis/db, and Tibik campaigns came and went during the run (noted per
  run). Searched Android frame pacing (Swappy, swap interval 2, sleep-based
  limiting), BufferQueue blocking semantics (non-async mode blocks in
  `dequeueBuffer` only when no buffer is free) and UI-thread lock contention
  on Android. Ran `limiter` (run 10), `readout` (run 11) and `uilatency`
  unpaced and at 60 Hz (run 12). Results:
  - Run 10: under a blocking-swap model, Canvas's limiter gives a flat
    20/s. That does not match the device's 23/s, so the model is wrong for a
    loop below the refresh rate.
  - Run 11: with the menu open, a readout latched every 0.5 s cuts presents
    on the skip-unchanged variant from about 30/s to about 2/s.
  - Run 12: UI-thread time in the input queries is at most about 1.3 µs at
    p99.9; Canvas's input locks are not a host-side source of latency.
  Built a queued-swap model in `fake_platform.cpp` (`PERF_SWAP_QUEUED`: the
  swap returns at once, one latch per vsync, block only with 2 buffers
  waiting; the blocking model now honours the swap interval). Added an
  `eglSwapInterval(2)` variant (`variant_swap_interval.h`) and rewrote
  `stage_limiter` to run three builds under both models. Next: run it, and
  repeat runs 11-12 CPU and maxima on a quiet machine.
- Perf passes 20-21 (2026-10-31): searched swap interval 2 on Android (AOSP
  `Surface.cpp` clamps the swap interval to 0..1, so two-vsync pacing on a
  device comes from Swappy: Choreographer plus presentation timestamps) and
  Swappy's pacing mechanism. Ran `limiter` with three builds under the
  blocking and queued swap models (run 13). Canvas's limiter under the queued
  model gives 24.2/s with 33/50 ms gaps in a 52/48 split, matching the device
  pattern. Two-vsync pacing gives a steady 30/s in both models. The
  one-vsync-wait design limits nothing once the swap queues, and is dropped.
  Per-frame CPU is the same in every build (p50 72-75 µs), so the judder is a
  pacing effect, not a cost effect. Relabelled `variant_swap_interval.h`
  accordingly and updated the queue (5 and 6 done; new 8 and 9). Load: a
  Tibik campaign ran for part of the run; presents and gaps are unaffected.
  Next: queue 9 (skip-unchanged under the queued model), then queue 8 on a
  quiet machine. The sanitizer loop's HWASan re-run comes first; perf passes
  during it do write-up only.
- Perf pass 22 (2026-10-31): the HWASan container was running, so no
  timing. Searched how the Hardware Composer handles translucent layers (AOSP
  HWC HAL: four planes typical, translucent overlays can be less efficient
  on a static screen, and a cached composition is reused only when the same
  buffers are composited again). Rewrote the "Summary so far" section
  (passes 1-21): item 6 now rests on run 13, items 7 (readout, run 11) and 8
  (UI-thread locks, run 12) are new, and the device paragraph notes the HWC
  cache point. Next: queue 9, then queue 8, once no Canvas container is
  running.
- Perf pass 23 (2026-10-31): the HWASan container and a Tibik campaign
  were running, so no timing. Searched AChoreographer frame callbacks
  (callbacks are requested per frame, so an app that posts only when content
  changes requests no frames while idle). Built queue 9 without running it:
  `stage_variant` takes `MODEL=queued` (queued-swap display model instead of
  a 16.7 ms sleep per swap). On the vsync clock the variant's skipped frames
  now wait for the next vsync boundary, as a Choreographer callback would.
  The default sleep model is unchanged, so runs 6-8 stay reproducible. Next:
  `MODEL=queued run_perf.sh variant` once no Canvas container is running.
- Perf pass 24 (2026-10-31): the HWASan container was running, so no
  timing. Searched Perfetto FrameTimeline (Android 12+, jank types per frame;
  SurfaceViews not supported yet). Updated the Device test plan:
  - optional build C' (latched readout) for the menu-open state;
  - a "Limit FPS" comparison, with the host's predictions from run 13 (A
    about 23-24/s alternating 33/50 ms, two-vsync pacing a steady 30/s, which
    on Android has to be Swappy, not `eglSwapInterval(2)`);
  - a note that the overlay's cadence must come from per-layer SurfaceFlinger
    latency, not FrameTimeline.
  Next: queue 9 (`MODEL=queued` variant), then queue 8, when no Canvas
  container is running.
- Perf pass 25 (2026-10-31): the HWASan container (F4 step) and a Tibik
  campaign were running, so no timing. Searched 60 Hz cadence for about
  24 fps content (3:2 pulldown: alternating 3- and 2-refresh holds). Checked
  run 13 against arithmetic: a share of k - 2 three-period gaps for an
  iteration of k periods predicts 47.5% and 24.24/s on the host (measured
  47.8%, 24.2/s). The device's 43.3 ms iterations predict 59.8% 50 ms gaps
  at 23.09/s, consistent with the audit's 23.1/s. Added this to run 13's
  reading. Next: queue 9, then queue 8, when no Canvas container is running.
- Perf pass 26 (2026-10-31): the HWASan container (F4 diagnostic) was
  running, so no timing. Searched sample sizes for tail percentiles. The
  sample maximum bounds p99.9 at 90% confidence after about 2,300 samples;
  about 10 observations beyond p99.9 (about 10,000 samples) give a stable
  estimate. Run 12's unpaced setting had only 340-1,320 calls per run. Added
  `PERF_UI_CALLS` to `perf_render.cpp`: the UI thread keeps querying until
  it has made that many calls (default 0 = unchanged). Next: queue 9, then
  queue 8 with `PERF_UI_CALLS=10000`, when no Canvas container is running.
- Perf pass 27 (2026-10-31): the HWASan container (F4 diagnostic) was
  running, so no timing. Searched tile-based GPU clear and store costs (Arm
  and Meta guidance: a clear at the start of a pass is close to free and
  avoids a memory read; full-surface stores and reads are what cost
  bandwidth). Added that to run 4's reading: the overlay's per-present cost
  is writing out and compositing the full surface, so presenting less often
  helps and dropping `glClear` would not. Next: queue 9, then queue 8, when
  no Canvas container is running.
- Perf pass 28 (2026-10-31): a Canvas sanitizer container (`hwasan_x86`,
  `artpatch_diff`, `unit`) and a Tibik campaign were running, so no timing.
  Searched SurfaceFlinger's per-layer refresh-rate heuristic (AOSP changes:
  rates snapped to known values, Max when unresolved, small-damage
  suppression). Added a device-plan item: record the active refresh rate
  and the overlay layer's vote with and without the overlay (builds B, C),
  since a full-damage layer presenting every vsync, or unevenly under
  "Limit FPS", may affect the refresh rate a variable-refresh display
  picks. Device question; no host claim. Next: queue 9, then queue 8, when
  no Canvas container is running.
- Perf pass 29 (2026-10-31): a Canvas sanitizer container and a Tibik
  campaign were running, so no timing. Searched Choreographer vsync
  timing; there are no published jitter figures, only the nominal 16.67 ms
  period and source that derives frame time from vsync timestamps.
  Predictions for queue 9, written before the run so the result tests the
  queued model:
  - menu hidden: baseline about 60 presents/s, the variant about 0/s after
    the first frame (as in run 6);
  - menu open: the variant below run 11's 29.8/s. In the queued model each
    frame starts at a vsync boundary, so `io.DeltaTime` varies less than in
    the sleep model, and the `%.3f ms/frame` text should repeat more often.
  How often that text repeats on a device depends on its real frame-time
  jitter, which the host cannot give. Next: queue 9, then queue 8, when no
  Canvas container is running.
- Perf pass 30 (2026-10-31): queue 9 (`MODEL=queued` variant, 4 rounds)
  was running, started at the end of sanitizer pass 20. No other timing.
  Searched `clock_nanosleep` wakeup accuracy. An absolute sleep never
  returns early; it overshoots by scheduling delay and timer slack (50 µs
  default for normal threads). Effect on the vsync models: in the queued
  model a present's time is the computed vsync boundary, so the gap
  histograms are exact. Overshoot only lengthens the iteration, and it is
  already included in run 13's measured 41.25 ms. It moves a frame to the
  next vsync only when the iteration ends within tens of µs of a boundary.
  Next: record queue 9.
- Perf pass 31 (2026-10-31): queue 9 was still running (started pass 30).
  A Tibik campaign started about a minute into it, so its CPU columns will
  need a repeat; presents and skip counts are counted, not timed. Searched
  interleaved A/B benchmarking (ABBA ordering cancels linear drift between
  builds; pinning and medians over rounds limit the rest). `stage_variant`
  already alternates the order per round and pins to one CPU. Next: record
  queue 9 when it finishes, and repeat its CPU columns on a quiet machine.
  Also from the search: one binary samples only one memory layout (the
  zenbench methodology notes; USENIX ATC'23 measured order effects of about
  7-8%). CPU differences of a few percent between two builds (runs 7-8, 11)
  are within what layout alone can produce, so they rule nothing in either
  way. Presents and instruction counts are unaffected by layout.
- Perf pass 32 (2026-10-31): searched how Dear ImGui users idle: the
  ImGui wiki puts power saving in the app's own loop; PR #4076 and a
  per-window refresh policy are experiments; one overlay skips ImGui's
  per-frame work whenever no UI is visible. Recorded queue 9 as run 14.
  Menu hidden: 60/s vs 0/s, as predicted. Menu open: 55.6/s, against a
  prediction below 29.8/s; the uneven frame times of the queued model move
  the FPS text on most frames. So the open-menu result depends on timing
  unless the readout is latched (run 11). The machine was quiet afterwards
  (no Tibik campaign), so I started the quiet-machine repeats: `uilatency`
  20 mods with `PERF_UI_CALLS=10000` unpaced and at 60 Hz (queue 8), then
  `readout` (run 11 CPU columns). Next: record them.
- Perf pass 33 (2026-10-31): the quiet-machine repeats started in pass 32
  were running; the `uilatency` parts had finished. Searched futex wake-up
  latency; the kernel's futex-wake benchmark times wakes from the waker's
  side only, so nothing to cite for the per-call outliers. Recorded run 15
  (run 12 on a quiet machine, 10,000+ calls per run). p99.9 is at most about
  1.3 µs; one call per run at 60 Hz takes 0.03-0.2 ms, cause not
  established. The conclusion is unchanged. Next: record the `readout`
  repeat (run 11 CPU columns) when it finishes.
- Perf pass 34 (2026-10-31): the `readout` repeat was still running, so no
  new timing. Searched `ANativeWindow_setFrameRate` (API 30+: a surface
  declares its intended rate, with DEFAULT or FIXED_SOURCE compatibility).
  For the device plan: an overlay that presents rarely could also say so,
  instead of leaving it to SurfaceFlinger's estimate (pass 28). That is a
  design note, not a host result. Updated summary item 5 with run 14: the
  skip-unchanged design holds under both display models, and with the menu
  open it needs a latched readout. Next: record the `readout` repeat.
- Perf pass 35 (2026-10-31): the `readout` repeat was still running. A Tibik
  campaign ran for part of it, so it is not a quiet result. Searched
  benchmark variance on hybrid Intel CPUs: a thread moved between P-cores
  and E-cores mid-run, and an E-core can take about 2x as long. Pinning
  fixes the variance only when it pins to a known core type. Method caveat,
  checked here: the host is an i9-14900HX (8 P-cores, 16 E-cores). Inside
  the Docker Desktop VM, `taskset -c 2` pins to virtual CPU 2, which Windows
  can place on either core type. The VM exposes no `cpufreq`, so frequency
  and core type cannot be logged. This can produce bimodal or wide CPU
  ranges, such as run 14's baseline frame CPU of 74-127 µs, and is one more
  reason the CPU columns are relative only. Presents, gap histograms and
  instruction counts do not depend on it. Next: record the `readout` repeat.
- Perf pass 36 (2026-10-31): recorded the `readout` repeat as run 16.
  Presents reproduce run 11 (59.1 / 33.3 / 1.95 per second). The CPU
  ordering repeats, but a Tibik campaign overlapped and core placement is
  unknown (pass 35), so CPU stays unsettled. Queue 8 is closed as far as
  this host allows. The machine was then handed to the sanitizer loop
  (`fuzz_decoders`, `render_quiet`, leak check, `unit`). Next perf item: the
  queue is done, so tighten the weakest measurement. Candidate: instruction
  counts (callgrind, independent of load and core type) for the
  skip-unchanged and latched-readout builds with the menu open, to settle
  the CPU question runs 11 and 16 leave open.
- Perf pass 37 (2026-10-31): a Canvas sanitizer container was running, so
  building only. Searched callgrind instruction counts as a benchmark
  metric. They are deterministic for a given build regardless of load and
  frequency; valgrind serializes threads. New `stage_readout_instr`, to settle
  runs 11/16's CPU question without timing. Under callgrind frames are slow,
  so the share that presents differs from 60 Hz. The variant and
  latched-readout builds present different shares. Assuming equal per-frame
  costs for a presented and a skipped frame in both builds, the stage solves
  for the two costs and weights them by run 16's 60 Hz mix (59.1 frames/s;
  33.3 vs 1.95 presents/s). The driver's own analysis is off
  (`PERF_NO_ANALYSIS`). Supporting changes: `perf_render.cpp` prints
  `window: frames= presents=`, and the readout build is now a shared
  `build_readout` function. Next: run `readout_instr` in
  `canvas-perf-harness:local` (instruction counts, so other load is fine,
  but not alongside a Canvas sanitizer run per the loop rule).
- Perf pass 38 (2026-10-31): no Canvas container running (Tibik's idle
  redis/db only). Searched callgrind's handling of threads: the default
  output combines all threads, and `--separate-threads` splits them. As in
  runs 3 and 9, the counters are zeroed and dumped on the render thread, so
  the window covers exactly the measured frames, with every thread's
  instructions in that window. Started `stage_readout_instr` in
  `canvas-perf-harness:local`. Instruction counts do not depend on load or
  core type. Next: record it.
- Perf pass 39 (2026-10-31): ran `readout_instr` (run 17). The first two
  attempts died on a clang segfault in the build. The VM had 29.8 GB free
  memory and 946 GB free disk, and the sanitizer loop's lifecycle build hit
  the same kind of crash in another file. Intermittent compiler crashes on
  unchanged code suggest the environment; that is not established, and it
  costs only reruns. Result: 429.6k instructions per presented frame, 388.3k
  per skipped frame. At run 16's mix the three builds are within 4% of each
  other (23.9M / 24.3M / 23.0M per second), so runs 11/16's CPU gap is
  small and not decisive; presents (59 vs 2/s) are what differ. Next: queue
  done. Remaining tightening candidate: the queued-model CPU columns of run
  14 under instruction counts, low value since run 17 already bounds the
  CPU differences.
- Perf passes 40-41 (2026-10-31): the queue is done, so this tightens the
  least faithful stand-in, the mods: about two thirds of a frame (run 3),
  and every stand-in panel is static. Searched Sky mod menus for live
  readouts and found no source describing any, so the scenario models
  possible mods, not known ones. Also searched change detection for ImGui
  draw data: `imgui-draw-cmp` compares cached vertex/index buffers with
  memcmp, as the variant does. The variant also compares each command's
  clip rect, texture and offsets, which buffer-only comparison misses. New
  in `perf_render.cpp`: `PERF_DYNAMIC_MODS=k` makes the first k stand-in
  mods show a live value; `PERF_DYNAMIC_TEXT=frame` (every frame, the
  default) or `tenth` (seconds to one decimal, 10 Hz). Running the variant
  stage with one live mod, both settings. No Canvas container; Tibik's
  idle redis/db only. Next: record it.
- Perf pass 42 (2026-10-31): the live-mod run (passes 40-41) was still
  running; no new timing. Searched partial updates on Android EGL:
  `EGL_KHR_partial_update` and `EGL_EXT_buffer_age` are advertised by
  Android's EGL wrapper, and `eglSetDamageRegionKHR` is in its entry table.
  Damage regions let a tile-based GPU skip loading and storing tiles outside
  the changed area. A design note for the device plan, not a host result:
  when a live value keeps the overlay changing, a damage-region design would
  redraw and store only that value's rectangle instead of the full
  1080x2376 surface. Canvas clears and presents the whole surface every
  frame (`androidbk.cpp:515-517`) and passes no damage. Whether a given
  driver and compositor use the hint is a device question. Next: record the
  live-mod run.
- Perf pass 43 (2026-10-31): the live-mod run's first half is done; the
  10 Hz half is running. With one stand-in mod showing a value that changes
  every frame, the skip-unchanged variant presents 58.6-59.0/s with the menu
  open or hidden, the same as baseline; with no mods it still presents 0/s.
  So one such mod removes the design's benefit entirely. The frame-CPU
  column read 0.00 in every row of this run, so it was not captured; the
  cause is to be checked before the result is written up. Searched power
  effects of redraw rate: the HotPower 2015 study of 40 Android games found
  fixed 30/60 fps whatever the game state; a 2006 measurement found modest
  savings at 10 vs 60 Hz on an LCD PDA; a vendor claims up to 22% from
  adaptive refresh. None measures an overlay, so device question only.
  Next: record the 10 Hz half and write up run 18.
- Perf pass 44 (2026-10-31): the live-mod run's 10 Hz half was still
  running. Found and half-fixed a harness bug from pass 37. The new
  `window: frames= presents=` line in `perf_render.cpp` made
  `stage_variant`'s `grep -o 'presents=...'` return two matches, splitting
  each round's TSV row. The presents column stayed right (first match), but
  the frame-CPU column read 0.00 in the live-mod run. Runs 14 and 16 came
  before the line was added and are unaffected; run 17 parses the window
  line on purpose. Fix, part 1: the line now says `presented=`. Part 2
  (`stage_readout_instr`'s parse) waits until `run_perf.sh` is no longer
  executing. Searched ImGui's per-window refresh policy: the vendored
  1.90.6 has `SetNextWindowRefreshPolicy` with `TryToAvoidRefresh` in
  `imgui_internal.h`, marked experimental, and it only helps windows
  without child windows (ImGui issue 7797). A design note for live mods,
  not a host result. Next: finish the fix, record run 18 (live mods),
  rerun its CPU columns if useful.
- Perf pass 45 (2026-10-31): recorded the live-mod run as run 18. The
  variant presents as often as the fastest-changing visible content: 59/s
  with a per-frame value, 10/s with a 10 Hz value (menu hidden), 0/s with
  static panels. The design's benefit depends on what mods draw. Finished
  the pass-44 fix: `stage_readout_instr` parses the renamed `presented=`
  field. Next: the CPU columns of run 18 are not needed (run 17 bounds the
  CPU differences). Remaining tightening: none urgent. The open questions
  are device questions (Device test plan).
- Perf pass 46 (2026-10-31): a Canvas sanitizer container was running, so
  write-up only. Searched user reports of overlays and frame drops: a forum
  report blames a benchmark overlay for PUBG frame drops, and Unity has an
  Android "slowdown when overlay UI is displayed" issue closed as By Design.
  Anecdotal; none is about a SurfaceView overlay like Canvas's. Updated the
  summary (passes 1-46): item 5 now covers run 18 (the benefit depends on
  what mods draw) and run 17 (instruction differences under 4%).
- Perf pass 47 (2026-10-31): searched ImGui's font atlas costs; its docs
  advise against large CJK ranges at default oversampling. Found that the
  app always merges the CJK fallback (`ImGUI.java:68` passes
  `enableDroidSans = true`) and that every perf run so far used the small
  atlas. New harness support: `FAKE_ASSET_DIR` in `fake_platform.cpp`
  (file-backed assets; unset keeps the sanitizer stages' behaviour),
  `PERF_DROID_SANS` in `perf_render.cpp`, and `startup:` lines giving atlas
  size and the time from `ImGUI_init` to the atlas and to the first frame.
  Run 19: 2048x4096, 3,352 glyphs, 32 MB as RGBA32, against 512x1024 and
  2 MB. Built in 41-49 ms against 3.5 ms. Per-frame instructions unchanged
  (404.7k). By source, about 70 MB of CPU+GPU memory for the font atlas in
  the shipped configuration. Side observations:
  - The frame stage's median picker now trips over the `startup:` lines,
    which have no `cpu_us`, so it printed no per-frame line this pass. To
    fix before the next timing run.
  - While reading `loadFonts`: the CJK buffer is handed to
    `AddFontFromMemoryTTF` with ImGui's default ownership, then `free()`d
    after `Build()`. That is a latent double free or use-after-free if the
    atlas is ever cleared or rebuilt. Passed to the sanitizer loop as a
    lead.
  Next: fix the median picker; add the font-memory comparison to the
  Device test plan.
- Perf pass 48 (2026-10-31): searched how Android reports a process's GPU
  memory (`dumpsys meminfo`: Graphics = Gfx dev + EGL mtrack + GL mtrack,
  through the vendor's memtrack HAL; snapshots). Fixed the frame stage's
  median picker so the `startup:` lines from pass 47 cannot be picked; it
  now also prints those lines. Added the font-memory comparison to the
  Device test plan and a summary item (9) for run 19. Machine free (Tibik's
  idle redis/db); no timing this pass. Next: a short frame run to confirm the
  picker fix; otherwise nothing queued, and the open questions are device
  questions.
- Perf pass 49 (2026-10-31): machine free (Tibik's idle redis/db). Short
  `frame` run (`vsync 5`, 3 runs x 300 frames) confirms the pass-48 median
  picker fix: it now reports the scenario line and the startup lines. Same
  code, different time: p50 122.8 µs (p95 146.6, p99 185.2, max 206.3)
  against run 2's 68.2 µs for `vsync 5`. Instruction counts are unchanged
  (about 405k per frame, runs 3/17/19), so the 1.8x is the host, most
  likely core placement under the VM (pass 35). Summary item 1 now names
  instructions as the stable figure and limits time comparisons to one
  interleaved run. Searched how to aggregate repeated runs: averaging
  percentiles is wrong, and a pooled histogram can hide one slow run. The
  frame stage reports the median run with that run's own p95/p99/max,
  i.e. a typical run's tail; PERF.md now says so here. Next: nothing
  queued. The remaining questions are device questions.
- Perf pass 50 (2026-10-31): tightening the weakest measurement, host CPU
  time (1.8x session-to-session for identical code, pass 49). Searched
  calibration loops: a dependent add chain is about 1 add per cycle;
  keep the fastest of several runs and pin per CPU (the mhz/calcmhz
  approach; Linux uses different P/E-core scaling factors on hybrid
  Intel). Added `harness/calib.c`; `environment()` now prints a
  calibration line for CPU 2 before every stage. Run 20: every virtual CPU
  runs a busy thread at 5.0-5.55 adds/ns in two rounds, with no E-core-like
  values. So the swing is in paced (sleep-heavy) timing, where wake-up clock
  and placement decide, not in busy-thread placement. Method note: paced
  60 Hz CPU times on this host are partly a host property; instructions are
  the code's cost. Next: nothing queued. Remaining questions are device
  questions.
- Perf pass 51 (2026-10-31): least faithful remaining stand-in, the GL
  backend. Searched `glGet*` costs on mobile drivers: driver-dependent,
  from µs to ms when a query forces a sync; the advice is to cache state.
  By reading the vendored backend (ES3): about 15 `glGetIntegerv` and 5
  `glIsEnabled` per frame to save state, and about 25 calls to restore it.
  That is redundant on Canvas's own EGL context. Recorded as an analysis
  (from source), not a measurement. Compiling the real backend against
  counting stubs would make the count exact, but it uses 56 distinct GL
  functions, which is a lot of work for little gain. Device test plan
  candidate: trace `RenderDrawData` on the render thread. Machine free; no
  timing. Next: nothing queued.
- Perf pass 52 (2026-10-31): write-up. Searched Android GPU Inspector's
  system profiler: it records ATrace, scheduling, GPU counters and Vulkan
  per-call timing, but has no GL ES per-call option. Added two items to
  step 3 of the Device test plan:
  - CPU sampling of the render thread to see time in `RenderDrawData` and
    the driver's `glGet*` (pass 51);
  - noting which installed mods draw per-frame values, which decides build
    C's benefit (run 18).
  No timing. Next: nothing queued. The perf track has converged; further
  passes only revisit results unless new questions come in.
- Perf pass 53 (2026-10-31): a design detail, by reading. Searched ImGui's
  text cursor blink. In the vendored source the caret is drawn when
  `CursorAnim mod 1.20 <= 0.80` (`imgui_widgets.cpp:5007`), with blinking on
  by default (`imgui.cpp:1326`). So while a mod's text field is focused, its
  draw data changes twice every 1.2 s (caret on, caret off). The
  skip-unchanged variant would present about 1.7 times a second for the
  blink alone, plus whatever typing changes, while a field is focused, and
  not at all otherwise. That is small against run 18's live-value case. Not
  measured; no new run. Machine free (Tibik's idle redis/db). Next: nothing
  queued. The perf track has converged.
- Perf pass 54 (2026-10-31): searched SurfaceView against TextureView
  composition (AOSP graphics docs). A SurfaceView gets its own layer that
  SurfaceFlinger can compose as a hardware overlay, but it adds two layers
  against the device's limited overlay planes; a TextureView is composited
  by the GPU into the window, so its pixels are composited twice. Canvas's
  overlay is a SurfaceView, so its cost on a device is mostly a question of
  plane budget and composition type, which the Device test plan already
  checks (per-layer composition type). Nothing new to measure on the host.
  Next: nothing queued.
- Perf pass 55 (2026-10-31): searched Android's low-memory killer for
  context on run 19's font memory. lmkd uses PSI memory-pressure monitors,
  kills by oom_adj score (cached and background first), and Android 11+
  also accounts for thrashing. The guidance for games is to size memory to
  the device's physical RAM and to check `ApplicationExitInfo` for
  low-memory deaths. For the Device test plan's font-memory step this adds
  two observables on a low-RAM phone: `ApplicationExitInfo` reasons across
  sessions, and whether background apps are killed more often with the CJK
  merge than without. No host work. Next: nothing queued.
- Perf pass 56 (2026-10-31): reviewed the Device test plan as a whole; it
  reads as one checklist. Added pass 55's low-memory observables
  (`ApplicationExitInfo` reasons, background kills) to the memory step, and
  to step 4 a reading for memory: build D (no CJK merge) separates the
  font-atlas cost from frame-rate effects. No search beyond pass 55's (same
  topic); no host work. Next: nothing queued.
  Correction to pass 56: a search was run after all (ApplicationExitInfo).
  `getHistoricalProcessExitReasons` returns past exits. `REASON_LOW_MEMORY`
  (3) is reported only where `isLowMemoryKillReportSupported()` is true;
  elsewhere low-memory kills show up as `REASON_SIGNALED` with SIGKILL, so
  the memory step should read both.
- Perf pass 57 (2026-10-31): a Canvas sanitizer container
  (`coverage_all`) was running, so no timing. The peer's redis/db
  containers were idle. Searched ImGui's input trickling
  (`ConfigInputTrickleEventQueue`; issues #4921 and #7889). Wrote
  "Analysis (pass 57)" from `Drain` and the vendored `UpdateInputEvents`:
  - A quick tap whose down and up fall between two overlay frames responds
    one frame later than a tap that spans a frame boundary. Canvas's
    choice not to tag touches avoids a second deferred frame.
  - The delay is in frames, so it follows the frame interval: about 17 ms
    unlimited, 33-50 ms under "Limit FPS" (run 13). Per-frame CPU does not
    enter.
  - Plain typing is not split; only key events interleaved with text are.
  Queued item 10: confirm the frame counts on the host with the real
  `Drain` and ImGui. Next: queue 10 when no sanitizer container is
  running.
- Perf pass 58 (2026-10-31): searched ImGui's button activation (it acts on
  release by default). The `coverage_all` sanitizer container was still
  running while the harness was written; it had exited before the run. Only
  the peer's idle redis/db containers remained.
  - New `taps` scenario in `perf_render.cpp`: one stand-in mod with a
    button and a text field, menu hidden. A script on the render thread
    feeds events through the real JNI entry points between frames.
  - New `stage_taps` (added to ALL).
  - Run 21: quick tap 2 frames (both release positions), tap spanning a
    frame boundary 1, 8 IME characters 1, characters with Shift
    interleaved 4. Identical over 3 runs; matches the pass 57 prediction.
  - Summary item 10 added; summary header updated to passes 1-58.
  Next: nothing queued. Possible verification: the touchscreen-tagged
  design variant, to show the 3-frame case Canvas avoids.
- Perf pass 59 (2026-10-31): searched ImGui's `AddMouseSourceEvent`/
  `ImGuiMouseSource_TouchScreen` (1.89.5, and the "#2702" trickling
  adjustment for touch screens). No Canvas container; the peer's redis/db
  were idle. `stage_taps` now also builds a DESIGN VARIANT, a patched
  scratch copy of `androidbk.cpp` whose `Drain` tags touches as
  touchscreen input. Run 22, frame counts, all 3 runs agreed in each build:
  - quick tap: 3 frames (4 with the release 2 px away), against Canvas's 2;
  - tap spanning a frame boundary: 2, against 1;
  - typing: unchanged.
  This confirms the design note at `androidbk.cpp:341-353` and Canvas's
  build reproduced run 21. Next: nothing queued. Possible check: whether
  the 2-frame floor goes away with trickling off; not a Canvas setting
  today.
- Perf pass 60 (2026-10-31): searched what happens with ImGui's trickling
  off. Non-trickle mode flattens each frame's events, so a down/up pair is
  lost (#2650, #5545). No Canvas container. A peer Tibik campaign container
  was running throughout; frame counts don't depend on load.
  - `stage_taps` variants are now one helper, `taps_variant`, covering
    touchsrc and notrickle. `perf_render.cpp` gains an optional per-frame
    input trace (`TAPS_DEBUG=1`).
  - Run 23: with trickling off, no tap registers, not even one spanning a
    frame boundary. Canvas's lift-off off-screen position lands in the
    release's frame, so the button is not hovered on release. Typing
    flattens to 1 frame.
  - Runs 21-22 reproduced in the same invocation. Conclusion: the 2-frame
    quick tap is the floor for Canvas's input design.
  Next: nothing queued.
- Perf pass 61 (2026-10-31): searched how long a tap lasts (no study found;
  an informal Chrome figure says about half of single-finger gestures end
  within 150 ms) and Android's BufferQueue/frame-pacing latency (Gerrit
  BufferQueueProducer changes; Frame Pacing Library docs). No Canvas
  container; the peer's redis/db were idle and unchanged; load 0.35 -> 0.04.
  - New `taplatency` scenario and `stage_taplatency` (in ALL): random-phase
    taps from the UI thread, with release-to-acting-frame and
    release-to-present on the queued-swap model's vsync clock.
  - Run 24, 100 taps per row, durations 0/50/100/150 ms, limiter off/on:
    - taps of 50 ms or more take 1 frame;
    - release to present p50 is about 59 ms unlimited (BufferQueue full,
      about 3 vsyncs) and 26-28 ms with "Limit FPS";
    - near-instant taps: 76 vs 72 ms.
  - Summary item 10 corrected: pass 57's "limiter triples it" counted
    frame intervals only. Device test plan step 3 gains an overlay
    tap-latency measurement (high-speed camera or Perfetto).
  Next: nothing queued. Possible tightening: the same run under the
  blocking-swap model, to show how much of the unlimited 59 ms is the
  queue-depth assumption.
- Perf pass 62 (2026-10-31): searched BufferQueue buffer counts. AOSP
  commits put the default queue at three slots; `maxDequeued` was lowered
  from 2 to 1 by a legacy flag that has since been removed.
  - `stage_taplatency` now runs both display models (`TAP_MODELS`).
  - Run 25: the first run had two peer Tibik containers that exited
    mid-run; load 2.60 -> 0.97.
    - The queued rows reproduced run 24 within 1 ms.
    - The new blocking rows were repeated on a quiet machine (load 0.82 ->
      0.30) and agreed within 0.3 ms.
    - The queue explains about 2 vsyncs of the unlimited 59 ms. With no
      queueing, unlimited is about 26 ms and limited about 31 ms (p50).
      Which way "Limit FPS" moves overlay tap latency therefore depends on
      the device's buffering. The queued case fits the device cadence (run
      13).
  Loop paused here at the owner's request. Next, if resumed: nothing
  queued; the remaining question is the device's tap-latency measurement.

Cleanup (2026-10-31): `out/` was cleared. Logs and binaries named in this document no longer exist; rerun the named stage to regenerate them. The output quoted here is the record.
