# Canvas native sanitizer findings

Evidence log for Canvas's first-party native code, gathered on a desktop host
(no device). Findings are recorded here and fixed later in a separate pass.
Nothing in this directory changes the app build.

## How to run

From the repo root in Git Bash (build the image once with the `Dockerfile` here
if `canvas-sanitizer-harness:local` is missing):

```sh
mkdir -p app/src/test/sanitizers/out
MSYS_NO_PATHCONV=1 docker run --rm --security-opt seccomp=unconfined \
  -v "$PWD/app/src:/repo:ro" -v "$PWD/app/src/test/sanitizers/out:/out" \
  canvas-sanitizer-harness:local bash /repo/test/sanitizers/run.sh [stage ...]
```

Stages: `confirmed unit fuzz_decoders msan ubsan_ext cfi procutils artpatch jni threads iconloader render`.
`seccomp=unconfined` lets TSan/MSan disable ASLR via `setarch -R`. Logs, corpora
and crash inputs land in `out/` (git-ignored). `FUZZ_SECS` sets fuzz budgets.

Harnesses compile the real Canvas sources. Where a source file drags in too
much platform code, `run.sh` extracts the needed top-level definitions verbatim
(`extract`, fails loudly if the source moves) into `out/gen/`. Stubs stand in
for Android, EGL/GLES, JNI, shadowhook and KittyMemory only.

## Scope

First-party: `Core/{Canvas,Cipher,ChatFlagFix,OtherAudio,Iconloader,fileselector}`,
`main.cpp`, `scroll.cpp`, `Utils/artpatch`, `Utils/imgui_androidbk` (SkyModLoader
lineage). Out of scope (vendored): `Core/imgui`, `Core/ceserver` (Cheat Engine),
`Utils/KittyMemory` (incl. Keystone), `Utils/ktx`, `include/misc/{json.hpp,
obfuscate.h,ranutils.hpp,Vector2.h,Vector3.h}`.

Toolchain: clang 18, compiler-rt, libc++ 18 (matches the NDK's libc++ layout,
so e.g. the 22-character `std::string` inline buffer is the same as on device).

## Index for the fix pass

Severity here is about consequence if reached; "trigger" is who or what can
reach it. Details, evidence and reproducing stages are in the sections below.

| ID | Where | Class (CWE) | Trigger | Consequence if reached | Severity |
|---|---|---|---|---|---|
| F16 | `androidbk.cpp:531-532` | NULL dereference (476) | Surface destroyed before the queued resurface runs (quick app switch) | Dispatch-thread crash | High |
| F2 | `artpatch.c:29-87`, `ChatFlagFixInstall.cpp:116` | Use of uninitialised memory (457/908) | Lowercase hex opcode; ChatFlagFix for most registers other than w8/w9 | Garbage written into game code | High |
| F9 | `main.cpp:365-367`, `CipherUtils.cpp:275` | Race (362), use after free (416) | Mod registers a keyboard listener while a completion is delivered | UI-thread use after free | High |
| F13 | `fileselector.cpp:102-107,153-158` | Missing detach | Mod calls a file picker from its own thread, which then exits | Process abort (ART rule) | High |
| F1 | `CipherUtils.cpp:33-58` and 5 `char[256]` callers | Stack overflow (121) | Mod passes an IDA pattern over 255 bytes | Stack corruption | High |
| F3 | `CipherUtils.cpp:280` | Use after free / double free (416/415) | Mod calls `get_DeviceInfo` with a device string over 22 chars | Heap corruption | High |
| F10 | `androidbk.cpp:35,505,674,734` | Race (362) | Surface destroyed while the dispatch thread is busy | Overlay keeps rendering after `surfaceDestroyed` | Medium |
| F12 | `procutils.c:51` | Off-by-one | Patch on the first page of a mapping that follows another | Code page left non-executable | Medium |
| F18 | `artpatch.c:92-94,103-104` | Write past the unprotected range (787) | Mod patch of 2+ instructions whose site lies within its length of a page end | SIGSEGV in the game at `Fire()`/`Restore()`; deterministic per site and game build | Medium |
| F11 | `artpatch.c:74-75,93,104` | Use of uninitialised memory (457) | Longer opcode set on a fired patch, then restored | Garbage restored into game code | Medium |
| F4 | `CipherPatch.cpp:68` | Invalid iterator (`erase(end())`) | Deleting a patch whose `Fire()` did nothing | Heap corruption | Medium |
| F6 | `CipherHook.cpp`, `CipherPatch.cpp` (`s_InstanceVec`) | Race (362) | Hooks and patches changed on different threads | Vector corruption | Medium |
| F20 | `IconLoader.cpp:88-90,115-131,163-192` | Race (362) on an unguarded map; heap corruption | Two threads in the exported `getImage` or `getUIIcon` with names not yet loaded (e.g. a mod's `InitLate` and another mod's `Draw`) | Heap corruption, crash | Medium (reachability depends on mods) |
| F21 | `androidbk.cpp:615-630` | Double free (415) / use after free (416), latent | Any future atlas clear, rebuild or context destroy (none today) | Heap corruption on the render thread | Low (latent) |
| F22 | `androidbk.cpp:374,703-706`; `ImGUITextInput.java:215` | Wrong character encoding (176) | User types an emoji into an ImGui text field | Invalid UTF-8 in mod text fields; two unknown glyphs shown | Low |
| F23 | `androidbk.cpp:547-566`; `ImGUI.java:40-42,89-94` | Unchecked Java exception (JNI contract; CWE-252/755) | A copy too large for the binder transaction (or any throw from `setClipboard`) | Silent copy failure, JNI calls with an exception pending (CheckJNI abort), exception rethrown in the dispatch thread when `init()` returns | Low (trigger needs a very large copy) |
| F14 | `IconLoader.cpp:77-113` | Missing return | KTX upload returns 1 of 16 unhandled codes | Garbage reference used | Medium |
| F7 | `procutils.c:43-45` | NULL dereference (476) | `/proc/self/maps` cannot be opened (fd exhaustion) | Crash | Low |
| F8 | `main.cpp:364,384-386`, `IconLoader.cpp:36,38` | Memory leak (401) | Every keyboard submit; device info; each icon | Unbounded small leak | Low |
| F5 | `CipherHook.cpp:57,128`, `CipherPatch.cpp:32` | Bad downcast (UB) | Any hook install after a patch | None today (base members only) | Low |
| F15 | `androidbk.cpp:160` | API misuse | Every overlay start | Swap interval never set (matches default) | Low |
| F19 | `main.cpp:398-412` | Missing run-once guard; concurrent re-entry (362) | `fadeoutLogos` running again in the same process (not established) | Every mod's `InitLate` runs twice, possibly concurrently | Low |
| F17 | `CipherRegistry.cpp:36-39,113-117` | Use after destruction (static destruction order) | Mod with a static-storage patch object; registry first used later; process runs `exit()` | Crash at exit | Low |

Leads still open (not confirmed): L5 `set_hex("")`, L6 missing-atlas leak,
L7 `CipherScanAll` NULL check order, L8 `-w` hiding warnings, L9 window
reference leak, L10 frozen thermal status, L12 `systemUI` global-ref leak,
L15 `DescribePatchesIn` range overflow,
L16 no I-cache flush after `CipherPatch` writes (arm64, device-only),
L17 `InitLate` never called for patch-only mods.

## Confirmed findings

Each was reproduced on the real source; `stage_confirmed` re-runs all of them.

### F1. `CipherUtils::patternToBytes` overflows a fixed 256-byte stack buffer
- Where: `app/src/main/cpp/Core/Cipher/CipherUtils.cpp:33` (the `memcpy` at :57,
  the terminator write at :58). Callers pass `char bytes[256]` with no size:
  `CipherUtils.cpp:208,220,232,244` (`CipherScanPattern*`) and
  `CipherRegistry.cpp:211` (`ExplainScanFailure`).
- Trigger: any IDA-style pattern with more than 255 byte tokens, from any mod.
- Found by: libFuzzer + ASan in about 79k executions. Input:
  `artifacts/patternToBytes-stack-overflow.bin`.
- Evidence:
  ```
  ERROR: AddressSanitizer: stack-buffer-overflow ... WRITE of size 260
    #0 __asan_memcpy
    #1 CipherUtils::patternToBytes(...) patternToBytes.inc:25:9
    #2 LLVMFuzzerTestOneInput fuzz_pattern.cpp:13:5
  ```
- Also: `std::isxdigit(_pattern[i])` with a negative `char` is UB by the C
  standard (no sanitizer checks ctype).

### F2. Lowercase hex makes artpatch write uninitialised heap bytes into code
- Where: `Utils/artpatch/artpatch.c:29-49` decodes only `0-9A-F`, but
  `artpatch_strip_nonnumbers` (:55-63) keeps `a-f`; on the first lowercase digit
  `artpatch_set_hex` returns early (:81) leaving the freshly `malloc`'d patch
  buffer unwritten, with `patchLength` already set. `artpatch_apply` then copies
  it onto the target.
- Production trigger: `ChatFlagFixInstall.cpp:116` formats the replacement with
  `%02x` (lowercase). Every shipping fixture uses `w8` (`08050012`, no letters);
  `ChatFlagFixTest.cpp:213` shows the decoder accepts any register, and
  w5-w7, w10-w15, w21-w23, w26-w30 all produce a letter. Mods calling
  `CipherPatch::set_Opcode` with lowercase hex hit the same path.
- Evidence (real artpatch.c; ASan fills fresh `malloc` memory with 0xbe):
  ```
  w8  hex="08050012" expected=12000508 written=12000508 -> ok
  w9  hex="29050012" expected=12000529 written=12000529 -> ok
  w10 hex="4a050012" expected=1200054a written=bebebebe -> GARBAGE WRITTEN TO CODE
  w13 hex="ad050012" expected=120005ad written=bebebebe -> GARBAGE WRITTEN TO CODE
  w21 hex="b5060012" expected=120006b5 written=bebebebe -> GARBAGE WRITTEN TO CODE
  ```
  ChatFlagFix's post-write check (`ChatFlagFixInstall.cpp:124`) only logs; the
  bytes are already in the game's code page.
- Second confirmation (MSan, `stage_msan`, mode `lowercase`):
  ```
  WARNING: MemorySanitizer: use-of-uninitialized-value
    #1 check artpatch_memory.c:28   <- reading the patched page
    stored by: __msan_memcpy <- artpatch_apply artpatch.c:94
    Uninitialized value was created by a heap allocation: malloc <- artpatch_set_hex artpatch.c:72
  ```

### F3. `CipherUtils::get_DeviceInfo` `memcpy`s a struct holding `std::string`s
- Where: `Core/Cipher/CipherUtils.cpp:280`. Copies `Canvas::DeviceInfo` bytewise
  into a `::DeviceInfo`; for any string longer than libc++'s 22-character
  inline buffer both objects then own the same heap block.
- Trigger: a mod calls `get_DeviceInfo()` and the device name, manufacturer or
  model (set at `main.cpp:384-386`) exceeds 22 characters. Destroying the mod's
  copy frees Canvas's buffer.
- Evidence:
  ```
  ERROR: AddressSanitizer: heap-use-after-free ... in printf (deviceinfo.cpp:28)
    freed by: operator delete <- std::string destructor (the mod's copy)
    allocated by: std::string::operator= <- deviceinfo.cpp:23 (Canvas's string)
  ```
- Note (corrected in pass 5): GCC 13's `-Wclass-memaccess` flags this line
  (`stage_warnings`: "writing to an object of type 'struct DeviceInfo' with no
  trivial copy-assignment"). Clang 18, the compiler in this image, has no
  equivalent warning, so an earlier note here claiming clang's
  `-Wnontrivial-memaccess` catches it was wrong. Whether the NDK's newer clang
  warns was not checked.

### F4. `~CipherPatch` calls `vector::erase(end())` for a patch never registered
- Where: `Core/Cipher/CipherPatch.cpp:68` erases `std::find(...)` without
  checking for `end()`. `Fire()` only registers the patch when it actually
  applies (address non-zero and not locked out).
- Trigger: deleting a patch whose scan failed (address 0) or whose `Fire()` was
  rejected by a locked patch, a normal cleanup path in mods.
- Evidence:
  ```
  vector:1532:41: runtime error: applying non-zero offset 8 to null pointer
    #0 vector<CipherBase*>::erase  #1 CipherPatch::~CipherPatch() CipherPatch.cpp:68
  ERROR: AddressSanitizer: negative-size-param: (size=-8)  in __asan_memmove
  ```

### F5. Hooks and patches downcast each other in the shared instance list
- Where: `Core/Cipher/CipherHook.cpp:57` casts every `s_InstanceVec` entry to
  `CipherHook*`; same pattern in `CipherPatch.cpp:32` and `CipherHook.cpp:128`.
- Trigger: any hook install after a patch has been registered. ChatFlagFix
  registers one at startup, so every mod hook install hits it.
- Evidence (UBSan `vptr`):
  ```
  CipherHook.cpp:57:37: runtime error: downcast of address 0x504000000050 which
  does not point to an object of type 'CipherHook'
  note: object is of type 'CipherPatch'
  ```
  Only base-class members are read through the bad pointer today, so it does
  not misbehave on current compilers, but it is UB.
- Second confirmation (Control Flow Integrity, `stage_cfi`):
  ```
  CipherHook.cpp:57:37: runtime error: control flow integrity check for type
  'CipherHook' failed during base-to-derived cast (vtable address ...)
  note: vtable is of type 'CipherPatch'
  ```
  A release build with `-fsanitize=cfi` in trapping mode would abort here.

### F6. `CipherBase::s_InstanceVec` is guarded by two different mutexes
- Where: `CipherHook.cpp` takes `cipher_hook_mtx` (`recursive_mutex`, :13),
  `CipherPatch.cpp` takes `cipher_patch_mtx` (:9); both mutate the one static
  vector (`CipherBase.cpp:10`). `CipherHook::Fire` also reads `stub` before
  taking its lock (:48).
- Trigger: a mod toggling a patch from `Draw()` (render thread) while hooks are
  installed or restored on another thread (e.g. `InitLate`).
- Evidence (TSan):
  ```
  WARNING: ThreadSanitizer: data race
    Read of size 8 by thread T2 (mutexes: write M0):  vector::begin <- CipherHook::Fire() CipherHook.cpp:56
    Previous write of size 8 by thread T1 (mutexes: write M1):  vector::__push_back_slow_path <- CipherPatch::Fire
    Mutex M0 = std::recursive_mutex, M1 = std::mutex
  ```

### F7. `procutils_read_mprotection` dereferences a NULL `FILE*`
- Where: `Utils/artpatch/procutils.c:43-45`: `fopen("/proc/self/maps")` is not
  checked before `fgetc(f)`.
- Trigger: `/proc/self/maps` cannot be opened when artpatch applies or restores
  a patch, e.g. under file-descriptor exhaustion (`EMFILE`). Thread: whichever
  thread calls `CipherPatch::Fire`/`Restore` (load-time constructor for
  ChatFlagFix, mod threads, render thread for toggles).
- Evidence (`stage_procutils`, mode `fopen-fails`), two sanitizers in one run:
  ```
  procutils.c:45:23: runtime error: null pointer passed as argument 1, which is declared to never be null
  ERROR: AddressSanitizer: SEGV on unknown address 0x000000000074 ... in _IO_getc
    #0 procutils_read_mprotection procutils.c:45:17
  ```

### F8. JNI UTF strings are copied and never released
- Where (one leaked copy per call): `main.cpp:364` `onKeyboardCompleteNative`
  (every keyboard submit), `main.cpp:384-386` `setDeviceInfoNative` (3),
  `IconLoader.cpp:36,38` `addIcon` (2 per icon, once per atlas entry at
  startup). Same pattern, not yet driven: `main.cpp:345-348` (4 per mod),
  `main.cpp:327` (dlopen-failure path only). `main.cpp:273` (`configsPath`) is
  held on purpose for the process lifetime.
- Trigger: normal use; the keyboard path grows with every text submission.
  Threads: Android UI thread (keyboard, device info), main thread (icons).
- Evidence (`stage_jni`, mode `leaks`: 1 device-info call, 25 keyboard
  submits, 40 icons), two independent counts that agree:
  ```
  GetStringUTFChars without ReleaseStringUTFChars: 108      <- fake JNIEnv counter
  SUMMARY: AddressSanitizer: 1366 byte(s) leaked in 108 allocation(s).   <- LSan
    onKeyboardCompleteNative (main.cpp:364)   25 objects
    setDeviceInfoNative (main.cpp:384,385,386) 1 object each
    addIcon (IconLoader.cpp:36, :38)           40 objects each
  ```
  An earlier run also flagged `new PrivateUIIcon` (`IconLoader.cpp:13`); that
  was a harness artifact (LSan ran after the global map's destructor) and the
  harness now checks before exit.

### F9. Keyboard listeners: unsynchronised list, use-after-free on growth
- Where: `Canvas::onKeyboardCompleteListeners` (`Canvas.cpp:43`) is appended by
  `CipherUtils::addOnKeyboardCompleteListener` (`CipherUtils.cpp:275`) from
  whatever thread a mod calls it on, and iterated by
  `onKeyboardCompleteNative` (`main.cpp:365-367`) on the Android UI thread. No
  lock on either side.
- Trigger: a mod registers a listener (from `Start`, `InitLate`'s detached
  thread, or its `Draw` on the render thread) while a keyboard completion is
  being delivered. If the append reallocates, the UI thread's loop reads the
  freed buffer.
- Evidence, two ways:
  - TSan (`stage_jni`, mode `race`), reproduced in passes 2, 2b and 3:
    ```
    WARNING: ThreadSanitizer: data race
      Read by main thread:  vector::begin <- onKeyboardCompleteNative (main.cpp:365)
      Previous write by T1: vector::__push_back_slow_path <- addOnKeyboardCompleteListener
    ```
  - Deterministic ASan (`stage_jni`, mode `interleave`): the UI thread is
    inside a listener when another thread registers one into a full vector:
    ```
    ERROR: AddressSanitizer: heap-use-after-free ... READ of size 8 thread T0
      #0 Java_..._onKeyboardCompleteNative  main.cpp:366 (next listener)
    freed by thread T1: operator delete <- vector reallocation in addOnKeyboardCompleteListener
    previously allocated by thread T0: vector allocate
    ```
  - HWASan, natively on x86-64 in page-aliasing mode (`stage_hwasan_x86`,
    pass 20), with the same interleaving: `tag-mismatch` READ of size 8 at
    `onKeyboardCompleteNative.inc:9`, in memory since reused for a
    `strdup`. HWASan is the sanitizer Android supports on device. Under
    arm64 emulation the same run crashed QEMU instead (queue 10).

### F10. Overlay shutdown is a racy flag that `resurface()` can swallow
- Where: `androidbk.cpp:35` `static bool run`; `shutdown()` (:674) writes it on
  the UI thread (`ImGUI.java:80`, `surfaceDestroyed`) and returns at once;
  `renderloop()` (:505) reads it on the "imgui dispatch thread"; `resurface()`
  (:734) sets it back to `true` after `init_sfc()` has already bound a new
  surface. The `SurfaceHolder.Callback` contract requires rendering to stop
  before `surfaceDestroyed` returns.
- Trigger: the surface is destroyed while the queued `resurface` job is between
  creating the new EGL surface and `run = true` (create/destroy in quick
  succession, e.g. app switched away right after returning to it). The shutdown
  is lost and the overlay keeps rendering after `surfaceDestroyed` returned.
  Even without the swallow, `shutdown()` never waits for the loop to stop.
- Evidence, two ways (real androidbk.cpp, ImGUI.java's thread layout):
  - TSan (`stage_render`), reproduced in passes 2b and 3; the only report in
    the run (the CanvasInput queue/snapshot bridge from #37 was clean across
    14,000 UI-thread events and 4 surfaces):
    ```
    WARNING: ThreadSanitizer: data race
      Read of size 1 by thread T1:             renderloop() androidbk.cpp:505:12
      Previous write of size 1 by main thread: Java_..._ImGUI_shutdown androidbk.cpp:674:9
    ```
  - Deterministic (`stage_render`, mode `lost-shutdown`; the harness pauses
    inside the fake `eglCreateWindowSurface`, Canvas's ordering is untouched):
    ```
    frames presented in the 300 ms after surfaceDestroyed's shutdown() returned: 454
      -> SHUTDOWN LOST, rendering continued
    ```
  - Also seen unforced (pass 8, ASan stress run, no pause hook): `render loop
    exited after 8 final shutdown() call(s)`; seven shutdowns were swallowed.
  - TSan's detection of this race is timing-dependent: in pass 8 it reported
    nothing, because the UI thread's next input push (under the CanvasInput
    queue mutex) ordered the write before the render thread's next read.
    Passes 2b and 3 reported it. Since pass 10, `stage_render_quiet` (no
    UI-thread synchronisation between `shutdown()` and the loop stopping)
    reports it in every run: 3 of 3, each naming `renderloop()`
    androidbk.cpp:505 and `shutdown()` androidbk.cpp:674.

### F11. artpatch restores uninitialised bytes after a longer opcode
- Where: `Utils/artpatch/artpatch.c:74-75` grows `backup` with `realloc` when
  `set_hex` gets a longer opcode; `artpatch_apply` (:93) only refills `backup`
  when `!patchApplied`, so after an earlier apply the new tail is never
  written; `artpatch_restore` (:104) copies the whole `backup` onto the target.
- Trigger: `CipherPatch::set_Opcode` with a longer opcode on a patch that was
  already fired, then `Fire()` (re-applies, `CipherPatch.cpp:20-23`) and
  `Restore()` or the destructor. The restored tail is garbage, written into
  game code.
- Evidence, two ways (`stage_msan` and `stage_artpatch`, mode `relength`):
  ```
  MSan: use-of-uninitialized-value ... stored by artpatch_restore artpatch.c:104
        Uninitialized value was created by a heap allocation: realloc <- artpatch_set_hex artpatch.c:75
  ASan: relength MISMATCH  page: 11 11 11 11 be be be be  expected: 11 11 11 11 11 11 11 11
  ```

### F12. artpatch leaves the first page of a mapping with the previous mapping's protection
- Where: `Utils/artpatch/procutils.c:51` compares `addr <= addr1`, treating the
  end of each `/proc/self/maps` range as inclusive, so an address equal to one
  mapping's end matches that mapping instead of the next. `artpatch_apply`
  (:90-96) and `artpatch_restore` (:101-106) put the reported protection back
  on the page after writing.
- Trigger: a `CipherPatch` (or ChatFlagFix) on the first page of a mapping
  that directly follows another, e.g. the first page of a library's code
  segment when its read-only segment ends at the same address. The page is left
  without execute permission; executing anything on it afterwards faults.
- Evidence, two ways:
  - Unit level (`stage_procutils`, mode `adjacent`): for
    `70000000-70010000 r--p` then `70010000-70400000 r-xp`, querying
    `0x70010000` returns `r--`.
  - End to end on the kernel's real maps (`stage_artpatch`, `artpatch_e2e`):
    two adjacent pages mapped `r--` and `r-x`, a 4-byte patch on the second:
    ```
    code page 0x7d5f44890000: before r-xp, after apply r--p, after restore r--p -> PROTECTION CHANGED
    ```
- Also in this function, recorded with F7: no `fopen` NULL check (:43-45). It
  parses lines without NUL termination and splits lines over 1024 characters;
  32M raw-input fuzz executions under ASan+UBSan found no memory error from
  that.

### F13. File-picker helpers attach mod threads to the VM and never detach them
- Where: `fileselector.cpp:102-107` (`requestFileMultiNamed`, which
  `requestFile` and `requestFileMulti` call) and `:153-158` (`requestFiles`)
  call `AttachCurrentThread` when the caller is not attached, and no path
  detaches. Same class, narrower: `CipherUtils::performHapticFeedback` attaches
  (`CipherUtils.cpp:294`) and returns without detaching for an out-of-range
  type (`:330`).
- Trigger: a mod calls `requestFile*`/`requestFiles` from a thread it created
  (not the render thread, which is a Java thread and is never attached here),
  and that thread later exits. ART's `Thread::ThreadExitCallback`
  (`art/runtime/thread.cc:2452-2467`, main branch) logs a warning on the first
  destructor pass and `LOG(FATAL)` on the second, so the process aborts.
- Evidence (`stage_threads`; real `fileselector.cpp`, fake JavaVM that tracks
  attachment per thread and applies the ART rule), with clean controls:
  ```
  java-thread     attaches=0 detaches=0                     survived   <- render-thread case
  mod-thread      before exit attaches=1 detaches=0
    W art: Native thread exiting without having called DetachCurrentThread (...)
    F art: Native thread exited without calling DetachCurrentThread       exit=134
  haptic-valid    attaches=1 detaches=1                     survived   <- CipherUtils pairs them
  haptic-invalid  attaches=1 detaches=0 ... F art: ...                  exit=134
  ```
  Two ways: the attach/detach count from Canvas's own code, and the
  thread-exit consequence under ART's rule (taken from ART's source, not from a
  device). The haptic case also needs the caller to pass an out-of-range enum
  value, which UBSan flags at the caller (`load of value 99, which is not a
  valid value for type 'HapticFeedbackType'`), so it is low severity.

### F14. `IconLoader::uploadImageKtx` returns no value for most upload results
- Where: `Core/Iconloader/IconLoader.cpp:77-113`: the `switch` on
  `ktxTexture_GLUpload`'s result handles `KTX_SUCCESS`, `KTX_GL_ERROR` and
  `KTX_UNSUPPORTED_TEXTURE_TYPE`; any other `KTX_error_code` reaches the
  closing brace of a function that returns `SkyImage&`. The caller then uses
  whatever reference comes back (`getImage`, `getAtlasImage` -> `getIcon`
  reads `textureId`). On those paths `assetBuf` and the `ktxTexture` are also
  never freed.
- Trigger: an image or atlas from Sky's assets that libktx can parse but not
  upload with one of the 16 unhandled results (e.g. `KTX_INVALID_OPERATION`,
  `KTX_OUT_OF_MEMORY`, `KTX_UNSUPPORTED_FEATURE`). Thread: render thread (Canvas
  menu and mod UI drawing icons).
- Evidence, two ways:
  - UBSan `return` check (`stage_iconloader`, every `KTX_error_code`): the 3
    handled codes return; all 16 others report
    `IconLoader.cpp: runtime error: execution reached the end of a
    value-returning function without returning a value`.
  - Compiler, on the same file without the app build's `-w`
    (`CMakeLists.txt:42`):
    `IconLoader.cpp:113:1: warning: non-void function does not return a value
    in all control paths [-Wreturn-type]`.

### F15. `eglSwapInterval` is called before any context or surface is current (low severity)
- Where: `androidbk.cpp:160` in `initContext()`, before `eglCreateContext`
  (:173) and before `init_sfc` makes a surface current (:535). Per the EGL man
  page the call then fails with `EGL_BAD_CONTEXT` (or `EGL_BAD_SURFACE`) and
  returns `EGL_FALSE`; the result is ignored. Effect: the requested interval
  of 1 is never applied, which matches the default for a new window surface,
  so this is a correctness defect rather than a likely behaviour change.
- Trigger: every overlay start (`ImGUI.init`, dispatch thread).
- Evidence, two ways:
  - Deterministic oracle (`stage_render`; the fake EGL tracks the current
    context and draw surface per thread and applies the man page's rule):
    `EGL: eglSwapInterval calls 1, made with no current context/surface 1
    (fails: EGL_BAD_CONTEXT); eglSwapBuffers with no current surface 0`.
  - Independently found by the Codex audit from source (its Q3.1).
- Sweep of EGL/window calls in `androidbk.cpp`: 1 call-order defect (this
  one); 10 calls whose result is ignored (`:160, :169, :170, :519, :522, :523,
  :533, :534, :535, :733`); 3 failures only logged before carrying on
  (`:155-159`, `:163-166`, and `:173-176`, where a failed `eglCreateContext`
  still leads into the render loop); `ANativeWindow_fromSurface` (`:531`) not
  checked before `ANativeWindow_acquire` (`:532`), see L14.

### F16. A queued resurface after the surface is destroyed dereferences a NULL window
- Where: `init_sfc` (`androidbk.cpp:531-532`) passes `ANativeWindow_fromSurface`'s
  result straight to `ANativeWindow_acquire`, from `ImGUI_init` and
  `ImGUI_resurface` (:732).
- Platform chain (AOSP main, read in source; not observed on a device):
  `SurfaceView.releaseSurfaces()` calls `mSurface.destroy()`
  (`SurfaceView.java:1005`), which calls `release()` and zeroes the native
  object (`Surface.java:427-429, 441-447`); `getSurface()` keeps returning that
  same object (`SurfaceView.java:2053-2054`); `ANativeWindow_fromSurface`
  returns NULL for it (`native_window_jni.cpp:30-36`); `ANativeWindow_acquire`
  dereferences its argument (`ANativeWindow.cpp:88`, `window->incStrong`).
- Trigger: `ImGUI.surfaceCreated` queues the init/resurface job on the
  dispatch thread, which reads `surfaceHolder.getSurface()` only when it runs.
  If `surfaceDestroyed` comes first (quick app switch, or the thread still
  inside `renderloop`, the F10 window), the job gets the destroyed Surface and
  the dispatch thread crashes.
- Evidence (`stage_render`, mode `released-surface`; the fake window returns
  NULL once released and dereferences on acquire, as AOSP does; a barrier job
  holds the dispatch thread while the surface is created and destroyed):
  ```
  resurface job now runs against the destroyed Surface
  fake_platform.cpp:131:53: runtime error: member access within null pointer   (UBSan)
  ERROR: AddressSanitizer: SEGV on unknown address ...                        (ASan)
    #0 ANativeWindow_acquire(ANativeWindow*)
    #1 init_sfc(_JNIEnv*, _jobject*)          androidbk.cpp:532:5
    #2 Java_git_artdeell_skymodloader_ImGUI_resurface  androidbk.cpp:732:5
  ```
  Two ways: the deterministic host repro, and each platform step in AOSP
  source above. Sweep: the only `ANativeWindow_fromSurface` call in
  first-party code.

### F17. The install registry can be destroyed before a mod's patch object at exit (low severity)
- Where: `State()` (`CipherRegistry.cpp:36-39`) keeps the registry in a
  function-local static. It is created on first use and destroyed at exit in
  reverse order of creation. `~CipherPatch` (`CipherPatch.cpp:75`) and
  `~CipherHook` (`CipherHook.cpp:124`) call `ForgetInstall`
  (`CipherRegistry.cpp:113-117`), which locks and erases from it.
- Trigger: three conditions together.
  1. A mod keeps a patch or hook in an object with static storage (a
     namespace-scope `CipherPatch`), built when the mod's `.so` loads.
  2. The registry is first used after that object is built: the mod's own
     `set_Opcode`/`Fire()`, or anyone's. Canvas's load-time constructor uses
     it only through `ChatFlagFix::Install` when that finds its site
     (`ChatFlagFixInstall.cpp:117-120`). If the site isn't found (e.g. after
     a game update), nothing touches the registry at load.
  3. The process exits through `exit()`, which runs static destructors. On
     Android an app process is usually killed instead; whether Sky ever exits
     through `exit()` is not known here.
  Then the registry is destroyed first. The mod's destructor then locks a
  destroyed `shared_mutex` and erases from a destroyed `unordered_map`, on
  the thread that called `exit()`. The comment above `RegistryState`
  (`CipherRegistry.cpp:23-27`) chose a function-local static to fix
  initialisation order; destruction order was not covered. That comment
  also describes a load-time ImGui hook `Fire()` that `main.cpp` no longer
  makes.
- Evidence (`stage_registry`, harness `cipher_exit.cpp`: a namespace-scope
  `CipherPatch` fired in `main`, normal return):
  ```
  main returning (registry touched first: no, patched: 1)
  unique_ptr.h:414:12: runtime error: applying non-zero offset 8 to null pointer      (UBSan)
  ERROR: AddressSanitizer: SEGV on unknown address 0x000000000008                      (ASan, libc++)
    #3 CipherInternal::ForgetInstall(CipherBase*)  CipherRegistry.cpp:115:24
    #4 CipherPatch::~CipherPatch()                 CipherPatch.cpp:75:5
    #6 exit
  WARNING: MemorySanitizer: use-of-uninitialized-value                                (MSan, libstdc++)
    #3 CipherInternal::ForgetInstall(CipherBase*)  CipherRegistry.cpp:115:24
    #4 CipherPatch::~CipherPatch()                 CipherPatch.cpp:75:5
    Member fields were destroyed ... ~unordered_map
  ```
  Control (`REGISTRY_TOUCH_FIRST=1`, registry used before the mod's object is
  built, as when ChatFlagFix patches at load): exit 0 under both. Two ways:
  ASan/UBSan with libc++ and MSan use-after-dtor with libstdc++, plus the
  order-dependent control. Sweep: two function-local static objects in
  first-party code (`CipherRegistry.cpp:37`, `IconLoader.cpp:42`). Only the
  registry is used from other objects' destructors.

### F18. A patch that crosses a page boundary writes into a read-only code page
- Where: `artpatch_apply` (`artpatch.c:89-98`) and `artpatch_restore`
  (`:99-111`) make only the page that holds the first byte writable
  (`mprotect(pagestart, PAGE_SIZE, ...)`, :92, :103). They then `memcpy` all
  `patchLength` bytes (:94, :104).
- Trigger: a mod's `CipherPatch` of two or more instructions, e.g.
  `mov x0, #0; ret` (8 bytes) at a function entry found by pattern scan. The
  write faults when the site lies within its length of a page end: the last
  4 bytes of a page for 8 bytes, the last 8 for 12. A code page's neighbour
  is also r-x, so the tail write faults. That is a SIGSEGV in the game
  process on the thread calling `Fire()`: a mod's init thread, or the render
  thread for patches toggled from `Draw()`. `Restore()` and the destructor
  take the same path (:104). Per site and game build the chance is about
  (instructions - 1)/1024 with 4 KB pages, or /4096 with 16 KB pages. It is
  deterministic for a given site, so one game update can make a working mod
  crash every time it patches. ChatFlagFix writes 4 aligned bytes and cannot
  cross.
- Evidence, two ways (`stage_artpatch_diff`):
  - Deterministic (`artpatch_cross.c`: two r-x pages, 8-byte patch on the last
    instruction of the first):
    ```
    patching 0x7e4885acdffc..0x7e4885ace004 (page boundary at 0x7e4885ace000)
    ERROR: AddressSanitizer: SEGV on unknown address 0x7e4885ace000 ... caused by a WRITE
      #1 __asan_memcpy  #2 artpatch_apply  artpatch.c:94:9
    ```
  - Found independently by the differential fuzzer (`fuzz_artpatch_diff.cpp`),
    unseeded with crossing allowed, after 16,865 executions. It is the same
    SEGV at `artpatch.c:94:9`, from `apply` at `fuzz_artpatch_diff.cpp:140`.
    Input kept as `artifacts/artpatch-page-crossing.bin` (24 bytes).
- Sweep: first-party sites that change one page's protection and then write
  a range: `artpatch.c:92-95` and `:103-105` (both can cross), and
  `OtherAudioApply.cpp:154-161` (cannot: one 8-byte-aligned slot).
  `PAGE_SIZE` is taken from `sysconf` at run time (`artpatch.c:11-14`), so
  16 KB-page devices change only the odds.

### F19. `lateInitUserLibs` has no run-once guard: a second call runs every `InitLate` again, concurrently (low severity)
- Where: `Java_..._MainActivity_lateInitUserLibs` (`main.cpp:398-412`) checks
  only `g_modInitialization.isReady()`, then starts a detached thread that
  calls every registered mod's `InitLate()`. Nothing records that this
  already happened.
- Trigger: the method is called from the end of the logo fade-out animation
  in `GameActivity.fadeoutLogos()` (`GameActivity.java:1335-1344`). If that
  runs again while the process lives (for example a recreated activity; this
  depends on the game binary and is not established here), each mod's
  `InitLate` runs a second time, on a second thread. If the first is still
  running, the two overlap. Mods write `InitLate` as one-time setup (hooks,
  patches, listeners), so it would run twice, and possibly concurrently with
  itself.
- Evidence (`stage_lifecycle`, mode `twice`; real `lateInitUserLibs`,
  `onModLibrary`, `UserThread`, `completeModInitialization` extractions and
  the real `Canvas.cpp`, two stand-in mods):
  ```
  MISMATCH mod_ui.so (menu): InitLate called 2 time(s), two calls overlapped     (ASan build, counters)
  WARNING: ThreadSanitizer: data race
    Write of size 4 by thread T2:          InitLate  (mod)  <- lateInitUserLibs.inc:7
    Previous write of size 4 by thread T1: InitLate  (mod)  <- lateInitUserLibs.inc:7
  ```
  Two ways: the call and overlap counters, and TSan's report of the two
  late-init threads inside the same `InitLate`. Mode `once`: exactly one
  call, no TSan report. Sweep: two detached-thread sites in first-party code.
  `main.cpp:411` is unguarded; `OtherAudioApply.cpp:241` is guarded by
  `g_started` under `g_mutex`.
- Related, exit path only (F17's class): the detached thread iterates
  `Canvas::userLibs` while `exit()` can run its static destructor. TSan shows
  the destruction unordered with those reads when the harness exits
  normally; the harness now uses `_exit`, so this is noted, not measured.

### F20. The exported `IconLoader::getImage` races on an unguarded map when called from two threads
- Where: `IconLoader::getImage` (`IconLoader.cpp:172-179`) looks the name up
  in the static `IconLoader::images` (:173). On a miss it calls
  `uploadImageKtx`, which inserts into the same map (:90; `atlas_images` at
  :88 for atlases). Nothing locks either map. The app build sets no
  `-fvisibility=hidden` and these functions have no visibility attribute, so
  `getImage`, `icon` and `iconButton` are exported for mods to call.
  `getIcon` and `getAtlasImage` are `PRIVATE_API`.
- Trigger: two `getImage` calls in flight on different threads with names
  not yet loaded. Examples: one mod in `Draw()` on the render thread, another
  in `InitLate()` (Canvas's detached late-init thread, which F19 can run
  twice) or on its own thread. The uploads also issue GL calls, which need
  the render thread's context. No mod source using this API was found
  (pass 29), so reachability depends on mods; the API documents no thread
  rule.
- Evidence (`stage_iconloader_race`; `iconloader_driver race`: two threads,
  300 distinct names each, real `IconLoader.cpp`):
  ```
  WARNING: ThreadSanitizer: data race   (42 reports; the run then made no progress until the 120 s timeout)
    Read  ... unordered_map::find   IconLoader::getImage        IconLoader.cpp:173
    Previous write ... __hash_table insert   IconLoader::uploadImageKtx  IconLoader.cpp:90
  ASan+UBSan, 3 of 3 runs, exit 1:
    __hash_table:1312: runtime error: member access within misaligned address 0xbebebebebebebebe
      #2 IconLoader::getImage  IconLoader.cpp:173:38
  ```
  `0xbe` is ASan's fill for fresh allocations. One thread follows a bucket
  pointer into memory the other thread's rehash has just allocated: heap
  corruption. Two ways: TSan's race reports and the ASan+UBSan crash in every
  run. This promotes L13, which also covers `atlas_images` and `icons`
  (the internal paths reached from `icon`/`iconButton`).
- Second exported entry point, pass 36 (`stage_icon_api`, `iconloader_driver
  icon-race`).
  - Path: `IconLoader::getUIIcon` (`IconLoader.cpp:181-192`) is also
    public, and its `UIIcon` result lets a mod draw the texture itself.
    It resolves through `getIcon` (:115-131), which writes the per-icon
    cache `ricon->atlasTexture` (:123) with no lock. Then `getAtlasImage`
    (:163-170) and `uploadImageKtx`, which inserts into `atlas_images`
    (:88).
  - Setup: two threads resolve the same 200 icons, each in its own atlas.
  - TSan: 43 to 59 reports per run. They include the cache write at :123
    against the read at :119, and `atlas_images` inserts inside
    `__hash_table`.
  - Duplicate uploads: 27 and 40 of 200 atlases were uploaded twice in two
    runs. Each duplicate is a GL texture and a `SkyImage` that no map
    keeps.
  - ASan+UBSan, 3 of 3 runs: the same
    `__hash_table: runtime error: member access within misaligned address
    0xbebebebebebebebe` crash as with `getImage`.
  - Trigger: as above, but through `getUIIcon`, for example a mod
    prefetching icon textures from `InitLate` while another draws. No mod
    source using it was found.
- Sweep: shared containers with no lock already listed (F6, L2, L13 row).
  `images`, `atlas_images` and `icons` are the IconLoader members.

### F21. `loadFonts` frees the CJK font buffer the atlas owns (latent: double free or use-after-free on any atlas clear, rebuild or destroy)
- Where: `loadFonts` (`androidbk.cpp:568-631`) reads `DroidSansFallback.ttf`
  into a `malloc`ed buffer (:615). It passes the buffer to
  `AddFontFromMemoryTTF` (:624) with an `ImFontConfig` that keeps
  `FontDataOwnedByAtlas = true`, the default (`imgui_draw.cpp:2320`). Then it
  calls `free()` on it after `Build()` (:630). ImGui's header says the atlas
  owns that data and frees it itself (`imgui.h:2983`; freed in
  `ClearInputData`, `imgui_draw.cpp:2400-2402`).
- Trigger: none today. `ImGUI.java:68` always passes
  `enableDroidSans = true`, so the buffer is always handed over, but Canvas
  creates the ImGui context once and never clears, rebuilds or destroys the
  atlas. Any future change that does (destroying the context on shutdown,
  rebuilding fonts for a size, scale or language change) double-frees or
  reads freed memory on the render thread. Low severity now, but every
  user carries the trap.
- Evidence (`stage_fonts`, `font_ownership.cpp`; real `loadFonts` extraction,
  the app's own `DroidSansFallback.ttf` via `FAKE_ASSET_DIR`):
  ```
  control (enableDroidSans=0, then Clear):  survived
  clear:   ERROR: AddressSanitizer: attempting double-free
             #1 ImFontAtlas::ClearInputData() imgui_draw.cpp:2402  #2 ImFontAtlas::Clear()
           freed by: free  <- loadFonts (androidbk.cpp:630)
  rebuild: ERROR: AddressSanitizer: heap-use-after-free
             #0 ImFontAtlasBuildWithStbTruetype imgui_draw.cpp:2806
           freed by: free  <- loadFonts (androidbk.cpp:630)
  destroy: ERROR: AddressSanitizer: attempting double-free  (via ~ImFontAtlas <- ImGui::DestroyContext)
  ```
  Two ways: two different failure classes from deterministic repros, against
  a clean control. ImGui's own issues show the same misuse crashing other
  apps on exit (ocornut/imgui #1259, #5330). Found by the perf track
  (pass 47); promotes L18.
- Sweep: first-party `AddFontFromMemoryTTF` calls: 1 (this one).
  `AddFontFromFileTTF` (`androidbk.cpp`, the Roboto and Georgian fonts) lets
  ImGui own its own copy and is not affected.

### F22. Typed emoji reach ImGui text fields as unpaired surrogates (invalid UTF-8) (low severity)
- Where: `ImGUITextInput.java:215` sends text one UTF-16 code unit at a time
  (`submitUnicodeEvent(text.charAt(i))`). `androidbk.cpp:703-706` queues
  each unit, and `Drain` passes it to `io.AddInputCharacter` (:374), which
  expects a whole code point. ImGui's `AddInputCharacterUTF16` pairs
  surrogates (imgui.cpp:1373-1400).
- Trigger: the user types any character outside the Basic Multilingual
  Plane (emoji, some CJK extensions) into a mod's or Canvas's ImGui text
  field, through the Java text input. UI thread to render thread.
- Evidence (`stage_unicode`, `render_threads unicode`; real
  `submitUnicodeEvent` -> `CanvasInput` -> `Drain` -> ImGui `InputText`):
  ```
  typed: 'a', U+00E9, U+1F600 (D83D DE00), 'b'
  field bytes: 61 C3 A9 ED A0 BD ED B8 80 62
  valid UTF-8: NO (encoded surrogates)
  ```
  Two ways: reading predicted these exact bytes (pass 32), and the host
  repro produced them. With this build's 16-bit `ImWchar` (`imconfig.h`,
  `IMGUI_USE_WCHAR32` off), the paired path would store U+FFFD (`EF BF BD`).
  Showing the emoji itself needs 32-bit `ImWchar` as well.
- Effect: ImGui draws two unknown glyphs, and a mod reading the field gets
  invalid UTF-8. That byte form happens to be JNI's modified UTF-8 for a
  supplementary character, so handing it back to Java through `NewStringUTF`
  round-trips. Anything that expects standard UTF-8 (files, network, the
  game's text APIs) gets invalid input.
- Sweep (same class: JNI or Java text reaching mods as something other than
  standard UTF-8). Corrected in pass 34. The pass 33 text said the 13
  `GetStringUTFChars` sites (L3 row) turn supplementary characters into
  6-byte pairs, as the JNI spec's modified UTF-8 would. ART does not.
  `runtime/jni/jni_internal.cc` (AOSP main) sets
  `kUtfUse4ByteSequence = true; // This is against the JNI spec.`, with a
  comment that ART "ha[s] been emitting 4-byte UTF-8 sequences for several
  years now". It also sets `kUtfUseShortZero = false` and
  `kUtfReplaceBadSurrogates = false`. So on ART, these sites deliver
  standard UTF-8 for well-formed text, including emoji typed into the
  keyboard dialog (`onKeyboardCompleteNative`, `main.cpp:362-368`). Only
  two cases come out as invalid UTF-8: U+0000 becomes `C0 80`, and an
  unpaired surrogate becomes a 3-byte form. Host check: the
  `stage_clipboard` model, Java-side cases.
  F22 itself stands: its bytes come from `submitUnicodeEvent` sending
  single UTF-16 code units, not from `GetStringUTFChars`. Copying F22's
  field text to the clipboard hands Java a valid surrogate pair:
  `NewStringUTF` keeps 3-byte surrogate halves as UTF-16 units. Reading the
  clip back returns the standard 4-byte form, `61 C3 A9 F0 9F 98 80 62`
  (`stage_clipboard`).

### F23. The clipboard helpers leave a Java exception pending on the render thread (low severity)
- Where:
  - `androidbk_set_clipboard` (`androidbk.cpp:561-566`) calls
    `CallStaticVoidMethod(setClipboard)` and never checks for an exception.
  - `androidbk_get_clipboard` (`:547-559`) uses the result of
    `CallStaticObjectMethod(getClipboard)` unchecked.
  - `ImGUI.setClipboard` calls `ClipboardManager.setPrimaryClip` unguarded
    (`ImGUI.java:40-42`).
- Trigger:
  - Menu code (Canvas's or a mod's) calls `ImGui::SetClipboardText` with
    text whose parcel is too large: `InputText` copy, `LogToClipboard`, or a
    mod's own copy button.
  - `setPrimaryClip` then throws
    `java.lang.RuntimeException: android.os.TransactionTooLargeException`.
    One public report shows a 1,193,472-byte parcel failing.
  - Thread: the render thread. That is the Java "imgui dispatch thread",
    inside the long `ImGUI.init()`/`resurface()` call.
- Evidence (`stage_clipboard`, `render_threads clipboard-throw` and
  `clipboard-throw-set`, ASan+UBSan):
  - Harness: the real `androidbk.cpp` and ImGui. The fake JNIEnv keeps a
    pending exception per thread. It counts every call outside the list in
    Android's JNI tips ("Exceptions"). Its `setClipboard` model throws for a
    String over 500 Ki UTF-16 units (about a 1 MB parcel) and leaves the
    clip unchanged.
  - Output:
    ```
    clipboard-throw:     setClipboard threw: 1 time(s)
                         JNI calls outside the allowed list made with the exception pending: 3 (first: CallStaticObjectMethod; CheckJNI aborts here)
                         paste after the failed copy returned: 73 6D 61 6C 6C 20 63 6C 69 70   ("small clip", the previous clip)
                         exception pending when init() returned to Java: YES
    clipboard-throw-set: setClipboard threw: 1 time(s)
                         JNI calls outside the allowed list made with the exception pending: 0
                         exception pending when init() returned to Java: YES
    ```
  - Two ways: predicted by reading in pass 34 (L20), and reproduced
    deterministically in both modes.
  - Modelled, not shown on a device: the size at which `setPrimaryClip`
    throws, and what release ART does when `CallStaticObjectMethod` runs
    with an exception pending. That call is outside the allowed list, so
    the result is undefined; in a debuggable build CheckJNI aborts.
- Effect, by the JNI rules and `ImGUI.java`:
  - The copy fails silently, and the next paste returns the previous clip.
  - The exception stays pending through every later JNI call on the
    render thread. Those include the next clipboard call and any JNI a
    mod's `Draw` makes on that thread.
  - When the render loop ends (`surfaceDestroyed` -> `shutdown()`),
    `init()` returns. Java then throws the exception in `ImGUIThread.run`,
    which catches only `InterruptedException` (`ImGUI.java:89-94`). It
    reaches the thread's uncaught-exception handler; on Android the
    default handler ends the process. That last step is from the platform's
    documented behaviour and is not run here.
- Sweep: 2 of 6 `Call*Method` sites in scope have no exception check, both
  here (sweeps table). Both `ThrowNew` sites (`main.cpp:264`, `:306` via
  `crash()`) return to Java right after throwing, which is correct.

### Independent corroboration

A separate Codex Desktop session (2026-10-30, "Canvas performance audit")
built its own harness against checkout `5881a85` and reproduced F1, F3, F4
(both the never-fired and the lock-rejected case), F5 (both install orders),
F6 (3/3 runs, with hook-only and patch-only controls clean) and L7, across 45
runs under ASan/UBSan with libc++ and libstdc++, TSan and checked iterators.
Plain libstdc++ completes F4 silently; libc++ and checked iterators catch it.
Bundle: `C:\Users\admin\Documents\Codex\2026-10-30\subject-canvas-performance-audit-pointed-questionnaire\outputs\canvas-sanitizer-harness\`
(`findings.json`, `results/matrix.json`). That counts as the second, independent
confirmation for those findings.

## Leads (reproduced at most once, or not yet)

- L1. Promoted to F10.
- L2. Promoted to F9.
- L3. Promoted to F8.
- L4. Promoted to F12.
- L5. The `relength` path is promoted to F11. Remaining: **`set_hex("")` after
  a non-empty opcode, reproduced by UBSan only (`ubsan_ext`, `artpatch`):**
  glibc `realloc(p, 0)` frees and returns NULL, then both copies in
  `artpatch_apply` get NULL (`artpatch.c:93:41` argument 1, `:94:41`
  argument 2: "null pointer passed as argument N, which is declared to never be
  null"). The copy size is 0, so nothing observable happens on glibc; bionic's
  `realloc(p, 0)` result is not established here. Low severity; no second way
  is available on the host beyond UBSan.
- L6. The fall-off-the-end path is promoted to F14. Remaining: a missing
  atlas is cached in `images` instead of `atlas_images`
  (`IconLoader.cpp:55-58`), so the next icon naming the same atlas misses the
  cache, tries the asset again, and its `images.insert` finds the key taken:
  the new `SkyImage` leaks. **Reproduced once (LSan, `iconloader
  shared-missing-atlas`, two icons on one missing atlas):**
  `Direct leak of 16 byte(s) in 1 object(s) ... IconLoader::uploadImageKtx
  IconLoader.cpp:55:23 <- getAtlasImage <- getIcon` (the other four leaks in
  that run are F8's `addIcon` strings). One `SkyImage` per extra icon, once,
  since each icon then caches `IL_NO_TEXTURE`. Low severity. Second way
  pending (count `images` entries against `SkyImage` allocations).
- L7. `CipherUtils::CipherScanAll(start, end, ...)` calls `strlen(_mask)` before
  its NULL check (`CipherUtils.cpp:156`).
- L8. `CMakeLists.txt:41-42` passes `-Werror -Wall` and then `-w`, which
  silences every warning. Checked in pass 5 (`stage_warnings`, clang 18 with
  `-Wreturn-type -Wnontrivial-memaccess -Wsometimes-uninitialized
  -Wuninitialized` over every first-party file): the only hidden warning is
  F14's (`IconLoader.cpp:113`). GCC's `-Wclass-memaccess` adds F3's.
- L9. `androidbk.cpp:531-532` (`init_sfc`): `ANativeWindow_fromSurface` already
  returns an acquired reference and the code calls `ANativeWindow_acquire`
  again, but replacing the surface releases only one, so one window reference
  leaks per surface cycle (app to background and back). Raised by the Codex
  session's reviewer; confirmed in source and against the NDK header's
  contract. **Reproduced once (deterministic reference-count oracle,
  `render`, identical under the TSan and ASan builds):**
  `ANativeWindow references held after 4 surfaces: 5 (expected 1)`.
  Second way pending (an on-device check is outside this loop's scope; on the
  host, a variant driving N surfaces should hold N + 1).
- L10. Not a sanitizer target, recorded for the performance investigation:
  Canvas's `SystemIO_android.GetCurrentThermalStatus` (`java/com/tgc/sky/
  SystemIO_android.java:1410-1421`) caches the first reading for the process
  lifetime; vanilla registers a thermal listener. The game's native response to
  the value is untraced (Codex session, thermal-review-validation.json).
- L11. Promoted to F13.
- L12. `main.cpp:428` `getSysetemUI` overwrites `Canvas::systemUI` with a new
  global reference without deleting the previous one. Called from
  `GameActivity.onCreate` (`GameActivity.java:259`) with a new
  `SystemUI_android`, which keeps its `GameActivity` (`SystemUI_android.java:123`),
  so every earlier activity stays reachable from a native global reference.
  The manifest's `configChanges` avoids recreation for most configuration
  changes; a `GameActivity` created again in the same process (relaunch, or a
  change not in that list such as density) leaks the previous one. No
  `DeleteGlobalRef` appears anywhere in first-party code. **Reproduced once
  (global-reference count, `jni systemui`):** `GameActivity created 3 times:
  live global refs 3 (expected 1, the current SystemUI)`. Second way pending
  (a heap-level check needs the Java side, outside this harness).
- L13. Promoted to F20 (pass 29). Original note: `IconLoader::images`, `atlas_images` and `icons` (`IconLoader.cpp:15-17`)
  are unguarded maps; `getImage`/`getIcon` insert on a miss and upload a GL
  texture. Safe only if every caller is on the render thread; a mod calling
  `IconLoader::getImage` from `InitLate` would race the render thread and
  upload without a current GL context.
- L14. Promoted to F16.
- L15. `Cipher::DescribePatchesIn(start, len)` (`CipherRegistry.cpp:164-179`)
  computes `end = start + len` without an overflow check, and its overlap test
  is true for an empty range that falls inside an install. Against an
  overflow-safe reference (`stage_registry`, mode `ranges`):
  ```
  MISMATCH DescribePatchesIn(0x8000, 0xffffffffffffffff) [len = SIZE_MAX]: got 0, reference 2
  MISMATCH DescribePatchesIn(0x8000, 0xffffffffffff8000) [len to the top]:  got 0, reference 2
  MISMATCH DescribePatchesIn(0x10002, 0x0) [empty range inside an install]: got 1, reference 0
  ```
  It is a contract gap, not memory unsafety: a mod passing `SIZE_MAX` to mean
  "everything from start" gets no installs. `IsAddressPatched` and
  `DescribePatchesAt` compute `address + bytes` the same way, but those values
  come from Fire() and cannot wrap for user-space addresses. No first-party
  caller of `DescribePatchesIn`. Deterministic; one way only, so it stays a
  lead.
- L16. No instruction-cache maintenance after patching code. Neither
  `artpatch_apply` nor `artpatch_restore` (`artpatch.c:89-111`), nor
  `CipherPatch::Fire`/`Restore` (`CipherPatch.cpp:17-56`), calls
  `__builtin___clear_cache` after writing instructions. On arm64,
  instruction fetch is not coherent with data writes: new code needs
  cache maintenance (DC CVAU / IC IVAU and barriers, which
  `__builtin___clear_cache` emits) before it is reliably executed. ChatFlagFix
  does this itself after `Fire()` (`ChatFlagFixInstall.cpp:120`). Mods that
  patch through `CipherPatch` get no flush, and neither does the restore path.
  Not testable on this host: x86 keeps instruction fetch coherent with
  stores, and QEMU user mode does not model the I-cache. The kernel may sync
  the I-cache when a newly copied page is first mapped executable. That would
  cover the first patch on a page, not later ones. Effect if real: a patch or
  restore that sometimes doesn't take effect, until the stale lines are
  evicted. Architectural reasoning only; needs a device check.
- L18. Promoted to F21 (pass 30). Original note: font buffer ownership in `loadFonts` (`androidbk.cpp:615-630`):
  `DroidSansFallback.ttf` is read into a `malloc`ed buffer and passed to
  `AddFontFromMemoryTTF` with an `ImFontConfig` whose `FontDataOwnedByAtlas`
  keeps ImGui's default, `true`. ImGui's header says the atlas then owns and
  later frees the data (`imgui.h:2983`; freed in `ClearInputData`,
  `imgui_draw.cpp:2400`). `loadFonts` also calls `free()` on it right after
  `Build()` (:630). The atlas keeps a dangling pointer. Clearing or
  destroying it frees the memory a second time; rebuilding it reads freed
  memory. Not reachable today: Canvas creates the ImGui context once, never
  destroys it, and never clears or rebuilds the atlas. And `ImGUI.java:68`
  always passes `enableDroidSans = true`, so the buffer is always handed
  over. Found by the perf track (pass 47) while reading the font code. A
  host repro would call the real `loadFonts` with the asset present, then
  `io.Fonts->Clear()` under ASan.
- L19. Promoted to F22 (pass 33). Original note: characters outside the Basic Multilingual Plane reach ImGui as two
  unpaired surrogates. `ImGUITextInput.java:215` calls
  `ImGUI.submitUnicodeEvent(text.charAt(i))` once per UTF-16 code unit.
  The native side queues each unit (`androidbk.cpp:703-706`), and `Drain`
  passes it to `io.AddInputCharacter` (:374), which takes a whole code
  point. ImGui's `AddInputCharacterUTF16` would pair the halves. Expected
  effect: an emoji typed into a mod's ImGui text field becomes two invalid
  3-byte sequences (`ED A0 BD ED B8 80` for U+1F600) instead of
  `F0 9F 98 80`. By reading (pass 32); a host repro through the real JNI
  path and an `InputText` is next. Functional, not memory-unsafe.
- L17. `InitLate()` is never called for mods that are not registered as menu
  mods. `UserThread` (`main.cpp:288-300`) adds a mod to `Canvas::userLibs`
  only if it has a name, a non-null `Draw` from `Start()`, and `isDraw`.
  `lateInitUserLibs` (`main.cpp:398-412`) walks only `userLibs`, so a
  patch-only mod that exports `InitLate` never has it called. Deterministic
  (`stage_lifecycle`, both builds and both modes):
  `MISMATCH mod_patch.so (patch-only): InitLate called 0 time(s)`, and 1 of 2
  mods registered. No documentation of the mod API says whether `InitLate`
  is meant for every mod, so this stays a contract question for the owner.
- L20. Promoted to F23 (pass 35). Original note: the clipboard helpers never check for a Java exception
  (`androidbk.cpp:547-566`). Found by reading, pass 34.
  - Get side: `androidbk_get_clipboard` passes the result of
    `CallStaticObjectMethod(getClipboard)` straight to
    `GetStringUTFLength`/`GetStringUTFChars`.
  - Set side: `androidbk_set_clipboard` ignores whatever
    `CallStaticVoidMethod(setClipboard)` leaves pending.
  - Java side: `ImGUI.getClipboard()` handles its own null cases, but
    `ImGUI.setClipboard()` calls `ClipboardManager.setPrimaryClip` unguarded
    (`ImGUI.java:40-42`). A large clip makes that throw
    `java.lang.RuntimeException: android.os.TransactionTooLargeException`.
    One public report shows a 1,193,472-byte parcel from `setPrimaryClip`.
  - Path: menu code calls `ImGui::SetClipboardText` with the text, on the
    render thread. That thread is the Java "imgui dispatch thread", inside
    the long `ImGUI.init()`/`resurface()` call. The exception then stays
    pending through every later JNI call on that thread; CheckJNI aborts on
    the first one (the next clipboard call). When the render loop exits and
    `init()` returns, Java throws it in `ImGUIThread.run`. That method
    catches only `InterruptedException` (`ImGUI.java:89-94`), so the
    exception goes to the thread's uncaught-exception handler.
  - Who triggers it: a mod or the user copying very large text, or a binder
    failure. Device behaviour is not shown here.
  - Host side, next: the fake JNIEnv models a pending exception.
  - Sweep: Java calls from native code with no exception check, 2 of 6 in
    scope (sweeps table).
- L21. `addIcon` with a name already registered leaks the new icon
  (`IconLoader.cpp:33-38`). Pass 36.
  - Mechanism: it allocates a `PrivateUIIcon` and calls
    `icons.insert(...)`. `insert` keeps the existing entry, so the new
    object, and its two UTF copies (F8), are unreachable. The first
    registration's uv stay in effect.
  - Two ways: by reading (`unordered_map::insert` semantics), and
    deterministically under LSan (`stage_icon_api`). LSan's check runs
    while the maps are alive and attributes exactly 1 direct leak to
    `IconLoader.cpp:33` (48 bytes), from the duplicate.
  - Who triggers it: `IconLoader.findIcons()` (Java, in
    `MainActivity.loadGame()`, before `settle`). Either
    `UIPackedAtlas.lua` lists a resource name twice (not checked: the
    game's assets are not in the repo), or `loadGame` runs a second time in
    one process. `main.cpp:262`'s guard ("Native game initialization
    already started") exists for that case, but `findIcons` runs before
    it, so every icon would be registered again and leaked once.
  - Bounded per call; low severity. A lead until a duplicate registration
    is shown to happen.

## Bug-class sweeps (pass 2, first-party tree)

| Class (finding) | Sites | Notes |
|---|---|---|
| `memcpy` into a non-trivially-copyable type (F3) | 1 | `CipherUtils.cpp:280`; the other 10 `mem*` calls copy raw bytes |
| `erase(find(...))` without an `end()` check (F4) | 2 | `CipherPatch.cpp:68` reachable; `CipherHook.cpp:117` only runs when `stub != nullptr`, which implies the hook was registered |
| C-style downcast of a `CipherBase*` (F5) | 3 | `CipherHook.cpp:57,128`, `CipherPatch.cpp:32` |
| Fixed buffer filled with no capacity check (F1) | 5 | `char[256]` at `CipherUtils.cpp:208,220,232,244`, `CipherRegistry.cpp:211`; the remaining fixed arrays are bounded |
| Shared container without one consistent lock (F6, L2, L13) | 3 | `s_InstanceVec` (two mutexes), `onKeyboardCompleteListeners` (none), IconLoader maps (none); `userLibs` is published through `InitializationGate` |
| Plain global shared across threads (L1) | 1 | `androidbk.cpp:35` `run`; `g_time` and `clipboard_buffer` stay on the render thread |
| `GetStringUTFChars` without `ReleaseStringUTFChars` (L3) | 10 + 1 + 1 | 10 always-leaking sites (`main.cpp:345-348,364,384-386`, `IconLoader.cpp:36,38`), 1 error-path (`main.cpp:327`), 1 deliberate (`main.cpp:273`) |
| Reference acquired without matching release (L9, L12) | 2 | `androidbk.cpp:531-532`, `main.cpp:428`; `fsel_setup` and `ImGUI_init` global refs are taken once per process |
| Thread attached to the VM and not detached on some path (F13) | 3 | `fileselector.cpp:103` (`requestFileMultiNamed`, every path; `requestFile` and `requestFileMulti` delegate to it), `fileselector.cpp:154` (`requestFiles`, every path), `CipherUtils.cpp:330` (`performHapticFeedback` out-of-range type); `shareFile` and androidbk's `CHECK_ENV`/`CHECK_END` detach correctly |
| Unchecked `fopen` (F7) | 1 | `procutils.c:43`; `OtherAudioApply.cpp:33` and `Utils.h` check |
| Single-page `mprotect` then a ranged write (F18, pass 19) | 2 of 3 | `artpatch.c:92-95`, `:103-105` can cross a page; `OtherAudioApply.cpp:154-161` writes one aligned 8-byte slot and cannot |
| Function-local static used from other objects' destructors (F17, pass 15) | 1 of 2 | `CipherRegistry.cpp:37` (from `~CipherPatch`/`~CipherHook`); `IconLoader.cpp:42` is not |
| Detached thread started from an entry point without a run-once guard (F19, pass 25) | 1 of 2 | `main.cpp:411` (`lateInitUserLibs`); `OtherAudioApply.cpp:241` is guarded by `g_started` under `g_mutex` |
| JNI local reference not deleted on a thread that stays in native code (pass 22) | 0 | 11 creation sites (`NewStringUTF`, `NewObjectArray`, `GetObjectClass`, `CallStaticObjectMethod`): all deleted (`androidbk.cpp:556,565`, `fileselector.cpp:83-89,123,133-134,172`, `CipherUtils.cpp:347,385-388`) or made global (`fileselector.cpp:20-21`). `main.cpp:263,304` `FindClass` run in short JNI calls that return to Java. The render loop runs inside one long `ImGUI_init` call, so a leak there would pile up; its clipboard helpers delete their refs, and `ImGUI.getClipboard()` never returns null (it returns `""`), so the unchecked `GetStringUTFLength` at `androidbk.cpp:550` is safe as written |
| Java call from native code with no exception check (F23, passes 34-35) | 2 of 6 | `androidbk.cpp:549,564` (clipboard get/set) never check; `CipherUtils.cpp:342,383` check and clear (`:334-344`, `:375-384`); `fileselector.cpp:128,167` check through `fsel_check_exception` |
| `NewStringUTF` on text from a mod or ImGui with no validation (pass 34) | 5 | `androidbk.cpp:563` (ImGui's clipboard text), `CipherUtils.cpp:381-382` (mod path and MIME type), `fileselector.cpp:81,121` (mod MIME types and suggested name). CheckJNI aborts on invalid modified UTF-8. Release ART replaces a sequence cut short by the NUL with `?` and logs a SafetyNet event (`jni_internal.cc`, `NewStringUTF`); `stage_clipboard` reproduces the `?` in its model. Only the fileselector sites check the result for null |

## Triaged, not findings

- `ubsan_ext` (`-fsanitize=integer,implicit-conversion` over the decoder
  corpus) reports unsigned wraparound at `OtherAudio.h:53` (`BlTarget`),
  `OtherAudio.h:60` (`AdrpPage`) and `ChatFlagFix.h:64` (`AdrpTarget`). Each adds
  a sign-extended negative branch/page displacement to a `uintptr_t`, which is
  defined modular arithmetic and intended. Not UB.

## Clean so far

- Decoder oracles (`fuzz_decoders`, rewritten in passes 21 and 26; reach
  measured by `stage_decoders_cov`), 119,562 executions with no oracle
  failure and no sanitizer report. The corpus of 3,424 inputs reaches:
  - ChatFlagFix: 16 accepted matches, all with an aligned site inside the
    image, `original` the AND-keep-bits-5-7 word there, and the
    AND-keep-bits-0-1 replacement on the same register;
  - `OtherAudio::Decide`: `Swap` x21 (each an 8-byte-aligned slot inside
    RELRO holding a target inside code), `NotAVtable` x32,
    `SlotOutsideRelro` x6, `AlreadyApplied` x3, `TargetOutsideCode` x40,
    `NotTheStub` x5.
  Unreached by design: `Disabled` and `NotLocated` (the harness always
  enables and locates), and the two `Unreadable` reads that follow a
  successful range check. Before pass 26 the same oracles were reached 0
  times: an accepted match needs the "chat" string megabytes from the code,
  and `Swap` needs a consistent pointer chain. The input now carries a
  small read-only region, and the seeds (`make_seeds.cpp`: `chatpos_*`,
  `decide_*`) supply both.
- `CipherRegistry.cpp` under concurrency (`stage_registry`, mode
  `concurrent`): 4 installer threads (RecordInstall/ForgetInstall/
  NotePatchBytes/LookupPatchBytes, 10240 installs), 2 query threads
  (IsAddressPatched/DescribePatchesAt/DescribePatchesIn), and listeners
  registered while installs run. TSan (with `detect_deadlocks=1`) and
  ASan+UBSan: no reports. Oracles hold: a listener registered first receives
  every install exactly once; later listeners never receive one twice; query
  results cover the address and are sorted by `installSeq`; the final state
  matches the model. Observed, not a defect under the header's contract:
  listeners receive concurrent installs out of `installSeq` order (about
  18,000 inversions across 6 listeners). No stale delivery was seen.
- ChatFlagFix + OtherAudio decoders (`fuzz_decoders`): about 28M libFuzzer
  executions under ASan+UBSan, no reports (first pass, unseeded; coverage
  plateaued at 109 edges). Pass 2, seeded with the 21 unit-test instruction
  windows: 2.9M executions, coverage 240 edges, 651-input corpus, no reports;
  the corpus replayed under MSan: no reports.
- `procutils_read_mprotection` raw-input fuzzing: 32M executions under
  ASan+UBSan, no memory errors (the logic error in L4 needs structured input).
- `ChatFlagFixTest`, `OtherAudioTest`, `OtherAudioSwapTest`,
  `InitializationGateTest` clean under ASan+UBSan; `InitializationGateTest`
  clean under TSan.
- File-picker callbacks (`fileselector.cpp` `callbackFunctionCall`,
  `multiCallbackFunctionCall`; `stage_threads`, mode `callbacks`): with the fake
  JNIEnv handing out malloc'd element copies, a single fd, a 3-fd array, a null
  array and an empty array all reach the callback correctly, every
  `GetIntArrayElements` is released (`JNI_ABORT`), and ASan+UBSan report
  nothing. Contract worth documenting for mods: the `fds` pointer is valid only
  during the callback, since Canvas releases the elements right after it.
- Clipboard helpers (`androidbk_get_clipboard`, `androidbk_set_clipboard`),
  pass 34 (`stage_clipboard`, `render_threads clipboard`). Called through
  `ImGui::SetClipboardText`/`GetClipboardText` on the render thread inside
  a frame. The fake JNIEnv models `ImGUI.getClipboard()`/`setClipboard()`
  and ART's `NewStringUTF`/`GetStringUTFChars` conversions, taken from
  `jni_internal.cc` (see the comment in `fake_jni.cpp`).
  - Result: all 14 cases return what the model predicts; ASan+UBSan and
    LSan report nothing. Cases: empty, ASCII, U+00E9, CJK, a 4-byte emoji,
    F22's field bytes, a lone surrogate, a truncated sequence, 1 MiB then
    2 bytes (the `realloc`'d buffer grows and shrinks), and Java-side
    U+0000, a pair, a lone low surrogate and an empty clip.
  - JNI balance: every `GetStringUTFChars` is released. Java calls:
    getClipboard 14, setClipboard 10.
  - TSan: one report, F10's run-flag race (`androidbk.cpp:674` vs `:505`)
    at shutdown, nothing in the clipboard path.
  - Contract for mods: the returned pointer is `clipboard_buffer`, which
    the next `GetClipboardText` reallocates. ImGui's own fallback clipboard
    has the same lifetime.
  - Text that differs after the round trip is a model property, not a
    memory error: F22's form is normalised, a truncated sequence becomes
    `?`, and Java-side U+0000 and lone surrogates reach ImGui as invalid
    UTF-8.
- IconLoader's mod-facing API, pass 36 (`stage_icon_api`,
  `iconloader_driver icon-api`). The real `IconLoader.cpp` and ImGui run
  headless, as in imgui's `example_null`, against the KTX/GL/AAsset
  stand-ins. All 6 checks pass, with no ASan/UBSan report or ImGui assert:
  - `getUIIcon` returns the atlas texture and the registered uv, and
    `IL_NO_TEXTURE` for an unknown name or a missing atlas;
  - `icon()` draws with the atlas texture, or the magenta placeholder;
  - `iconButton` reports a click both as an image button and through its
    text-`Button` fallback.
  Single-threaded use is clean; the two-thread case is F20.

## Queue

Ranked by who can supply the input or interleaving, how unsafe the code is,
oracle strength, and effort.

1. Done (passes 2-4): L1-L5 and F2/F5 second confirmations.
2. Stateful Cipher API fuzzer: bytes -> sequence of new/set_Address/set_Opcode/
   Fire/Restore/delete on hooks and patches at a few addresses, with a model of
   which addresses should be patched (covers F4-F6, the recursive restore chain
   in `CipherHook::m_Restore`, lock rejection).
3. Done (pass 15): real `CipherRegistry.cpp` under TSan and ASan+UBSan
   (`stage_registry`): concurrency clean; found F17 (exit order) and L15
   (range contract).
4. Done (passes 17-20): artpatch differential fuzzer (`stage_artpatch_diff`).
   Found F18 (page crossing); within one page, 53,944 executions agree with
   the byte and protection model. Left: a short leak check.
5. `fuzz_decoders` oracle: a single match must lie inside the scanned range,
   `site.original` must equal the word at `site`, `Decide == Swap` implies the
   slot is inside RELRO and the target inside code. Grow coverage past 109 edges.
   Rewritten in pass 21 (fixed virtual base, so `Swap` is reachable; oracles
   for both decoders and `Decide`); not yet run.
6. Done (pass 5): `IconLoader::uploadImageKtx` (F14, missing-atlas lead).
7. Done (passes 4 and 7): fileselector attach (F13) and callbacks (clean).
8. OtherAudio thread topology under TSan (`Locate` from the load-time
   constructor and `settle`, the `Attempt` worker, `Replacement` on an audio
   thread) using a synthetic image built from `OtherAudioSwapFixtures.h`.
   Read in pass 16; ranked below 4. By inspection: `g_result` is written once
   under `g_mutex` before `g_found` is set and never changes, so the pointer
   `Result()` returns is safe to read after the lock; `g_gate` is published
   with release/acquire; `g_started` is guarded. The harness would need a
   synthetic game `.so` that `dl_iterate_phdr`, `/proc/self/maps` and the
   RELRO `mprotect`+CAS accept: high effort for a likely-clean result.
   Untested cross-module risk: `Write()` returns the slot's page to
   read-only, which would close another module's write window on the same
   page.
9. Full `main.cpp` lifecycle harness (settle, onModLibrary with a test `.so`,
   completeModInitialization, lateInitUserLibs, render loop) under TSan/ASan.
   Mod part done in pass 25 (`stage_lifecycle`: real `onModLibrary`,
   `UserThread`, `completeModInitialization`, `lateInitUserLibs` and two test
   `.so` mods): F19 and L17. Left: `settle` (needs the ElfScanner, ChatFlagFix
   and OtherAudio pieces) and the render loop together with mod `Draw`.
10. HWASan (the sanitizer usable on device) via an arm64 container under QEMU,
    re-running F1-F6 to show what an on-device HWASan build would report.
    Partly done (passes 12-14): the smoke test and F3 reproduce; F1 and F9
    die in QEMU with SIGILL; F4 hangs with no output. `run_hwasan.sh` now
    disables core dumps and kills each step after `HW_TIMEOUT` (180 s), and
    adds an F4 run with `handle_segv=0` to locate the hang. Re-run (passes
    16-20):
    - smoke and F3: reproduce again (exit 99);
    - F1 and F9: `qemu: uncaught target signal 4 (Illegal instruction)`, then
      a stall that `ulimit -c 0` does not prevent, killed at 180 s;
    - F4: no output and no fault in 180 s, with or without HWASan's SEGV
      handler. With `handle_segv=0`, QEMU would print `uncaught target
      signal 11` on a fault, so the process spins without faulting.
    Hypothesis for F4: the vector is empty, so the call is memmove(NULL,
    0x8, SIZE_MAX-7). HWASan's interceptor checks the shadow from NULL over
    that whole size, a scan of the zero-filled shadow that does not end in
    practice. If so it is HWASan's behaviour, not emulation, and it would
    hang on a device too instead of reporting. Tested natively in
    `stage_hwasan_x86` (x86-64 page-aliasing mode, primary heap only),
    pass 20:
    - smoke: `tag-mismatch`;
    - F9: `HWAddressSanitizer: tag-mismatch` READ of size 8 at
      `onKeyboardCompleteNative.inc:9`, through a stale pointer into memory
      reused for a `strdup`. HWASan's cause line says heap-buffer-overflow,
      but its cause is a heuristic; the read is F9's use after reallocation.
      A third sanitizer for F9.
    - F4: no report and no hang ("survived", exit 0). HWASan does not detect
      F4 in either setup; ASan's `negative-size-param` check does. The
      arm64 hang therefore stays unexplained; the shadow-scan idea is
      untested.
    F1 (a stack overflow) cannot be tested in aliasing mode: it has no stack
    tagging.
11. Overlay lifecycle oracles in `render`: surface generation per frame (no
    frame after `shutdown()` returns may use the old surface), the L9 window
    reference count, EGL call order (`eglSwapInterval` before a current
    context, `androidbk.cpp:160`).
    Mostly covered: the surface after `surfaceDestroyed` is F10 (including
    "`shutdown()` never waits") and F16, the window reference is L9, and the
    EGL order is F15. Left: count the swaps that start after `shutdown()`
    returns, without a swallow, in `render_threads` (`quiet-shutdown`).
12. Overlay CPU cost baseline: build the `render` harness without sanitizers at
    -O2 and record CPU time per overlay frame (idle menu, menu with mods drawn,
    input bursts). Host numbers are relative only, for spotting regressions and
    comparing variants (e.g. a dirty-gated loop); they say nothing about GPU or
    compositor cost on a phone.
    Moved to the performance track (`app/src/test/perf`, PERF.md runs 1-14).
13. Done in pass 35: F23 (`stage_clipboard`, throw modes). L20 on the host: give the fake JNIEnv a per-thread pending exception.
    Let `setClipboard` throw above a size threshold. Count JNI calls made
    while an exception is pending, excluding those the JNI spec allows
    (`DeleteLocalRef`, `Release*`, `Exception*`, `DetachCurrentThread`).
    Check whether an exception is still pending when `init()`/`resurface()`
    return to Java.
14. Repo-wide coverage (`stage_coverage_all`, pass 34): the in-scope files
    no harness compiles, and the least-covered compiled ones, are the
    targets for deepening; see the pass 34 log entry.

## Pass log

- Pass 1 (2026-10-31): static review of first-party code; harness built;
  F1-F6 confirmed (ASan, UBSan, TSan, libFuzzer); decoders and unit tests clean.
- Pass 2 (2026-10-31): evidence recorded here; stages added for MSan, extended
  UBSan, CFI, LSan via fake JNIEnv, TSan on the render loop and keyboard
  listeners, procutils. Searches: NDK `ANativeWindow_fromSurface` reference
  semantics, `SurfaceHolder.Callback.surfaceDestroyed` contract (both back L9
  and L1). Ran all stages. Result: F2 confirmed by MSan, F5 by CFI, new F7
  (UBSan + ASan); L2 (TSan), L3 (LSan), L4 (deterministic), L5 (MSan, UBSan)
  reproduced once each; decoders seeded to 240 edges, clean under ASan, UBSan,
  MSan; integer/conversion reports triaged as intended wraparound; sweeps
  recorded; L11-L13 added from sweeps. `render` failed to compile (missing
  `Core/imgui` include path), fixed and re-running with `unit` and `jni`.
  Next: `render` results, then second confirmations for L2-L5.
- Pass 2b (2026-10-31): re-ran `unit jni render` after the include fix and the
  LSan-before-exit change. `unit` green. L3 promoted to F8 (LSan and the fake
  JNIEnv counter agree on 108 leaked copies). L1 reproduced by TSan (`run`
  race, androidbk.cpp:505 vs :674); CanvasInput bridge clean under TSan. L9
  reproduced by the reference-count oracle (5 held, expected 1). Render loop
  clean under ASan+UBSan. Next: deterministic second ways for L1 (forced lost
  shutdown) and L2 (forced reallocation mid-iteration), then L4/L5.
- Pass 3 (2026-10-31): searches on forced-interleaving reproduction and
  `vector::push_back` iterator invalidation (reallocation invalidates every
  iterator). Built `jni interleave` (UI thread held inside a listener while
  another thread grows the list) and `render lost-shutdown` (surfaceDestroyed
  inside resurface's window before `run = true`, paused in the fake EGL); added
  the `artpatch` stage (ASan content checks for L5, end-to-end protection check
  for L4 on real /proc/self/maps). Ran `unit jni render`: `unit` green; L2
  promoted to F9 (deterministic ASan heap-use-after-free at main.cpp:366 plus
  TSan); L1 promoted to F10 (454 frames presented after shutdown returned plus
  TSan); L9 again 5 references for 4 surfaces. Harness incident: `run.sh` was
  edited while the container executed it, producing two shell errors after the
  last stage; results above were complete before that. Next: `artpatch` stage
  (L4, L5 second ways), then L11 (ART behaviour for attached threads that exit).
- Pass 3b (2026-10-31): ran `unit artpatch`. `unit` green. L5 `relength`
  promoted to F11 (ASan shows `be be be be` restored into the page, matching
  the MSan origin at artpatch.c:75); L4 promoted to F12 (real kernel maps: the
  first page of an `r-x` mapping after an `r--` one is `r--p` after apply and
  restore). `set_hex("")` stays a low-severity UBSan-only lead. Next: L11 (ART
  behaviour when an attached native thread exits; fake JavaVM attach/detach
  counting through fileselector), then the stateful Cipher fuzzer (queue 2).
- Pass 4 (2026-10-31): searches on ART's behaviour for native threads that
  exit while attached; read `Thread::ThreadExitCallback` in ART's source
  (warning, then `LOG(FATAL)` on the second destructor pass). Added opt-in
  per-thread attachment tracking with that rule to the fake JavaVM and a
  `threads` stage (real fileselector.cpp, extracted performHapticFeedback).
  Ran `unit threads`: `unit` green; L11 promoted to F13 (requestFile from a
  mod thread: attaches=1 detaches=0, then the ART rule aborts; controls clean).
  Sweep: 3 attach-without-detach sites (2 in fileselector, 1 narrow in
  CipherUtils). Next: stateful Cipher API fuzzer (queue 2).
- Pass 5 (2026-10-31): searched libktx's documented `ktxTexture_GLUpload`
  results. The previous pass's work was set aside (its output was withheld);
  queue item 2 is deferred. Added `iconloader` (real IconLoader.cpp with
  libktx/GL/AAsset stand-ins, every `KTX_error_code`, missing-atlas LSan mode)
  and `warnings` (compiler sweep of hidden warnings, clang and GCC). Ran
  `unit iconloader warnings`: `unit` green; L6's fall-through promoted to F14
  (UBSan return check for 16 of 19 codes plus `-Wreturn-type` at
  IconLoader.cpp:113); missing-atlas leak reproduced once by LSan; F3 note
  corrected (GCC flags it, clang 18 does not). Sweep: one site each for the F3
  and F14 classes, no uninitialized-variable warnings. Next: L12
  (`getSysetemUI` global-ref replacement), then fileselector callbacks
  (queue 7).
- Pass 6 (2026-10-31): searched JNI global-reference lifetime and ART's
  global table limits. Traced `getSysetemUI` to `GameActivity.onCreate` and
  `SystemUI_android`'s activity field; added global-ref counting to the fake
  JNIEnv and a `jni systemui` mode. Ran `unit jni`: `unit` green; F8/F9
  re-reproduced; L12 reproduced once (3 live refs after 3 activity creations).
  Sweep: one repeating `NewGlobalRef` site (`main.cpp:428`); the other two
  (`fsel_setup`, `ImGUI_init`) run once per process; no `DeleteGlobalRef` in
  first-party code. Shared harness change: `fake_platform.cpp` gained opt-in
  perf-track knobs (swap wait, window size, frame hooks, `FAKE_PLATFORM_NO_
  CANVAS_MENU`); defaults unchanged. Next: fileselector callbacks (queue 7).
- Pass 7 (2026-10-31): searched the JNI array-elements contract (pointer valid
  until release; `JNI_ABORT` frees a copy without write-back). Gave the fake
  JNIEnv real int arrays (malloc'd element copies, outstanding count) and added
  a `threads callbacks` mode. Ran `unit threads`: `unit` green; F13
  re-reproduced; file-picker callbacks clean (correct values for single, 3,
  null and empty arrays; 0 outstanding elements; no ASan/UBSan report). Queue
  items 1, 6, 7 marked done. Next: overlay lifecycle oracles (queue 11: EGL
  call order, frames after `shutdown()` returns).
- Pass 8 (2026-10-31): searched the EGL man page rules for `eglSwapInterval`
  (needs a current context and draw surface, else `EGL_BAD_CONTEXT` /
  `EGL_BAD_SURFACE`). The fake EGL now tracks the current context and surface
  per thread and counts contract violations. Ran `unit render`: `unit` green;
  new F15 (call before any context, oracle plus Codex's static finding); F10
  also manifested unforced (8 shutdown calls needed); TSan missed the F10 race
  this run (timing-dependent, noted); L9 again 5 references. Sweep of EGL and
  window calls recorded under F15; L14 added. Next: L14 host repro.
- Pass 9 (2026-10-31): searched and read the AOSP chain for a destroyed
  SurfaceView's Surface (SurfaceView.releaseSurfaces -> Surface.destroy ->
  release; ANativeWindow_fromSurface -> NULL; ANativeWindow_acquire
  dereferences). Fake window now models it; added `render released-surface`.
  Ran `unit render`: `unit` green; L14 promoted to F16 (UBSan null member
  access plus ASan SEGV at androidbk.cpp:532 via resurface :732); TSan caught
  F10's race again; lost-shutdown 435 frames; L9 and F15 unchanged. Next: a
  quiet-gap stress mode so TSan catches F10 every run, then HWASan setup
  (queue 10).
- Pass 10 (2026-10-31): searched why happens-before detectors miss races when
  a lock handoff orders the accesses in the schedule that ran. Added
  `render quiet-shutdown` and `stage_render_quiet` (no UI-thread sync after
  `shutdown()`). Ran `unit render_quiet`: `unit` green; F10's TSan report now
  appears in 3 of 3 runs instead of depending on timing. Next: HWASan
  feasibility on this machine (arm64 container under emulation; queue 10).
- Pass 12 (2026-10-31): HWASan feasibility (queue 10). Searched QEMU and the
  arm64 tagged-address ABI, then HWASan's detection model (stack tagging,
  no redzone size limit). arm64 containers run here under Docker's QEMU;
  built `canvas-hwasan-arm64:local` (`Dockerfile.hwasan`, clang 18) and
  `run_hwasan.sh`. HWASan works under emulation: the smoke test reports a
  `tag-mismatch`, and F3 reproduces as `invalid-free` (the double free at
  exit). F1/F9/F4 still compiling under emulation. While waiting, wrote the
  "Index for the fix pass" table (severity, trigger, CWE class) at the top.
  `unit` was re-run in pass 11 during this item; next pass re-runs it.
  Next: record the remaining HWASan results.
- Pass 13 (2026-10-31): HWASan results so far (`run_hwasan.sh` in
  `canvas-hwasan-arm64:local`, arm64 under QEMU). Searched HWASan's detection
  model (stack tagging; no redzone or quarantine limits). Smoke test:
  `tag-mismatch` reported. F3: reproduced as `HWAddressSanitizer:
  invalid-free` (the second free of Canvas's string at exit; HWASan does not
  intercept the earlier printf read that ASan flags). F1 (stack overflow via
  the memcpy interceptor): the process died with `qemu: uncaught target
  signal 4 (Illegal instruction)`, then hung writing a core dump of HWASan's
  shadow mapping; stopped by hand. That is an emulation limit, not a Canvas
  result: F1 under HWASan is not established here. F9 and F4 still running.
  Next: add `ulimit -c 0` to run_hwasan.sh once the current run ends; record
  F9/F4; run `unit` (skipped this pass: the HWASan run is using out/).
- Pass 13 addendum (2026-10-31): HWASan F9 (`jni_hw interleave`) also died
  with `qemu: uncaught target signal 4 (Illegal instruction)` and stalled on
  the core dump; stopped by hand (exit 137). So far HWASan under QEMU user
  mode reports simple cases (C smoke test, F3's invalid-free) but crashes in
  the emulator on the C++/libc++ report paths (F1, F9). F4 running.
- Pass 14 (2026-10-31): HWASan F4 and the emulator failures (queue 10).
  Searched QEMU linux-user aarch64 signal mapping (`cpu_loop.c`: undefined
  instructions become SIGILL, `brk` becomes SIGTRAP; HWASan reports through
  `brk`, so the SIGILL in F1/F9 is not HWASan's own trap; cause not found)
  and libc++ `vector::erase(end())` (no source on HWASan's handling; the
  standard requires a dereferenceable position). F4 (`cipher_hw
  unfired-delete`): no output in 5 min 49 s at 100% CPU and 12 MB resident;
  stopped by hand (exit 137). On x86 the same input gives UBSan "applying
  non-zero offset 8 to null pointer" and ASan `negative-size-param
  (size=-8)` in memmove, i.e. memmove(NULL, 0x8, SIZE_MAX-7) on an empty
  vector. Untested hypothesis for the hang: HWASan's range check computes the
  end address as start + size, which wraps for this size and checks nothing,
  so the fault comes from the real memmove and goes to HWASan's SEGV
  handler, which may be the part that spins under QEMU. `run_hwasan.sh`:
  `ulimit -c 0`, a `timeout -s KILL` on every step (`HW_TIMEOUT`, default
  180 s), and an F4 run with `handle_segv=0` that separates the two.
  Result so far: HWASan under QEMU user mode confirms the smoke test and F3
  only; F1, F9 and F4 stay confirmed by ASan/UBSan/TSan on x86, not by
  HWASan. Stages run: `unit` (ChatFlagFix, OtherAudio, OtherAudioSwap and
  InitializationGate under ASan+UBSan, the gate under TSan: all exit 0).
  Next: re-run `run_hwasan.sh` (with timeouts) in the background, and
  meanwhile start queue 3 (real `CipherRegistry.cpp` under TSan).
- Pass 15 (2026-10-31): queue 3, the real `CipherRegistry.cpp`. Searched
  MSan use-after-dtor (`-fsanitize-memory-use-after-dtor`, `poison_in_dtor`)
  and TSan's handling of libc++ `shared_mutex`/lock-order checks. New
  `stage_registry`: `cipher_registry.cpp` (modes `concurrent`, `ranges`),
  `cipher_exit.cpp` (static destruction order) and `registry_env.cpp` (Canvas
  globals for the real `Canvas.h`; `patternToBytes` from the run.sh
  extraction). Concurrency under TSan and ASan+UBSan: clean, all oracles hold
  (see Clean so far). Exit order: confirmed as F17, ASan/UBSan SEGV in
  `ForgetInstall` (`CipherRegistry.cpp:115`) from `~CipherPatch` during
  `exit()`, and MSan use-after-dtor on the destroyed `unordered_map`; the
  control is clean under both. Ranges: L15, a contract gap. The MSan build
  first failed (`Cipher.h` relies on libc++'s transitive `<cstdint>`);
  fixed with `-include` in run.sh. Stages run: `registry` (twice), `unit`
  (all exit 0). The HWASan re-run was held while the perf track timed. Next:
  re-run `run_hwasan.sh` with timeouts, then queue 8 (OtherAudio thread
  topology).
- Pass 16 (2026-10-31): queue 8 (OtherAudio thread topology), reading only.
  Searched TSan's treatment of a compare-exchange on a slot that other threads
  read without atomics (an uninstrumented reader hides the edge; a plain C++
  reader is a real race). By inspection the topology is race-free (details in
  queue 8), and a host harness would need a synthetic game `.so`. Ranked
  below queue 4. Stages run: `unit` (all exit 0). Started the HWASan re-run
  (`run_hwasan.sh` with timeouts and the F4 `handle_segv=0` diagnostic) in
  the background. Next: record HWASan results; start queue 4 (artpatch
  differential fuzzer: hex strings and apply/restore sequences against a
  reference decoder and a byte model of the page).
- Pass 17 (2026-10-31): queue 4, artpatch differential fuzzer; written,
  not yet run (the HWASan re-run is using out/). Searched FuzzedDataProvider
  and stateful, model-checked libFuzzer targets. Reading `artpatch.c` for the
  model gave a candidate: `artpatch_apply`/`artpatch_restore` make only the
  page holding the first byte writable (`mprotect(pagestart, PAGE_SIZE, ...)`,
  :90-95 and :101-105), then copy all `patchLength` bytes. A multi-instruction
  patch that starts within its length of a page end writes into the next,
  still read-only code page. New:
  - `artpatch_cross.c`: deterministic. An 8-byte patch (`mov x0, #0; ret`)
    on the last instruction of the first of two r-x pages; the oracle is the
    bytes and the protection of both pages.
  - `fuzz_artpatch_diff.cpp`: set_hex / set_addr / apply / restore / die
    sequences as CipherPatch issues them, checked after every operation
    against a byte model and r-x protection of 5 pages. It keeps out the
    known classes F2, F11, F12 and L5. Run once with crossing allowed, then
    with `ARTPATCH_DIFF_NO_CROSS`.
  Wired as `stage_artpatch_diff`. HWASan so far in the re-run: smoke and F3
  reproduce again (exit 99). F1 again hits `qemu: uncaught target signal 4`
  and then stalls despite `ulimit -c 0`, until the 180 s timeout kills it
  (exit 137). `unit` not re-run this pass (out/ busy; last run pass 16,
  all exit 0). Next: run `artpatch_diff` and `unit` once HWASan finishes,
  then record the HWASan F9/F4 results.
- Pass 18-19 (2026-10-31): the HWASan re-run still held out/ (F9 timed out
  after a QEMU SIGILL like F1; F4 running), so no stages ran. Searched
  arm64 inline-patching practice (protection changes cover the whole patched
  range; I-cache maintenance after writing code) and FuzzedDataProvider's
  encoding (integers come from the end of the input).
  - Sweep for the page-crossing class: first-party sites that change one
    page's protection and then write a range are `artpatch.c:92-95` and
    `:103-105` (can cross) and `OtherAudioApply.cpp:154-161` (cannot: one
    8-byte-aligned slot).
  - New lead L16: no I-cache flush after `CipherPatch` writes (arm64,
    device-only).
  - New `make_artpatch_seeds.cpp`: fuzzer seeds from the ChatFlagFix
    fixture instruction words (set_hex, set_addr, apply, restore, apply).
  - `stage_artpatch_diff` now runs the crossing-allowed fuzzer unseeded, so a
    page-crossing crash is the fuzzer's own find, and the within-one-page run
    seeded.
  `unit` not re-run (out/ busy). Next: once HWASan ends, run `artpatch_diff`
  and `unit`, then record the HWASan results.
- Pass 20 (2026-10-31): HWASan results, queue 4 run, native HWASan.
  Searched HWASan's x86-64 page-aliasing mode (primary heap only) and
  16 KB pages on Android (page size taken at run time). Stages run:
  `hwasan_x86`, `artpatch_diff`, `unit` (all unit tests exit 0).
  - HWASan arm64 re-run finished: smoke and F3 reproduce; F1, F9 and F4 do
    not (details in queue 10).
  - `hwasan_x86` (new): smoke reports. F9 is confirmed by HWASan (a third
    sanitizer). F4 is not detected by HWASan (no report, no hang).
  - `artpatch_diff`: F18 confirmed. The deterministic repro and the
    unseeded fuzzer, which found it itself after 16,865 executions, both
    SEGV at `artpatch.c:94`. Input in `artifacts/artpatch-page-crossing.bin`.
  - Within one page, seeded with 78 inputs from the ChatFlagFix fixtures:
    53,944 executions and 374 coverage points; no mismatch against the byte
    and protection model, no sanitizer report. With F2, F11, F12 and L5
    excluded, set_hex/apply/restore/die agree with the model.
  - The fuzzer's memory grew to 416 MB, consistent with ASan's quarantine
    (256 MB by default); leak detection was off.
  Next: a leak check of `fuzz_artpatch_diff` (`-detect_leaks=1`, short),
  then queue 5 (the `fuzz_decoders` oracle).
- Pass 21 (2026-10-31): queue 5, the `fuzz_decoders` oracle; written, not
  yet run (the perf track was timing). Read the decoders against their
  callers. The old harness used the fuzzer's data pointer as the image base,
  which changes per input, so fuzzed bytes could never form the absolute
  pointer chain `OtherAudio::Decide` follows, and its `Swap` path was
  effectively unreachable. Rewrote `fuzz_decoders.cpp`:
  - the input is mapped at a fixed virtual base `0x7000000000`;
  - the image splits into a code half and a RELRO half;
  - `Located` takes `systemIoGlobal` and, when no chain decodes, a slot
    from the input.
  New oracles, failing with a message and a trap:
  - ChatFlagFix: the site is aligned and inside the image; `original` is the
    word there and is `AND wN, wN, #0xe0`; `replacement` is `AND wN, wN, #3`
    on the same register; `Decide` tells them apart; the flag offset fits 12
    bits.
  - OtherAudio: decoded sites are inside the image; slots are multiples of
    8; return addresses fall inside the matched window.
  - `Swap` only for an 8-byte-aligned slot inside RELRO, holding a target
    inside code, as `Write()`'s 8-byte compare-exchange needs. On arm64 a
    misaligned exclusive access faults.
  By reading, the alignment holds: `slot` is always `Imm12 * 8`, and
  `Decide` rejects unaligned vtables. `unit` not re-run (perf timing in
  progress). Next: run `fuzz_decoders` (and replay the corpus under
  `msan`/`ubsan_ext`) when the perf run ends; then the `fuzz_artpatch_diff`
  leak check.
- Pass 22 (2026-10-31): a bug-class sweep, no stages; the perf track was
  timing queue 9 (`unit` last run in pass 20, all exit 0). Searched ART's
  local reference table: overflow is fatal, and threads that never return to
  Java must delete their local refs. Swept JNI local references created on
  threads that stay in native code: 0 sites. All 11 creation sites delete
  their refs or make them global. The render loop, which runs inside one
  long `ImGUI_init` call, creates them only in the clipboard helpers, which
  delete them. Checked `ImGUI.java` `getClipboard()`: it returns `""`
  instead of null, so the unchecked native use is safe as written. Added the
  sweep, plus the F17 and F18 class sweeps, to the sweeps table. Next: run
  `fuzz_decoders` (rewritten in pass 21) and the `fuzz_artpatch_diff` leak
  check when the perf run ends, with `unit`.
- Pass 23 (2026-10-31): queue housekeeping and the rest of queue 11; no
  stages, because the perf track was running quiet-machine repeats (`unit`
  last run in pass 20, all exit 0). Searched the `SurfaceHolder.Callback`
  contract: once `surfaceDestroyed` returns, nothing may touch the surface,
  so the render thread must have stopped. Queue statuses:
  - 4: done (F18);
  - 5: rewritten, not yet run;
  - 11: mostly covered by F10 (which already records that `shutdown()`
    never waits), F16, L9 and F15;
  - 12: moved to the perf track.
  For queue 11's last part, `render_threads quiet-shutdown` now counts the
  swaps that start after `shutdown()` returns with no resurface involved,
  printed per run by `stage_render_quiet`. That is the contract window F10
  describes even without the swallow. Next: run `render_quiet`,
  `fuzz_decoders` and the `fuzz_artpatch_diff` leak check, with `unit`, once
  the perf repeats end.
- Pass 24 (2026-10-31): queue 5 preparation; no stages, because the perf
  track's `readout` repeat was running (`unit` last run in pass 20, all
  exit 0). Searched libFuzzer value profiling (comparison operands count as
  coverage; suggested when the fuzzer cannot get past magic comparisons).
  Checked that the rewritten `fuzz_decoders.cpp` gets every type it uses
  from its includes (`Located` via `OtherAudioSwap.h` ->
  `OtherAudioLocate.h`). Added `harness/decoders.dict` with the values
  `Decide`'s Swap path compares against: the virtual image base, the
  return-false stub's five words, and the zero vtable header.
  `stage_fuzz_decoders` now runs with that dictionary, `-use_value_profile=1`
  and final stats, and prints `ORACLE` lines. Next: run `fuzz_decoders`,
  `render_quiet` and the `fuzz_artpatch_diff` leak check, with `unit`, once
  the perf repeat ends.
- Pass 25 (2026-10-31): runs from passes 21-24, and queue 9's mod part.
  Searched sanitizers with `dlopen`ed libraries (TSan tests build helpers
  as shared objects; the interceptors track loaded modules). Stages run:
  - `fuzz_decoders` (rewritten in pass 21: fixed virtual base, oracles,
    dictionary, value profile): 115,617 executions in 184 s, coverage 272 ->
    296, corpus 1,991. No oracle failure, no sanitizer report.
  - `render_quiet`: F10's TSan report in 3 of 3 runs. Swaps started after
    `shutdown()` returned with no resurface involved: 0 in each run, so on
    this host the loop stopped before its next swap; F10's swallow case
    remains the one that shows rendering after return.
  - `fuzz_artpatch_diff` leak check: corpus replay plus 20,000 runs with
    LSan on, no leak. The 418 MB peak is ASan's quarantine.
  - New `stage_lifecycle`: verbatim extractions of `crash`, `UserThread`,
    `onModLibrary`, `completeModInitialization` and `lateInitUserLibs`, with
    the real `Canvas.cpp` and two stand-in mods (`mod_stub.cpp`: one with a
    menu, one patch-only). Found F19 (no run-once guard; a second delivery
    runs `InitLate` twice, concurrently; confirmed by counters and TSan) and
    L17 (`InitLate` never called for patch-only mods).
  Harness notes: `dlopen` is wrapped to accept bionic's `RTLD_LOCAL`-only
  flags. The stub's `Start` was first declared `void *()`, and UBSan's
  function-type check flagged the stub; it is now declared with the type
  Canvas calls through. One TSan build hit a transient clang segfault in
  `imgui_widgets.cpp`; the rebuild passed. `unit`: all exit 0. Next: queue 2
  stays deferred. Deepen: `settle` under the lifecycle harness, or a
  coverage report for `fuzz_decoders` to see whether `Decide`'s `Swap`
  branch was reached.
- Pass 26 (2026-10-31): checked whether the queue 5 oracles are reached.
  Searched libFuzzer corpus coverage with source-based coverage
  (`-fprofile-instr-generate -fcoverage-mapping`, `llvm-profdata`,
  `llvm-cov show`). New `stage_decoders_cov` replays the corpus and prints
  line counts for each `Decide` outcome and for ChatFlagFix's accepted
  match. First result: `Swap` reached 0 times and ChatFlagFix matches 0
  times, so the pass-21 oracles had never run on a positive case.
  Fixes:
  - `fuzz_decoders.cpp` serves a second read-only region described by the
    input's last 24 bytes (the game's .rodata, where "chat" lives);
  - `make_seeds.cpp` adds `chatpos_*` (fixture windows at their real page
    offset with "chat" at the fixture's displacement) and `decide_*` (a
    pointer chain to `Swap` and one to each refusal).
  Re-run: 119,562 executions, every reachable outcome hit (`Swap` x21,
  ChatFlagFix matches x16), no oracle failure (see Clean so far). Stages
  run: `decoders_cov`, `unit` (all exit 0), `fuzz_decoders`, `decoders_cov`.
  The perf track's instruction-count build also hit a transient clang
  segfault this pass. Next: `msan` and `ubsan_ext` replay the decoder
  corpus, which is now larger and reaches more; then `settle` under the
  lifecycle harness.
- Pass 27 (2026-10-31): L13 (IconLoader maps without a lock), prepared
  but not yet run; the perf track was timing. Searched symbol visibility:
  under default visibility a static member function without a visibility
  attribute is exported. Checked by reading:
  - the app build sets no `-fvisibility=hidden` (CMakeLists.txt);
  - `IconLoader::getImage`, `icon` and `iconButton` carry no attribute, so
    they are exported and callable by mods;
  - `getIcon` and `getAtlasImage` are `PRIVATE_API`.
  So L13's race needs no misuse of internals: two mods calling the exported
  `getImage` from different threads both insert into `IconLoader::images`.
  One mod would be in `Draw()` on the render thread, the other in
  `InitLate()` (F19's thread) or its own thread. New `iconloader_driver
  race` mode: two threads, 300 distinct `getImage` names each. The oracle
  is that every call returns an uploaded image, and TSan and ASan stay quiet.
  New `stage_iconloader_race` (TSan once, ASan+UBSan three times). `unit`
  last run in pass 26 (all exit 0). Next: run `iconloader_race` and `unit`.
- Pass 28 (2026-10-31): waiting on the perf track's live-mod run, so no
  stages (`unit` last run in pass 26, all exit 0). Searched how concurrent
  `std::unordered_map` inserts show up. Unsynchronised insertion is a data
  race TSan should flag directly. A rehash invalidates every iterator and
  reference, so an ASan heap-use-after-free would need another thread
  holding a stale reference across it. `IconLoader::getImage` returns
  `SkyImage&` into the map's values, which are pointers, so a returned
  reference survives a rehash. The `race` oracle (every call returns an
  uploaded image) is the check on what callers get back. While reading the
  perf track's output, found a perf-harness bug of mine from pass 37: its
  `window: ... presents=` line makes `stage_variant`'s `presents=` grep match
  twice, which shifts the frame-CPU column to 0.00 (recorded in PERF.md).
  Next: `iconloader_race` and `unit` when the perf run ends.
- Pass 29 (2026-10-31): L13, the IconLoader maps. Searched for mods using
  Canvas's IconLoader API (no public source found) and concurrent
  `unordered_map` failure modes. The first `iconloader_race` build failed to
  link: the TSan runtime has no `__lsan_do_leak_check`, which an existing
  mode calls; it is now ASan-only. Result: L13 promoted to F20. TSan reports
  42 races between `getImage`'s find (:173) and `uploadImageKtx`'s insert
  (:90). ASan+UBSan crash in 3 of 3 runs, following ASan's fresh-allocation
  fill `0xbebebebebebebebe` from a concurrent rehash at
  `getImage` :173. Reachability depends on mods calling the exported API
  from two threads; F19's re-run `InitLate` is one way in. Stages run:
  `iconloader_race` (twice, the first failing to link), `unit` (all exit 0).
  Next: deepen another topology, or `settle` under the lifecycle harness.
- Pass 30 (2026-10-31): L18 (font buffer ownership), from the perf track's
  pass 47. Searched ImGui's `AddFontFromMemoryTTF` ownership (the atlas
  frees the data unless `FontDataOwnedByAtlas = false`; ImGui issues
  #1259/#5330 show the same misuse crashing apps). New `stage_fonts` with
  `font_ownership.cpp`: the real `loadFonts` (new run.sh extraction) with
  the app's CJK font served by the fake asset manager. Result: L18 promoted
  to F21, latent:
  - double free on `Clear()`;
  - use-after-free on rebuild;
  - double free on `DestroyContext`;
  - the control without the CJK font is clean.
  Build notes: the first link failed on a duplicate
  `Canvas::frameRateLimited` (fake_platform's stub, now
  `-DFAKE_PLATFORM_NO_CANVAS_MENU`). The next build hit another clang
  segfault, the fourth today, this time in the register allocator on
  `imgui_draw.cpp`; the retry passed. Stages run: `fonts` (three times),
  `unit` (all exit 0). Next: deepen a remaining topology (`settle` with the
  lifecycle harness), or a CFI/vptr pass over the newer harnesses.
- Pass 31 (2026-10-31): two topologies by reading, plus the enlarged
  decoder corpus under MSan and UBSan's integer checks. Searched when
  library constructors and `JNI_OnLoad` run: inside `dlopen`, on the
  loading thread; ART makes a second loader wait for `JNI_OnLoad`. Findings
  by reading, both clean:
  - (a) the load-time constructor `main()` and `settle()` both call
    `install_chat_fix()` and `OtherAudio::Locate()`, but both are guarded:
    `g_installMutex` + `g_finished` (`ChatFlagFixInstall.cpp:49-58`) and
    `g_mutex` + `g_done`;
  - (b) the constructor's `sleep(1)` loop until libBootloader is mapped
    cannot block Canvas's own loading thread, because `MainActivity` loads
    libBootloader through `ElfLoader` (`MainActivity.java:211`) before
    `System.loadLibrary("ciphered")` (:212).
  Stages run:
  - `msan`: F2 and F11 reproduce again (regression-stable); the decoder
    corpus (3,424 inputs, which now reach `Swap` and ChatFlagFix matches)
    is clean;
  - `ubsan_ext`: the known L5 null pointer. Integer reports are unsigned
    address arithmetic that wraps on purpose (negative ADRP/BL
    displacements; `OtherAudioSwap.h:109` with a fuzzed pointer near the
    top of the address space, rejected by the bounded read that follows),
    so not defects;
  - `unit`: all exit 0.
  Next: CFI/vptr builds of the newer harnesses (lifecycle, registry,
  iconloader_race), or the `settle` extraction under the lifecycle harness.
- Pass 32 (2026-10-31): looked for the weakest harness by measuring
  coverage. Searched llvm-cov per-function reports. New `stage_render_cov`
  runs the `render` harness built with source-based coverage across its
  four modes. Expected exits: `lost-shutdown` 1 (F10's oracle), and
  `released-surface` segfaults (F16) and loses its profile.
  - `androidbk.cpp`: 29 of 31 functions run, 55% of lines, 33% of regions.
  - `scroll.cpp`: 2 of 2 functions, 83% of lines.
  - Gaps: the clipboard helpers (0%); `ImGui_ImplAndroid_KeyCodeToImGuiKey`
    (5% of 111 lines; few keys pushed, but unknown keys map to
    `ImGuiKey_None`, which ImGui ignores, imgui.cpp:1487); `loadFonts`'s
    CJK branch (covered by `stage_fonts`).
  Checked by reading what Java sends:
  - `submitButtonEvent` only ever button 0 (`GameActivity.java:689-759`),
    so the unchecked index into `io.MouseDown[5]` is unreachable;
  - characters arrive one UTF-16 unit at a time, giving new lead L19
    (unpaired surrogates for emoji).
  Stages run: `render_cov`, `unit` (all exit 0). Next: L19's repro (an
  `InputText` in a stand-in mod, the real `submitUnicodeEvent`/`Drain`
  path), then the clipboard helpers on the render thread.
- Pass 33 (2026-10-31): L19's repro. Searched ImGui's
  `AddInputCharacterUTF16` (keeps a pending high surrogate in
  `InputQueueSurrogate`, pairs it, and substitutes U+FFFD for orphans or
  for builds with 16-bit `ImWchar`). New `render_threads unicode` mode and
  `stage_unicode`:
  - the fake `CanvasMenu`'s text field can be focused and read back after
    shutdown (`g_fakeFocusMenuText`, `FakeMenuText` in `fake_platform.cpp`);
  - `a`, `é` and U+1F600 go through the real `submitUnicodeEvent` and
    `Drain`.
  Result: field `61 C3 A9 ED A0 BD ED B8 80 62`. Encoded surrogates, as
  predicted, so L19 becomes F22. Sweep: JNI's `GetStringUTFChars` gives
  the same modified UTF-8 at 13 sites; user text reaches mods this way in
  `onKeyboardCompleteNative`. Stages run: `unicode`, `unit` (all exit 0).
  Next: the clipboard helpers (0% coverage) under ASan with the fake JNI's
  static calls, then CFI over the newer harnesses.
- Pass 34 (2026-10-31). Targets: the clipboard helpers (0% coverage in
  pass 32), then repo-wide coverage, at the owner's question.
  - Searched: JNI null and exception handling around `GetStringUTF*`;
    ImGui's clipboard-pointer lifetime; ART's string conversions (read
    `jni_internal.cc` and `utf.cc` on AOSP main);
    `setPrimaryClip`/`TransactionTooLargeException`.
  - Clipboard harness: new `stage_clipboard` and `render_threads clipboard`.
    `fake_jni.cpp` models `ImGUI.getClipboard()`/`setClipboard()` with
    ART's conversions; `fake_platform.cpp` adds a probe that runs inside
    the fake menu on the render thread. Result: 14 of 14 cases match the
    model. ASan, UBSan and LSan clean; JNI strings balanced; TSan reports
    only F10 (see Clean so far).
  - From reading, with ART's source:
    - F22's sweep corrected: ART emits 4-byte UTF-8, not 6-byte pairs.
    - New lead L20: clipboard Java exceptions are never checked; a large
      clip can make `setPrimaryClip` throw.
    - Two new sweep rows: unchecked Java calls (2 of 6), unvalidated
      `NewStringUTF` (5 sites).
  - Coverage: new `stage_coverage_all` reruns 14 ASan/TSan stages with
    source-based coverage into `out/coverage_all`. The decoder corpus is
    mounted read-only, and the `decoders_cov` profile is merged in. Not in
    ALL. `mod_lifecycle.cpp` now writes its profile before `_exit`. The
    clipboard stage was added after this run started, so it is not
    included. Lower bound: the profile is lost wherever a run crashes,
    aborts or times out. That is why `patternToBytes`, `get_DeviceInfo`,
    `onKeyboardCompleteNative` and most `uploadImageKtx` codes read 0%:
    F1, F3, F9 and F14 end those runs.
    Line coverage, real files and verbatim extractions:
    | Area | Lines covered |
    |---|---|
    | `procutils.c`, `scroll.cpp`, `InitializationGate.h` | 100% |
    | `artpatch.c` | 97% |
    | `ChatFlagFix.h` | 95% |
    | `OtherAudio.h` | 94% |
    | `OtherAudioSwap.h` | 72% |
    | `androidbk.cpp` | 65% (`loadFonts.inc` copy 65%) |
    | `CipherRegistry.cpp` | 60% |
    | `fileselector.cpp` | 57% |
    | `CipherBase.cpp` | 53% |
    | `CipherHook.cpp` | 52% |
    | `CipherPatch.cpp` | 45% |
    | `IconLoader.cpp` | 39% |
    | `Canvas.cpp` | 33% |
    | `main.cpp`, 9 extracted functions | 58 of 101 lines |
    | `CipherUtils.cpp`, 4 extracted functions | 32 of 98 lines |
    | Total instrumented | 1,249 of 1,867 lines (67%) |
    Never compiled by any harness: `Cipher.cpp`, `CipherArm64.cpp`,
    `ChatFlagFixInstall.cpp`, `OtherAudioApply.cpp`, `OtherAudioLocate.cpp`,
    and the rest of `main.cpp` and `CipherUtils.cpp`. That is about 945
    lines, estimated as 0.83 x their non-comment lines, the ratio llvm-cov
    shows for compiled files. `Cipher.h` and `CipherUtils.h` are
    declarations. Repo-wide estimate: about 1,250 of about 2,800 executable
    lines, so about 44% (roughly 40-45%). Out of scope but compiled:
    `include/misc/Utils.h`, 0 of 66.
  - Fixed after the run: the report's path stripping (llvm-cov prints
    `repo/main/cpp/...`), which made the "no harness compiles" list print
    every file.
  - Process note: while the coverage run was executing, `run.sh` was
    restored to the bytes it started with. bash reads a script as it runs,
    so growing the file mid-run could have executed stray lines.
  - Stages run: `coverage_all` (own out dir), `clipboard`, `unit` (all
    exit 0; TSan's clipboard exit 66 is F10).
  - Next: queue 13 (L20's pending-exception model), then the never-compiled
    files, ranked by who supplies their input. `OtherAudioApply.cpp` and
    `ChatFlagFixInstall.cpp` patch the game binary; queue 8 already covers
    the synthetic-image route.
- Pass 35 (2026-10-31): queue 13, L20 on the host.
  - Searched: which JNI functions may be called while an exception is
    pending. Android's JNI tips "Exceptions" section has the full list;
    a Dalvik commit adds that Delete* and Release* are allowed.
  - Harness: the fake JNIEnv keeps a per-thread pending exception and
    counts calls outside that list; `ExceptionCheck`/`ExceptionClear` and
    `ThrowNew` now act on it. Its `setClipboard` throws above 500 Ki UTF-16
    units. `render_threads` gains the `clipboard-throw` and
    `clipboard-throw-set` modes. The dispatch thread checks for a pending
    exception whenever a job returns to Java. Existing harnesses never set
    one, so their behaviour is unchanged.
  - Result: both modes exit 1 on their oracle. After the failed copy, a
    paste makes 3 JNI calls with the exception pending, starting with
    `CallStaticObjectMethod`, and returns the previous clip. In both modes
    the exception is still pending when `init()` returns. L20 is promoted
    to F23 (low), with the throw threshold and release-ART behaviour
    labelled as modelled. Sweep: `onModLibrary` returns right after each
    `crash()`; both `ThrowNew` sites are correct.
  - Stages run: `clipboard` (first build failed on a broken string literal
    in the harness, fixed and rerun; ASan+UBSan modes as above, TSan only
    F10), `unit` (all exit 0).
  - Next: deepen the weakest coverage from pass 34. `IconLoader.cpp` (39%)
    and `Canvas.cpp` (33%) are compiled but thinly exercised; or CFI over
    the newer harnesses.
- Pass 36 (2026-10-31): deepened the weakest compiled file from pass 34's
  coverage, `IconLoader.cpp` (39%).
  - Per function: the mod-facing `getIcon`/`getUIIcon`/`getAtlasImage`/
    `addIcon` read 0% because the `shared-missing-atlas` run ends in LSan's
    leak report, which loses the profile. `icon`/`iconButton`/
    `placeholder` had never run.
  - Searched: running ImGui headless for tests (imgui's `example_null`:
    build the font atlas, then `NewFrame`/`Render` in a loop).
  - New `stage_icon_api` (in ALL), with `iconloader_driver` modes
    `icon-api` and `icon-race`, plus a texture counter in
    `fake_ktx_assets.cpp`.
  - Results:
    - API checks: 6 of 6 pass.
    - LSan, taken while the maps are alive: 1 leak from a duplicate
      `addIcon` (new lead L21) and 8 F8 copies.
    - Two threads in `getUIIcon`: TSan 43-59 reports, 27-40 duplicate atlas
      uploads, and ASan+UBSan crashes on `0xbebebebebebebebe` 3 of 3. That
      is F20 through a second exported entry; its index row and section are
      updated.
  - Java side read: `findIcons` runs in `MainActivity.loadGame()` before
    `settle` and the overlay, so `addIcon` does not race the render thread
    in the normal order.
  - `Canvas.cpp`'s `isLibLoaded`/`findLib` (0%) only wrap
    `include/misc/Utils.h`, which is out of scope; left.
  - Stages run: `icon_api` (three times while the stage's leak accounting
    and error grep were fixed; product results identical each time),
    `unit` (all exit 0).
  - Next: CFI over the newer harnesses (registry, lifecycle, icon_api,
    clipboard), which mix vtables and function pointers.

Cleanup (2026-10-31): `out/` was cleared except `corpus/` and `crashes/` (fuzzer corpora kept). Logs and binaries named in this document no longer exist; rerun the named stage to regenerate them. The output quoted here is the record.

## Fix status (2026-10-31, merged into skyprotocol `osp-master` as #55, `f47ad97`)

Implemented, each a small local change, no comments added. Every one was
confirmed at least two ways and is reachable by a user, a mod, or Canvas
itself:

| ID | Change |
|---|---|
| F16 | `init_sfc` returns false when `ANativeWindow_fromSurface` is NULL; `resurface` stops there |
| F10 | `run` is `std::atomic<bool>`; `resurface` sets `run = true` before `init_sfc` |
| F2 | `artpatch_decode_hex_digit` accepts `a`-`f` |
| F1 | `patternToBytes` leaves the buffer untouched for 256+ bytes |
| F3 | `get_DeviceInfo` copies member-wise |
| F9 | `onKeyboardCompleteListenersMutex` guards adds; delivery iterates over a copy |
| F13 | file pickers detach a thread they attached (RAII guard) |
| F14 | `default:` on the upload-error branch |
| F4 | `~CipherPatch` erases only when found |
| F18 | apply/restore `mprotect` the whole page span of the patch |
| F12 | `/proc/self/maps` end is exclusive (`<`) |

Verification (`out/verify-1.log`, `out/verify-2.log`): each finding's stage
now passes. The ABI check, clean NDK builds of both trees, found no exported
symbol removed and the mod API identical; one libc++ template symbol was
added. `unit` passes. One crash in the in-page artpatch fuzzer (inside
`procutils_buf_putchar`, after 13,122 runs) did not reproduce: its input runs
clean on the original and the fixed code, and 3-minute runs of each (48,354
and 57,578 executions) found nothing.

Deferred:
- not minimal: F5, F6, F11, F20;
- contract or semantics open: F7 (fallback protection), F19, L17;
- low value: F8, F15, F17, F21, F22, F23, and the `performHapticFeedback`
  invalid-enum path of F13's class (needs a mod to pass an invalid value);
- unconfirmed: every L-series lead.
