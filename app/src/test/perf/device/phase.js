#!/usr/bin/env node
// Where in the game's frame cycle the overlay's GPU work finishes, and how that
// lines up with the game's late frames. Reads summary.json and the SurfaceFlinger
// samples of a run directory; run summarize.js first.
//
//   node phase.js <run dir> [variant...]
//
// Per segment it prints:
// - game long %: game frames shown 50 ms or more after the previous one;
// - game ready p50/p90: when the game's buffer became ready, in ms after its
//   previous frame was shown (about 17 ms is the last moment that still makes
//   the next vsync on a 60 Hz panel);
// - overlay presents per second;
// - overlay after game %: overlay frames that became ready 17-24 ms after the
//   game's last shown frame, which is after the game's frame and before its next
//   one starts on the GPU;
// - overlay ready after the game's last ready: how long after the game's most
//   recent finished frame each overlay frame became ready (median, and the share
//   within 6 ms, that is right behind the game's frame on the GPU);
// - the overlay's ready-time histogram in 2 ms bins from 0 to 34 ms;
// - the after-game share and the game long % in 10 s windows, to see phase
//   changes within a run.
const fs = require("node:fs");
const path = require("node:path");

const [root, ...names] = process.argv.slice(2);
if (!root) {
  console.error("usage: node phase.js <run dir> [variant...]");
  process.exit(2);
}
const V = 16.667e6;
const sum = JSON.parse(fs.readFileSync(path.join(root, "summary.json"), "utf8"));
const quantile = (a, p) => {
  const b = [...a].sort((x, y) => x - y);
  return b.length ? b[Math.min(b.length - 1, Math.floor(p * b.length))] : Number.NaN;
};

function frames(dir, layer) {
  const m = new Map();
  let on = false;
  for (const line of fs.readFileSync(path.join(dir, "samples.txt"), "utf8").replaceAll("\r", "").split("\n")) {
    if (line.startsWith("L ")) on = line.slice(2).trim() === layer;
    else if (/^[TGFBC] /.test(line)) on = false;
    else if (on) {
      const p = line.trim().split(/\s+/).map(Number);
      if (p.length === 3 && p[1] > 0 && p[1] < 9e18 && p[2] > 0 && p[2] < 9e18) m.set(p[1], { shown: p[1], ready: p[2] });
    }
  }
  return [...m.values()].sort((a, b) => a.shown - b.shown);
}

function gameStats(g, t0) {
  const ready = [], windows = [];
  let long = 0, n = 0;
  for (let k = 1; k < g.length; k++) {
    const d = g[k].shown - g[k - 1].shown;
    if (d > 8 * V) continue;
    n++;
    const isLong = d >= 2.4 * V;
    if (isLong) long++;
    else ready.push((g[k].ready - g[k - 1].shown) / 1e6);
    const w = Math.floor((g[k].shown - t0) / 10e9);
    if (w < 0) continue;
    windows[w] ??= [0, 0];
    windows[w][0]++;
    if (isLong) windows[w][1]++;
  }
  return { longPct: (100 * long) / n, ready, windows };
}

function overlayPhases(g, o) {
  const hist = new Array(17).fill(0), windows = [];
  let j = 0, n = 0;
  for (const f of o) {
    while (j < g.length - 1 && g[j + 1].shown <= f.ready) j++;
    if (g[j].shown > f.ready) continue;
    const ph = (f.ready - g[j].shown) / 1e6;
    if (ph >= 34) continue;
    hist[Math.floor(ph / 2)]++;
    (windows[Math.floor((f.ready - o[0].ready) / 10e9)] ??= []).push(ph);
    n++;
  }
  const after = (a) => Math.round((100 * a.filter((p) => p >= 17 && p < 24).length) / a.length);
  const readies = g.map((f) => f.ready).sort((a, b) => a - b);
  const sinceReady = [];
  let k = 0;
  for (const f of o) {
    while (k < readies.length - 1 && readies[k + 1] <= f.ready) k++;
    if (readies[k] <= f.ready) sinceReady.push((f.ready - readies[k]) / 1e6);
  }
  return {
    hist: hist.map((c) => Math.round((100 * c) / n)), after: after(windows.flat()), windows: [...windows].map((w) => (w ? after(w) : "-")),
    sinceP50: quantile(sinceReady, 0.5), sinceNear: Math.round((100 * sinceReady.filter((d) => d < 6).length) / sinceReady.length),
  };
}

for (const r of [...sum.matrix, ...(sum.sustained || [])]) {
  if (names.length && !names.includes(r.name)) continue;
  r.segments.forEach((s, i) => {
    const dir = path.join(root, r.dir, `s${i}`);
    const g = frames(dir, s.layers[0]);
    const o = s.layers[1] ? frames(dir, s.layers[1]) : [];
    const gs = gameStats(g, o.length ? o[0].ready : g[0].shown);
    const out = [`${r.dir}${r.segments.length > 1 ? ` s${i}` : ""}`,
      `game long ${gs.longPct.toFixed(1)}%`,
      `game ready p50/p90 ${quantile(gs.ready, 0.5).toFixed(1)}/${quantile(gs.ready, 0.9).toFixed(1)} ms`];
    if (o.length > 1) {
      const op = overlayPhases(g, o);
      out.push(`overlay ${((o.length - 1) / ((o.at(-1).shown - o[0].shown) / 1e9)).toFixed(1)}/s`,
        `overlay after game ${op.after}%`,
        `overlay ready after game's last ready p50 ${op.sinceP50.toFixed(1)} ms, within 6 ms ${op.sinceNear}%`, `histogram ${op.hist.join(" ")}`, `after-game by 10 s ${op.windows.join(" ")}`);
    }
    out.push(`game long % by 10 s ${gs.windows.map((x) => (x ? ((100 * x[1]) / x[0]).toFixed(1) : "-")).join(" ")}`);
    console.log(out.join("\t"));
  });
}
