#!/bin/bash
# Host-side performance measurements of Canvas's overlay frame. Runs inside
# canvas-sanitizer-harness:local; see PERF.md for the docker command.
# Mounts: /repo = app/src (read-only), /out = app/src/test/perf/out.
#
#   run_perf.sh                 every stage
#   run_perf.sh frame           named stages only
set -u

REPO=/repo
SRC=$REPO/main/cpp
H=$REPO/test/sanitizers      # shared stubs and fakes
P=$REPO/test/perf
OUT=/out
GEN=$OUT/gen
mkdir -p "$OUT" "$GEN/android"
cd "$OUT"

# As the app builds it (CMakeLists.txt: -O3 -flto, Release => NDEBUG), with libc++.
CXX="clang++ -std=c++17 -stdlib=libc++ -O3 -DNDEBUG -flto -fuse-ld=lld -fomit-frame-pointer -w"
INC="-I$GEN -I$H/stubs/app -I$H/stubs/common -I$SRC/Utils -I$SRC/Utils/KittyMemory -I$SRC/Core/imgui -I$SRC/Core -I$SRC"
IMGUI_SRC="$SRC/Core/imgui/imgui.cpp $SRC/Core/imgui/imgui_draw.cpp $SRC/Core/imgui/imgui_tables.cpp $SRC/Core/imgui/imgui_widgets.cpp"
REPS=${REPS:-5}
FRAMES=${FRAMES:-3000}

banner() { echo; echo "================ $*"; }

extract() {  # same contract as ../sanitizers/run.sh
    awk -v start="$2" 'index($0, start) == 1 {p = 1} p {print} p && /^}/ {exit}' "$1" > "$3.tmp"
    if ! grep -q '^}' "$3.tmp"; then echo "EXTRACTION FAILED: $2 in $1" >&2; rm -f "$3.tmp"; return 1; fi
    mv "$3.tmp" "$3"
}

generate() {
    extract $SRC/main.cpp 'PRIVATE_API static void HelpMarker(' $GEN/HelpMarker.inc
    # First anonymous namespace (ModStyleScope). Its struct closes with "};" at
    # column 0, so this block ends on a bare "}" line instead (main.cpp is CRLF).
    awk 'index($0, "namespace {") == 1 {p = 1} p {print} p && /^}\r?$/ {exit}' $SRC/main.cpp > $GEN/ModStyleScope.inc
    extract $SRC/main.cpp 'PRIVATE_API void DrawMods()' $GEN/DrawMods.inc
    extract $SRC/main.cpp 'PRIVATE_API void Canvas::CanvasMenu()' $GEN/CanvasMenu.inc
    grep -q 'ModStyleScope &operator=' $GEN/ModStyleScope.inc && [ "$(wc -l < $GEN/ModStyleScope.inc)" -lt 60 ] ||
        { echo "ModStyleScope extraction looks wrong" >&2; return 1; }
    grep -o 'AKEYCODE_[A-Z0-9_]*' $SRC/Utils/imgui_androidbk/androidbk.cpp | sort -u |
        awk '{printf "#define %s %d\n", $1, NR}' > $GEN/android/keycodes.h
}

environment() {
    echo "host: $(nproc) CPUs, $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')"
    echo "load before: $(cut -d' ' -f1-3 /proc/loadavg)"
    # Host speed at this moment on the CPU the interleaved stages pin to (pass 50).
    [ -x calib ] || clang -O2 $P/harness/calib.c -o calib
    echo "cpu ${CALIB_CPU:-2} $(taskset -c ${CALIB_CPU:-2} ./calib)"
}

stage_frame() {
    banner "frame: CPU time per overlay frame (render thread), $REPS runs x $FRAMES frames per scenario"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    $CXX -DFAKE_PLATFORM_NO_CANVAS_MENU $INC $SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp $IMGUI_SRC \
        $SRC/Core/Canvas/Canvas.cpp $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp \
        $P/harness/perf_render.cpp -o perf_render -pthread || return 1
    environment
    local s r list
    # SCENARIOS="vsync 5;menu 5" runs a subset
    IFS=";" read -ra list <<< "${SCENARIOS:-menu 0;menu 1;menu 5;menu 20;hidden 5;touch 5;vsync 5;limited 5}"
    for s in "${list[@]}"; do
        set -- $s
        for r in $(seq 1 $REPS); do ./perf_render $1 $2 $FRAMES; done > frame_$1_$2.log 2>&1
        # report the run with the median p50 CPU time (only the per-run scenario
        # lines carry cpu_us; startup/window/UI lines are skipped, pass 48)
        grep 'cpu_us p50=' frame_$1_$2.log | awk -F'cpu_us p50=' '{split($2, a, " "); print a[1] "\t" $0}' | sort -n |
            awk -v n=$REPS 'NR == int((n + 1) / 2) {sub(/^[^\t]*\t/, ""); print}'
        grep -m2 'startup:' frame_$1_$2.log
    done
    echo "load after: $(cut -d' ' -f1-3 /proc/loadavg)"
}

stage_taps() {  # pass 58: frames from a tap or typed text to the widget (counts, not timings)
    banner "taps: overlay frames from input to widget (real JNI entry points, Drain, ImGui input queue; one stand-in mod)"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    $CXX -DFAKE_PLATFORM_NO_CANVAS_MENU $INC $SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp $IMGUI_SRC \
        $SRC/Core/Canvas/Canvas.cpp $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp \
        $P/harness/perf_render.cpp -o perf_render -pthread || return 1
    local r
    for r in 1 2 3; do  # deterministic: the three runs should agree
        timeout 60 ./perf_render taps 1 0 > taps_$r.log 2>&1; echo "run $r: exit=$?"
    done
    grep 'taps:' taps_1.log
    echo "runs 2-3 identical to run 1 (frame numbers aside): $(for r in 2 3; do diff <(grep -o -- '-> .*' taps_1.log) <(grep -o -- '-> .*' taps_$r.log) > /dev/null && echo yes || echo NO; done | tr '\n' ' ')"
    # DESIGN VARIANTS (patched scratch copies of androidbk.cpp, one statement added
    # just before Drain's replay loop):
    #   touchsrc (pass 59): tag replayed touches as ImGuiMouseSource_TouchScreen, as
    #     upstream backends do; Canvas deliberately does not (Drain's comment).
    #   notrickle (pass 60): io.ConfigInputTrickleEventQueue = false (ImGui's default
    #     is on; Canvas leaves it on).
    taps_variant touchsrc "io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);" || return 1
    taps_variant notrickle "io.ConfigInputTrickleEventQueue = false;" || return 1
}

stage_taplatency() {  # pass 61: release-to-response time in the queued-swap model, by tap duration
    banner "taplatency: release to the frame that acts on a tap, and to its present (queued-swap model, 100 taps per row)"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    $CXX -DFAKE_PLATFORM_NO_CANVAS_MENU $INC $SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp $IMGUI_SRC \
        $SRC/Core/Canvas/Canvas.cpp $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp \
        $P/harness/perf_render.cpp -o perf_render -pthread || return 1
    environment
    # Display models (pass 62): queued = non-async BufferQueue, swaps return until
    # the queue is full (run 24); blocking = every swap waits for the next vsync
    # and presents at it, so nothing queues (PERF_VSYNC_ALIGNED, as in run 10).
    local model lim d log env
    for model in ${TAP_MODELS:-queued}; do
        for lim in 0 1; do
            for d in ${TAP_MS_LIST:-0 50 100 150}; do
                env="PERF_TAP_MS=$d"
                [ $model = queued ] && env="$env PERF_SWAP_QUEUED=1" || env="$env PERF_VSYNC_ALIGNED=1"
                [ $lim = 1 ] && env="$env PERF_TAP_LIMITED=1"
                log=taplatency_${model}_${lim}_$d.log
                env $env timeout 300 ./perf_render taplatency 1 0 > $log 2>&1
                grep '^taplatency' $log | sed "s/^taplatency/taplatency model=$model/" ||
                    { echo "model=$model limited=$lim tap_ms=$d: no result"; tail -3 $log; }
            done
        done
    done
    echo "load after: $(cut -d' ' -f1-3 /proc/loadavg)"
}

taps_variant() {  # $1 = name, $2 = statement inserted before Drain's replay loop
    local V=$OUT/taps_$1
    rm -rf $V && mkdir -p $V/Utils/imgui_androidbk && ln -s $SRC/include $V/include
    cp $SRC/Utils/imgui_androidbk/androidbk.h $V/Utils/imgui_androidbk/
    awk -v stmt="$2" '!done && index($0, "for (const Event &e : local) {") {
            print "    " stmt "  // DESIGN VARIANT"; done = 1 } { print }' \
        $SRC/Utils/imgui_androidbk/androidbk.cpp > $V/Utils/imgui_androidbk/androidbk.cpp
    grep -q 'DESIGN VARIANT' $V/Utils/imgui_androidbk/androidbk.cpp || { echo "variant patch did not apply"; return 1; }
    $CXX -DFAKE_PLATFORM_NO_CANVAS_MENU $INC $V/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp $IMGUI_SRC \
        $SRC/Core/Canvas/Canvas.cpp $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp \
        $P/harness/perf_render.cpp -o perf_render_$1 -pthread || return 1
    local r
    for r in 1 2 3; do
        timeout 60 ./perf_render_$1 taps 1 0 > taps_$1_$r.log 2>&1; echo "DESIGN VARIANT $1 run $r: exit=$?"
    done
    grep 'taps:' taps_$1_1.log | sed "s/^  taps:/  VARIANT $1 taps:/"
    echo "variant $1 runs 2-3 identical to run 1: $(for r in 2 3; do diff <(grep -o -- '-> .*' taps_$1_1.log) <(grep -o -- '-> .*' taps_$1_$r.log) > /dev/null && echo yes || echo NO; done | tr '\n' ' ')"
}

stage_instr() {
    banner "instr: instructions per overlay frame (callgrind; independent of machine load)"
    command -v valgrind > /dev/null || { echo "valgrind missing: run in canvas-perf-harness:local (Dockerfile.perf)"; return 1; }
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    $CXX -g -DFAKE_PLATFORM_NO_CANVAS_MENU $INC $SRC/Utils/imgui_androidbk/androidbk.cpp $SRC/scroll.cpp $IMGUI_SRC \
        $SRC/Core/Canvas/Canvas.cpp $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp \
        $P/harness/perf_render.cpp -o perf_render_cg -pthread || return 1
    local s list dump total frames
    IFS=";" read -ra list <<< "${INSTR_SCENARIOS:-menu 0;menu 1;menu 5;menu 20;hidden 5;touch 5}"
    for s in "${list[@]}"; do
        set -- $s
        rm -f cg_$1_$2.out*
        valgrind --tool=callgrind --callgrind-out-file=cg_$1_$2.out ./perf_render_cg $1 $2 ${INSTR_FRAMES:-500} \
            > instr_$1_$2.log 2>&1
        dump=cg_$1_$2.out.1  # the "measured frames" dump; the bare file holds startup and shutdown
        total=$(callgrind_annotate "$dump" 2> /dev/null | awk '/PROGRAM TOTALS/ {gsub(",", "", $1); print $1}')
        frames=$(grep -o 'callgrind_frames=[0-9]*' instr_$1_$2.log | cut -d= -f2)
        if [ -z "$total" ] || [ -z "$frames" ] || [ "$frames" -eq 0 ]; then echo "$1 $2: no data (see instr_$1_$2.log)"; continue; fi
        echo "$1 $2: $total instructions over $frames frames = $((total / frames)) per frame"
        echo "  inclusive, per frame (Canvas-level functions):"
        # function summaries only (they end in "[binary]"); skip the per-call-site lines
        callgrind_annotate --inclusive=yes "$dump" 2> /dev/null | grep -E '^ *[0-9,]+ \(' | grep -F '[/out/perf_render_cg]' |
            grep -E 'renderloop\(\)|Canvas::CanvasMenu\(\)|DrawMods\(\)|stand_in_mod|ModStyleScope|ImGui::NewFrame\(\)|ImGui::Render\(\)|ImGui_ImplOpenGL3_RenderDrawData|CanvasInput::Drain|CanvasInput::PublishSnapshot|do_scroll|HelpMarker' |
            awk -v f="$frames" '{c=$1; gsub(",", "", c); pct=$2; $1=""; $2=""; sub(/^ +/, ""); n=split($0, a, ":"); name=a[n]; sub(/ \[.*/, "", name); printf "    %9d  %7s  %s\n", c / f, pct, substr(name, 1, 90)}'
        echo "  top self cost, per frame:"
        callgrind_annotate --inclusive=no "$dump" 2> /dev/null | grep -E '^ *[0-9,]+ \(' | grep -v 'PROGRAM TOTALS' | head -6 |
            awk -v f="$frames" '{c=$1; gsub(",", "", c); pct=$2; $1=""; $2=""; sub(/^ +/, ""); n=split($0, a, ":"); name=a[n]; sub(/ \[.*/, "", name); printf "    %9d  %7s  %s\n", c / f, pct, substr(name, 1, 90)}'
    done
}

# Builds perf_baseline and perf_variant (extra flags in $1, e.g. -g) from the
# real androidbk.cpp and from a patched scratch copy of it. app/src/main is
# mounted read-only and never touched.
build_variant_pair() {
    local extra="${1:-}"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    local V=$OUT/variant
    rm -rf $V && mkdir -p $V/Utils/imgui_androidbk && ln -s $SRC/include $V/include
    cp $SRC/Utils/imgui_androidbk/androidbk.h $V/Utils/imgui_androidbk/
    awk '
        /#include "androidbk.h"/ { print; print "#include \"variant_skip_unchanged.h\""; next }
        !done && /glViewport\(0, 0, \(int\)io\.DisplaySize\.x/ {
            print "        if (perf_variant::frame_changed(ImGui::GetDrawData())) {"
            print "            ++perf_variant::g_presented;"
            inblock = 1
        }
        inblock {
            print "    " $0
            if ($0 ~ /eglSwapBuffers\(g_EglDisplay, g_EglSurface\);/) {
                print "        } else {"; print "            perf_variant::wait_next_vsync();"; print "        }"
                inblock = 0; done = 1
            }
            next
        }
        { print }' $SRC/Utils/imgui_androidbk/androidbk.cpp > $V/Utils/imgui_androidbk/androidbk.cpp
    grep -q 'perf_variant::wait_next_vsync' $V/Utils/imgui_androidbk/androidbk.cpp || { echo "variant patch did not apply"; return 1; }
    local common="$SRC/scroll.cpp $IMGUI_SRC $SRC/Core/Canvas/Canvas.cpp $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp $P/harness/perf_render.cpp"
    $CXX $extra -DFAKE_PLATFORM_NO_CANVAS_MENU $INC $SRC/Utils/imgui_androidbk/androidbk.cpp $common -o perf_baseline -pthread || return 1
    # VARIANT_CHANGE=memcmp (default; compare with the previous frame) or hash (byte-wise FNV, run 7)
    local vflag=-DVARIANT_MEMCMP; [ "${VARIANT_CHANGE:-memcmp}" = hash ] && vflag=
    echo "variant change test: ${VARIANT_CHANGE:-memcmp}"
    $CXX $extra -DFAKE_PLATFORM_NO_CANVAS_MENU $vflag -I$P/harness $INC $V/Utils/imgui_androidbk/androidbk.cpp $common -o perf_variant -pthread || return 1
}

stage_variant_instr() {
    banner "variant_instr: instructions per frame, baseline vs DESIGN VARIANT (callgrind; independent of load)"
    command -v valgrind > /dev/null || { echo "valgrind missing: run in canvas-perf-harness:local (Dockerfile.perf)"; return 1; }
    build_variant_pair -g || return 1
    local s b dump total frames
    for s in "menu 5" "hidden 5" "hidden 0"; do
        set -- $s
        for b in baseline variant; do
            rm -f cgv_${b}_$1_$2.out*
            valgrind --tool=callgrind --callgrind-out-file=cgv_${b}_$1_$2.out ./perf_$b $1 $2 ${INSTR_FRAMES:-300} \
                > instrv_${b}_$1_$2.log 2>&1
            dump=cgv_${b}_$1_$2.out.1
            total=$(callgrind_annotate "$dump" 2> /dev/null | awk '/PROGRAM TOTALS/ {gsub(",", "", $1); print $1}')
            frames=$(grep -o 'callgrind_frames=[0-9]*' instrv_${b}_$1_$2.log | cut -d= -f2)
            if [ -z "$total" ] || [ -z "$frames" ] || [ "$frames" -eq 0 ]; then echo "$b $1 $2: no data"; continue; fi
            printf '%-9s %-8s %2s mods: %9d instructions/frame\n' "$b" "$1" "$2" $((total / frames))
        done
    done
}

stage_readout_instr() {
    banner "readout_instr: instructions per presented and per skipped frame, menu open, 5 mods (callgrind; independent of load and core type)"
    command -v valgrind > /dev/null || { echo "valgrind missing: run in canvas-perf-harness:local (Dockerfile.perf)"; return 1; }
    build_variant_pair -g && build_readout -g || return 1
    # Under valgrind frames are slow, so the share of frames that present is
    # not the 60 Hz share. The two variant builds present different shares;
    # assuming a presented frame and a skipped frame cost the same in both,
    # their totals give the two per-frame costs, which are then weighted by
    # the 60 Hz mix measured in run 16. The driver's own analysis is off.
    local b dump total frames wf wp
    : > readout_instr.tsv
    for b in baseline variant variant_readout; do
        rm -f cgr_$b.out*
        PERF_NO_ANALYSIS=1 valgrind --tool=callgrind --callgrind-out-file=cgr_$b.out ./perf_$b menu 5 ${INSTR_FRAMES:-300} \
            > instrr_$b.log 2>&1
        dump=cgr_$b.out.1
        total=$(callgrind_annotate "$dump" 2> /dev/null | awk '/PROGRAM TOTALS/ {gsub(",", "", $1); print $1}')
        frames=$(grep -o 'callgrind_frames=[0-9]*' instrr_$b.log | cut -d= -f2)
        wf=$(grep -o 'window: frames=[0-9]*' instrr_$b.log | cut -d= -f2)
        wp=$(grep -o 'window: frames=[0-9]* presented=[0-9]*' instrr_$b.log | sed 's/.*presented=//')
        if [ -z "$total" ] || [ -z "$frames" ] || [ -z "$wf" ] || [ -z "$wp" ]; then echo "$b: no data"; continue; fi
        printf '%s\t%s\t%s\t%s\t%s\n' "$b" "$total" "$frames" "$wf" "$wp" >> readout_instr.tsv
        printf '%-16s %10d instructions/frame over %s frames; %s of %s window frames presented\n' \
            "$b" $((total / frames)) "$frames" "$wp" "$wf"
    done
    awk -F'\t' -v F=${MIX_FPS:-59.1} -v Pv=${MIX_VARIANT:-33.3} -v Pr=${MIX_READOUT:-1.95} '
        $1 == "baseline" { ab = $2 / $3 }
        $1 == "variant" { a1 = $2 / $3; f1 = $5 / $4 }
        $1 == "variant_readout" { a2 = $2 / $3; f2 = $5 / $4 }
        END {
            if (f1 == f2) { print "cannot split: both builds presented the same share"; exit }
            d = (a1 - a2) / (f1 - f2); is = a1 - f1 * d; ip = is + d
            printf "per presented frame %.0f, per skipped frame %.0f instructions (present shares %.3f / %.3f)\n", ip, is, f1, f2
            printf "at %.1f frames/s (run 16 mix): baseline %.1fM, variant %.1fM (%.1f presents/s), variant_readout %.1fM (%.2f presents/s) instructions/s\n",
                F, ab * F / 1e6, (Pv * ip + (F - Pv) * is) / 1e6, Pv, (Pr * ip + (F - Pr) * is) / 1e6, Pr
        }' readout_instr.tsv
}

stage_limiter() {
    banner "limiter: \"Limit FPS\" pacing on two display models; Canvas's 41 ms sleep vs DESIGN VARIANTS (one-vsync wait; eglSwapInterval(2))"
    mkdir -p /system/fonts
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/Roboto-Regular.ttf
    cp $REPO/main/res/font/lato_regular.ttf /system/fonts/NotoSansGeorgian-VF.ttf
    # Patched scratch copies of androidbk.cpp: the "Limit FPS" line is replaced.
    patch_limiter() {  # $1 = dir, $2 = header, $3 = replacement statement
        rm -rf $1 && mkdir -p $1/Utils/imgui_androidbk && ln -s $SRC/include $1/include
        cp $SRC/Utils/imgui_androidbk/androidbk.h $1/Utils/imgui_androidbk/
        awk -v hdr="$2" -v stmt="$3" '
            /#include "androidbk.h"/ { print; print "#include \"" hdr "\""; next }
            /if\(Canvas::frameRateLimited\) usleep\(41000\);/ { print "        " stmt "  // DESIGN VARIANT"; done = 1; next }
            { print }
            END { if (!done) exit 1 }' $SRC/Utils/imgui_androidbk/androidbk.cpp > $1/Utils/imgui_androidbk/androidbk.cpp
    }
    patch_limiter $OUT/limiter variant_vsync_limiter.h "if(Canvas::frameRateLimited) perf_limiter::wait_one_vsync();" ||
        { echo "limiter patch did not apply"; return 1; }
    patch_limiter $OUT/limiter_si2 variant_swap_interval.h "perf_limiter::apply_swap_interval(g_EglDisplay, Canvas::frameRateLimited);" ||
        { echo "swap-interval patch did not apply"; return 1; }
    local common="$SRC/scroll.cpp $IMGUI_SRC $SRC/Core/Canvas/Canvas.cpp $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp $P/harness/perf_render.cpp"
    $CXX -DFAKE_PLATFORM_NO_CANVAS_MENU $INC $SRC/Utils/imgui_androidbk/androidbk.cpp $common -o perf_limit_canvas -pthread || return 1
    $CXX -DFAKE_PLATFORM_NO_CANVAS_MENU -I$P/harness $INC $OUT/limiter/Utils/imgui_androidbk/androidbk.cpp $common -o perf_limit_variant -pthread || return 1
    $CXX -DFAKE_PLATFORM_NO_CANVAS_MENU -I$P/harness $INC $OUT/limiter_si2/Utils/imgui_androidbk/androidbk.cpp $common -o perf_limit_si2 -pthread || return 1
    environment
    local model b
    # blocking: eglSwapBuffers returns at the next vsync. queued: it returns at
    # once and the buffer is latched at the next free vsync (non-async BufferQueue).
    for model in blocking queued; do
        for b in canvas variant si2; do
            echo "-- model=$model build=$b"
            if [ $model = queued ]; then export PERF_SWAP_QUEUED=1; unset PERF_VSYNC_ALIGNED; else export PERF_VSYNC_ALIGNED=1; unset PERF_SWAP_QUEUED; fi
            ./perf_limit_$b limited 5 ${FRAMES:-300} 2>&1 | grep -v '^\[' |
                grep -E "scenario=|per second|present gaps" |
                sed -E 's/ \| (stages_p50_us|wall_us|gl draw_lists|unchanged_frames|callgrind_frames)[^|]*//g' | cut -c1-220
        done
    done
    unset PERF_SWAP_QUEUED PERF_VSYNC_ALIGNED
    echo "load after: $(cut -d' ' -f1-3 /proc/loadavg)"
}

stage_variant() {
    banner "variant: skip clear/submit/present when the frame is unchanged (DESIGN VARIANT; frames 16.7 ms apart)"
    build_variant_pair || return 1
    environment
    # Interleaved rounds (A,B then B,A ...) pinned to one CPU, so both builds
    # see the same drift; medians and min-max over rounds are reported.
    local rounds=${ROUNDS:-4} s b r order log
    # MODEL=queued: the queued-swap display model (run 13) instead of a 16.7 ms sleep per swap.
    local model_env=PERF_VSYNC=1; [ "${MODEL:-sleep}" = queued ] && model_env=PERF_SWAP_QUEUED=1
    echo "display model: ${MODEL:-sleep}"
    : > variant_rounds.tsv
    for s in "menu 5" "hidden 5" "hidden 0"; do
        set -- $s
        for r in $(seq 1 $rounds); do
            order="baseline variant"; [ $((r % 2)) -eq 0 ] && order="variant baseline"
            for b in $order; do
                log=vr_${b}_$1_$2_$r.log
                env $model_env taskset -c 2 ./perf_$b $1 $2 ${FRAMES:-480} > $log 2>&1
                printf '%s %s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$b" \
                    "$(grep -o 'render-thread CPU=[0-9.]*' $log | cut -d= -f2)" \
                    "$(grep -o 'presents=[0-9.]*' $log | cut -d= -f2)" \
                    "$(grep -o 'cpu_us p50=[0-9.]*' $log | cut -d= -f2)" >> variant_rounds.tsv
            done
        done
    done
    echo "scenario   build     | CPU ms/s median [min-max] | presents/s median | frame CPU p50 us median"
    local key col
    for key in "menu 5	baseline" "menu 5	variant" "hidden 5	baseline" "hidden 5	variant" "hidden 0	baseline" "hidden 0	variant"; do
        stats() { grep -F "$key" variant_rounds.tsv | cut -f$1 | sort -n | awk '{v[NR]=$1} END {m=(NR%2)?v[(NR+1)/2]:(v[NR/2]+v[NR/2+1])/2; printf "%.2f [%.2f-%.2f]", m, v[1], v[NR]}'; }
        printf '%-20s | %s | %s | %s\n' "$(echo "$key" | tr '\t' ' ')" "$(stats 3)" "$(stats 4)" "$(stats 5)"
    done
    echo "load after: $(cut -d' ' -f1-3 /proc/loadavg)"
}

# Builds perf_variant_readout (extra flags in $1, e.g. -g): the skip-unchanged
# variant plus a patched scratch copy of the CanvasMenu extraction.
build_readout() {
    local extra="${1:-}"
    # Patched scratch copy of the CanvasMenu extraction: the FPS text reads a
    # value latched every 0.5 s. -I$OUT/readout precedes -I$GEN, so this copy
    # wins the #include "CanvasMenu.inc" lookup; $GEN/CanvasMenu.inc is untouched.
    local R=$OUT/readout
    rm -rf $R && mkdir -p $R
    { echo '#include "variant_readout_latch.h"'
      awk '/Application average %\.3f ms\/frame/ {t = 1}
           t && /ImGui::GetIO\(\)\.Framerate/ {gsub(/ImGui::GetIO\(\)\.Framerate/, "perf_readout::latched()")}
           t && /\);/ {t = 0}
           {print}' $GEN/CanvasMenu.inc; } > $R/CanvasMenu.inc
    [ "$(grep -c 'perf_readout::latched()' $R/CanvasMenu.inc)" -eq 2 ] || { echo "readout patch did not apply"; return 1; }
    local common="$SRC/scroll.cpp $IMGUI_SRC $SRC/Core/Canvas/Canvas.cpp $H/harness/app/fake_platform.cpp $H/harness/app/fake_jni.cpp $P/harness/perf_render.cpp"
    $CXX $extra -DFAKE_PLATFORM_NO_CANVAS_MENU -DVARIANT_MEMCMP -I$P/harness -I$R $INC $OUT/variant/Utils/imgui_androidbk/androidbk.cpp $common \
        -o perf_variant_readout -pthread || return 1
}

stage_readout() {
    banner "readout: menu open, skip-unchanged DESIGN VARIANT with Canvas's per-frame FPS text vs a readout latched twice a second"
    build_variant_pair && build_readout || return 1
    echo "built perf_baseline, perf_variant, perf_variant_readout"
    [ "${BUILD_ONLY:-0}" = 1 ] && return 0
    environment
    # Interleaved rounds pinned to one CPU, as in stage_variant. Presents per
    # second is the result; CPU is reported for completeness.
    local rounds=${ROUNDS:-4} r b order log
    : > readout_rounds.tsv
    for r in $(seq 1 $rounds); do
        order="baseline variant variant_readout"; [ $((r % 2)) -eq 0 ] && order="variant_readout variant baseline"
        for b in $order; do
            log=ro_${b}_$r.log
            PERF_VSYNC=1 taskset -c 2 ./perf_$b menu 5 ${FRAMES:-480} > $log 2>&1
            printf '%s\t%s\t%s\t%s\n' "$b" \
                "$(grep -o 'presents=[0-9.]*' $log | cut -d= -f2)" \
                "$(grep -o 'render-thread CPU=[0-9.]*' $log | cut -d= -f2)" \
                "$(grep -o 'cpu_us p50=[0-9.]*' $log | cut -d= -f2)" >> readout_rounds.tsv
        done
    done
    echo "menu 5 mods, build | presents/s median [min-max] | CPU ms/s median [min-max] | frame CPU p50 us median [min-max]"
    local key
    for key in baseline variant variant_readout; do
        stats() { awk -F'\t' -v k="$key" '$1 == k' readout_rounds.tsv | cut -f$1 | sort -n | awk '{v[NR]=$1} END {m=(NR%2)?v[(NR+1)/2]:(v[NR/2]+v[NR/2+1])/2; printf "%.2f [%.2f-%.2f]", m, v[1], v[NR]}'; }
        printf '%-16s | %s | %s | %s\n' "$key" "$(stats 2)" "$(stats 3)" "$(stats 4)"
    done
    echo "load after: $(cut -d' ' -f1-3 /proc/loadavg)"
}

ALL="frame instr variant variant_instr limiter readout readout_instr taps taplatency"
generate || exit 1
for s in ${*:-$ALL}; do
    if declare -f "stage_$s" > /dev/null; then "stage_$s"; else echo "unknown stage: $s (have: $ALL)"; fi
done
