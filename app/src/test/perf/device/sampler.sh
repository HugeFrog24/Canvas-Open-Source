#!/system/bin/sh
# On-device sampler for run_device.sh. Runs as root when su is available.
# Usage: sampler.sh <seconds> <outdir> <package> [interval_s]
dur=$1
out=$2
pkg=$3
interval=${4:-0.5}
mkdir -p "$out"
pid=$(dumpsys activity processes "$pkg" | grep -m1 -oE "ProcessRecord[{][0-9a-f]+ [0-9]+:" | grep -oE "[0-9]+:$" | tr -d :)
[ -n "$pid" ] || pid=$(pidof "$pkg")
echo "$pid" > "$out/pid.txt"
dumpsys SurfaceFlinger --list | grep -F "SurfaceView[$pkg/com.tgc.sky.GameActivity]" > "$out/layers_raw.txt"
grep -F "](BLAST)#" "$out/layers_raw.txt" | sed -e 's/^RequestedLayerState{//' -e 's/ parentId=.*//' -e 's/}$//' > "$out/layers.txt"
gpu=/sys/class/kgsl/kgsl-3d0

snap() {
    for t in /proc/$pid/task/*; do
        echo "$(cat "$t/comm" 2>/dev/null)|$(cat "$t/stat" 2>/dev/null)"
    done > "$out/threads_$1.txt"
    head -1 /proc/stat > "$out/procstat_$1.txt"
    cat /proc/$pid/stat > "$out/pidstat_$1.txt" 2>/dev/null
    echo "$(date +%s.%N) $(cut -d' ' -f1 /proc/uptime)" > "$out/clock_$1.txt"
}

snap start
end=$(( $(date +%s) + dur ))
while [ "$(date +%s)" -lt "$end" ]; do
    echo "T $(date +%s.%N)"
    while IFS= read -r layer; do
        echo "L $layer"
        dumpsys SurfaceFlinger --latency "$layer"
    done < "$out/layers.txt"
    echo "G $(cat $gpu/gpu_busy_percentage 2>/dev/null | tr -d ' %') $(cat $gpu/gpuclk 2>/dev/null) $(cat $gpu/temp 2>/dev/null)"
    echo "F $(cat /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq 2>/dev/null | tr '\n' ' ')"
    echo "C $(cat $gpu/max_gpuclk $gpu/devfreq/max_freq $gpu/devfreq/min_freq $gpu/thermal_pwrlevel 2>/dev/null | tr '\n' ' ')| $(cat /sys/devices/system/cpu/cpufreq/policy*/scaling_max_freq 2>/dev/null | tr '\n' ' ')| $(cat /sys/devices/system/cpu/cpufreq/policy*/scaling_min_freq 2>/dev/null | tr '\n' ' ')"
    echo "B $(cat /sys/class/power_supply/battery/temp 2>/dev/null) $(cat /sys/class/power_supply/battery/current_now 2>/dev/null) $(cat /sys/class/power_supply/battery/voltage_now 2>/dev/null)"
    sleep "$interval"
done > "$out/samples.txt"
snap end
