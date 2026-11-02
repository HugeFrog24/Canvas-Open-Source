# Hardware-mouse Y axis inverted in GameActivity (Canvas only)

Evidence log for the owner's report. Explains, does not fix: no file under
`app/src/main` is changed by this investigation.

## Symptom (owner's report)

- A USB or Bluetooth mouse's vertical axis is inverted in the landscape
  `GameActivity` when Canvas is installed.
- Not in vanilla Sky, and not in the Mod Manager window (`MainActivity`,
  plain Android views).
- The same with or without mods, with the original or a custom
  `libBootloader.so`, with Canvas alone and no lib loaded, and after a
  settings reset.
- Seen on several devices, from a OnePlus 13 to a Samsung A52.

The owner's report does not say which in-game effect is inverted. The game
gets hardware-mouse Y on two paths (below): the cursor position (hover
highlights) and the camera drag delta. Both are affected.

## Mouse-to-game Y paths in Canvas

All in `app/src/main/java/com/tgc/sky/GameActivity.java` at the time of
writing (branch `fix/canvas-process-restoration`, HEAD `5881a85`).

| Path | Canvas | What reaches the game |
|---|---|---|
| Hover (`ACTION_HOVER_MOVE/ENTER/EXIT`) | `onGenericMotionEvent` :1071-1077: `transformPointToProgram(getX(), getY())` -> `onMouseMoved` -> `onMouseMovedNative(x, y)` (:729-732) | cursor position |
| Primary-button drag (`getButtonState() == 1`) | `dispatchTouchEvent` :746-775: `transformPointToProgram(getX() + offset, getY() + offset)`; `dx = (x - last.x) * 3`, `dy = -(y - last.y) * 3` -> `onMouseDeltaNative`; then `onMouseMoved(point)` | camera delta and cursor position |
| Wheel (`ACTION_SCROLL`) | `onGenericMotionEvent` :1079-1092: `AXIS_VSCROLL`/`AXIS_HSCROLL` -> key taps 10/11 and `onMouseScrollingDeltaNative` | not checked against stock yet |
| Captured pointer | `onCapturedPointer` :300-315: releases capture for anything but Sony touchpads (since #45) | nothing for a mouse |
| Overlay (ImGui) | `submitImGuiMousePosition` :711-720: raw view-relative coordinates to `ImGUI.submitPositionEvent` | the overlay only; it works |

The transform both game paths use (:722-727):

```java
private PointF transformPointToProgram(float x, float y) {
    PointF p = new PointF();
    p.x = transformWidthToProgram(x);   // identity (:195)
    p.y = transformHeightToProgram(y);  // identity (:193)
    return p;
}
```

## Stock comparison (Sky 0.34.5 and 0.35.1, decoded smali, read only)

`C:\Users\admin\Desktop\sky-decoded\0.34.5\smali_classes3\com\tgc\sky\GameActivity.smali`
(0.35.1 has the same method body):

```smali
.method public transformPointToProgram(FF)Landroid/graphics/PointF;   # :6465
    invoke-virtual {p0}, ...GameActivity;->getWindow()
    invoke-virtual {v0}, ...Window;->getDecorView()
    invoke-virtual {v0}, ...View;->getHeight()I
    int-to-float v0, v0
    sub-float/2addr v0, p2              # v0 = decorViewHeight - y
    new-instance p2, Landroid/graphics/PointF;
    invoke-direct {p2, p1, v0}, ...PointF;-><init>(FF)V   # (x, height - y)
```

- Stock's `transformHeightToProgram`/`transformWidthToProgram` are identity
  too (:6453, :6566). The flip lives only in `transformPointToProgram`.
- Stock's `dispatchTouchEvent` (:2633) computes the drag delta exactly as
  Canvas does: `dx = (x - last.x) * 3.0f`, then `neg-float` and
  `mul-float 3.0f` for `dy`, then `onMouseDeltaNative`. The difference is
  the point it is computed on, which stock has flipped.
- Stock's hover path (`onGenericMotionEvent` :4478, smali lines 221-234 of
  the method) also goes through `transformPointToProgram` -> `onMouseMoved`.
- Canvas's own `transformPointToSystem` (:1190-1192) keeps the flip
  (`getDecorView().getHeight() - f2`). Only the to-program direction lost
  it.

| Path | Stock (program space, origin bottom-left) | Canvas (origin top-left) | Effect in Canvas |
|---|---|---|---|
| Hover position | `(x, H - y)` | `(x, y)` | cursor Y mirrored: moving up is seen as moving down |
| Drag delta | `dy = -((H - y) - (H - last)) * 3 = (y - last) * 3` | `dy = -(y - last) * 3` | camera vertical delta has the opposite sign |
| Wheel | `AXIS_HSCROLL` (0xa), `AXIS_VSCROLL` (0x9); key 11 if v < 0, 10 if v > 0 (press then release); `onMouseScrollingDelta(h, v)`, no transform (`onGenericMotionEvent` :4478, `pswitch_2`) | the same (:1079-1092), plus an early exit to ImGui when `wantsMouse()` | none: parity |

Decompiled stock (jadx 1.5.0 on the decoded smali, in `canvas-mouse-harness:local`;
output in `out/stock-0.34.5/`, git-ignored):

```java
public PointF transformPointToProgram(float f, float f2) {
    return new PointF(f, getWindow().getDecorView().getHeight() - f2);
}
...
PointF transformPointToProgram = transformPointToProgram(motionEvent.getX(), motionEvent.getY());
if (motionEvent.getButtonState() == 1) {
    onMouseDeltaNative((int) ((transformPointToProgram.x - this.m_lastMouseLocation.x) * 3.0f),
                       (int) ((-(transformPointToProgram.y - this.m_lastMouseLocation.y)) * 3.0f));
}
onMouseMoved((int) transformPointToProgram.x, (int) transformPointToProgram.y);
```

## Host harness (pass 3)

`run_mouse.sh` in `canvas-mouse-harness:local` (`Dockerfile.mouse`; command
at the top of the script).

What is real and what is a stand-in:
- Real, on each side: 15 methods extracted verbatim by the same awk rule.
  - Canvas's come from `GameActivity.java`, plus its
    `updateGameInputOffset` and `submitImGuiMousePosition`.
  - Stock's come from jadx's decompile of the 0.34.5 smali.
  - The methods: `isSony`, `isDualShock`, `isDualSense`, `isGamepadEvent`,
    `getDeviceVendorId`/`ProductId`, `isHardwareMouseEvent`,
    `isGamepadWithTouchpadEvent`, `transformPointToProgram`,
    `transformHeightToProgram`/`WidthToProgram`, `onMouseMoved`,
    `onMouseScrollingDelta`, `dispatchTouchEvent`, `onGenericMotionEvent`.
- Stand-ins, the same for both sides (`harness/`):
  - stub `android.view`/`graphics`/`os`/`widget` classes;
  - a landscape 2400 x 1080 window with the game view at the origin;
  - a USB mouse device (source `0x2002`, not virtual);
  - `ImGUI.wantsMouse() == false`, a pointer outside the overlay;
  - `MouseBase`, which supplies the remaining members and records each
    native call.

Output (`out/run_mouse.log`; view coordinates, so moving down the screen
means increasing y):

```
== hover: pointer moves down the screen, y 300 -> 700
  DIFFERS  canvas: onMouseMovedNative(1200, 300)   stock: onMouseMovedNative(1200, 780)
  DIFFERS  canvas: onMouseMovedNative(1200, 700)   stock: onMouseMovedNative(1200, 380)
== primary-button drag: hover to y 500, press, move down to 600, then up to 400, release
  DIFFERS  canvas: onMouseMovedNative(1200, 500)   stock: onMouseMovedNative(1200, 580)
  same     canvas: onMouseDeltaNative(0, 0)        stock: onMouseDeltaNative(0, 0)
  DIFFERS  canvas: onMouseMovedNative(1200, 500)   stock: onMouseMovedNative(1200, 580)
  DIFFERS  canvas: onMouseDeltaNative(0, -300)     stock: onMouseDeltaNative(0, 300)
  DIFFERS  canvas: onMouseMovedNative(1200, 600)   stock: onMouseMovedNative(1200, 480)
  DIFFERS  canvas: onMouseDeltaNative(0, 600)      stock: onMouseDeltaNative(0, -600)
  DIFFERS  canvas: onMouseMovedNative(1200, 400)   stock: onMouseMovedNative(1200, 680)
  DIFFERS  canvas: onMouseMovedNative(1200, 400)   stock: onMouseMovedNative(1200, 680)
== wheel: one notch with AXIS_VSCROLL = +1
  same     canvas: onButtonPress(10, 3, true)                stock: same
  same     canvas: onButtonPress(10, 3, false)               stock: same
  same     canvas: onMouseScrollingDeltaNative(0.0, 1.0)     stock: same
calls that differ: 9
```

- Hover: as the pointer moves down, stock sends a decreasing y
  (1080 - y), Canvas an increasing one.
- Drag: every vertical delta has the opposite sign.
- X, the wheel and button taps are identical.
- An earlier run without the hover before the press also showed a first
  delta measured from the initial `(0, 0)` anchor. Both sides do that; it
  is not a Canvas difference.

This is the second way, after the static comparison, and it is real code on
both sides. The game's own interpretation of these arguments is not run here.
That is left to the device log (item 3).

## Ranked hypotheses

1. **Confirmed by static comparison: Canvas's `transformPointToProgram`
   drops stock's `decorViewHeight - y` flip.** Both game-facing Y paths,
   cursor position and drag delta, are mirrored relative to stock. That
   fits every part of the report:
   - Canvas only: the Java layer is Canvas's.
   - Independent of mods and of `libBootloader.so`: it is Java, before any
     native code.
   - Not in the Mod Manager: plain Android views.
   - Every device: no device-specific code is involved.
   The negated `dy` line is stock-identical; it is not the bug on its own.
   Reproduced on the host with real code on both sides (pass 3), against
   stock 0.34.5 and 0.35.1 alike (pass 5). Consistent with Canvas's own
   inverse transform (pass 5). Pending: the owner's device log (item 3).
2. Ruled out (pass 2): the wheel path. Axes, key taps 10/11 and the
   unflipped scroll delta match stock.
3. Not a Y inversion: Canvas adds `m_gameInputOffset` to the drag path's
   coordinates (:749-751); stock passes raw `getX()/getY()`. That would
   offset the cursor, not mirror it.

## Who introduced it

`git log -S` on `GameActivity.java`:
- `transformPointToProgram` (identity version), the drag path with `dy`
  negated, and an `onCapturedPointer` that sent `(dx, -dy)` all arrived in
  **24f93b0 "Canvas 1.7.2 Release", MisterGatto, 2026-03-15**. That is a
  squashed release commit (15 files, +1,656/-1,311). The 2023 open-source
  version had none of these mouse paths.
- 5c0b41b (#45, HugeFrog24, 2026-08-31) removed the `onCapturedPointer`
  delta and added the overlay bridge. It kept 24f93b0's
  `transformPointToProgram` and drag delta.

### Three-way run: stock, Canvas 1.7.2 (before #45), Canvas HEAD (pass 4)

`out/canvas-1.7.2/GameActivity.java` is `git show 5c0b41b^:...`, the
parent of #45. `run_mouse.sh` adds it as a third side, plus a
captured-pointer scenario. Same events as above.

| Path | Stock 0.34.5 | Canvas 1.7.2 | Canvas HEAD |
|---|---|---|---|
| Hover y, pointer moving down (300 -> 700) | 780 -> 380 | 300 -> 700 | 300 -> 700 |
| Drag `dy`, moving down 100 / up 200 | +300 / -600 | -300 / +600 | -300 / +600 |
| Wheel, one notch | key 10 tap, then `onMouseScrollingDeltaNative(0, 1)` | delta only, no key tap | as stock |
| Captured pointer, relative move down 10 then up 10 | nothing (Sony touchpads only) | `onMouseDeltaNative(0, -10)`, then `(0, 10)` | nothing, as stock |

(`out/run_mouse.log`. The captured row assumes relative y > 0 means down,
as for view coordinates. Indirect support, pass 6: AOSP's captured-touchpad
converter sets `AXIS_RELATIVE_Y = slot.getY() - oldY` in a y-down space, and a
captured mouse's `getX/getY` deltas are copied into the same relative axes. No
source states the sign for mice outright.)

Stock 0.35.1 as a fourth side (pass 5, `/stock2` mount, `out/run_mouse_4way.log`)
gives exactly the same calls as 0.34.5 in all four scenarios. The stock contract
did not change between the two versions.

### Internal consistency (pass 5)

- No Canvas native code is on these paths. `onMouseMovedNative`,
  `onMouseDeltaNative` and `onMouseScrollingDeltaNative` are the game's
  JNI natives; nothing under `app/src/main/cpp` registers or hooks them,
  and `RegisterNatives` is not used.
- Canvas's own game-to-screen direction keeps the flip.
  `TextLabelManager` (`ui/text/TextLabelManager.java:93-97`, `:208-213`)
  places the game's text labels through `transformPointToSystem`
  (`GameActivity.java:1190-1192`, `decorHeight - y`). So Canvas's working
  code already treats the game's space as bottom-left origin. Only the
  screen-to-game direction used by the mouse lacks the flip. This is a
  third, independent line of evidence: by reading, it holds as long as text
  labels appear in the right place, which the owner has not reported
  otherwise.

### Did #45 (HugeFrog24) fix it, half-fix it, or make it worse?

Neither, for the Y axis, with one caveat.
- **The hover and drag inversion predates #45 and is unchanged by it.**
  1.7.2 and HEAD send identical Y values on both paths. The cause,
  `transformPointToProgram` without stock's flip, came with 24f93b0
  (MisterGatto, 1.7.2 release). #45 kept it.
- **#45's commit message misattributes the symptom.** It says "Sky's own
  hover highlights tracked upside down because a captured mouse got stuck
  in relative mode forever". The harness shows the hover path mirrored in
  1.7.2 whether or not capture is involved: hover events never go through
  the captured-pointer callback. Releasing capture fixed the stuck relative
  mode, but not the upside-down tracking.
- **What #45 did fix, toward stock parity:**
  - clicks and wheel key taps now reach the game;
  - capture is released for a mouse;
  - a Canvas-only captured-pointer delta path is gone. That path is not in
    stock. It sent `(dx, -dy)`, which, under the relative-y assumption
    above, was inverted against the convention stock's drag path uses
    (`dy > 0` for moving down).
- Caveat: before #45 a mouse was "effectively dead" by its own account. The
  inverted Y may only have become noticeable once #45 made the mouse
  usable. That is perception, not a code change in the Y path.

## Device instrumentation plan

`mouse-log.patch` is opt-in: it is never applied to `app/src/main`. It adds
`Log.d` lines under the tag `CanvasMouse` to `GameActivity.java`:
- every hardware-mouse event in `dispatchTouchEvent` and
  `onGenericMotionEvent`: action, buttons, `x`, `y`, `rawY`,
  `AXIS_RELATIVE_Y`, `AXIS_VSCROLL`, decor-view height, source, device
  name, and `stockProgramY`, the y stock's `transformPointToProgram` would
  send;
- every native call on these paths, with its arguments:
  `onMouseMovedNative`, `onMouseDeltaNative`, `onMouseScrollingDeltaNative`;
- a line when the overlay takes the event and nothing reaches the game.

Checked on the host: `git apply --check` is clean against HEAD. With
`PATCHED=1`, `run_mouse.sh` applies the patch to a copy in `out/patched/`,
compiles the patched methods, and prints the lines they log
(`out/run_mouse_patched.log`, 19 lines for the scenarios above).

Steps for the owner (debug build, any of the affected phones):
```sh
git apply app/src/test/mouse/mouse-log.patch      # local only; revert with git apply -R
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb logcat -c
adb logcat -s CanvasMouse:D | tee canvas-mouse.log
```
In game, with the USB mouse:
1. Move the pointer slowly from the top of the screen to the bottom,
   without pressing.
2. Hold the left button and drag downward, then upward.
3. Turn the wheel one notch away from you.

Reading the log:
- On hover lines, `y` grows as the pointer moves down. The next
  `onMouseMovedNative` carries that same `y`, where stock would send
  `stockProgramY` (decorH - y). If this holds on the device, the hover
  path is the mirrored one.
- On drag, `onMouseDeltaNative`'s second argument is negative while moving
  down. Stock's would be positive.
- If no `native` line follows an event, the overlay took it.

What this does not show: how the game interprets these values. That is
inferred from stock sending the flipped values, not observed.

## Pass log

- Pass 1 (2026-10-31): recon.
  - Searched MotionEvent mouse and hover semantics; the docs do not state
    `AXIS_RELATIVE_Y`'s sign, and view coordinates grow downward.
  - Mapped Canvas's five mouse paths and read stock 0.34.5's
    `transformPointToProgram`, `dispatchTouchEvent` and
    `onGenericMotionEvent`, and 0.35.1's `transformPointToProgram`.
  - Result: Canvas's `transformPointToProgram` lacks stock's
    `decorViewHeight - y` flip, which mirrors both the hover position and
    the drag delta. Introduced in 24f93b0 (MisterGatto, Canvas 1.7.2
    release).
  - Next: compare the wheel path with stock; then the host harness (a
    derived image with a JDK, verbatim extraction of Canvas's methods, stub
    `MotionEvent`, synthetic hover and drag in landscape); then the
    opt-in logging patch for the device.
- Pass 2 (2026-10-31): wheel path and tooling.
  - Searched the `AXIS_VSCROLL` sign (not settled by the docs; irrelevant
    once the code matches stock) and jadx usage.
  - Read stock 0.34.5's scroll case (`pswitch_2`): same axes, key choice and
    unflipped `onMouseScrollingDelta` as Canvas. Wheel ruled out.
  - Built `canvas-mouse-harness:local` from `Dockerfile.mouse`: OpenJDK
    17.0.20.1 and jadx 1.5.0, zip SHA-256
    `c5a713fa4800cbb9e6df85ced1bef95ba329040c95cb87d54465f108483e4ef9`.
  - Decompiled stock's `GameActivity.smali` alone, mounted read-only, into
    `out/stock-0.34.5/`, git-ignored via a new `.gitignore`. jadx agrees
    with the smali reading: `new PointF(f, decorViewHeight - f2)`, and the
    same drag-delta expression as Canvas.
  - Next: the host harness. Extract Canvas's and stock's
    `transformPointToProgram`, `dispatchTouchEvent` and hover branch
    verbatim; compile both against shared stub `android.*` classes; feed
    identical synthetic mouse events (hover up and down, drag up and down)
    in a landscape window; compare the `onMouseMovedNative` and
    `onMouseDeltaNative` arguments.
- Pass 3 (2026-10-31): host harness.
  - Searched host-side testing of `MotionEvent` code (Robolectric shadows,
    AOSP's note that host tests cannot reach framework classes). Chose stub
    classes plus verbatim extraction instead.
  - New: `run_mouse.sh`, `harness/` (stubs, `MouseBase`, `GameActivity`
    alias for jadx's locals, `Main`).
  - Result: 9 native calls differ, all Y. Hover y is mirrored (300/700
    against 780/380); drag `dy` has the opposite sign (-300/+600 against
    +300/-600). X, the wheel and buttons are identical.
  - Fixed during the pass: jadx's `GameActivity` locals (alias class), and
    a hover added before the press so the first delta starts at the press
    point.
  - Next: item 3, the opt-in on-device logging patch and adb steps.
- Pass 4 (2026-10-31): instrumentation, plus the owner's question about #45.
  - Searched `Log.isLoggable`/`log.tag` properties. Not used: `Log.d`
    reaches logcat on release devices anyway, and applying the patch is
    the opt-in.
  - New `mouse-log.patch`, generated from a scratch copy that keeps the
    file's CRLF line endings; `git apply --check` is clean. The harness
    gained `PATCHED=1`, an `android.util.Log` stub, and `patch` in
    `Dockerfile.mouse`.
  - Three-way harness run, stock vs Canvas 1.7.2 vs HEAD:
    - hover and drag Y are identical in 1.7.2 and HEAD, both mirrored
      against stock;
    - #45 brought the wheel key taps to parity and removed the Canvas-only
      captured-pointer delta.
    - Answer: #45 neither fixed nor worsened the Y inversion, and its
      commit message misattributed the symptom.
  - Fixed during the pass:
    - the first generated patch missed the `onGenericMotionEvent` log line,
      caught by counting the inserted calls;
    - `run_mouse.sh` was rewritten after a lost `\r` escape;
    - the image lacked `patch`.
  - Next: nothing open on the host. Remaining: the owner's device log. If
    the loop continues, cross-check 0.35.1's decompiled `dispatchTouchEvent`
    and `onGenericMotionEvent` (only `transformPointToProgram` was checked
    for 0.35.1).
- Pass 5 (2026-10-31): stock 0.35.1 cross-check and native recon.
  - Searched Sky's Android mouse/keyboard patch notes; nothing public for
    0.34 or 0.35, so the decompiled code stays the reference.
  - Decompiled 0.35.1's `GameActivity.smali` (read-only `/stock2`) into
    `out/stock-0.35.1/` and ran it as a fourth side: identical to 0.34.5 in
    every scenario.
  - No Canvas native code on the mouse paths.
  - `TextLabelManager` uses the flipped `transformPointToSystem`, so
    Canvas's own inverse transform agrees with stock's program space.
  - Result: the cause is confirmed three ways (static comparison, harness
    against two stock versions, internal consistency). Nothing open on the
    host.
  - Next: the owner's device log with `mouse-log.patch`. Until then,
    passes re-check that `GameActivity.java`'s mouse paths and the patch
    still match HEAD (`git apply --check`, harness rerun).
- Pass 6 (2026-10-31): the open sign assumption and a staleness check.
  - Searched captured-pointer relative axes. AOSP changes "Add relative
    field to captured cursor device" (mouse deltas copied into
    `AXIS_RELATIVE_X/Y`) and "CapturedTouchpadEventConverter: report
    relative axes" (`slot.getY() - oldY`) support relative y > 0 meaning
    down. That only matters for the removed 1.7.2 captured path in the #45
    assessment, which is now better supported. Not confirmed for mice.
  - HEAD unchanged (5881a85), `app/src/main` clean, `mouse-log.patch`
    still applies; harness results stand without a rerun.
  - Next: nothing open on the host; waiting for the owner's device log.
    Passes re-check staleness, and rerun the harness if `GameActivity.java`
    changes.

## Fix build for side-by-side testing (owner request, after the loop was paused)

Built from a detached git worktree at HEAD (`5881a85`) in the session
scratchpad. The owner's working tree, branch and `app/src/main` are
untouched, and nothing is committed. Two patches record the changes, and both
`git apply --check` clean against HEAD:

- `mouse-fix.patch`: the fix. `transformPointToProgram` sets
  `p.y = getWindow().getDecorView().getHeight() - transformHeightToProgram(y)`,
  stock's flip. Harness check (`HEAD_SRC_OVERRIDE=/out/fixed/GameActivity.java`,
  `out/run_mouse_fixed.log`): the fixed Canvas now matches stock 0.34.5 and
  0.35.1 in every row. Hover sends 780 -> 380; drag sends `dy` +300 / -600.
- `sidebyside-debug.patch`: packaging, so the build installs next to the real
  Canvas with its own data:
  - debug build type: `applicationIdSuffix ".mousefix"`, giving package
    `git.artdeell.skymodloader.mousefix`, and `versionNameSuffix
    "-mousefix"`;
  - provider authorities become `${applicationId}.provider` and
    `${applicationId}.updater`. The literal ones would fail the install
    with `INSTALL_FAILED_CONFLICTING_PROVIDER` while Canvas is installed;
  - the two call sites (`SystemUI_android.java:1372`,
    `CanvasUpdaterActivity.java:170`) use `getPackageName()` + suffix;
  - a debug-only `app_name` of "Canvas (mouse fix)".
- Signing: the default Android debug keystore, self-signed.
- Result: `BUILD SUCCESSFUL` (Gradle 8.7-rc-1, AGP 8.2.2, Android Studio JBR
  21.0.8, NDK 29.0.14206865). Two environment fixes were needed first:
  `sdk.dir` in `local.properties` was escaped wrongly, and Ninja's "manifest
  still dirty" loop came from Windows path length under the scratchpad. The
  worktree was moved to `C:\Users\admin\cmf`; remove it later with
  `git worktree remove C:/Users/admin/cmf`.
- APK: `out/Canvas-mousefix-1.9.4-debug.apk` (16,983,805 bytes, git-ignored).
  Checked:
  - `aapt2 dump badging`: package `git.artdeell.skymodloader.mousefix`,
    versionName `1.9.4-mousefix`, label "Canvas (mouse fix)";
  - all three provider authorities carry the new package name;
  - `apksigner`: "CN=Android Debug";
  - `libciphered.so` and the other native libraries are present;
  - `javap` on the compiled `GameActivity`: `transformPointToProgram` does
    `getDecorView().getHeight()` -> `i2f` -> `fsub`.
- Install next to Canvas: `adb install app/src/test/mouse/out/Canvas-mousefix-1.9.4-debug.apk`.
  It has its own data folder, so mods, configs and any sign-in start empty;
  the real Canvas's data is not touched. Not yet run on a device.

## Device validation (owner)

- OnePlus 13 with a hardware USB mouse (owner, 2026-10-31): unpatched Canvas
  has the Y axis inverted. The `Canvas-mousefix-1.9.4-debug.apk` build
  (`mouse-fix.patch` plus `sidebyside-debug.patch`) puts the pointer position
  right. This confirms ranked hypothesis 1 on a device. That makes four
  lines of evidence: static comparison, host harness against two stock
  versions, Canvas's own inverse transform, and the device test.
- Outstanding: the MuMu emulator. The owner expects MuMu to deliver the host
  mouse as touchscreen input. If so, events take `onTouchEvent` ->
  `onTouchNative`, with raw view coordinates and no
  `transformPointToProgram`, and the fix does not apply there. To be
  checked: which input-device class MuMu's Android exposes for the host
  mouse.
- MuMu, first look (2026-10-31): instance "Android Device" (index 1,
  Android 15, ABIs x86_64/arm64-v8a/x86, adb 127.0.0.1:16416) has Sky and
  the real Canvas installed. `dumpsys input` lists:
  - "Xiaomi Mouse": `/dev/input/event6`, classes `CURSOR | EXTERNAL`,
    source `MOUSE`;
  - "Xiaomi Touchscreen": `/dev/input/event4`, `TOUCH_MT`;
  - "Xiaomi Joystick": `event7`, which also includes `CURSOR`/`MOUSE`;
  - a keyboard and the virtual keyboard.
  So the host mouse can reach Android either as a mouse or as touches.
  Canvas's `isHardwareMouseEvent` would accept the Xiaomi Mouse (source
  `0x2002`, not virtual, id >= 0), which would put MuMu on the same buggy
  path as the phone.
  A 3-minute `getevent -lt` capture (`out/mumu-getevent.log`) recorded no
  input events, because the mouse was not moved over the window during it.
  Open: which device MuMu feeds.
- MuMu, second capture, with the owner moving the mouse
  (`out/mumu-getevent-2.log`, `getevent -lt`, about 2 minutes). MuMu
  splits the host mouse by state:
  - pointer movement with no button held: Xiaomi Mouse, `event6`. 964
    `REL_X`/`REL_Y` reports, every one while no touch was down;
  - button presses and drags: Xiaomi Touchscreen, `event4`. 84 touch
    down/up pairs (`ABS_MT_TRACKING_ID`) and 1,705 `ABS_MT_POSITION_Y`
    samples, with no `event6` motion during any of them, and no `BTN_*` on
    `event6` at all;
  - wheel: `event6`, 24 `REL_WHEEL`;
  - right-click: apparently `KEY_BACK` on `event5`.
  Implication, from Canvas's code and the device classes, not yet logged at
  the Android level:
  - Hover in MuMu reaches Canvas as `SOURCE_MOUSE` and takes the
    `onGenericMotionEvent` -> `transformPointToProgram` path. The game gets
    the mirrored cursor Y there as on the OnePlus 13, and the fix applies.
  - Clicks and drags become touches (`onTouchEvent` -> `onTouchNative`,
    raw view coordinates), which neither the bug nor the fix touches.
  The owner's expectation holds for clicks and drags but not for hover. To
  confirm at the Android level, install a build with `mouse-log.patch` in
  MuMu: `CanvasMouse` hover lines should appear, and no
  `dispatchTouchEvent` mouse lines during drags.
- MuMu, Android-level check, first attempt:
  - Installed `out/Canvas-mousefix-log-1.9.4-debug.apk` next to the real
    Canvas: `mouse-fix.patch` + `sidebyside-debug.patch` +
    `mouse-log.patch`, versionName `1.9.4-mousefix-log`, `primaryCpuAbi`
    arm64-v8a.
  - A 5-minute `logcat -s CanvasMouse:D` capture got 0 lines. Neither
    Canvas build was running; the launcher stayed in front and no crash
    was logged. The test did not happen in that window; this says nothing
    about the fix. To be repeated with the owner at the mouse.
- MuMu, Android-level check, in game (owner at the mouse;
  `out/mumu-canvasmouse-4.log`):
  - Build: test app reinstalled with `-r` (data and sign-in kept) as
    `1.9.4-mousefix-widelog`. On top of fix + `mouse-log.patch`, it logs
    every event on entry to `dispatchTouchEvent` and `onGenericMotionEvent`,
    and after a `dispatchGenericMotionEvent` override, with source, device,
    `isVirtual` and `isHardwareMouseEvent`. The combined diff is in
    `out/mumu-diagnostic-GameActivity.patch`.
  - What reached the game screen, 711 lines: all `dispatchTouchEvent`, source
    `0x1002` (touchscreen). 28 down, 655 move, 28 up; `hardwareMouse=false`
    on every line.
  - What did not: generic motion events. None at
    `dispatchGenericMotionEvent`, although the owner hovered and scrolled in
    game. Canvas's hardware-mouse branches and the three natives were never
    called.
  - State at the time:
    - pointer capture disabled;
    - the game's `GameActivity` window was the only input window on top
      (its SurfaceViews have `NO_INPUT_CHANNEL`);
    - no spy or monitor pilfering;
    - the mouse pointer controller active (`Presentation: POINTER`).
  - Conclusion for MuMu as configured: in game, clicks and drags arrive as
    touches and take Canvas's touch path. Hover and wheel do not reach the
    game window at all. Neither the Y bug nor the fix is exercised, so the
    owner's expectation holds in effect.
  - Unexplained: on (probably) the home screen, the raw capture showed
    `REL_X/Y`/`REL_WHEEL` on the Xiaomi Mouse. In game, none reached the
    window. Either MuMu stops sending them while the game has focus, or
    Android drops them before dispatch. A simultaneous `getevent` + logcat
    capture would tell; it does not change the conclusion for Canvas.
- OnePlus 13 (CPH2653, Android 16), scripted A/B over USB ADB (2026-10-31):
  - Virtual mouse: AOSP's `uinput` shell tool registers "Canvas Test
    Mouse" (bus usb, `EV_KEY`/`EV_REL`, `BTN_LEFT/RIGHT/MIDDLE`,
    `REL_X/Y/WHEEL`). Android makes it input device 19 and dispatches
    `source=MOUSE, action=HOVER_MOVE`, the path a USB mouse takes.
    `adb shell input mouse` was not used: its events come from the virtual
    device, which `isHardwareMouseEvent` rejects.
  - Positioning: pin to the top-left, then slow 10-count steps (about 2.04
    px per count, no acceleration at that pace). The dispatcher's
    `x/yCursorPosition` confirmed each hold at landscape (1190, 298),
    (1190, 451) and (1190, 757). Script: `out/phone/hover3.json`.
  - Screen: Play (Global) -> "Sign In Method" -> provider list, the
    owner's suggested test, which needs no sign-in. The provider buttons
    sit at landscape y 297 / 451 / 602 / 756 / 909 (Google, Facebook,
    Apple, Nintendo, Huawei). The screen is 1080 px tall.
  - Results (screenshots in `out/phone/05-07` unpatched, `11-13` fixed):

    | Pointer over (y) | Mirrored y (1080 - y) | Unpatched Canvas highlights | Canvas (mouse fix) highlights |
    |---|---|---|---|
    | Google (298) | 782 | Nintendo | Google |
    | Facebook (451) | 629 | Apple | Facebook |
    | Nintendo (757) | 323 | Google | Nintendo |

    Unpatched Canvas highlights exactly the item at the mirrored y; the
    fix build highlights the item under the pointer. Same phone, Sky
    0.35.2, script and pointer coordinates. The only differences are
    `mouse-fix.patch` and the unpatched build's two mods, which the owner
    found make no difference.
  - Cleanup: scripts removed from `/data/local/tmp`. The virtual device is
    gone when `uinput` exits. Both game sessions this test started were
    force-stopped.

## Status and cleanup (2026-10-31)

- Fixed: merged into skyprotocol's `osp-master` as #56 (`d9a53c2`), "Flip
  mouse Y into Sky's program space". One line in `transformPointToProgram`.
- Removed as temporary:
  - `out/`: test APKs, the jadx output of stock Sky, the phone screenshots,
    all logs. The output quoted in this document is the remaining record;
    paths to `out/...` above no longer exist.
  - `mouse-fix.patch`, superseded by the commit.
  - `sidebyside-debug.patch` and `mouse-log.patch`, test packaging and
    diagnostics.
  - the `C:\Users\admin\cmf` build worktree.
  - the `canvas-mouse-harness:local` image; rebuild with `Dockerfile.mouse`.
- Kept: `run_mouse.sh` and `harness/`. Rerunning them re-decompiles stock
  Sky from the read-only smali into a fresh `out/`.
- Still installed elsewhere:
  - on the owner's phone, the plain fix build ("Canvas (mouse fix)",
    installed by the owner);
  - in MuMu, the diagnostic `-widelog` build. MuMu was shut down during
    cleanup; remove it later with
    `adb -s 127.0.0.1:16416 uninstall git.artdeell.skymodloader.mousefix`.
