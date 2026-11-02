#!/bin/bash
# Host-side sanitizer evidence for Canvas's first-party native code.
# Runs inside canvas-sanitizer-harness:local (see Dockerfile); see FINDINGS.md
# for the docker command. Mounts: /repo = app/src (read-only), /out = output.
#
#   run.sh                 every stage
#   run.sh msan jni ...    named stages only
set -u

REPO=/repo
SRC=$REPO/main/cpp
T=$REPO/test/cpp
H=$REPO/test/sanitizers
OUT=/out
GEN=$OUT/gen
mkdir -p "$OUT" "$GEN/android" "$OUT/corpus/decoders" "$OUT/corpus/procutils" "$OUT/crashes"
cd "$OUT"

CXX="clang++ -std=c++17 -stdlib=libc++ -g -O1 -fno-omit-frame-pointer -w"  # libc++ = NDK's string/vector layout
FUZZCXX="clang++ -std=c++17 -g -O1 -fno-omit-frame-pointer -w"             # this image's libFuzzer is libstdc++-built
CC="clang -g -O1 -fno-omit-frame-pointer -w -include stdint.h"             # artpatch.c gets uintptr_t from bionic transitively
NOASLR="setarch $(uname -m) -R"                                            # TSan/MSan shadow layout vs. recent kernels' ASLR
INC_CIPHER="-I$H/stubs/common -I$H/stubs/cipher -I$SRC/Utils -I$SRC/Core -I$SRC"
INC_APP="-I$GEN -I$H/stubs/app -I$H/stubs/common -I$SRC/Utils -I$SRC/Utils/KittyMemory -I$SRC/Core/imgui -I$SRC/Core -I$SRC"
CIPHER_SRC="$SRC/Core/Cipher/CipherBase.cpp $SRC/Core/Cipher/CipherHook.cpp $SRC/Core/Cipher/CipherPatch.cpp $H/harness/cipher_env.cpp"
IMGUI_SRC="$SRC/Core/imgui/imgui.cpp $SRC/Core/imgui/imgui_draw.cpp $SRC/Core/imgui/imgui_tables.cpp $SRC/Core/imgui/imgui_widgets.cpp"
export UBSAN_OPTIONS=print_stacktrace=1

banner() { echo; echo "================ $*"; }
trim() { grep -E "$1" | grep -v '^\[' | cut -c1-220 | head -"${2:-14}"; }

# Verbatim extraction of one top-level definition: from the line starting with
# $2 to the first line starting with '}'. Fails loudly if the source moved.
extract() {
    awk -v start="$2" 'index($0, start) == 1 {p = 1} p {print} p && /^}/ {exit}' "$1" > "$3.tmp"
    if ! grep -q '^}' "$3.tmp"; then echo "EXTRACTION FAILED: $2 in $1" >&2; rm -f "$3.tmp"; return 1; fi
    { [ -n "${4:-}" ] && echo "$4"; cat "$3.tmp"; } > "$3"
    rm -f "$3.tmp"
}

generate() {
    extract $SRC/Core/Cipher/CipherUtils.cpp 'void CipherUtils::patternToBytes(' $GEN/patternToBytes.inc
    extract $SRC/Core/Cipher/CipherUtils.cpp 'DeviceInfo CipherUtils::get_DeviceInfo(' $GEN/get_DeviceInfo.inc
    extract $SRC/Core/Cipher/CipherUtils.cpp 'void CipherUtils::addOnKeyboardCompleteListener(' $GEN/addOnKeyboardCompleteListener.inc
    extract $SRC/Core/Cipher/CipherUtils.cpp 'void CipherUtils::performHapticFeedback(' $GEN/performHapticFeedback.inc
    local jni='extern "C" JNIEXPORT void JNICALL'
    extract $SRC/main.cpp 'Java_git_artdeell_skymodloader_MainActivity_onKeyboardCompleteNative(' $GEN/onKeyboardCompleteNative.inc "$jni"
    extract $SRC/main.cpp 'Java_git_artdeell_skymodloader_MainActivity_setDeviceInfoNative(' $GEN/setDeviceInfoNative.inc "$jni"
    extract $SRC/main.cpp 'Java_git_artdeell_skymodloader_MainActivity_nativeSetSkyBuildKey(' $GEN/nativeSetSkyBuildKey.inc "$jni"
    extract $SRC/main.cpp 'Java_git_artdeell_skymodloader_MainActivity_getSysetemUI(' $GEN/getSysetemUI.inc "$jni"
    extract $SRC/Core/Iconloader/IconLoader.cpp 'Java_git_artdeell_skymodloader_iconloader_IconLoader_addIcon(' $GEN/addIcon.inc "$jni"
    extract $SRC/Utils/imgui_androidbk/androidbk.cpp 'PRIVATE_API static void loadFonts(' $GEN/loadFonts.inc  # stage_fonts, pass 30
    # Mod lifecycle (stage_lifecycle, pass 25)
    extract $SRC/main.cpp 'PRIVATE_API void crash(' $GEN/crash.inc
    extract $SRC/main.cpp 'PRIVATE_API void *UserThread(' $GEN/UserThread.inc
    extract $SRC/main.cpp 'Java_git_artdeell_skymodloader_LibrarySelectorListener_onModLibrary(' $GEN/onModLibrary.inc "$jni"
    extract $SRC/main.cpp 'Java_git_artdeell_skymodloader_MainActivity_completeModInitialization(' $GEN/completeModInitialization.inc "$jni"
    extract $SRC/main.cpp 'Java_git_artdeell_skymodloader_MainActivity_lateInitUserLibs(' $GEN/lateInitUserLibs.inc "$jni"
    grep -o 'AKEYCODE_[A-Z0-9_]*' $SRC/Utils/imgui_androidbk/androidbk.cpp | sort -u |
        awk '{printf "#define %s %d\n", $1, NR}' > $GEN/android/keycodes.h
}

artpatch_objs() {  # $1 = tag, rest = sanitizer flags
    local tag=$1; shift
    $CC "$@" -I$H/stubs/common -c $SRC/Utils/artpatch/artpatch.c -o artpatch_$tag.o
    $CC "$@" -I$H/stubs/common -c $SRC/Utils/artpatch/procutils.c -o procutils_$tag.o
}

# ---------------------------------------------------------------- stages

# Extractions only (generate always runs); used by run_hwasan.sh.
stage_gen() { :; }

stage_confirmed() {
    banner "confirmed: re-run of the first pass's reproducers"
    artpatch_objs asan -fsanitize=address,undefined
    artpatch_objs tsan -fsanitize=thread
    $CXX -fsanitize=address,undefined $INC_CIPHER $H/harness/artpatch_case.cpp artpatch_asan.o procutils_asan.o -o artpatch_case
    for r in 8 9 10 13 21; do ASAN_OPTIONS=detect_leaks=0 ./artpatch_case $r; done
    $CXX -fsanitize=address,undefined -include cstdint -I$GEN $INC_CIPHER $H/harness/deviceinfo.cpp -o deviceinfo
    ./deviceinfo 2>&1 | trim "ERROR|SUMMARY|mod read|canvas still" 6
    $CXX -fsanitize=address,undefined $INC_CIPHER $CIPHER_SRC $H/harness/cipher_lifecycle.cpp artpatch_asan.o procutils_asan.o -o cipher_asan
    ASAN_OPTIONS=detect_leaks=0 ./cipher_asan unfired-delete 2>&1 | trim "ERROR|SUMMARY|runtime error|survived" 6
    ASAN_OPTIONS=detect_leaks=0 ./cipher_asan mixed-types 2>&1 | trim "runtime error|survived" 4
    $CXX -fsanitize=thread $INC_CIPHER $CIPHER_SRC $H/harness/cipher_lifecycle.cpp artpatch_tsan.o procutils_tsan.o -o cipher_tsan
    TSAN_OPTIONS=halt_on_error=1 timeout 120 $NOASLR ./cipher_tsan race > cipher_tsan.log 2>&1; echo "cipher_tsan exit=$?"
    trim "WARNING|SUMMARY" 4 < cipher_tsan.log
    $FUZZCXX -fsanitize=fuzzer,address,undefined -include cstdint -I$GEN $INC_CIPHER $H/harness/fuzz_pattern.cpp -o fuzz_pattern
    ./fuzz_pattern $H/artifacts/patternToBytes-stack-overflow.bin 2>&1 | trim "ERROR|WRITE|SUMMARY" 4
}

stage_unit() {
    banner "unit: existing app/src/test/cpp tests under ASan+UBSan, gate test under TSan"
    for t in ChatFlagFixTest OtherAudioTest OtherAudioSwapTest InitializationGateTest; do
        $CXX -fsanitize=address,undefined -fno-sanitize-recover=all -UNDEBUG -I$T -I$SRC/Core $T/$t.cpp -o unit_$t -pthread
        ./unit_$t > /dev/null 2> unit_$t.err; echo "$t asan+ubsan exit=$? $(grep -m1 -E 'ERROR|runtime error' unit_$t.err)"
    done
    $CXX -fsanitize=thread -UNDEBUG -I$SRC/Core $T/InitializationGateTest.cpp -o unit_gate_tsan -pthread
    $NOASLR ./unit_gate_tsan > /dev/null 2> unit_gate_tsan.err; echo "InitializationGateTest tsan exit=$? $(grep -m1 WARNING unit_gate_tsan.err)"
}

stage_fuzz_decoders() {
    banner "fuzz-decoders: ChatFlagFix + OtherAudio, seeded from unit-test fixtures (${FUZZ_SECS:-120}s)"
    $FUZZCXX -I$T $H/harness/make_seeds.cpp -o make_seeds && ./make_seeds $OUT/corpus/decoders
    $FUZZCXX -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -I$SRC/Core $H/harness/fuzz_decoders.cpp -o fuzz_decoders
    # Pass 24: dictionary with the values Decide's Swap path compares against, and value profiling.
    timeout $(( ${FUZZ_SECS:-120} + 60 )) ./fuzz_decoders $OUT/corpus/decoders -max_len=4096 -max_total_time=${FUZZ_SECS:-120} \
        -dict=$H/harness/decoders.dict -use_value_profile=1 -print_final_stats=1 \
        -artifact_prefix=$OUT/crashes/decoders- > fuzz_decoders.log 2>&1; echo "exit=$?"
    trim "ORACLE|ERROR|runtime error|SUMMARY|Done|number_of_executed_units" 8 < fuzz_decoders.log
    grep -E "^#[0-9]+" fuzz_decoders.log | tail -1
    echo "corpus: $(ls $OUT/corpus/decoders | wc -l) inputs"
}

stage_msan() {
    banner "msan: artpatch (C) and decoder corpus replay"
    $CC -fsanitize=memory -fsanitize-memory-track-origins=2 -I$H/stubs/common -c $SRC/Utils/artpatch/artpatch.c -o artpatch_msan.o
    $CC -fsanitize=memory -fsanitize-memory-track-origins=2 -I$SRC/Utils $H/harness/artpatch_memory.c artpatch_msan.o -o artpatch_msan
    for m in lowercase relength empty; do
        $NOASLR ./artpatch_msan $m > msan_$m.log 2>&1; echo "artpatch $m exit=$?"
        trim "WARNING|SUMMARY|^    #[0-1] |Uninitialized value was created|ok$|MISMATCH|survived" 8 < msan_$m.log
    done
    $FUZZCXX -fsanitize=memory -fsanitize-memory-track-origins=2 -I$SRC/Core -c $H/harness/fuzz_decoders.cpp -o fuzz_decoders_msan.o
    clang -g -O1 -fsanitize=memory -c $H/harness/replay_main.c -o replay_msan.o
    $FUZZCXX -fsanitize=memory fuzz_decoders_msan.o replay_msan.o -o replay_decoders_msan
    find $OUT/corpus/decoders -type f | $NOASLR xargs -n 400 ./replay_decoders_msan > msan_decoders.log 2>&1; echo "decoders exit=$?"
    trim "WARNING|SUMMARY|replayed" 6 < msan_decoders.log
}

stage_ubsan_ext() {
    banner "ubsan-ext: null-pointer checks on artpatch; integer/implicit-conversion checks on decoder corpus"
    $CC -fsanitize=undefined -fno-sanitize-recover=all -I$H/stubs/common -c $SRC/Utils/artpatch/artpatch.c -o artpatch_ubsan.o
    $CC -fsanitize=undefined -fno-sanitize-recover=all -I$SRC/Utils $H/harness/artpatch_memory.c artpatch_ubsan.o -o artpatch_ubsan
    ./artpatch_ubsan empty 2>&1 | trim "runtime error|survived|^    #[0-2] " 6
    local ext="-fsanitize=integer,implicit-conversion -fsanitize-recover=all"
    $FUZZCXX $ext -I$SRC/Core -c $H/harness/fuzz_decoders.cpp -o fuzz_decoders_int.o
    clang -g -O1 -c $H/harness/replay_main.c -o replay_plain.o
    $FUZZCXX $ext fuzz_decoders_int.o replay_plain.o -o replay_decoders_int
    find $OUT/corpus/decoders -type f | UBSAN_OPTIONS=print_stacktrace=0 xargs -n 400 ./replay_decoders_int > ubsan_int.log 2>&1
    echo "distinct integer/conversion reports (not UB; triage):"
    grep "runtime error" ubsan_int.log | sed -E 's/^([^ ]+): runtime error: (.{0,140}).*/\1  \2/' | sort | uniq -c | sort -rn | head -20
}

stage_cfi() {
    banner "cfi: CipherHook/CipherPatch casts under Control Flow Integrity"
    local cfi="-flto -fuse-ld=lld -fvisibility=hidden -fsanitize=cfi -fno-sanitize-trap=cfi -fsanitize-recover=cfi"
    clang -g -O1 -w -include stdint.h -flto -I$H/stubs/common -c $SRC/Utils/artpatch/artpatch.c -o artpatch_cfi.o
    clang -g -O1 -w -include stdint.h -flto -I$H/stubs/common -c $SRC/Utils/artpatch/procutils.c -o procutils_cfi.o
    $CXX $cfi $INC_CIPHER $CIPHER_SRC $H/harness/cipher_lifecycle.cpp artpatch_cfi.o procutils_cfi.o -o cipher_cfi
    ./cipher_cfi mixed-types 2>&1 | trim "runtime error|survived|note:" 8
}

stage_procutils() {
    banner "procutils: /proc/self/maps protection lookup used by artpatch apply/restore"
    artpatch_objs asan -fsanitize=address,undefined
    $CXX -fsanitize=address,undefined $H/harness/procutils_maps.cpp procutils_asan.o -Wl,--wrap=fopen -o procutils_maps
    ./procutils_maps adjacent
    ./procutils_maps fopen-fails 2>&1 | trim "ERROR|SUMMARY|survived|^    #[0-2] " 6
    $FUZZCXX -fsanitize=fuzzer,address,undefined -DFUZZ $H/harness/procutils_maps.cpp procutils_asan.o -Wl,--wrap=fopen -o fuzz_procutils
    timeout $(( ${FUZZ_SECS:-60} + 60 )) ./fuzz_procutils $OUT/corpus/procutils -max_len=4096 -max_total_time=${FUZZ_SECS:-60} \
        -artifact_prefix=$OUT/crashes/procutils- > fuzz_procutils.log 2>&1; echo "fuzz exit=$?"
    trim "ERROR|runtime error|SUMMARY|READ|WRITE|^    #[0-3] |Done" 10 < fuzz_procutils.log
}

stage_artpatch() {
    banner "artpatch: patch bytes and page protection under ASan+UBSan (second confirmations for L4, L5)"
    artpatch_objs asan -fsanitize=address,undefined
    # artpatch_memory.c supplies its own procutils answer, so only artpatch.o is linked
    $CC -fsanitize=address,undefined -I$SRC/Utils $H/harness/artpatch_memory.c artpatch_asan.o -o artpatch_memory_asan
    for m in lowercase relength empty; do
        ASAN_OPTIONS=detect_leaks=0 ./artpatch_memory_asan $m 2>&1 | trim "ok$|MISMATCH|survived|runtime error|ERROR" 4
    done
    $CC -fsanitize=address,undefined -I$SRC/Utils $H/harness/artpatch_e2e.c artpatch_asan.o procutils_asan.o -o artpatch_e2e
    ASAN_OPTIONS=detect_leaks=0 ./artpatch_e2e 2>&1 | trim "code page|ERROR|runtime error" 4
}

stage_jni() {
    banner "jni: JNI string leaks (LeakSanitizer) and keyboard-listener race (TSan)"
    $CXX -fsanitize=address,undefined $INC_APP $H/harness/app/jni_entry.cpp $H/harness/app/fake_jni.cpp -o jni_asan
    ASAN_OPTIONS=detect_leaks=1 ./jni_asan leaks > jni_leaks.log 2>&1; echo "leaks exit=$?"
    grep -E "without Release|^(Direct|Indirect) leak|SUMMARY" jni_leaks.log | head -10
    grep -E "^    #[0-9] .*Java_" jni_leaks.log | sed -E 's/.* in (Java_[A-Za-z_]+).*\/([A-Za-z_]+\.inc:[0-9]+).*/  \1 (\2)/' | sort | uniq -c
    $CXX -fsanitize=thread $INC_APP $H/harness/app/jni_entry.cpp $H/harness/app/fake_jni.cpp -o jni_tsan
    TSAN_OPTIONS=halt_on_error=1 timeout 120 $NOASLR ./jni_tsan race > jni_tsan.log 2>&1; echo "race exit=$?"
    trim "WARNING|SUMMARY|^    #[0-1] |Previous|Write of|Read of" 14 < jni_tsan.log
    ASAN_OPTIONS=detect_leaks=0 timeout 60 ./jni_asan interleave > jni_interleave.log 2>&1; echo "interleave exit=$?"
    trim "ERROR|SUMMARY|READ of|freed by|previously allocated|^    #[0-3] |survived" 16 < jni_interleave.log
    ASAN_OPTIONS=detect_leaks=0 ./jni_asan systemui; echo "systemui exit=$?"
}

stage_threads() {
    banner "threads: VM attachment by mod-facing JNI helpers (fake JavaVM with ART's thread-exit rule)"
    $CXX -fsanitize=address,undefined $INC_APP $H/harness/app/jni_threads.cpp $SRC/Core/fileselector/fileselector.cpp \
        $H/harness/app/fake_jni.cpp -o jni_threads -pthread
    for m in java-thread mod-thread haptic-valid haptic-invalid callbacks; do
        ASAN_OPTIONS=detect_leaks=0 timeout 60 ./jni_threads $m > threads_$m.log 2>&1; echo "$m exit=$?"
        trim "attaches=|art:|survived|runtime error|single fd|CONTRACT|ERROR|SUMMARY" 6 < threads_$m.log
    done
}

stage_iconloader() {
    banner "iconloader: every KTX upload result (UBSan return check), missing-atlas cache (LSan), compiler warning"
    local files="$SRC/Core/Iconloader/IconLoader.cpp $IMGUI_SRC $H/harness/app/fake_ktx_assets.cpp $H/harness/app/fake_jni.cpp $H/harness/app/iconloader_driver.cpp"
    $CXX -fsanitize=address,undefined $INC_APP $files -o iconloader -pthread
    for code in $(seq 0 18); do
        ASAN_OPTIONS=detect_leaks=0 timeout 30 ./iconloader upload $code > icon_$code.log 2>&1; local rc=$?
        echo "$(head -1 icon_$code.log | cut -c1-70) exit=$rc $(grep -m1 -o 'runtime error: .*' icon_$code.log | cut -c1-110)"
    done
    echo "compiler on IconLoader.cpp without the app build's -w:"
    clang++ -std=c++17 -fsyntax-only -Wreturn-type $INC_APP $SRC/Core/Iconloader/IconLoader.cpp 2>&1 | grep -A1 "warning:.*return" | head -4
    ASAN_OPTIONS=detect_leaks=1 ./iconloader shared-missing-atlas > icon_atlas.log 2>&1; echo "shared-missing-atlas exit=$?"
    grep -E "both icons|SUMMARY" icon_atlas.log
    grep -A4 "^Direct leak" icon_atlas.log | grep -E "^Direct leak|IconLoader.cpp" | cut -c1-160
}

stage_warnings() {
    banner "warnings: first-party files under warnings the app build's -w hides (sweep for F3, F14 classes)"
    local f log
    # bionic supplies __unused and pulls <cstdint>/<cstring> in transitively; glibc does not
    local compat="-include cstdint -include cstring -D__unused=__attribute__((unused))"
    for f in $SRC/main.cpp $SRC/scroll.cpp $SRC/Core/Canvas/Canvas.cpp $SRC/Core/Cipher/*.cpp $SRC/Core/ChatFlagFix/*.cpp \
             $SRC/Core/OtherAudio/*.cpp $SRC/Core/Iconloader/*.cpp $SRC/Core/fileselector/*.cpp \
             $SRC/Utils/imgui_androidbk/androidbk.cpp; do
        log=warn_$(basename "$f").log
        clang++ -std=c++17 -fsyntax-only -Wno-everything -Wreturn-type -Wnontrivial-memaccess -Wsometimes-uninitialized \
            -Wuninitialized $compat $INC_APP "$f" > "$log" 2>&1 ||
            echo "COMPILE FAILED: ${f#$SRC/} ($(grep -m1 'error:' "$log" | sed "s|$SRC/||" | cut -c1-120))"
        grep -E "warning:" "$log" | sed "s|$SRC/||" |
            grep -vE "^(Core/imgui|Utils/KittyMemory|Utils/ktx|include/misc/(json|obfuscate|ranutils|Vector))"
        # GCC's -Wclass-memaccess covers the F3 class; clang 18 has no equivalent
        g++ -std=c++17 -fsyntax-only -Wclass-memaccess $compat $INC_APP "$f" 2>&1 | grep "\[-Wclass-memaccess\]" |
            sed "s|$SRC/||" | grep -vE "^(Core/imgui|Utils/KittyMemory|Utils/ktx|include/misc/)" | cut -c1-200
    done | sort -u | head -40
}

stage_render_quiet() {
    banner "render_quiet: F10's run-flag race under TSan with no UI-thread sync after shutdown() (3 runs)"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    local files="$SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp $IMGUI_SRC $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp $H/harness/app/render_threads.cpp"
    $CXX -fsanitize=thread $INC_APP $files -o render_tsan -pthread || return 1
    local i
    for i in 1 2 3; do
        TSAN_OPTIONS=halt_on_error=0:report_signal_unsafe=0 timeout 120 $NOASLR ./render_tsan quiet-shutdown > render_quiet_$i.log 2>&1
        echo "run $i: exit=$? TSan reports=$(grep -c 'WARNING: ThreadSanitizer' render_quiet_$i.log)," \
             "naming shutdown() at androidbk.cpp:674: $(grep -c 'androidbk.cpp:674' render_quiet_$i.log);" \
             "$(grep -o 'swaps started after shutdown() returned[^-]*' render_quiet_$i.log)"
    done
    grep -m4 -E "^  (Read|Previous write) of size|^    #0 " render_quiet_1.log | cut -c1-160
}

stage_render() {
    banner "render: androidbk render loop + UI-thread input bridge (TSan, then ASan+UBSan)"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    local files="$SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp $IMGUI_SRC $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp $H/harness/app/render_threads.cpp"
    $CXX -fsanitize=thread $INC_APP $files -o render_tsan -pthread
    TSAN_OPTIONS=halt_on_error=0:report_signal_unsafe=0 timeout 240 $NOASLR ./render_tsan > render_tsan.log 2>&1; echo "tsan exit=$?"
    grep -E "render loop exited|ANativeWindow references|^EGL:" render_tsan.log
    echo "distinct TSan reports: $(grep -c 'WARNING: ThreadSanitizer' render_tsan.log)"
    grep -A3 -E "^  (Write|Read|Atomic [a-z]+|Previous [a-z]+) of size" render_tsan.log | grep -E "of size|#0 " | cut -c1-200 | head -16
    $CXX -fsanitize=address,undefined $INC_APP $files -o render_asan -pthread
    ASAN_OPTIONS=detect_leaks=1 timeout 240 ./render_asan > render_asan.log 2>&1; echo "asan exit=$?"
    trim "ERROR|SUMMARY|runtime error|render loop exited|ANativeWindow references|^EGL:|leak of" 10 < render_asan.log
    ASAN_OPTIONS=detect_leaks=0 timeout 60 ./render_asan lost-shutdown > render_lost.log 2>&1; echo "lost-shutdown exit=$?"
    trim "frames presented|ERROR|SUMMARY|runtime error" 6 < render_lost.log
    ASAN_OPTIONS=detect_leaks=0 timeout 60 ./render_asan released-surface > render_released.log 2>&1; echo "released-surface exit=$?"
    trim "resurface job|ERROR|SUMMARY|^    #[0-4] |survived" 8 < render_released.log
}

stage_registry() {
    banner "registry: real CipherRegistry.cpp - concurrency (TSan, ASan+UBSan), range contract, exit order (ASan+UBSan, MSan)"
    local reg="$SRC/Core/Cipher/CipherRegistry.cpp $H/harness/registry_env.cpp"
    $CXX -fsanitize=thread $INC_APP $reg $H/harness/cipher_registry.cpp -o registry_tsan -pthread || return 1
    TSAN_OPTIONS=detect_deadlocks=1 timeout 300 $NOASLR ./registry_tsan concurrent > registry_tsan.log 2>&1
    echo "registry_tsan concurrent exit=$?"; trim "WARNING|SUMMARY|FAIL|installs=" 8 < registry_tsan.log
    $CXX -fsanitize=address,undefined $INC_APP $reg $H/harness/cipher_registry.cpp -o registry_asan -pthread || return 1
    ./registry_asan concurrent > registry_asan.log 2>&1; echo "registry_asan concurrent exit=$?"
    trim "ERROR|SUMMARY|runtime error|FAIL|installs=" 8 < registry_asan.log
    ./registry_asan ranges 2>&1 | trim "ok |MISMATCH|mismatches|ERROR|runtime error" 8
    # Exit order: Cipher sources first on the link line, so their statics are built first (Canvas loads before mods).
    artpatch_objs asan -fsanitize=address,undefined
    local cip="$SRC/Core/Cipher/CipherBase.cpp $SRC/Core/Cipher/CipherHook.cpp $SRC/Core/Cipher/CipherPatch.cpp"
    $CXX -fsanitize=address,undefined $INC_APP $cip $reg $H/harness/cipher_exit.cpp artpatch_asan.o procutils_asan.o -o exit_asan -pthread || return 1
    ASAN_OPTIONS=detect_leaks=0 ./exit_asan > exit_asan.log 2>&1; echo "exit_asan (registry first used by the mod's Fire) exit=$?"
    trim "main returning|ERROR|SUMMARY|runtime error|^    #[0-4] " 10 < exit_asan.log
    REGISTRY_TOUCH_FIRST=1 ASAN_OPTIONS=detect_leaks=0 ./exit_asan > exit_asan_control.log 2>&1; echo "exit_asan control (registry used first) exit=$?"
    trim "main returning|ERROR|SUMMARY|runtime error" 6 < exit_asan_control.log
    # Second way: MSan poisons an object's storage when its destructor finishes (use-after-dtor).
    $CC -fsanitize=memory -fsanitize-memory-track-origins=2 -I$H/stubs/common -c $SRC/Utils/artpatch/artpatch.c -o artpatch_msan.o
    $CC -fsanitize=memory -fsanitize-memory-track-origins=2 -I$H/stubs/common -c $SRC/Utils/artpatch/procutils.c -o procutils_msan.o
    # libstdc++ here (no MSan-built libc++ in the image); Cipher.h relies on libc++'s transitive <cstdint>/<cstring>.
    $FUZZCXX -fsanitize=memory -fsanitize-memory-track-origins=2 -fsanitize-memory-use-after-dtor -include cstdint -include cstring $INC_APP $cip $reg $H/harness/cipher_exit.cpp \
        artpatch_msan.o procutils_msan.o -o exit_msan -pthread || return 1
    MSAN_OPTIONS=poison_in_dtor=1 $NOASLR ./exit_msan > exit_msan.log 2>&1; echo "exit_msan exit=$?"
    trim "main returning|WARNING|SUMMARY|^    #[0-4] |Member fields were destroyed|Uninitialized value was created" 12 < exit_msan.log
    REGISTRY_TOUCH_FIRST=1 MSAN_OPTIONS=poison_in_dtor=1 $NOASLR ./exit_msan > exit_msan_control.log 2>&1; echo "exit_msan control exit=$?"
    trim "main returning|WARNING|SUMMARY" 4 < exit_msan_control.log
}

stage_artpatch_diff() {
    banner "artpatch_diff: page-crossing patch (deterministic) and a differential fuzzer against a byte model of the pages"
    artpatch_objs asan -fsanitize=address,undefined
    $CC -fsanitize=address,undefined -I$SRC/Utils $H/harness/artpatch_cross.c artpatch_asan.o procutils_asan.o -o artpatch_cross
    ASAN_OPTIONS=detect_leaks=0 ./artpatch_cross > artpatch_cross.log 2>&1; echo "artpatch_cross exit=$?"
    trim "patching|survived|restore|ERROR|SUMMARY|WRITE|^    #[0-3] " 10 < artpatch_cross.log
    # Coverage-instrumented artpatch for the fuzzer.
    artpatch_objs fuzz -fsanitize=fuzzer-no-link,address,undefined
    $FUZZCXX -fsanitize=fuzzer,address,undefined -I$SRC/Utils $H/harness/fuzz_artpatch_diff.cpp artpatch_fuzz.o procutils_fuzz.o \
        -o fuzz_artpatch_diff || return 1
    local opts="-print_final_stats=1 -timeout=10"
    # 1. Crossing allowed, from an empty corpus: does the fuzzer find the page-crossing case on its own?
    rm -rf $OUT/corpus/artpatch_diff_cross && mkdir -p $OUT/corpus/artpatch_diff_cross
    ASAN_OPTIONS=detect_leaks=0 ./fuzz_artpatch_diff $opts -max_total_time=${DIFF_CROSS_SECS:-60} \
        -artifact_prefix=$OUT/crashes/artpatch_diff_cross- $OUT/corpus/artpatch_diff_cross > artpatch_diff_cross.log 2>&1
    echo "fuzz, crossing allowed, unseeded: exit=$?"
    trim "MISMATCH|ops:|ERROR|SUMMARY|runtime error|^    #[0-3] |Test unit written|number_of_executed_units" 12 < artpatch_diff_cross.log
    # 2. Patches inside one page, seeded with instruction words from the ChatFlagFix fixtures.
    mkdir -p $OUT/corpus/artpatch_diff
    $FUZZCXX -I$REPO/test/cpp $H/harness/make_artpatch_seeds.cpp -o make_artpatch_seeds &&
        ./make_artpatch_seeds $OUT/corpus/artpatch_diff "$(getconf PAGESIZE)"
    ARTPATCH_DIFF_NO_CROSS=1 ASAN_OPTIONS=detect_leaks=0 ./fuzz_artpatch_diff $opts -max_total_time=${DIFF_SECS:-180} \
        -artifact_prefix=$OUT/crashes/artpatch_diff- $OUT/corpus/artpatch_diff > artpatch_diff.log 2>&1
    echo "fuzz, patches inside one page: exit=$?"
    trim "MISMATCH|ops:|ERROR|SUMMARY|runtime error|^    #[0-3] |Test unit written|number_of_executed_units|cov:" 12 < artpatch_diff.log | tail -12
}

stage_hwasan_x86() {
    banner "hwasan_x86: HWASan natively on x86-64, page-aliasing mode (tags the primary heap only: no stack, no globals)"
    local hwx="-fsanitize=hwaddress -fsanitize-hwaddress-experimental-aliasing"
    $CC $hwx $H/harness/hwasan_smoke.c -o smoke_hwx &&
        { timeout 60 ./smoke_hwx > smoke_hwx.log 2>&1; echo "smoke exit=$?"; trim "HWAddressSanitizer|SUMMARY|^    #[0-1] " 4 < smoke_hwx.log; }
    $CXX $hwx $INC_APP $H/harness/app/jni_entry.cpp $H/harness/app/fake_jni.cpp -o jni_hwx -pthread &&
        { timeout 120 ./jni_hwx interleave > jni_hwx.log 2>&1; echo "F9 (jni_hwx interleave) exit=$?"
          trim "HWAddressSanitizer|SUMMARY|^    #[0-3] |freed by|previously allocated|survived" 14 < jni_hwx.log; }
    artpatch_objs hwx $hwx
    # F4: on arm64 under QEMU this ran without output until killed. Natively, stop it after 30 s with
    # SIGABRT and have HWASan print where it is (handle_abort=1).
    $CXX $hwx $INC_CIPHER $CIPHER_SRC $H/harness/cipher_lifecycle.cpp artpatch_hwx.o procutils_hwx.o -o cipher_hwx &&
        { HWASAN_OPTIONS=handle_abort=1 timeout -s ABRT 30 ./cipher_hwx unfired-delete > cipher_hwx.log 2>&1; echo "F4 (cipher_hwx unfired-delete) exit=$?"
          trim "HWAddressSanitizer|SUMMARY|^    #[0-7] |survived|ABRT" 14 < cipher_hwx.log; }
}

stage_decoders_cov() {
    banner "decoders_cov: which Decide/ChatFlagFix outcomes the fuzz_decoders corpus reaches (source-based coverage)"
    $FUZZCXX -fsanitize=fuzzer -fprofile-instr-generate -fcoverage-mapping -I$SRC/Core $H/harness/fuzz_decoders.cpp \
        -o fuzz_decoders_cov || return 1
    rm -f decoders.profraw
    LLVM_PROFILE_FILE=decoders.profraw ./fuzz_decoders_cov -runs=0 $OUT/corpus/decoders > decoders_cov.log 2>&1
    llvm-profdata merge -sparse decoders.profraw -o decoders.profdata || return 1
    echo "corpus: $(ls $OUT/corpus/decoders | wc -l) inputs; line executions for each Decide outcome (OtherAudioSwap.h):"
    llvm-cov show ./fuzz_decoders_cov -instr-profile=decoders.profdata $SRC/Core/OtherAudio/OtherAudioSwap.h 2> /dev/null |
        grep -E 'return Stop\(plan, Decision::' | sed -E 's/^ *([0-9]+)\| *([0-9.kKMG]*)\| *return Stop\(plan, Decision::([A-Za-z]+).*/  line \1: \3 x\2/'
    echo "ChatFlagFix: matches accepted (Candidate returns true):"
    llvm-cov show ./fuzz_decoders_cov -instr-profile=decoders.profdata $SRC/Core/ChatFlagFix/ChatFlagFix.h 2> /dev/null |
        grep -E 'out.flagOffset = offset;' | sed -E 's/^ *([0-9]+)\| *([0-9.kKMG]*)\|.*/  line \1: x\2/'
}

stage_icon_api() {  # pass 36
    banner "icon_api: IconLoader's mod-facing API (getUIIcon, icon, iconButton) in headless ImGui frames; getUIIcon from two threads"
    local files="$SRC/Core/Iconloader/IconLoader.cpp $IMGUI_SRC $H/harness/app/fake_ktx_assets.cpp $H/harness/app/fake_jni.cpp $H/harness/app/iconloader_driver.cpp"
    $CXX -fsanitize=address,undefined $INC_APP $files -o iconloader_api -pthread || return 1
    # The driver runs a recoverable leak check while the global maps are alive; the exit
    # check is off, since the maps hold raw pointers that become unreachable once their
    # destructors run.
    ASAN_OPTIONS=detect_leaks=1:leak_check_at_exit=0 timeout 120 ./iconloader_api icon-api > icon_api.log 2>&1; echo "icon-api exit=$?"
    trim "^ok|MISMATCH|registered twice|failures=|ERROR: AddressSanitizer|runtime error|Assertion" 12 < icon_api.log
    # Leaks by allocation site: the PrivateUIIcon addIcon news (IconLoader.cpp:33) vs its
    # unreleased GetStringUTFChars copies (:36, :38; F8).
    echo "direct leaks: $(grep -c '^Direct leak' icon_api.log); allocated by addIcon's new PrivateUIIcon: $(grep -A3 '^Direct leak' icon_api.log | grep -c 'IconLoader.cpp:33:')," \
         "by its GetStringUTFChars copies (F8): $(grep -A3 '^Direct leak' icon_api.log | grep -cE 'IconLoader.cpp:3[68]:')"
    $CXX -fsanitize=thread $INC_APP $files -o iconloader_api_tsan -pthread || return 1
    TSAN_OPTIONS=halt_on_error=0 timeout 120 $NOASLR ./iconloader_api_tsan icon-race > icon_api_race_tsan.log 2>&1
    echo "icon-race tsan exit=$? reports=$(grep -c 'WARNING: ThreadSanitizer' icon_api_race_tsan.log)"
    trim "icon-race:|^  (Write|Read|Previous write|Previous read) of size|^    #[0-1] " 14 < icon_api_race_tsan.log
    local i
    for i in 1 2 3; do
        ASAN_OPTIONS=detect_leaks=0 timeout 120 ./iconloader_api icon-race > icon_api_race_asan_$i.log 2>&1
        echo "icon-race asan run $i: exit=$? $(grep -m1 -oE 'runtime error: [a-z ]+|ERROR: AddressSanitizer: [A-Za-z-]+|icon-race: .*' icon_api_race_asan_$i.log)"
    done
}

stage_iconloader_race() {
    banner "iconloader_race: exported IconLoader::getImage from two threads (L13) under TSan and ASan+UBSan"
    local files="$SRC/Core/Iconloader/IconLoader.cpp $IMGUI_SRC $H/harness/app/fake_ktx_assets.cpp $H/harness/app/fake_jni.cpp $H/harness/app/iconloader_driver.cpp"
    $CXX -fsanitize=thread $INC_APP $files -o iconloader_tsan -pthread || return 1
    TSAN_OPTIONS=halt_on_error=0 timeout 120 $NOASLR ./iconloader_tsan race > icon_race_tsan.log 2>&1
    echo "tsan exit=$? reports=$(grep -c 'WARNING: ThreadSanitizer' icon_race_tsan.log)"
    trim "race:|^  (Write|Read|Previous write|Previous read) of size|^    #[0-2] " 12 < icon_race_tsan.log
    $CXX -fsanitize=address,undefined $INC_APP $files -o iconloader_asan_race -pthread || return 1
    local i
    for i in 1 2 3; do
        ASAN_OPTIONS=detect_leaks=0 timeout 120 ./iconloader_asan_race race > icon_race_asan_$i.log 2>&1
        echo "asan run $i: exit=$? $(grep -m1 -oE 'ERROR: AddressSanitizer: [a-z-]+|race: .*' icon_race_asan_$i.log)"
    done
}

stage_unicode() {
    banner "unicode: L19, an emoji typed through submitUnicodeEvent and Drain into an ImGui text field (ASan+UBSan)"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    local files="$SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp $IMGUI_SRC $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp $H/harness/app/render_threads.cpp"
    $CXX -fsanitize=address,undefined $INC_APP $files -o render_asan -pthread || return 1
    ASAN_OPTIONS=detect_leaks=0 timeout 60 ./render_asan unicode > render_unicode.log 2>&1; echo "unicode exit=$?"
    trim "field bytes|valid UTF-8|ERROR|SUMMARY|runtime error" 6 < render_unicode.log
}

stage_clipboard() {
    banner "clipboard: androidbk's clipboard helpers via ImGui's API on the render thread, against a model of ImGUI.java and ART's string conversions (ASan+UBSan, TSan)"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    local files="$SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp $IMGUI_SRC $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp $H/harness/app/render_threads.cpp"
    $CXX -fsanitize=address,undefined $INC_APP $files -o render_asan -pthread || return 1
    ASAN_OPTIONS=detect_leaks=1 timeout 120 ./render_asan clipboard > render_clipboard.log 2>&1; echo "asan+ubsan exit=$?"
    trim "^ok|MISMATCH|expected|Java calls|ERROR|SUMMARY|runtime error|leak of" 24 < render_clipboard.log
    # Pass 35 (L20): setClipboard throws (copy too large); then a paste, or nothing more.
    local m
    for m in clipboard-throw clipboard-throw-set; do
        ASAN_OPTIONS=detect_leaks=0 timeout 120 ./render_asan $m > render_$m.log 2>&1; echo "$m exit=$?"
        trim "setClipboard threw|allowed list|paste after|returned to Java|ERROR|SUMMARY|runtime error" 8 < render_$m.log
    done
    $CXX -fsanitize=thread $INC_APP $files -o render_tsan -pthread || return 1
    TSAN_OPTIONS=halt_on_error=0:report_signal_unsafe=0 timeout 120 $NOASLR ./render_tsan clipboard > render_clipboard_tsan.log 2>&1
    echo "tsan exit=$? TSan reports=$(grep -c 'WARNING: ThreadSanitizer' render_clipboard_tsan.log); $(grep 'Java calls' render_clipboard_tsan.log)"
}

stage_render_cov() {
    banner "render_cov: which androidbk.cpp/scroll.cpp functions the render harness reaches (source-based coverage, all modes)"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    local files="$SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp $IMGUI_SRC $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp $H/harness/app/render_threads.cpp"
    clang++ -std=c++17 -stdlib=libc++ -g -O1 -w -fprofile-instr-generate -fcoverage-mapping $INC_APP $files -o render_cov -pthread || return 1
    rm -f render-*.profraw
    local m
    for m in stress lost-shutdown released-surface quiet-shutdown unicode clipboard; do
        # released-surface ends in F16's NULL dereference without a sanitizer; its profile is lost.
        LLVM_PROFILE_FILE=render-%p.profraw timeout 120 ./render_cov $([ $m = stress ] || echo $m) > render_cov_$m.log 2>&1
        echo "$m: exit=$?"
    done
    llvm-profdata merge -sparse render-*.profraw -o render.profdata || return 1
    llvm-cov report ./render_cov -instr-profile=render.profdata $SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp 2> /dev/null | tail -4
    echo "functions in androidbk.cpp/scroll.cpp never executed:"
    llvm-cov report -show-functions ./render_cov -instr-profile=render.profdata $SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp 2> /dev/null |
        awk '$0 ~ /^[A-Za-z_]/ && $0 !~ /^(Name|File|TOTAL|-)/ { if ($(NF-6) == "0.00%" || $4 == "0.00%") print "  " $1 }' | head -40
}

stage_fonts() {
    banner "fonts: L18, the CJK font buffer from loadFonts after the atlas is cleared, rebuilt or destroyed (ASan+UBSan)"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    local files="$H/harness/app/font_ownership.cpp $IMGUI_SRC $SRC/Core/Canvas/Canvas.cpp $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp"
    $CXX -fsanitize=address,undefined -DFAKE_PLATFORM_NO_CANVAS_MENU $INC_APP $files -o font_ownership -pthread || return 1
    local m
    for m in control clear rebuild destroy; do
        FAKE_ASSET_DIR=$REPO/main/assets ASAN_OPTIONS=detect_leaks=0 timeout 120 ./font_ownership $m > fonts_$m.log 2>&1
        echo "$m: exit=$? $(grep -m1 -oE 'ERROR: AddressSanitizer: [a-z-]+|survived' fonts_$m.log)"
        grep -m1 'loadFonts(' fonts_$m.log
        grep -E '^    #[0-4] |freed by|previously allocated' fonts_$m.log |
            grep -E 'androidbk|loadFonts|ImFontAtlas|font_ownership|freed by|previously' | head -6 | cut -c1-170
    done
}

stage_lifecycle() {
    banner "lifecycle: mod registration and InitLate (real main.cpp extractions, real Canvas.cpp), ASan+UBSan and TSan"
    local files="$H/harness/app/mod_lifecycle.cpp $SRC/Core/Canvas/Canvas.cpp $IMGUI_SRC $H/harness/app/fake_jni.cpp"
    local san tag flags d m
    for san in "asan:-fsanitize=address,undefined" "tsan:-fsanitize=thread"; do
        tag=${san%%:*}; flags=${san#*:}; d=$OUT/mods_$tag
        mkdir -p $d
        # Stand-in mods, built with the same sanitizer as the host so their accesses are seen.
        $CXX $flags -shared -fPIC -DMOD_UI=1 $H/harness/app/mod_stub.cpp -o $d/mod_ui.so || return 1
        $CXX $flags -shared -fPIC -DMOD_UI=0 $H/harness/app/mod_stub.cpp -o $d/mod_patch.so || return 1
        $CXX $flags $INC_APP $files -Wl,--wrap=dlopen -o lifecycle_$tag -pthread -ldl || return 1
        for m in once twice; do
            if [ $tag = tsan ]; then
                TSAN_OPTIONS=halt_on_error=0 timeout 120 $NOASLR ./lifecycle_$tag $m $d > lifecycle_${tag}_$m.log 2>&1
            else
                ASAN_OPTIONS=detect_leaks=0 timeout 120 ./lifecycle_$tag $m $d > lifecycle_${tag}_$m.log 2>&1
            fi
            echo "$tag $m: exit=$? TSan/ASan reports=$(grep -cE 'WARNING: ThreadSanitizer|ERROR: AddressSanitizer' lifecycle_${tag}_$m.log)"
            trim "ok |MISMATCH|registered with|runtime error|SUMMARY" 6 < lifecycle_${tag}_$m.log
        done
    done
    grep -m6 -E "^  (Write|Previous write|Read) of size|^    #0 |^    #1 " lifecycle_tsan_twice.log | cut -c1-160
}

# Pass 34: line coverage of the in-scope tree across the harnesses. Re-runs the
# ASan/TSan stages with source-based coverage added to every compiler (bash
# locals are dynamically scoped, so the stages called here see these values).
# Not in ALL. Run it against its own output directory, e.g. mount
# out/coverage_all as /out (and out/corpus/decoders read-only at
# /out/corpus/decoders for the decoder corpus). Lower bound: runs that crash,
# abort or hit `timeout` lose their profile.
stage_coverage_all() {
    banner "coverage_all: in-scope line coverage across the ASan/TSan stages (source-based coverage)"
    local covflags="-fprofile-instr-generate -fcoverage-mapping"
    local CXX="$CXX $covflags" CC="$CC $covflags" FUZZCXX="$FUZZCXX $covflags"
    local -x LLVM_PROFILE_FILE=$OUT/cov/%m-%p.profraw
    local -x DIFF_CROSS_SECS=${DIFF_CROSS_SECS:-20} DIFF_SECS=${DIFF_SECS:-30}
    rm -rf $OUT/cov && mkdir -p $OUT/cov
    local s
    for s in ${COV_STAGES:-confirmed unit procutils artpatch jni threads iconloader render registry artpatch_diff lifecycle fonts unicode clipboard decoders_cov}; do
        "stage_$s" > $OUT/cov/stage_$s.log 2>&1
        echo "$s: done ($(ls $OUT/cov/*.profraw 2> /dev/null | wc -l) profiles so far)"
    done
    local profiles="$OUT/cov/*.profraw"
    [ -f decoders.profdata ] && profiles="$profiles decoders.profdata"
    llvm-profdata merge -sparse $profiles -o $OUT/cov/all.profdata 2> $OUT/cov/merge.err || return 1
    echo "profile merge warnings: $(grep -c warning $OUT/cov/merge.err)"
    local objs="" f
    for f in $(find $OUT -maxdepth 2 -type f \( -perm -u+x -o -name '*.so' \) ! -name '*.sh'); do
        grep -qa __llvm_covmap "$f" && objs="$objs -object $f"
    done
    objs=${objs# -object }
    local ignore='(/imgui/|KittyMemory|/ktx/|/test/|/usr/|/stubs/|json\.hpp|obfuscate\.h)'
    llvm-cov report $objs -instr-profile=$OUT/cov/all.profdata -ignore-filename-regex="$ignore" 2> /dev/null |
        sed -E "s|^/?repo/main/cpp/||; s|^/?out/gen/|gen/|" > $OUT/cov/report.txt
    # Columns: file, lines, missed lines, line % (Lines are the last three numeric groups before Branches).
    awk 'NR > 2 && $1 !~ /^-/ { print }' $OUT/cov/report.txt | cut -c1-200
    echo "in-scope sources no harness compiles:"
    local p
    for p in $(cd $SRC && find Core/Canvas Core/Cipher Core/ChatFlagFix Core/OtherAudio Core/Iconloader Core/fileselector \
            Utils/artpatch Utils/imgui_androidbk main.cpp scroll.cpp -type f \( -name '*.cpp' -o -name '*.c' -o -name '*.h' \) | sort); do
        grep -q "^$p " $OUT/cov/report.txt || echo "  $p ($(grep -cvE '^\s*(//.*)?$' $SRC/$p) non-blank lines)"
    done
}

ALL="confirmed unit fuzz_decoders msan ubsan_ext cfi procutils artpatch jni threads iconloader warnings render render_quiet registry artpatch_diff hwasan_x86 lifecycle decoders_cov iconloader_race fonts render_cov unicode clipboard icon_api"
STAGES="${*:-$ALL}"
generate || exit 1
for s in $STAGES; do
    s=${s//-/_}
    if declare -f "stage_$s" > /dev/null; then "stage_$s"; else echo "unknown stage: $s (have: $ALL)"; fi
done
