#!/usr/bin/env bash
# Unattended device run for the Canvas perf-diag APK. See DEVICE.md.
# Phases (PHASES, space separated): matrix thermal profile sustained
set -u
export MSYS_NO_PATHCONV=1
HERE="$(cd "$(dirname "$0")" && pwd)"
APK=${APK:-/c/Users/admin/cpd/app/build/outputs/apk/debug/app-debug.apk}
PKG=${PKG:-git.artdeell.skymodloader.mousefix}
SKY=${SKY:-com.tgc.sky.android}
PHASES=${PHASES:-"matrix thermal profile sustained"}
ROUNDS=${ROUNDS:-2}
WARMUP=${WARMUP:-75}
MEASURE=${MEASURE:-60}
COOL_MAX=${COOL_MAX:-150}
THERMAL_SEG=${THERMAL_SEG:-60}
SUSTAIN=${SUSTAIN:-600}
VANILLA=${VANILLA:-1}
INSTALL=${INSTALL:-1}
ONLY=${ONLY:-}
SUSTAIN_LIST=${SUSTAIN_LIST:-"base nooverlay"}
RETRIES=${RETRIES:-2}
FAIL_STREAK_MAX=${FAIL_STREAK_MAX:-3}
DEVICE_WAIT=${DEVICE_WAIT:-1800}
OUT=${OUT:-$HERE/out/$(date +%Y%m%d-%H%M%S)}
DEV=/data/local/tmp/perfdiag

BASE="overlay=1,menu=shown,limit=off,skip=0,latch=0,cjk=1,thermal=frozen"
declare -A CFG=(
  [base]="$BASE"
  [limit]="$BASE,limit=canvas"
  [pace30]="$BASE,limit=pace30"
  [skip]="$BASE,skip=1"
  [skiplatch]="$BASE,skip=1,latch=1"
  [hidden]="$BASE,menu=hidden"
  [hiddenskip]="$BASE,menu=hidden,skip=1"
  [nooverlay]="$BASE,overlay=0"
  [nocjk]="$BASE,cjk=0"
  [thermallive]="$BASE,thermal=live"
  [prio]="$BASE,prio=low"
  [nodepth]="$BASE,depth=0"
  [damage]="$BASE,damage=1"
  [lean]="$BASE,prio=low,depth=0,damage=1"
  [little]="$BASE,cpus=little"
  [nice10]="$BASE,nice=10"
  [pace30pt]="$BASE,limit=pace30,ptime=2"
  [after]="$BASE,limit=after"
  [afterfence]="$BASE,limit=afterfence"
  [afterskip]="$BASE,limit=afterfence,skip=1,latch=1"
  [game]="$BASE,limit=game"
  [prod]="$BASE,limit=game,skip=1,latch=1"
  [lag4]="$BASE,limit=game,lag=4"
  [lag8]="$BASE,limit=game,lag=8"
  [lag16]="$BASE,limit=game,lag=16"
  [lag24]="$BASE,limit=game,lag=24"
  [heavybase]="$BASE,heavy=1"
  [heavyprod]="$BASE,limit=game,skip=1,latch=1,heavy=1"
)
MATRIX=(base limit pace30 skip skiplatch hidden hiddenskip nooverlay nocjk)
[ "$VANILLA" = 1 ] && MATRIX+=(vanilla)
[ -n "$ONLY" ] && read -r -a MATRIX <<< "$ONLY"

# The real adb.exe, not a launcher shim: killing a shim leaves its adb child running.
ADB_BIN=$(adb version 2> /dev/null | sed -n 's/^Installed as //p' | tr -d '\r')
[ -n "$ADB_BIN" ] && command -v cygpath > /dev/null && ADB_BIN=$(cygpath -u "$ADB_BIN")
[ -n "$ADB_BIN" ] || ADB_BIN=adb
if [ -z "${SERIAL:-}" ]; then
  SERIAL=$("$ADB_BIN" devices | awk 'NR > 1 && $2 == "device" { print $1 }' | head -1)
fi
[ -n "$SERIAL" ] || { echo "no device in 'adb devices' (USB debugging authorised?)"; exit 1; }
A() { timeout "${ADB_TO:-120}" "$ADB_BIN" -s "$SERIAL" "$@"; }
S() { A shell "$@" | tr -d '\r'; }
W() { if command -v cygpath > /dev/null; then cygpath -w "$1"; else echo "$1"; fi; }
log() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$OUT/run.log"; }

mkdir -p "$OUT"
echo $$ > "$OUT/runner.pid"
log "device $SERIAL -> $OUT"
ROOT=0
[ "$(S "su -c id" 2>/dev/null | grep -c 'uid=0')" -ge 1 ] && ROOT=1
log "root: $ROOT"
R() { if [ "$ROOT" = 1 ]; then S "su -c '$*'"; else S "$*"; fi; }

{
  for p in ro.product.manufacturer ro.product.model ro.build.display.id ro.build.version.release ro.build.version.sdk; do
    echo "$p=$(S getprop $p)"
  done
  S "wm size; wm density"
  S "settings get system peak_refresh_rate; settings get system min_refresh_rate"
  S "dumpsys battery"
  S "dumpsys thermalservice | head -40"
  S "pm list packages --show-versioncode $SKY; pm list packages --show-versioncode $PKG"
} > "$OUT/device.txt" 2>&1

if [ "$INSTALL" = 1 ] && [ ! -f "$OUT/installed" ]; then
  [ -f "$APK" ] || { log "APK not found: $APK"; exit 1; }
  log "installing $(basename "$APK")"
  res=$(A install -r -d "$(W "$APK")" 2>&1 | tr -d '\r' | tail -2)
  log "install: $res"
  case "$res" in *Success*) date -Is > "$OUT/installed" ;; *) log "install failed; not uninstalling anything (that would delete the app's data). Stopping."; exit 1 ;; esac
fi
S "pm path $SKY" | grep -q package: || { log "$SKY not installed"; exit 1; }

# The settings found on the first start are kept in OUT, so a resumed run restores
# those rather than the test values a crashed run left behind.
ORIG="$OUT/settings.orig"
if [ -f "$ORIG" ]; then
  log "resuming in $OUT"
else
  {
    echo "OLD_STAYON=$(S settings get global stay_on_while_plugged_in)"
    echo "OLD_BMODE=$(S settings get system screen_brightness_mode)"
    echo "OLD_BRIGHT=$(S settings get system screen_brightness)"
    echo "OLD_TIMEOUT=$(S settings get system screen_off_timeout)"
  } > "$ORIG"
fi
. "$ORIG"
put_setting() { if [ "$3" = null ] || [ -z "$3" ]; then S "settings delete $1 $2" > /dev/null; else S "settings put $1 $2 $3"; fi; }
restore() {
  log "restoring device settings"
  S "cmd thermalservice reset" > /dev/null 2>&1
  S "am force-stop $PKG; am force-stop $SKY"
  put_setting global stay_on_while_plugged_in "$OLD_STAYON"
  put_setting system screen_brightness_mode "$OLD_BMODE"
  put_setting system screen_brightness "$OLD_BRIGHT"
  put_setting system screen_off_timeout "$OLD_TIMEOUT"
  R "pkill -f $DEV/sampler.sh" > /dev/null 2>&1
  R "rm -rf $DEV"
  stop_logcat
}
trap restore EXIT
trap 'exit 130' INT TERM
prepare_device() {
  S "settings put global stay_on_while_plugged_in 7; settings put system screen_brightness_mode 0; settings put system screen_brightness 120; settings put system screen_off_timeout 1800000"
  S "cmd thermalservice reset" > /dev/null 2>&1
  S "input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard"
  S "pkill -f 'logcat -v epoch CanvasPerf'" > /dev/null 2>&1
  R "pkill -f $DEV/sampler.sh" > /dev/null 2>&1
  R "rm -rf $DEV; mkdir -p $DEV; chmod 777 $DEV"
  A push "$(W "$HERE/sampler.sh")" /data/local/tmp/perfdiag_sampler.sh > /dev/null
  A push "$(W "$HERE/title.sh")" /data/local/tmp/perfdiag_title.sh > /dev/null
  R "mv /data/local/tmp/perfdiag_sampler.sh $DEV/sampler.sh; mv /data/local/tmp/perfdiag_title.sh $DEV/title.sh; chmod 755 $DEV/sampler.sh $DEV/title.sh"
}
prepare_device
SCREEN=$(S "wm size" | grep -oE "[0-9]+x[0-9]+" | tail -1)
SW=$(echo "$SCREEN" | cut -dx -f1); SH=$(echo "$SCREEN" | cut -dx -f2)
[ "$SW" -lt "$SH" ] && { t=$SW; SW=$SH; SH=$t; }
PLAY_X=$((SW * 5017 / 10000)); PLAY_Y=$((SH * 4796 / 10000))

batt_temp() { S "dumpsys battery" | awk -F': ' '/temperature/ { print $2; exit }'; }
T_BASE=$(batt_temp)
log "battery temperature at start: $T_BASE (tenths of a degree C)"

cooldown() {
  local t waited=0
  while :; do
    t=$(batt_temp)
    [ -z "$t" ] && break
    [ "$t" -le $((T_BASE + 10)) ] && break
    [ "$waited" -ge "$COOL_MAX" ] && { log "  cooldown capped at ${COOL_MAX}s (battery $t)" >&2; break; }
    sleep 10; waited=$((waited + 10))
  done
  echo "$t"
}

# Ending logcat on the phone closes the stream, so adb exits on the PC; killing the
# backgrounded shell alone leaves adb.exe (and any launcher shim) running.
stop_logcat() {
  S "pkill -f 'logcat -v epoch CanvasPerf'" > /dev/null 2>&1
  [ -n "${LOGCAT_PID:-}" ] && { kill "$LOGCAT_PID" 2> /dev/null; wait "$LOGCAT_PID" 2> /dev/null; }
  LOGCAT_PID=
}

app_pid() { S "dumpsys activity processes $1" | grep -m1 -oE "ProcessRecord[{][0-9a-f]+ [0-9]+:" | grep -oE "[0-9]+:$" | tr -d :; }

app_of() { [ "$1" = vanilla ] && echo "$SKY" || echo "$PKG"; }

launch() {
  local name=$1 app
  app=$(app_of "$name")
  if [ "$name" = vanilla ]; then
    S "monkey -p $SKY -c android.intent.category.LAUNCHER 1" > /dev/null 2>&1
  else
    S "am start -n $PKG/git.artdeell.skymodloader.elfmod.ModManagerActivity --es perfcfg 'tag=$name,${CFG[$name]}'" > /dev/null
  fi
  local i resumed=0
  for i in $(seq 1 60); do
    if S "dumpsys activity activities" | grep -E 'ResumedActivity' | grep -q "$app/com.tgc.sky.GameActivity"; then resumed=1; break; fi
    sleep 1
  done
  [ "$resumed" = 1 ] || return 1
  press_play
  return 0
}

# Sky stops at its title screen; press Play only when the screen really shows it.
press_play() {
  local i taps=0
  for i in $(seq 1 ${TITLE_MAX:-60}); do
    if [ "$(R "sh $DEV/title.sh $DEV/s.raw 2>/dev/null")" = title ]; then
      S "input tap $PLAY_X $PLAY_Y"; taps=$((taps + 1))
      sleep 4
      [ "$(R "sh $DEV/title.sh $DEV/s.raw 2>/dev/null")" = title ] || { log "  pressed Play ($taps tap(s))"; return 0; }
      [ "$taps" -ge 3 ] && { log "  Play still showing after 3 taps"; return 0; }
    fi
    sleep 2
  done
  log "  title screen not seen within $((${TITLE_MAX:-60} * 2)) s"
}

# run_one <name> <dir> <measure_s> [thermal]
run_one() {
  local name=$1 dir=$2 measure=$3 mode=${4:-} app t0 t1
  app=$(app_of "$name")
  mkdir -p "$dir"
  S "am force-stop $PKG; am force-stop $SKY"
  sleep 2
  t0=$(cooldown)
  A logcat -c
  ADB_TO=0 A logcat -v epoch CanvasPerf:I AndroidRuntime:E DEBUG:I libc:F '*:S' > "$dir/logcat.txt" 2>&1 &
  LOGCAT_PID=$!
  echo "name=$name app=$app cfg=${CFG[$name]:-} warmup=$WARMUP measure=$measure mode=$mode battery_start=$t0 started=$(date -Is)" > "$dir/run.txt"
  if ! launch "$name"; then
    log "  $name: GameActivity did not resume within 60 s"
    A exec-out screencap -p > "$dir/screen_fail.png"
    echo "launch_failed=1" >> "$dir/run.txt"
  fi
  sleep "$WARMUP"
  A exec-out screencap -p > "$dir/screen_start.png"
  if [ "$mode" = thermal ]; then
    S "cmd thermalservice override-status 0" > "$dir/override.txt" 2>&1
    ADB_TO=$((measure + 120)) R "sh $DEV/sampler.sh $measure $DEV/s0 $app 0.5"
    S "cmd thermalservice override-status 3" >> "$dir/override.txt" 2>&1
    echo "override_switched=$(date -Is)" >> "$dir/run.txt"
    ADB_TO=$((measure + 120)) R "sh $DEV/sampler.sh $measure $DEV/s1 $app 0.5"
    A exec-out screencap -p > "$dir/screen_severe.png"
    S "cmd thermalservice reset" >> "$dir/override.txt" 2>&1
  else
    ADB_TO=$((measure + 120)) R "sh $DEV/sampler.sh $measure $DEV/s0 $app 0.5"
  fi
  A exec-out screencap -p > "$dir/screen_end.png"
  S "dumpsys SurfaceFlinger" > "$dir/sf.txt" 2>&1
  S "dumpsys meminfo $(app_pid "$app")" > "$dir/meminfo.txt" 2>&1
  S "dumpsys thermalservice" > "$dir/thermal.txt" 2>&1
  echo "pid_end=$(app_pid "$app")" >> "$dir/run.txt"
  t1=$(batt_temp)
  echo "battery_end=$t1 ended=$(date -Is)" >> "$dir/run.txt"
  R "chmod -R 755 $DEV"
  for s in s0 s1; do
    if [ "$(S "[ -d $DEV/$s ] && echo yes")" = yes ]; then
      mkdir -p "$dir/$s"
      for f in $(S "ls $DEV/$s"); do A pull "$DEV/$s/$f" "$(W "$dir/$s/$f")" > /dev/null; done
    fi
  done
  R "rm -rf $DEV/s0 $DEV/s1"
  stop_logcat
  S "am force-stop $PKG; am force-stop $SKY"
  grep -q -E 'FATAL EXCEPTION|Fatal signal' "$dir/logcat.txt" && log "  $name: crash in logcat (see $dir/logcat.txt)"
  log "  $name done (battery $t0 -> $t1)"
}

wait_device() {
  local waited=0
  until [ "$(A get-state 2> /dev/null | tr -d '\r')" = device ]; do
    [ "$waited" = 0 ] && log "  waiting for $SERIAL (up to ${DEVICE_WAIT}s)"
    [ "$waited" -ge "$DEVICE_WAIT" ] && { log "device did not come back; stopping (run again with the same OUT to resume)"; exit 3; }
    sleep 10; waited=$((waited + 10))
  done
  if [ "$waited" -gt 0 ]; then
    log "  device back after ${waited}s"
    ROOT=0; [ "$(S "su -c id" 2> /dev/null | grep -c 'uid=0')" -ge 1 ] && ROOT=1
    prepare_device
  fi
}

# Prints why a run's data cannot be used; prints nothing for a good run.
check_run() {
  local dir=$1 measure=$2 mode=$3 s n need p0 p1
  grep -q "launch_failed=1" "$dir/run.txt" 2> /dev/null && { echo "launch failed"; return; }
  for s in s0 $([ "$mode" = thermal ] && echo s1); do
    [ -s "$dir/$s/samples.txt" ] || { echo "no samples in $s"; return; }
    [ -s "$dir/$s/layers.txt" ] || { echo "no app layers in $s"; return; }
    n=$(grep -c '^T ' "$dir/$s/samples.txt"); need=$((measure * 2 / 3))
    [ "$n" -ge "$need" ] || { echo "$s has $n samples, expected at least $need"; return; }
  done
  p0=$(tr -d '\r\n ' < "$dir/s0/pid.txt" 2> /dev/null)
  p1=$(sed -n 's/^pid_end=\([0-9]*\).*/\1/p' "$dir/run.txt")
  [ -n "$p0" ] && [ "$p0" = "$p1" ] || echo "game process died or restarted (start ${p0:-none}, end ${p1:-none})"
}

FAIL_STREAK=0
set_aside() { [ -d "$1" ] || return 0; mkdir -p "$OUT/failed"; mv "$1" "$OUT/failed/$(echo "${1#"$OUT"/}" | tr / _)-$(date +%H%M%S)"; }

# attempt <name> <dir> <measure_s> [thermal]: run_one with checks, retries and a done marker.
attempt() {
  local name=$1 dir=$2 measure=$3 mode=${4:-} try why
  [ -f "$dir/done" ] && { log "  $name: done earlier, skipping"; return 0; }
  for try in $(seq 1 $((RETRIES + 1))); do
    wait_device
    set_aside "$dir"
    run_one "$name" "$dir" "$measure" "$mode"
    why=$(check_run "$dir" "$measure" "$mode")
    if [ -z "$why" ]; then date -Is > "$dir/done"; FAIL_STREAK=0; return 0; fi
    log "  $name: attempt $try unusable: $why"
  done
  set_aside "$dir"
  FAIL_STREAK=$((FAIL_STREAK + 1))
  log "  $name: giving up after $((RETRIES + 1)) attempts (kept in failed/)"
  [ "$FAIL_STREAK" -ge "$FAIL_STREAK_MAX" ] && { log "$FAIL_STREAK runs in a row failed; stopping (run again with the same OUT to resume)"; exit 4; }
  return 1
}

profile_run() {
  local dir=$OUT/profile name=base pid tid
  [ -f "$dir/done" ] && { log "  profile: done earlier, skipping"; return 0; }
  wait_device
  mkdir -p "$dir"
  S "am force-stop $PKG; am force-stop $SKY"; sleep 2; cooldown > /dev/null
  launch base || log "  profile: GameActivity did not resume"
  sleep "$WARMUP"
  pid=$(S "dumpsys activity processes $PKG" | grep -m1 -oE "ProcessRecord[{][0-9a-f]+ [0-9]+:" | grep -oE "[0-9]+:$" | tr -d :)
  tid=$(R "grep -l '^imgui dispatch' /proc/$pid/task/*/comm" | head -1 | awk -F/ '{ print $5 }')
  log "  profile: pid=$pid imgui tid=$tid"
  if [ "$ROOT" = 1 ]; then
    R "simpleperf record -p $pid -e cpu-clock -f 2000 --duration 20 -o $DEV/proc.data" > "$dir/record_proc.txt" 2>&1
    R "simpleperf report -i $DEV/proc.data --sort comm --percent-limit 0.2" > "$dir/report_threads.txt" 2>&1
    R "simpleperf report -i $DEV/proc.data --sort comm,dso --percent-limit 0.2" > "$dir/report_thread_dso.txt" 2>&1
    if [ -n "$tid" ]; then
      R "simpleperf record -t $tid -e cpu-clock -f 4000 --duration 20 -o $DEV/imgui.data" > "$dir/record_imgui.txt" 2>&1
      R "simpleperf report -i $DEV/imgui.data --sort dso,symbol --percent-limit 0.3" > "$dir/report_imgui.txt" 2>&1
    fi
  else
    S "simpleperf record --app $PKG -e cpu-clock -f 2000 --duration 20 -o /data/local/tmp/proc.data" > "$dir/record_proc.txt" 2>&1
    S "simpleperf report -i /data/local/tmp/proc.data --sort comm --percent-limit 0.2" > "$dir/report_threads.txt" 2>&1
  fi
  S "am force-stop $PKG"
  date -Is > "$dir/done"
  log "  profile done"
}

for phase in $PHASES; do
  case $phase in
    matrix)
      for r in $(seq 1 "$ROUNDS"); do
        order=("${MATRIX[@]}")
        if [ $((r % 2)) -eq 0 ]; then
          order=(); for ((i = ${#MATRIX[@]} - 1; i >= 0; i--)); do order+=("${MATRIX[$i]}"); done
        fi
        log "matrix round $r: ${order[*]}"
        for v in "${order[@]}"; do attempt "$v" "$OUT/matrix/r$r-$v" "$MEASURE"; done
      done ;;
    thermal)
      log "thermal phase: override NONE then SEVERE, ${THERMAL_SEG}s each"
      tl=(base thermallive); [ "$VANILLA" = 1 ] && tl+=(vanilla)
      for v in "${tl[@]}"; do attempt "$v" "$OUT/thermal/$v" "$THERMAL_SEG" thermal; done ;;
    profile)
      log "profile phase (simpleperf)"; profile_run ;;
    sustained)
      [ "$SUSTAIN" -gt 0 ] || continue
      log "sustained phase: $SUSTAIN_LIST, ${SUSTAIN}s each"
      i=0; for v in $SUSTAIN_LIST; do i=$((i + 1)); attempt "$v" "$OUT/sustained/r$i-$v" "$SUSTAIN"; done ;;
  esac
done
log "all phases done; summarising"
node "$(W "$HERE/summarize.js")" "$(W "$OUT")" | tee -a "$OUT/run.log"
node "$(W "$HERE/phase.js")" "$(W "$OUT")" > "$OUT/phase.txt" 2>&1
