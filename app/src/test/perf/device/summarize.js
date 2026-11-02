// Summarises one run_device.sh output directory: node summarize.js <outdir>
// Writes <outdir>/summary.md and summary.json and prints the markdown.
"use strict";
const fs = require("fs");
const path = require("path");

const OUT = process.argv[2];
if (!OUT) { console.error("usage: node summarize.js <outdir>"); process.exit(1); }
const read = (f) => { try { return fs.readFileSync(f, "utf8").replace(/\r/g, ""); } catch { return ""; } };
const exists = (f) => { try { fs.accessSync(f); return true; } catch { return false; } };
const num = (x) => (x === undefined || x === null || Number.isNaN(x) ? null : x);
const mean = (a) => (a.length ? a.reduce((s, v) => s + v, 0) / a.length : null);
const median = (a) => { if (!a.length) return null; const s = [...a].sort((x, y) => x - y); const m = s.length >> 1; return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2; };
const pct = (sorted, p) => (sorted.length ? sorted[Math.min(sorted.length - 1, Math.round(p * (sorted.length - 1)))] : null);
const f1 = (v, d = 1) => (v === null || v === undefined ? "-" : Number(v).toFixed(d));
const PENDING = 9e18;

function parseSamples(text) {
  const snaps = [];
  let cur = null, layer = null, gpu = [], freq = [], batt = [], caps = [];
  for (const line of text.split("\n")) {
    if (line.startsWith("T ")) { cur = { t: parseFloat(line.slice(2)), layers: {} }; snaps.push(cur); layer = null; continue; }
    if (line.startsWith("L ")) { layer = line.slice(2).trim(); if (cur) cur.layers[layer] = { period: null, rows: [] }; continue; }
    if (line.startsWith("G ")) { layer = null; const [b, c, t] = line.slice(2).trim().split(/\s+/).map(Number); gpu.push({ busy: b, clk: c, temp: t }); if (cur) cur.g = { busy: b, clk: c }; continue; }
    if (line.startsWith("F ")) { layer = null; const f = line.slice(2).trim().split(/\s+/).map(Number).filter((v) => !Number.isNaN(v)); freq.push(f); if (cur) cur.f = f; continue; }
    if (line.startsWith("C ")) {
      layer = null;
      const [g, mx, mn] = line.slice(2).split("|").map((x) => x.trim().split(/\s+/).filter(Boolean).map(Number));
      const c = { gmax: g?.[0], gdevmax: g?.[1], gdevmin: g?.[2], tlvl: g?.[3], cmax: mx || [], cmin: mn || [] };
      caps.push(c);
      if (cur) cur.cap = c;
      continue;
    }
    if (line.startsWith("B ")) { layer = null; const [t, i, v] = line.slice(2).trim().split(/\s+/).map(Number); batt.push({ temp: t, current: i, voltage: v }); continue; }
    if (layer && cur) {
      const parts = line.trim().split(/\s+/).filter(Boolean);
      const L = cur.layers[layer];
      if (parts.length === 1 && L.period === null) L.period = Number(parts[0]);
      else if (parts.length === 3) L.rows.push(parts.map(Number));
    }
  }
  return { snaps, gpu, freq, batt, caps };
}

function layerStats(snaps, name) {
  const withLayer = snaps.filter((s) => s.layers[name]);
  if (!withLayer.length) return null;
  let baseline = 0;
  for (const r of withLayer[0].layers[name].rows) if (r[1] > 0 && r[1] < PENDING) baseline = Math.max(baseline, r[1]);
  const presents = new Set(), afterGap = new Set();
  let gaps = 0, prevMax = null;
  for (const s of withLayer) {
    const valid = s.layers[name].rows.filter((r) => r[1] > baseline && r[1] < PENDING).map((r) => r[1]);
    const rowsLen = s.layers[name].rows.length;
    if (valid.length && prevMax !== null && Math.min(...valid) > prevMax && valid.length >= rowsLen) { gaps++; afterGap.add(Math.min(...valid)); }
    if (valid.length) prevMax = Math.max(prevMax ?? 0, ...valid);
    for (const v of valid) presents.add(v);
  }
  const sorted = [...presents].sort((a, b) => a - b);
  const iv = [];
  let unseen = 0;
  for (let i = 1; i < sorted.length; i++) {
    const d = (sorted[i] - sorted[i - 1]) / 1e6;
    if (afterGap.has(sorted[i])) unseen += d; else iv.push(d);
  }
  const ivs = [...iv].sort((a, b) => a - b);
  const p50 = pct(ivs, 0.5);
  const span = sorted.length > 1 ? (sorted[sorted.length - 1] - sorted[0]) / 1e9 - unseen / 1e3 : 0;
  const local = [];
  for (let i = 1, a = 1, b = 1; i < sorted.length; i++) {
    if (afterGap.has(sorted[i])) continue;
    while (sorted[a] < sorted[i] - 5e9) a++;
    while (b < sorted.length - 1 && sorted[b + 1] <= sorted[i] + 5e9) b++;
    let n = 0, sum = 0;
    for (let k = Math.max(a, 1); k <= b; k++) if (!afterGap.has(sorted[k])) { n++; sum += sorted[k] - sorted[k - 1]; }
    local.push([(sorted[i] - sorted[i - 1]) / 1e6, n / (sum / 1e9)]);
  }
  return {
    local,
    frames: sorted.length,
    span_s: span,
    fps: span > 0 ? iv.length / span : 0,
    p50_ms: p50, p95_ms: pct(ivs, 0.95), p99_ms: pct(ivs, 0.99), max_ms: ivs.length ? ivs[ivs.length - 1] : null,
    jank_pct: ivs.length ? (100 * iv.filter((v) => v > 1.5 * p50).length) / iv.length : null,
    vsync_ms: withLayer[0].layers[name].period ? withLayer[0].layers[name].period / 1e6 : null,
    coverage_gaps: gaps,
  };
}

// Game first, overlay second. The overlay SurfaceView uses setZOrderOnTop, so its
// container has z >= 0 and the game's is negative; without that, creation order.
function orderLayers(names, raw) {
  const byNum = (a, b) => Number(a.split("#").pop()) - Number(b.split("#").pop());
  const containerZ = new Map(), parentOf = new Map();
  for (const line of raw.split("\n")) {
    const id = line.match(/\]#(\d+)/), parent = line.match(/parentId=(\d+)/), z = line.match(/ z=(-?\d+)/);
    if (!id) continue;
    if (line.includes("](BLAST)#")) { if (parent) parentOf.set(id[1], parent[1]); }
    else if (z) containerZ.set(id[1], Number(z[1]));
  }
  const zOf = (n) => containerZ.get(parentOf.get(n.split("#").pop()));
  if (names.length && names.every((n) => zOf(n) !== undefined)) return [...names].sort((a, b) => zOf(a) - zOf(b) || byNum(a, b));
  return [...names].sort(byNum);
}

function parseThreads(text) {
  const m = new Map();
  for (const line of text.split("\n")) {
    const bar = line.indexOf("|");
    if (bar < 0) continue;
    const comm = line.slice(0, bar), stat = line.slice(bar + 1);
    const close = stat.lastIndexOf(")");
    if (close < 0) continue;
    const tid = stat.slice(0, stat.indexOf(" "));
    const f = stat.slice(close + 2).split(" ");
    m.set(tid, { comm, ticks: Number(f[11]) + Number(f[12]) });
  }
  return m;
}
const pidTicks = (text) => { const c = text.lastIndexOf(")"); if (c < 0) return null; const f = text.slice(c + 2).split(" "); return Number(f[11]) + Number(f[12]); };
const procStat = (text) => { const f = text.trim().split(/\s+/).slice(1).map(Number); if (f.length < 4) return null; const idle = f[3] + (f[4] || 0); return { total: f.reduce((s, v) => s + v, 0), idle }; };
const clock = (text) => { const [e] = text.trim().split(/\s+/); return parseFloat(e); };

// Only lines from the run's own process count: a logcat file can hold later runs too.
function parseLogcat(text, t0, t1, pid) {
  const sec = [], thermal = [], other = [], setup = [];
  let atlas = null, native = null, config = null, crash = false;
  for (const line of text.split("\n")) {
    const mt = line.match(/^\s*(\d+\.\d+)\s+(\d+)/);
    if (pid && (!mt || mt[2] !== pid)) continue;
    if (/FATAL EXCEPTION|Fatal signal/.test(line)) crash = true;
    const t = mt ? parseFloat(mt[1]) : null;
    const msg = line.includes("CanvasPerf:") ? line.slice(line.indexOf("CanvasPerf:") + 11).trim() : null;
    if (!msg) continue;
    if (msg.startsWith("atlas")) atlas = msg;
    else if (msg.startsWith("native")) native = msg;
    else if (msg.startsWith("config")) config = msg;
    else if (msg.startsWith("thermal")) { if (t0 === null || (t >= t0 && t <= t1)) thermal.push({ t, msg }); }
    else if (msg.startsWith("sec ")) {
      if (t0 !== null && (t < t0 || t > t1)) continue;
      const kv = {};
      for (const m of msg.matchAll(/(\w+)=(-?[\d.]+)/g)) kv[m[1]] = Number(m[2]);
      sec.push(kv);
    } else if (/^(egl|affinity|cpus|procs|nice|damage|vkhook|vkdevice) /.test(msg)) setup.push(msg);
    else other.push(msg);
  }
  const sum = (k) => sec.reduce((s, x) => s + (x[k] || 0), 0);
  const spanS = sum("span_ms") / 1000;
  return {
    crash, atlas, native, config, other, thermal, setup,
    seconds: sec.length,
    presents_per_s: spanS ? sum("presented") / spanS : null,
    frames_per_s: spanS ? sum("frames") / spanS : null,
    thread_cpu_ms_per_s: spanS ? sum("thread_cpu_ms") / spanS : null,
    work_cpu_us_p50: median(sec.map((x) => x.cpu_us_p50).filter((v) => v >= 0)),
    swap_us_p50: median(sec.map((x) => x.swap_us_p50).filter((v) => v >= 0)),
    swap_us_p95: median(sec.map((x) => x.swap_us_p95).filter((v) => v >= 0)),
    overlay_big_pct: sum("frames") && sec.some((x) => x.big_frames !== undefined) ? (100 * sum("big_frames")) / sum("frames") : null,
    damage_pct: median(sec.map((x) => x.damage_pct).filter((v) => v >= 0)),
    game_presents_per_s: spanS && sec.some((x) => x.game_presents > 0) ? sum("game_presents") / spanS : null,
    hook_us: median(sec.map((x) => x.hook_us_avg).filter((v) => v >= 0)),
    trig_us_p50: median(sec.map((x) => x.trig_us_p50).filter((v) => v >= 0)),
    fence_us_p50: median(sec.map((x) => x.fence_us_p50).filter((v) => v >= 0)),
    fallback_per_s: spanS && sec.some((x) => x.fallback_frames !== undefined) ? sum("fallback_frames") / spanS : null,
  };
}

function parseMeminfo(text) {
  const g = (re) => { const m = text.match(re); return m ? Number(m[1]) / 1024 : null; };
  return {
    pss_mb: g(/TOTAL PSS:\s+(\d+)/) ?? g(/^\s*TOTAL\s+(\d+)/m),
    native_heap_mb: g(/Native Heap:\s+(\d+)/),
    graphics_mb: g(/Graphics:\s+(\d+)/),
    gl_mtrack_mb: g(/^\s*GL mtrack\s+(\d+)/m),
    egl_mtrack_mb: g(/EGL mtrack\s+(\d+)/),
  };
}

function parseSf(text, app) {
  const lines = text.split("\n");
  const comp = {}, vote = {};
  for (let i = 0; i < lines.length - 1; i++) {
    const name = lines[i].trim();
    if (!/SurfaceView\[.*\]\(BLAST\)#\d+$/.test(name) || !name.includes("com.tgc.sky.GameActivity")) continue;
    const cells = lines[i + 1].split("|").map((c) => c.trim());
    const type = cells.find((c) => /^(DEVICE|CLIENT|SOLID_COLOR|CURSOR|SIDEBAND|DISPLAY_DECORATION)$/.test(c));
    if (!type) continue;
    const key = "#" + name.split("#").pop();
    comp[key] = type;
    const rate = (cells[6] || "").match(/^([\d.]+)/);
    if (rate) vote[key] = Number(rate[1]);
  }
  const mode = text.match(/activeMode=\{[^}]*?vsyncRate=([\d.]+)/);
  return { composition: comp, vote, active_hz: mode ? Number(mode[1]) : null };
}

function segment(dir, app, logText) {
  const sdir = dir;
  const samples = parseSamples(read(path.join(sdir, "samples.txt")));
  const layers = orderLayers(read(path.join(sdir, "layers.txt")).split("\n").map((s) => s.trim()).filter(Boolean),
    read(path.join(sdir, "layers_raw.txt")));
  const t0 = clock(read(path.join(sdir, "clock_start.txt"))), t1 = clock(read(path.join(sdir, "clock_end.txt")));
  const win = t1 - t0;
  const th0 = parseThreads(read(path.join(sdir, "threads_start.txt"))), th1 = parseThreads(read(path.join(sdir, "threads_end.txt")));
  const threads = [];
  for (const [tid, e] of th1) { const s = th0.get(tid); if (s) threads.push({ tid, comm: e.comm, ms_per_s: ((e.ticks - s.ticks) * 10) / win }); }
  threads.sort((a, b) => b.ms_per_s - a.ms_per_s);
  const imgui = threads.find((t) => t.comm.startsWith("imgui dispatch"));
  const p0 = pidTicks(read(path.join(sdir, "pidstat_start.txt"))), p1 = pidTicks(read(path.join(sdir, "pidstat_end.txt")));
  const s0 = procStat(read(path.join(sdir, "procstat_start.txt"))), s1 = procStat(read(path.join(sdir, "procstat_end.txt")));
  const gpu = samples.gpu.filter((g) => !Number.isNaN(g.busy));
  const policies = samples.freq.length ? samples.freq[0].length : 0;
  const freqAvg = [];
  for (let p = 0; p < policies; p++) freqAvg.push(mean(samples.freq.map((f) => f[p]).filter((v) => v > 0)) / 1000);
  const batt = samples.batt.filter((b) => !Number.isNaN(b.temp));
  const states = [];
  for (const sn of layers[0] ? samples.snaps : []) {
    const L = sn.layers[layers[0]];
    if (!L || !sn.g) continue;
    const v = L.rows.map((r) => r[1]).filter((x) => x > 0 && x < PENDING).sort((a, b) => a - b);
    if (v.length >= 8) states.push({ fps: (v.length - 1) / ((v[v.length - 1] - v[0]) / 1e9), g: sn.g, f: sn.f || [], cap: sn.cap || null });
  }
  const capMin = (get) => { const v = samples.caps.map(get).filter((x) => Number.isFinite(x)); return v.length ? Math.min(...v) : null; };
  const seg = {
    window_s: win,
    game: layers[0] ? layerStats(samples.snaps, layers[0]) : null,
    overlay: layers[1] ? layerStats(samples.snaps, layers[1]) : null,
    layers,
    gpu_busy_pct: mean(gpu.map((g) => g.busy)),
    gpu_mhz: gpu.length ? mean(gpu.map((g) => g.clk)) / 1e6 : null,
    gpu_temp_max_c: gpu.length ? Math.max(...gpu.map((g) => g.temp)) / 1000 : null,
    cpu_policy_mhz: freqAvg,
    battery_c: batt.length ? [batt[0].temp / 10, batt[batt.length - 1].temp / 10] : null,
    battery_ma: batt.length ? mean(batt.map((b) => b.current)) / 1000 : null,
    process_cpu_pct: p0 !== null && p1 !== null ? ((p1 - p0) * 10) / (win * 10) : null,
    system_cpu_pct: s0 && s1 ? (100 * (s1.total - s0.total - (s1.idle - s0.idle))) / (s1.total - s0.total) : null,
    imgui_ms_per_s: imgui ? imgui.ms_per_s : null,
    top_threads: threads.slice(0, 8).map((t) => `${t.comm.trim()} ${t.ms_per_s.toFixed(1)}`),
    caps_min: samples.caps.length ? {
      gpu_max_mhz: capMin((c) => c.gdevmax) / 1e6,
      gpu_floor_mhz: capMin((c) => c.gdevmin) / 1e6,
      cpu_max_mhz: (samples.caps[0].cmax || []).map((_, i) => capMin((c) => c.cmax[i]) / 1000),
    } : null,
    log: parseLogcat(logText, t0, t1, read(path.join(sdir, "pid.txt")).trim()),
  };
  Object.defineProperty(seg, "states", { value: states, enumerable: false });
  return seg;
}

function runDir(dir) {
  const info = Object.fromEntries(read(path.join(dir, "run.txt")).split(/\s+/).filter((x) => x.includes("=")).map((x) => { const i = x.indexOf("="); return [x.slice(0, i), x.slice(i + 1)]; }));
  const app = info.app;
  const logText = read(path.join(dir, "logcat.txt"));
  const segs = ["s0", "s1"].filter((s) => exists(path.join(dir, s, "samples.txt"))).map((s) => segment(path.join(dir, s), app, logText));
  return {
    dir: path.relative(OUT, dir).replace(/\\/g, "/"), name: info.name, app, launch_failed: info.launch_failed === "1",
    segments: segs, mem: parseMeminfo(read(path.join(dir, "meminfo.txt"))), sf: parseSf(read(path.join(dir, "sf.txt")), app),
    crash: segs.some((s) => s.log.crash),
  };
}

const subdirs = (d) => (exists(d) ? fs.readdirSync(d).map((x) => path.join(d, x)).filter((x) => fs.statSync(x).isDirectory()).sort() : []);
const result = {
  matrix: subdirs(path.join(OUT, "matrix")).map(runDir),
  thermal: subdirs(path.join(OUT, "thermal")).map(runDir),
  sustained: subdirs(path.join(OUT, "sustained")).map(runDir),
};
// The game can sit in a slower state for minutes (on the A52: 20 fps with GPU and CPU
// clocks down) whatever the variant. Long frames count only in the steady state, within
// 7% of the best rate seen in any run, so that state does not skew the comparison.
const games = Object.values(result).flat().flatMap((r) => r.segments).map((s) => s.game).filter(Boolean);
const steadyFps = Math.max(0, ...games.map((g) => median(g.local.map((x) => x[1])) || 0));
for (const g of games) {
  const st = g.local.filter((x) => x[1] >= 0.93 * steadyFps);
  g.steady_s = st.reduce((s, x) => s + x[0], 0) / 1000;
  g.slow_s = g.local.filter((x) => x[1] < 0.75 * steadyFps).reduce((s, x) => s + x[0], 0) / 1000;
  g.steady_long_pct = st.length ? (100 * st.filter((x) => x[0] >= 1200 / steadyFps).length) / st.length : null;
}
for (const g of games) Object.defineProperty(g, "local", { enumerable: false });
// What the clocks and their limits were while the game ran slow, against the steady
// state of the same segment: a lowered cap means something imposed it, an unchanged
// cap with a lower clock means the governors settled there.
const avgOf = (a, get) => { const v = a.map(get).filter((x) => Number.isFinite(x)); return v.length ? mean(v) : null; };
const stateStats = (a) => {
  const c = a.filter((x) => x.cap);
  const pol = (a[0]?.f || []).length, cpol = (c[0]?.cap.cmax || []).length;
  return {
    samples: a.length,
    gpu_mhz: avgOf(a, (x) => x.g.clk) / 1e6, gpu_busy_pct: avgOf(a, (x) => x.g.busy),
    gpu_cap_mhz: c.length ? avgOf(c, (x) => x.cap.gdevmax) / 1e6 : null, gpu_floor_mhz: c.length ? avgOf(c, (x) => x.cap.gdevmin) / 1e6 : null,
    gpu_thermal_level: c.length ? avgOf(c, (x) => x.cap.tlvl) : null,
    cpu_mhz: [...Array(pol).keys()].map((i) => avgOf(a, (x) => x.f[i]) / 1000),
    cpu_cap_mhz: [...Array(cpol).keys()].map((i) => avgOf(c, (x) => x.cap.cmax[i]) / 1000),
  };
};
const slowNotes = [];
for (const r of Object.values(result).flat()) r.segments.forEach((sg, i) => {
  const slow = sg.states.filter((x) => x.fps < 0.75 * steadyFps), ok = sg.states.filter((x) => x.fps >= 0.93 * steadyFps);
  if (!slow.length) return;
  sg.clock_states = { slow: stateStats(slow), steady: ok.length ? stateStats(ok) : null };
  const d = (x) => (x ? `GPU ${f1(x.gpu_mhz, 0)} MHz at ${f1(x.gpu_busy_pct, 0)}% busy, cap ${f1(x.gpu_cap_mhz, 0)}, floor ${f1(x.gpu_floor_mhz, 0)}, thermal level ${f1(x.gpu_thermal_level)}; CPU ${x.cpu_mhz.map((v) => f1(v, 0)).join("/")} MHz, caps ${x.cpu_cap_mhz.map((v) => f1(v, 0)).join("/")} (${x.samples} samples)` : "-");
  slowNotes.push(`${r.dir}${r.segments.length > 1 ? ` s${i}` : ""}: slow: ${d(sg.clock_states.slow)}; steady: ${d(sg.clock_states.steady)}`);
});
fs.writeFileSync(path.join(OUT, "summary.json"), JSON.stringify(result, null, 1));

const md = [];
md.push(`# Device run ${path.basename(OUT)}`, "", "```", read(path.join(OUT, "device.txt")).split("\n").slice(0, 7).join("\n"), "```", "");
const row = (cells) => "| " + cells.join(" | ") + " |";
const HDR = ["variant", "n", "game fps", "game p95 ms", "game jank %", "game long % (steady)", "game steady / slow s", "overlay presents/s", "native presents/s", "imgui ms/s", "overlay on big cores %", "damage %", "hooked game presents/s", "hook µs", "game present to draw p50 µs", "fence wait p50 µs", "fallback draws/s", "work cpu p50 µs", "swap p50 µs", "proc CPU %", "sys CPU %", "GPU busy %", "GPU MHz", "batt °C", "PSS MB", "Graphics MB", "EGL mtrack MB", "comp game/overlay"];
const seg0 = (r) => r.segments[0] || {};
const agg = (runs, get, d = 1) => { const v = runs.map(get).filter((x) => x !== null && x !== undefined && !Number.isNaN(x)); if (!v.length) return "-"; const m = mean(v); return v.length > 1 ? `${f1(m, d)} (${f1(Math.min(...v), d)}-${f1(Math.max(...v), d)})` : f1(m, d); };
const comp = (r) => { const l = seg0(r).layers || []; return l.map((n) => r.sf.composition["#" + n.split("#").pop()] || "?").join("/") || "-"; };
function table(runs, title) {
  if (!runs.length) return;
  md.push(`## ${title}`, "", row(HDR), row(HDR.map(() => "---")));
  const names = [...new Set(runs.map((r) => r.name))];
  for (const n of names) {
    const rs = runs.filter((r) => r.name === n && r.segments.length);
    if (!rs.length) { md.push(row([n, "0", ...HDR.slice(2).map(() => "-")])); continue; }
    md.push(row([n, String(rs.length),
      agg(rs, (r) => seg0(r).game?.fps), agg(rs, (r) => seg0(r).game?.p95_ms), agg(rs, (r) => seg0(r).game?.jank_pct),
      agg(rs, (r) => seg0(r).game?.steady_long_pct), rs.map((r) => `${f1(seg0(r).game?.steady_s, 0)}/${f1(seg0(r).game?.slow_s, 0)}`).join(" "),
      agg(rs, (r) => seg0(r).overlay?.fps), agg(rs, (r) => seg0(r).log?.presents_per_s), agg(rs, (r) => seg0(r).imgui_ms_per_s),
      agg(rs, (r) => seg0(r).log?.overlay_big_pct, 0), agg(rs, (r) => seg0(r).log?.damage_pct),
      agg(rs, (r) => seg0(r).log?.game_presents_per_s), agg(rs, (r) => seg0(r).log?.hook_us),
      agg(rs, (r) => seg0(r).log?.trig_us_p50, 0), agg(rs, (r) => seg0(r).log?.fence_us_p50, 0),
      agg(rs, (r) => seg0(r).log?.fallback_per_s, 2),
      agg(rs, (r) => seg0(r).log?.work_cpu_us_p50, 0), agg(rs, (r) => seg0(r).log?.swap_us_p50, 0),
      agg(rs, (r) => seg0(r).process_cpu_pct), agg(rs, (r) => seg0(r).system_cpu_pct),
      agg(rs, (r) => seg0(r).gpu_busy_pct), agg(rs, (r) => seg0(r).gpu_mhz, 0),
      agg(rs, (r) => (seg0(r).battery_c ? seg0(r).battery_c[1] : null)), agg(rs, (r) => r.mem.pss_mb, 0), agg(rs, (r) => r.mem.graphics_mb, 0), agg(rs, (r) => r.mem.egl_mtrack_mb, 0),
      [...new Set(rs.map(comp))].join(" "),
    ]));
  }
  md.push("");
}
table(result.matrix, "Matrix (mean over rounds, range in brackets)");
table(result.sustained, "Sustained");
if (result.thermal.length) {
  md.push("## Thermal override (s0 = NONE, s1 = SEVERE)", "", row(["variant", "segment", "game fps", "game p95 ms", "GPU busy %", "GPU MHz", "proc CPU %", "status returned to game (logcat)"]), row(Array(8).fill("---")));
  for (const r of result.thermal) {
    r.segments.forEach((s, i) => {
      const th = s.log.thermal.map((x) => x.msg.match(/returned=(-?\d+)/)?.[1]).filter(Boolean);
      md.push(row([r.name, i ? "SEVERE" : "NONE", f1(s.game?.fps), f1(s.game?.p95_ms), f1(s.gpu_busy_pct), f1(s.gpu_mhz, 0), f1(s.process_cpu_pct), [...new Set(th)].join(",") || "-"]));
    });
  }
  md.push("", "Thermal log lines per segment (returned to game / framework status: lines):", "");
  for (const r of result.thermal) r.segments.forEach((s, i) => {
    const n = new Map();
    for (const x of s.log.thermal) { const k = `${x.msg.match(/returned=(-?\d+)/)?.[1]}/${x.msg.match(/framework=(-?\d+)/)?.[1]}`; n.set(k, (n.get(k) || 0) + 1); }
    md.push(`- ${r.name} ${i ? "SEVERE" : "NONE"}: ${[...n].map(([k, c]) => `${k}: ${c}`).join(", ") || "none"}`);
  });
  md.push("");
}
const notes = [];
for (const r of [...result.matrix, ...result.thermal, ...result.sustained]) {
  if (r.launch_failed) notes.push(`${r.dir}: GameActivity did not resume (see screen_fail.png)`);
  if (r.crash) notes.push(`${r.dir}: crash in logcat`);
  for (const s of r.segments) {
    if (s.game?.coverage_gaps) notes.push(`${r.dir}: ${s.game.coverage_gaps} sampling gaps on the game layer`);
    if (!s.layers.length) notes.push(`${r.dir}: no app layers found`);
  }
}
const first = result.matrix.find((r) => r.segments.length && r.segments[0].log.atlas);
const nocjk = result.matrix.find((r) => r.name === "nocjk" && r.segments.length && r.segments[0].log.atlas);
md.push("## Notes", "");
if (first) md.push(`- font atlas (${first.name}): ${first.segments[0].log.atlas}`);
if (nocjk) md.push(`- font atlas (nocjk): ${nocjk.segments[0].log.atlas}`);
const b = result.matrix.find((r) => r.segments.length);
if (b) md.push(`- top threads (${b.dir}, ms CPU per s): ${b.segments[0].top_threads.join(", ")}`, `- active display mode at end: ${b.sf.active_hz ?? "?"} Hz`);
for (const n of notes) md.push(`- ${n}`);
const seenSetup = new Set();
for (const r of [...result.matrix, ...result.sustained]) {
  if (seenSetup.has(r.name) || !r.segments.length) continue;
  seenSetup.add(r.name);
  const su = r.segments[0].log.setup.filter((m) => !m.startsWith("cpus ") || r.name === "base");
  if (su.length) md.push(`- setup (${r.name}): ${su.join(" / ")}`);
}
const failedDir = path.join(OUT, "failed");
if (exists(failedDir)) { const f = fs.readdirSync(failedDir); if (f.length) md.push(`- unusable attempts kept in failed/: ${f.join(", ")}`); }
if (slowNotes.length) md.push("", "## Slow state: clocks and their limits", "", ...slowNotes.map((n) => `- ${n}`));
const prof = path.join(OUT, "profile", "report_threads.txt");
if (exists(prof)) md.push("", "## simpleperf, CPU samples by thread (base)", "", "```", read(prof).split("\n").slice(0, 30).join("\n"), "```");
const text = md.join("\n") + "\n";
fs.writeFileSync(path.join(OUT, "summary.md"), text);
console.log(text);
