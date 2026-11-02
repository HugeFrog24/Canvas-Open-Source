#!/usr/bin/env bash
# Runs run_device.sh to the end without anyone watching. After a crash, a lost
# device or a run of failures it starts run_device.sh again in the same OUT, which
# skips finished runs and retries the interrupted one. On Windows it also keeps the
# PC from sleeping until it exits. Takes the same environment variables as
# run_device.sh; to resume by hand, run it again with the same OUT.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
export OUT=${OUT:-$HERE/out/$(date +%Y%m%d-%H%M%S)}
MAX_RESTARTS=${MAX_RESTARTS:-6}
RESTART_WAIT=${RESTART_WAIT:-120}
mkdir -p "$OUT"
note() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$OUT/run.log"; }

keep_awake() {
  command -v powershell.exe > /dev/null && [ -r /proc/$$/winpid ] || return 0
  local ps
  ps=$(cat <<'PS'
$k = Add-Type -Name Awake -Namespace PerfDiag -PassThru -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);'
[void]$k::SetThreadExecutionState([uint32]2147483649)
while (Get-Process -Id WINPID -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 30 }
PS
)
  ps=${ps//WINPID/$(cat /proc/$$/winpid)}
  powershell.exe -NoProfile -NonInteractive -EncodedCommand "$(printf '%s' "$ps" | iconv -f UTF-8 -t UTF-16LE | base64 -w0)" > /dev/null 2>&1 &
  note "keeping the PC awake while this runs"
}

note "unattended run in $OUT (up to $MAX_RESTARTS restarts)"
keep_awake
rc=0
for i in $(seq 0 "$MAX_RESTARTS"); do
  [ "$i" -gt 0 ] && note "restart $i/$MAX_RESTARTS, resuming"
  bash "$HERE/run_device.sh"
  rc=$?
  case $rc in
    0) note "finished"; exit 0 ;;
    130) note "interrupted; not restarting"; exit 130 ;;
  esac
  note "run_device.sh exited with $rc; restarting in ${RESTART_WAIT}s"
  sleep "$RESTART_WAIT"
done
note "giving up after $MAX_RESTARTS restarts"
exit "$rc"
