# Device diagnostic: one APK, every proposal, unattended

Turns PERF.md's device test plan into one runtime-switchable debug build and a
script that runs every comparison over adb, without anyone touching the phone.

## The APK

`perfdiag.patch`, applied to `osp-master` `d9a53c2` (worktree
`C:\Users\admin\cpd`, never `app/src/main`), built with `assembleDebug`.

- Package `git.artdeell.skymodloader.mousefix`, label "Canvas (perf diag)",
  versionName `1.9.4-perfdiag`, default debug keystore. It reuses the
  side-by-side mouse-fix package, so `adb install -r` upgrades that build and
  keeps its data, including any Sky sign-in. The real Canvas is not touched.
- Native code is built as release: `CMakeLists.txt` forces
  `CMAKE_BUILD_TYPE Release` with `-O3 -flto`.
- Configured per launch through an intent extra on the exported Mod Manager,
  which then presses Play itself:
  `am start -n <pkg>/git.artdeell.skymodloader.elfmod.ModManagerActivity --es perfcfg 'tag=x,overlay=1,menu=shown,limit=off,skip=0,latch=0,cjk=1,thermal=frozen'`.
  Launched without the extra (from the launcher), it behaves like Canvas.

| Key | Values | What changes |
|---|---|---|
| `overlay` | `1` / `0` | `0`: the ImGui `SurfaceView` is `GONE`, so no overlay layer exists (plan build B) |
| `menu` | `shown` / `hidden` | overrides the "hide Canvas menu" preference for this launch |
| `limit` | `off` / `canvas` / `pace30` / `after` / `afterfence` / `game` | "Limit FPS" on at start; `canvas` is Canvas's 41 ms sleep, `pace30` waits on AChoreographer vsyncs until 31.3 ms have passed (steady 30/s at 60 or 120 Hz). `after` draws one overlay frame per game frame, right after the game's `vkQueuePresentKHR`; `afterfence` waits until that frame is done on the GPU (an empty `vkQueueSubmit` with a fence on the game's queue). Both hook the game's Vulkan presentation with ShadowHook, and fall back to one vsync after 100 ms without a game frame. `game` is `after` with the production fallback: once the game has not presented for `fallback` ms, the overlay draws every vsync until it does |
| `lag` | `0` / N | with `after`, `afterfence` or `game`: the overlay starts its frame N ms after the game's present instead of right away |
| `fallback` | N | ms without a game frame before the overlay falls back to vsync; 70 for `game`, 100 otherwise |
| `heavy` | `0` / `1` | `1`: a second window over the right 42% of the screen with a frame-time graph and 32 rows of widgets, a stand-in for a large open menu with a live value, so it changes every frame. It takes no input and stays clear of the Play button |
| `skip` | `0` / `1` | skip clear, GL submission and `eglSwapBuffers` when the draw data equals the previous frame's (memcmp variant, runs 6-9); a skipped frame waits for the next vsync (plan build C) |
| `latch` | `0` / `1` | the FPS readout updates twice a second (run 11, plan build C') |
| `cjk` | `1` / `0` | `0`: `enableDroidSans = false`, no CJK fallback in the font atlas (run 19, plan build D) |
| `thermal` | `frozen` / `live` | `live`: `GetCurrentThermalStatus` follows a `PowerManager` thermal listener, as vanilla does, instead of Canvas's first-read cache (sanitizer FINDINGS L10) |
| `prio` | `default` / `low` / `high` | the overlay's EGL context priority (`EGL_IMG_context_priority`), so the GPU can favour the game's work |
| `depth` | `24` / `0` | the overlay config's depth bits; ImGui never uses depth |
| `damage` | `0` / `1` | `1`: per frame, clear and swap only the area ImGui draws in, plus what older buffers need (buffer age, `EGL_KHR_partial_update`, `eglSwapBuffersWithDamageKHR`), instead of the whole surface |
| `cpus` | `all` / `little` / `big` | pins the overlay thread to the slowest or the faster cores (by `cpu_capacity`) |
| `nice` | number | `setpriority` for the overlay thread |
| `ptime` | `0` / N | with `limit=pace30`: `eglPresentationTimeANDROID` at the paced vsync plus N periods, so SurfaceFlinger shows frames on an even cadence |

Logged under the `CanvasPerf` tag:
- once per second, from the overlay thread: frames, presents and skips, the
  thread's CPU time, per-frame CPU p50/p95/max, `eglSwapBuffers`
  p50/p95/max, how many frames ended on a faster core, and the mean damaged
  share of the surface;
- once: the config, the font atlas size and glyph count, whether
  Choreographer was available, the EGL depth and stencil bits and the
  context priority actually granted, which extensions are present, the CPU
  classes, and the affinity mask and nice value actually in effect;
- with `limit=after`, `afterfence` or `game`: whether each Vulkan hook went
  in, and per second the game presents the hook saw, the hook's own time on
  the game's thread, the time from the game's present to the overlay starting
  its frame (including `lag`), the fence wait, the waits that timed out, and
  the frames drawn on the vsync fallback;
- every status change, and at least every 5 s while the game polls: the value
  `GetCurrentThermalStatus` returned to the game, the framework's live status,
  and the call count.

## The run

Unattended, from Git Bash on the PC with the phone on USB and debugging
authorised:

```sh
OUT=app/src/test/perf/device/out/<name> bash app/src/test/perf/device/run_resumable.sh
```

`run_resumable.sh` runs `run_device.sh` until it finishes and keeps a Windows PC
from sleeping meanwhile. If the runner crashes, loses the phone, or stops after
repeated failures, it restarts it in the same `OUT` (up to `MAX_RESTARTS`, 6, after
`RESTART_WAIT`, 120 s). Running either script again with the same `OUT` resumes
too, for example after a reboot of the PC:
- each finished run has a `done` marker and is skipped;
- an unfinished or unusable run is moved to `OUT/failed/` and run again;
- the phone's original settings are saved in `OUT/settings.orig` on the first
  start, so a resumed run restores those, not the test values;
- the APK is installed once per `OUT`.

A run counts as finished only if:
- the game launched;
- every sampler segment produced at least two thirds of its expected samples
  and found the app's layers;
- the game process at the end is the one that was measured.

Otherwise it is retried up to `RETRIES` times (2). After `FAIL_STREAK_MAX`
(3) failed runs in a row, the runner stops rather than spend the schedule on
a phone that cannot run them, for example one rebooted behind a PIN. It
waits up to `DEVICE_WAIT` (1800 s) for a disconnected phone. Every adb call
has a timeout, and the logcat readers are stopped on the phone, so nothing
is left running.

Preflight:
- installs the APK with `-r`, and stops if that fails rather than
  uninstalling anything;
- records device facts;
- uses `su` if granted, for GPU busy/clock/temperature from `kgsl` and for
  simpleperf;
- keeps the screen on with fixed brightness, and restores both, plus the
  thermal override, on exit.

Each run:
1. force-stop;
2. wait until the battery is within 1 °C of its starting temperature (capped);
3. launch with the variant's config;
4. wait for `GameActivity` to resume, then `WARMUP` seconds;
5. screenshot;
6. run the on-device `sampler.sh` for `MEASURE` seconds.

`sampler.sh` collects, every 0.5 s: SurfaceFlinger `--latency` for the game
and overlay layers, GPU stats, CPU policy frequencies, the GPU and CPU
frequency limits (max, min, thermal level), and battery temperature, current
and voltage. At its start and end it also snapshots
per-thread and process CPU from `/proc`.

After each run:
- `dumpsys SurfaceFlinger` (composition type per layer, active mode);
- `dumpsys meminfo`;
- `dumpsys thermalservice`;
- a screenshot;
- the logcat of the run.

Phases (`PHASES`, default all):
- `matrix`: `base limit pace30 skip skiplatch hidden hiddenskip nooverlay
  nocjk vanilla`, `ROUNDS` times (default 2). Even rounds run in reverse
  order, so thermal drift does not favour one variant.
- `thermal`: `base` (frozen), `thermallive` and vanilla. `THERMAL_SEG`
  seconds with the framework status overridden to NONE, then the same with
  SEVERE (`cmd thermalservice override-status`). This shows whether the game
  reacts to the status and whether Canvas's cache hides that.
- `profile`: simpleperf, 20 s by thread and by thread and library for the
  process, and 20 s by library and symbol for the overlay thread.
- `sustained`: `SUSTAIN_LIST` (default `base nooverlay`) for `SUSTAIN`
  seconds each (default 600).

Defaults take roughly two hours. Shorter, for example:
`ROUNDS=1 PHASES="matrix thermal" bash .../run_device.sh`, or
`ONLY="base nooverlay" ...`.

Output: `out/<timestamp>/` (git-ignored), with `summary.md` and
`summary.json` written by `summarize.js`.

`summary.md` columns that need a word:
- `game long % (steady)`: the share of game frames at least 1.2 target intervals long
  (40 ms at 30 fps), counted only while the game runs within 7% of the best rate seen
  in any run;
- `game steady / slow s`: seconds in that state and below 75% of it;
- `overlay on big cores %`: frames the overlay thread finished on a faster
  core;
- "Slow state: clocks and their limits": per segment that ran slow, the GPU and
  CPU clocks and their caps (`devfreq/max_freq`, `scaling_max_freq`) while slow
  and while steady. A lower cap means something imposed the slowdown; the same
  cap with a lower clock means the governors settled there.
The game can sit in a slower state for minutes whatever the variant, so plain
means over a run mix two states (see the A52 results).

`node phase.js out/<name> [variant...]` shows when, within the game's frame
cycle, the overlay's frames become ready. It reports the share that land 17-24 ms
after the game's last shown frame, after the game's own frame is ready. It also
prints the game's long frames and that share in 10 s windows, so a phase change
within a run shows up.

## Results: Galaxy A52 (SM-A525F, Android 17), 2026-11-01

Run `out/a52-20261101-1504`, guest character idle in the opening tutorial
scene, no mods, 60 Hz panel mode. Phases: matrix (2 rounds), thermal,
profile, sustained (10 min each); no vanilla Sky, because its content was
never downloaded on this phone. Sky targets 30 fps here (SurfaceFlinger
vote about 29 Hz). Every layer was composited in hardware (HWC `DEVICE`).
The battery stayed at 30.7-31.8 °C, and the thermal status was 0 outside
the override.

1. **The overlay does not lower the game's frame rate.** In the steady state the
   game held 30.1 fps in every variant.
2. **It does add hitches.** With the overlay presenting every vsync and the
   menu shown, 6.6-8.8% of the game's frames took 50 ms instead of 33 ms in
   the matrix runs, and 8.7-10.0% in the thermal runs. When the overlay
   presented nothing (`nooverlay`, `hiddenskip`, `skiplatch`), it was
   3.9-4.3%. The 10-minute runs agree: 6.7% against 3.5%. The groups do not
   overlap across 15 steady-state segments. At 30 fps that is about 2.2
   against 1.2 missed frames a second. The rate falls with fewer overlay
   presents, though run 2 shows that when they land matters more than how
   many there are:
   - `limit` (23/s): 5.0-5.4%;
   - `pace30` (30/s): 5.5-9.1%;
   - `hidden` (60/s, menu hidden but still clearing and presenting):
     5.2-6.9%.
3. **Skip-unchanged plus a latched FPS readout removes it with the menu
   shown.** `skiplatch`:
   - 1.9 presents/s;
   - game long frames 3.9%, the level with no overlay;
   - overlay thread 64 → 18 ms CPU per second.

   `skip` alone does nothing with the menu shown (59 presents/s), because the
   FPS readout changes every frame. Caveat: `skiplatch` and `hiddenskip` each
   have one steady-state run, because their other round was in the slow
   state (item 7).
4. **Cost of presenting every vsync:**
   - CPU: the overlay thread uses 64 ms CPU per second, about 0.96 ms per
     frame. That is 14x the host figure, on the A52's cores. Process CPU is
     5.5-8 points higher (of one core).
   - GPU: 1-2 points more busy at the same clock.
   - Memory: Graphics 310 MB against 246 MB without the overlay. Of that,
     30 MB is the overlay's window buffers (EGL mtrack 90 vs 60 MB) and
     about 28 MB the font atlas.
5. **CJK font merge:**
   - Atlas: 2048x4096 with 3596 glyphs, against 1024x1024 with 711 without
     the merge.
   - Graphics memory: 282 MB against 310 MB.
   - Frame timing: no effect (long frames 6.6-8.5%).
   - Total PSS is not comparable: the phone was swapping (300-650 MB of
     swapped PSS).
6. **Limiters:**
   - **Canvas's `Limit FPS`:** 23.4 presents/s, with 43% of intervals at
     33 ms and 52% at 50 ms, as the host model predicted (run 13).
   - **`pace30`:** 30.1/s, with 82-87% of intervals at 33 ms. The rest are a
     late frame and a catch-up (16.7 + 50 ms), so a fully steady cadence
     would need presentation timestamps (Swappy or
     `EGL_ANDROID_presentation_time`). Run 2's `pace30pt` adds them: 92-100%
     at 33 ms.
   - Neither changed the game's frame rate.
7. **A slow state not caused by the overlay.** For tens of seconds to minutes, the
   game ran at 20 fps:
   - GPU clock 565 → 360 MHz, big cores 1.9 → 1.2 GHz, GPU busy unchanged at
     about 68%;
   - it recovered without input (`sustained/nooverlay`: 270-450 s);
   - not thermal (battery 31 °C, status 0).

   It hit runs with the overlay (`r1-skip`, the SEVERE segment of
   `thermallive`) and without (`r1-nooverlay`, `r1-skiplatch`,
   `r2-hiddenskip`, `sustained/nooverlay`). That was 37% of the measured
   time without overlay presents against 3% with them, but only six
   occurrences, too few to call. It is why the plain means make
   `nooverlay` look slower.
8. **Thermal:** the frozen status is confirmed. With the framework status
   forced to SEVERE, the frozen build told the game 0 on all 12 polls while
   the framework said 3, and the live build passed 3 through. Sky showed no
   measurable reaction in the 60 s at SEVERE: GPU load was unchanged for the
   first 30 s, and the drop after 40 s is item 7's state. The cache remains
   a correctness bug, with its practical effect not shown here.

## Results: run 2, same phone, 2026-11-01

Run `out/a52-20261101-run2` (`launch.sh` there), 17:30-20:58:
- same scene and setup as run 1;
- matrix, 3 rounds of `base prio nodepth damage lean little skiplatch
  nooverlay pace30pt`;
- four 10-minute runs in the order `base skiplatch skiplatch base`;
- no failed attempts and no restarts; the phone's settings were restored;
- battery 28.7-30.6 °C.

The setup lines in `summary.md` confirm each option took effect:
- `prio` got a low-priority context (0x3103);
- `nodepth` got a config without depth or stencil;
- `damage` cleared and swapped 7.6% of the surface after the first three
  buffers;
- `little` got mask 0x3f.

1. **When the overlay draws matters more than how much it draws.** Game
   long frames in the steady state (mean, range over 3 rounds):

   | variant | overlay presents/s | long % |
   | --- | --- | --- |
   | `base` | 60 | 9.8 (8.2-12.2) |
   | `nodepth` | 59 | 8.1 (6.9-9.2) |
   | `prio` | 56 | 7.1 (5.6-8.6) |
   | `damage` | 60 | 6.9 (6.5-7.5) |
   | `little` | 60 | 6.9 (5.3-7.8) |
   | `lean` (prio + nodepth + damage) | 58 | 6.2 (5.1-7.1) |
   | `skiplatch` | 1.9 | 4.5 (2.7-5.4) |
   | `pace30pt` | 30 | 3.7 (3.2-4.0) |
   | `nooverlay` | 0 | 3.7 (2.7-4.7) |

   - Making each present cheaper helps by up to 3.6 points, but never reaches
     the no-overlay level.
   - `pace30pt` reaches it in all three rounds, with presents that cost as
     much as `base`'s. It is the only variant besides `nooverlay` with a
     game p95 of 33.3 ms instead of 50 ms.
   - Run 1's `pace30`, the same rate without presentation times, was at
     5.5-9.1%.
2. **Why:**
   - **A 1-2 ms margin.** The game's buffer is ready a median 15 ms after its
     previous frame is shown (p90 16.6-17.1 ms), and is shown at the next
     vsync about 18 ms later. A buffer ready after about 17 ms waits one
     vsync more, and is shown about 31 ms after it was ready. That accounts
     for 99-100% of long frames without the overlay. With the overlay
     presenting every vsync it is 68-100%, and the rest were ready even
     later. So the game runs with a margin of 1-2 ms.
   - **The overlay finishes inside that window.** In `base` the overlay's
     frames finish all through the game's cycle, and only 11-16% land
     17-24 ms in, after the game's frame. With the overlay, the game's p90 is about
     0.3 ms later. `pace30pt` frames land there 92-94% of the time
     (`phase.js`).
   - **Within one run, only the phase changed.** In run 1's `r1-pace30`,
     30 s in, the overlay's share after the game's frame moved from 14-26% to
     83-100% by itself. The game's long frames went from 7.7-8.0% to 3.0-3.7%.
     Same run, rate and scene.
   - **Not established: why `pace30pt` lands there every round.** The pacer
     picks which vsync of each pair it uses at startup. Without presentation
     times it landed there in one round of two, after 30 s. Three rounds out
     of three is a 1-in-8 chance if the choice is random. A production version
     should time the overlay from the game's frame, not rely on this. Run 3
     found it did not hold the phase (item 2 there).
3. **Per-present costs:**
   - `damage`: GPU busy 1.1 points lower, swap p50 1159 → 1038 µs.
   - `nodepth`: Graphics memory unchanged at 310-311 MB, so the depth buffer
     cost no counted memory.
   - `prio`: 56 presents/s instead of 60.
   - `little`: without pinning, the overlay thread finished 92% of its
     frames on the two big cores, the ones the game's main thread needs. On
     the small cores its CPU time doubles (137 against 65 ms/s, 2.2 ms per
     frame), and process CPU rises 6 points.
4. **The slow state is a governor equilibrium, not throttling.** In all 11
   segments with slow time, the frequency limits never moved: GPU max
   750 MHz, minimum 267 MHz, thermal level 0, CPU maximums 1805/2323 MHz.
   - **Clocks while slow:** GPU 366-448 MHz instead of 565, big cores mostly
     about 1.3 GHz instead of 1.9-2.0.
   - **GPU busy:** 57-70% while slow, against 69-72% while steady.
   - **Same work per frame in both states:** 565 MHz × 72% × 33.3 ms is
     about 13.6 million GPU cycles, and 370 MHz × 68% × 50 ms about 12.6
     million.
   - **So:** the GPU governor sees about the same load at 30 fps and 565 MHz
     as at 20 fps and 370 MHz, and has no reason to leave either state.
5. **Overlay presents shorten slow episodes but do not prevent them.**
   - In the 10-minute runs, `base` spent 18 and 30 s slow and `skiplatch`
     62 and 75 s.
   - Every 10-minute run, in both runs, had one or two episodes.
   - Episode lengths, from 5 s windows below 23 fps:
     - `base`: 25 s (cut short by the end of its run) and 35 s;
     - `skiplatch`: 35, 35 and 75 s;
     - run 1's `base`: only dips of 5-15 s;
     - run 1's `nooverlay`: one of 155 s.

     That is a small sample.
   - Over 10 minutes the two roughly cancel. `skiplatch` had 3.8-4.9%
     steady-state long frames against 8.0-8.2%, but 28.8 fps against 29.6,
     and 10.3-11.6% long frames overall against 10.0-10.6%.
   - This is the phone's governors, with or without the overlay. Vanilla Sky
     was not measured, so Canvas's loader and hooks are not ruled out. The
     overlay's every-vsync work happens to keep the clocks from settling low.
6. **`pace30pt` cadence:** 92-100% of the overlay's intervals at 33 ms,
   against run 1's 82-87% without presentation times.

What this suggests for Canvas:
- Draw the overlay right after the game's frame, at most once per game frame,
  and skip frames that are unchanged.
- Canvas does not hook the game's `eglSwapBuffers` today. The only call is
  the overlay's own, in `androidbk.cpp`. So timing from the game's frame needs
  a new hook, and its effect needs a device run. Run 3 is that run; Sky
  presents through Vulkan here.
- The per-present savings (`damage`, `prio`) are small next to timing.
- Pinning the overlay to the small cores trades the game's big-core time for
  twice the overlay CPU.

## Results: run 3, same phone, 2026-11-01

Run `out/a52-20261101-run3` (`launch.sh` there), 21:38-23:59, times the
overlay from the game's frames:
- same scene and setup as runs 1 and 2;
- matrix, 3 rounds of `base nooverlay pace30pt after afterfence afterskip
  skiplatch`, where `afterskip` is `afterfence` plus skip-unchanged and the
  latched readout;
- four 10-minute runs in the order `base afterfence afterfence base`;
- no failed attempts and no restarts; the phone's settings were restored;
- battery 29-31 °C.

The dry run before it (`out/dryrun-after`) and the setup lines in
`summary.md` show:
- Sky presents through Vulkan here: `dumpsys gpu` lists a Vulkan device and
  swapchain under the engine name "Sky", and the single GLES context is the
  overlay's;
- all three hooks went in on every run, and saw the game's 30 presents/s;
- the game's frame finishes on the GPU a median 14 ms after its
  `vkQueuePresentKHR` (the fence wait).

1. **Drawing once per game frame, right after the game presents, reaches the
   no-overlay level in every round.** Steady state (mean, range over 3
   rounds):

   | variant | overlay presents/s | long % | game p95 ms |
   | --- | --- | --- | --- |
   | `base` | 60 | 9.0 (8.3-10.0) | 49.8 |
   | `pace30pt` | 30 | 7.9 (7.7-8.2) | 49.8 |
   | `skiplatch` | 1.9 | 5.6 (5.4-5.8) | 49.8 |
   | `afterfence` | 30 | 5.4 (4.7-5.9) | 44.3 (33.3-49.8) |
   | `nooverlay` | 0 | 5.3 (5.0-5.6) | 49.8 (33.3-66.4) |
   | `after` | 30 | 4.9 (3.8-6.6) | 38.8 (33.3-49.8) |
   | `afterskip` | 1.9 | 4.3 (3.4-4.8) | 33.4 (33.3-33.5) |

   - `after`, `afterfence` and `afterskip` are within a point of
     `nooverlay`. `afterskip` is the only variant with a p95 of 33.3 ms in
     all three rounds.
   - The 10-minute runs agree: `afterfence` had 4.5 and 5.0% steady-state
     long frames against `base`'s 7.4 and 8.3%.
   - The fence is not needed: `after` is as good as `afterfence`. The dry
     run's hint that `after` would not be was wrong.
   - `nooverlay` is 1.6 points higher than in run 2 and `base` 0.8 lower, so
     compare within a run.
2. **`pace30pt`'s run 2 result was a lucky phase.**
   - In run 3 it put 81-96% of its frames 17-24 ms after the game's shown
     frame only for the first 30 s of round 1, with 4.3-4.4% long frames.
     Then it slipped to 18-27% and 7.3-12.3% long frames.
   - Rounds 2 and 3 never had that phase (19-20%) and had 7.8%.
   - That is a second natural experiment within one run, after run 1's
     `r1-pace30`, and it points the same way. Pacing from vsync does not hold
     the phase.
3. **At the same rate, the trigger decides.** `after` and `pace30pt` both
   present 30 times a second at the same cost, with 3.8-6.6% against 7.7-8.2%
   long frames. When the overlay's frame finishes does not explain it alone:
   - `after` frames finish a median 0.3 ms after the game's (90-96% within
     6 ms), `afterfence` and `afterskip` frames 1.4 ms after (100%).
   - Run 2's `prio` and `lean`, at 55-58 presents/s, also finished 0.3-0.5 ms
     after the game's frame (93-100%), but had 5.1-15.5% long frames, and
     `pace30pt` in rounds 2-3 here (0.3 ms, 84%) had 7.8%.
   - A likely reading is that the overlay hurts when its GPU work is queued
     ahead of the game's frame, which a trigger from the game's present rules
     out. The SurfaceFlinger timestamps show only when each buffer finished,
     so this is not established.
4. **Costs:**
   - **On the game's thread:** the hook adds 11 µs per game frame for
     `after` (take the time, wake the overlay thread), and 24-25 µs with the
     fence's empty submit.
   - **Latency:** `after` starts drawing 0.18 ms after the game's present.
     `afterfence` waits for the game's frame to finish on the GPU, 14 ms
     (fence wait p50 13.9-14.1 ms), which adds to the menu's input-to-screen
     time.
   - **Overlay CPU halves with the rate:** ImGui 28-30 ms/s against 62, swap
     p50 0.50-0.59 ms against 1.12 ms. Process CPU is 100% against 105%
     (`nooverlay` 94%).
   - **Fallback:** the overlay waits at most 100 ms for a game frame, then
     draws at the next vsync. In the first 15 s after launch, before the game
     presents, the overlay drew 9-10 frames/s. After that, a few seconds per
     run had 1-8 timeouts.
5. **Slow state:**
   - **10-minute runs:** `base` spent 35 s slow (round 1, from the start)
     and 0 s (round 4). `afterfence` spent 0 s (round 2) and 7 s (round 3,
     from the start).
   - **Matrix:** `nooverlay` spent 40 s slow in round 1.
   - **Fewer episodes than run 2:** two of the four 10-minute runs had none,
     where every one in run 2 had one or two.
   - **Not lengthened:** `afterfence`'s 30 presents/s did not lengthen
     episodes the way `skiplatch`'s 1.9/s did in run 2 (35-75 s). Two
     episodes are too few to say more.
   - **Over 10 minutes `afterfence` came out ahead:** 30.0 fps against 29.7,
     and 5.0-5.6% long frames overall against 8.7-10.8%. Run 2's `skiplatch`
     roughly cancelled out.
   - **Clocks while slow:** GPU 372-484 MHz, as in run 2, with the limits
     unchanged.

What this suggests for Canvas:
- **When to draw:** once per game frame, triggered from a hook on the game's
  present, without a fence. Skip-unchanged on top also cuts presents while
  the menu is static.
- **Where to hook:** on this phone, Sky presents through Vulkan.
  - ShadowHook, which Canvas already uses, can hook libvulkan's
    `vkQueuePresentKHR`, one function shared by every device.
  - The addresses come from a throwaway instance and device created at
    startup.
  - Where Sky renders with GLES, the hook would go on `eglSwapBuffers`
    instead. That is untested.
- **Fallback:** keep a vsync fallback for when the game stops presenting.
  Waiting 100 ms gave a 10 fps menu while loading, so a production version
  should fall back sooner, for example after two missed game frames.
- **Trade-off:** the overlay then updates at the game's rate, 30/s here,
  instead of 60/s.

## Results: run 4, same phone, 2026-11-02

Run `out/a52-20261102-run4` (`launch.sh` there), 00:26-02:25, tests the
production design before it goes into Canvas:
- same scene and setup as runs 1-3;
- matrix, 3 rounds of `base nooverlay prod game lag4 lag8 lag16 lag24
  heavybase heavyprod`, no 10-minute runs:
  - `prod` is `limit=game` with skip-unchanged and the latched readout, the
    design as it would ship;
  - `game` and `lagN` redraw every game frame, starting N ms after the game's
    present;
  - `heavybase` and `heavyprod` are `base` and `prod` with `heavy=1`;
- no failed attempts and no restarts; the phone's settings were restored;
- battery 29-30 °C.

Three runs (`r2-game`, `r3-lag4`, `r3-heavyprod`) logged "Play still showing
after 3 taps". Their screenshots show the game world, so the title check was
late, not the tap.

1. **The production design matches no-overlay.** Steady state (mean, range
   over 3 rounds):

   | variant | overlay presents/s | long % | game p95 ms |
   | --- | --- | --- | --- |
   | `heavybase` | 60 | 12.3 (12.0-12.6) | 49.9 |
   | `base` | 60 | 10.1 (9.4-11.3) | 49.8 |
   | `heavyprod` | 30 | 5.7 (4.6-6.8) | 44.3 (33.3-49.8) |
   | `nooverlay` | 0 | 4.8 (3.1-6.1) | 49.8 (33.3-66.4) |
   | `prod` | 1.8 | 4.7 (3.7-6.1) | 38.8 (33.2-49.8) |

   - **Static menu:** `prod` is at the no-overlay level. Process CPU is
     97.4%, against 94.0% without the overlay and 104.7% for `base`.
   - **Large menu that changes every frame:** `prod` redraws every game
     frame and stays within a point of no-overlay (5.7%). `base` gets worse
     (12.3%), about 2.5 times the no-overlay level against `heavyprod`'s 1.2.
   - **Heavy window's cost:** GPU busy 75.1% against 72.2% at 60
     presents/s, and 72.7% against 70.2% with `prod`.
2. **The draw can start up to 16 ms after the game's present. At 24 ms most
   of the benefit is gone.**

   | lag | long % | overlay ready after the game's |
   | --- | --- | --- |
   | 0 ms (`game`) | 6.3 (5.7-6.7) | 0.3 ms |
   | 4 ms | 5.7 (4.6-7.0) | 0.3 ms |
   | 8 ms | 5.4 (5.1-5.6) | 0.3 ms |
   | 16 ms | 5.3 (5.0-5.8) | 3.5 ms |
   | 24 ms | 8.0 (6.7-9.6) | 26 ms |

   - **Up to 8 ms:** the game's frame finishes on the GPU 14 ms after its
     present. The overlay's work queues behind it and finishes 0.3 ms after.
   - **At 16 ms:** it runs once the game's frame is done.
   - **At 24 ms:** it finishes 6-12 ms after the game's last shown frame, the
     slot where most of `base`'s frames cluster. That is during the game's
     next frame, before its deadline.
   - **What it supports:** run 3 item 3's reading. The overlay costs the
     game a vsync when its GPU work lands in the game's next frame before the
     deadline. Running behind the game's frame or in the idle gap after it
     costs little.
   - **What it doesn't show:** the GPU queue itself; this is still read off
     completion times.
   - **No lag:** `game` at 6.3% is 1.5 points above `nooverlay` here. Run 3's
     `after`, the same timing, was 0.4 points below, so the lags from 0 to
     16 ms differ by less than the spread between runs.
3. **Fallback:** in every `game`-mode run, the overlay drew 59-61 frames/s
   during the 12-14 s of loading before the game's first present, against
   9-10 in run 3.
   - After that, 140-270 frames per run were drawn on the fallback, during
     the load from title to world.
   - In the measured minute, it drew at most 0.02 fallback frames/s.
4. **Slow state:** `nooverlay` spent 44 s slow in round 1, as in run 3, and
   `lag16` 7 s in round 2. No other run had any.

What this suggests for Canvas:
- **The design to implement:**
  - hook the game's present and draw once per game frame;
  - skip unchanged frames;
  - fall back to every vsync after 70 ms without a game frame.

  On this phone it matches no-overlay with a static menu, and stays within a
  point with a large menu that changes every frame.
- **Keep the overlay's frame short:** everything between the trigger and the
  GPU submit counts, including mods' draw callbacks.
  - Starting the draw within 16 ms of the game's present, half a frame at
    30 fps, keeps the benefit; at 24 ms most of it is gone. On the OnePlus 13,
    16 ms already loses most of it (run 5) and 8 ms does not (run 6).
  - The window is set by the game's frame, so at 60 fps it would be about
    half as long. That is untested.

## Results: OnePlus 13 (CPH2653, Android 16), 2026-11-02

Run `out/op13-20261102-run5` (`launch.sh` there), 05:34-07:23, repeats the
core of run 4 on a second phone:
- Snapdragon 8 Elite (Adreno 830), screen at 1080x2376;
- the panel can switch between 60, 90 and 120 Hz, but every sample reported
  60 Hz;
- Sky ran at 30 fps and presents through Vulkan here too, and the present hook
  went in;
- "Canvas (perf diag)" was a fresh install, so Sky started as a new guest. The
  character idles in the opening tutorial scene at the same hint as on the A52
  ("Drag the screen with two fingers to look around");
- matrix, 3 rounds of `base nooverlay prod heavybase heavyprod lag16`;
- no failed attempts and no restarts; the phone's settings were restored;
- battery 26-29 °C. From the third run on, the cooldown hit its 150 s cap with
  the battery 1-2 °C above its start.

In 10 of 18 runs Sky skipped its title screen and loaded straight into the
world. The runner then waited its full 120 s for the title, so those runs
measured after about 3 minutes in the world instead of 1. All 36 start and end
screenshots show the world. `r3-base`'s "crash in logcat" was another app's
service (`com.oceanwing.soundcore`); the game's process was the same at start
and end.

1. **The production design matches no-overlay here too.** Steady state (mean,
   range over 3 rounds):

   | variant | overlay presents/s | long % | game p95 ms |
   | --- | --- | --- | --- |
   | `base` | 60 | 5.5 (5.1-5.6) | 49.8 |
   | `heavybase` | 60 | 4.4 (3.8-5.1) | 38.7 (33.2-49.8) |
   | `lag16` | 30 | 4.3 (4.0-4.8) | 33.2 |
   | `nooverlay` | 0 | 1.0 (0.6-1.5) | 33.2 |
   | `prod` | 1.8 | 1.0 (0.6-1.4) | 33.2 |
   | `heavyprod` | 30 | 0.9 (0.7-1.2) | 33.2 |

   - **Static menu:** `prod` is at the no-overlay level. Process CPU is
     80.5%, against 79.9% without the overlay and 86.8% for `base`.
   - **Large menu that changes every frame:** `heavyprod` is at the
     no-overlay level too. Unlike on the A52, `heavybase` is not worse than
     `base`.
   - **GPU:** 23-26% busy at 222 MHz in every variant; the clock never rose.
   - **Compared with the A52:** fewer long frames overall (1.0% without the
     overlay against 4.8%), but `base` still has about five times the
     no-overlay level.
2. **A 16 ms lag loses most of the benefit on this phone.** `lag16` has 4.3%
   long frames, against 1.0% for `prod` and 5.5% for `base`. On the A52, 16 ms
   was within the spread of no-overlay.
   - **The game's frame is done when its present returns:** a separate
     check launch with `afterfence` (40 s in the world, not part of the
     matrix) waited 14 µs for the game's queue (median; p95 20 µs), against
     about 14 ms on the A52.
   - **What SurfaceFlinger shows here:** the ready time equals the queue
     time for every game frame. So `phase.js`'s game "ready" on this phone
     is when the game queued the frame. The game queues each frame about
     2 ms after its previous frame is shown, and the frame is shown two
     vsyncs later.
   - **Where the overlay's frames land:**
     - `prod` and `heavyprod`: ready 2.5 ms after the game queues, all
       within 6 ms;
     - `lag16`: 22 ms after the game queues, 23-24 ms after the game's
       shown frame;
     - `base`: two clusters, 6-10 ms and 24-26 ms after the game's shown
       frame.
   - **Game queue depth:** in `base` rounds 1-2 and `heavybase` round 1, the
     game queued frames two deep, 14 ms before its previous frame was shown.
     Long frames were the same as in `base` round 3, which queued like the
     other variants.
   - **What it supports:** on both phones, the cheap slot is right behind
     the game's present. Here the game's frame is already finished by then,
     so what `lag16` runs into must be later work, most likely the game's
     next frame. That is not observed directly.
   - **Lags between 0 and 16 ms:** run 6 found 4 and 8 ms at the no-overlay
     level.
3. **Fallback:** Sky takes about 3 s to its first present here, and the
   overlay drew at the vsync rate meanwhile.
   - Over each run, 269-309 frames were drawn on the fallback, during the
     load from launch to world.
   - None were drawn in the measured minutes.
4. **Hook:** 33-36 µs per game frame on the game's thread, against 11 µs on
   the A52 in run 3. The overlay's frame starts 71-75 µs after the game's
   present.
5. **Slow state:** none. Every run spent 59-60 s of its minute steady.

What this suggests for Canvas:
- **The design holds on a second phone:** with a much faster GPU, `prod`
  and `heavyprod` are at the no-overlay level and `base` has five times as
  many long frames.
- **Start the draw right after the game's present:** keep everything up to
  the overlay's GPU submit short, including mods' draw callbacks.
  - 16 ms was safe on the A52 but not here; run 6 puts this phone's limit
    between 8 and 16 ms.

## Results: run 6, OnePlus 13, 2026-11-02

Run `out/op13-20261102-run6` (`launch.sh` there), 08:06-08:59, fills in
run 5's gap between no lag and 16 ms:
- same phone, scene and setup as run 5;
- matrix, 3 rounds of `nooverlay game lag4 lag8 lag16`. `game` is the
  no-lag control: like the `lagN` variants it redraws every game frame;
- `TITLE_MAX=30`, so a launch that skips the title waits 60 s for it instead
  of 120 s. Sky skipped it in 9 of 15 runs; all 30 start and end screenshots
  show the world;
- no failed attempts, no restarts and no crashes in logcat; the phone's
  settings were restored;
- battery 27-29 °C, no slow state (58-60 s steady in every run).

1. **Up to 8 ms of lag is at the no-overlay level; 16 ms is not.** Steady
   state (mean, range over 3 rounds):

   | lag | long % | overlay ready after the game's shown frame |
   | --- | --- | --- |
   | no overlay | 1.0 (0.4-1.5) | - |
   | 0 ms (`game`) | 1.2 (1.1-1.4) | 2-6 ms |
   | 4 ms | 1.4 (0.9-1.7) | 6-10 ms |
   | 8 ms | 1.3 (1.0-1.5) | 10-14 ms |
   | 16 ms | 4.4 (4.2-4.5) | 20-26 ms |

   - **0-8 ms:** all three are within the no-overlay spread; game p95
     33.2 ms everywhere.
   - **16 ms:** reproduces run 5 (4.3%).
   - **Where the boundary sits:** overlay frames that finish up to 14 ms
     after the game's shown frame cost nothing measurable here; frames that
     finish 20-26 ms after it cost a vsync now and then. `base` in run 5 had
     one cluster in each slot (6-10 and 24-26 ms).
   - **Cost:** process CPU 83.4-84.4% with any lag against 78.8% without
     the overlay; GPU busy 23.8-24.5% against 23.5%.

What this suggests for Canvas, with runs 4 and 5:
- **Budget for the overlay's frame:** starting the draw within 8 ms of the
  game's present kept the no-overlay level on both phones. That is a quarter
  of a frame at 30 fps.
- **Beyond that:** 16 ms was still fine on the A52 but not on the OnePlus 13.
- **What counts:** everything between the trigger and the GPU submit,
  including mods' draw callbacks.

Not covered:
- vanilla Sky;
- movement or camera motion;
- real mods (the `heavy` window stands in for a large menu that changes every
  frame; with a mod drawing a live value, `skiplatch` presents more often, host
  run 18);
- Sky at 60 fps and higher refresh modes (both phones ran Sky at 30 fps on a
  60 Hz panel);
- GPUs other than Qualcomm's Adreno (both phones are Snapdragon), and Sky
  rendering with GLES;
- lags between 8 and 16 ms on the OnePlus 13.

## Caveats

- USB keeps the phone charging, which adds heat and makes battery current
  readings only roughly comparable.
- The scene is wherever Sky loads after `WARMUP`. If the perf-diag package is
  not signed in to Sky, that is the sign-in screen. The screenshots show it,
  and a one-time sign-in in "Canvas (perf diag)" fixes it.
- On a fresh install (as on the OnePlus 13), Sky creates a new guest and plays
  its intro first. Launch it once with any `perfcfg` and wait until the
  tutorial scene shows, about 3 minutes, before starting a run.
- Sky does not always stop at its title screen. When it loads straight into
  the world, `press_play` waits `TITLE_MAX` x 2 s (120 s) first, which only
  lengthens the warmup.
- Vanilla Sky keeps its own game settings (FPS, quality), which may differ
  from Canvas's. Its frame-rate vote in `sf.txt` shows what it asked for.
- The thermal override changes the status every app sees for the length of
  the segment; the script resets it afterwards and on exit.
