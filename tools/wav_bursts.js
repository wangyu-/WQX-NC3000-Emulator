// Segment a WAV into sound bursts and report duration + dominant frequency of each.
// usage: node tools/wav_bursts.js <in.wav> [threshFrac] [hopMs]
const fs = require('fs');

function readWav(path) {
  const b = fs.readFileSync(path);
  if (b.toString('ascii', 0, 4) !== 'RIFF') throw new Error('not RIFF');
  let o = 12, fmt = null, data = null;
  while (o + 8 <= b.length) {
    const id = b.toString('ascii', o, o + 4);
    const sz = b.readUInt32LE(o + 4);
    if (id === 'fmt ') {
      fmt = {
        tag: b.readUInt16LE(o + 8), ch: b.readUInt16LE(o + 10),
        rate: b.readUInt32LE(o + 12), bits: b.readUInt16LE(o + 22),
      };
    } else if (id === 'data') {
      data = b.slice(o + 8, o + 8 + sz);
    }
    o += 8 + sz + (sz & 1);
  }
  if (!fmt || !data) throw new Error('missing fmt/data');
  let x;
  if (fmt.bits === 16) {
    x = new Float64Array(data.length / 2);
    for (let i = 0; i < x.length; i++) x[i] = data.readInt16LE(i * 2);
  } else if (fmt.bits === 8) {
    x = new Float64Array(data.length);
    for (let i = 0; i < x.length; i++) x[i] = data[i] - 128;
  } else throw new Error('bits ' + fmt.bits);
  if (fmt.ch > 1) {
    const y = new Float64Array(x.length / fmt.ch);
    for (let i = 0; i < y.length; i++) {
      let s = 0;
      for (let c = 0; c < fmt.ch; c++) s += x[i * fmt.ch + c];
      y[i] = s / fmt.ch;
    }
    x = y;
  }
  return { x, rate: fmt.rate, ch: fmt.ch, bits: fmt.bits };
}

// Goertzel-ish: |DFT| at a given frequency over [a,b)
function magAt(x, sr, a, b, f) {
  const w = 2 * Math.PI * f / sr;
  let re = 0, im = 0;
  for (let i = a; i < b; i++) { re += x[i] * Math.cos(w * i); im += x[i] * Math.sin(w * i); }
  const n = b - a;
  return Math.hypot(re, im) / n;
}

function bestFreq(x, sr, a, b, lo, hi, step) {
  let bf = 0, bm = -1;
  for (let f = lo; f <= hi; f += step) {
    const m = magAt(x, sr, a, b, f);
    if (m > bm) { bm = m; bf = f; }
  }
  return { f: bf, mag: bm };
}

function main() {
  const path = process.argv[2];
  const frac = process.argv[3] ? parseFloat(process.argv[3]) : 0.15;
  const hopMs = process.argv[4] ? parseFloat(process.argv[4]) : 5;
  const { x, rate, ch, bits } = readWav(path);
  const hop = Math.max(1, Math.round(hopMs * rate / 1000));
  const nWin = 256;
  // local RMS per hop window
  const rms = [];
  for (let i = 0; i + nWin <= x.length; i += hop) {
    let s = 0;
    for (let k = 0; k < nWin; k++) s += x[i + k] * x[i + k];
    rms.push({ i, v: Math.sqrt(s / nWin) });
  }
  const peak = Math.max(...rms.map(r => r.v));
  const th = Math.max(peak * frac, 30);
  const runs = [];
  let start = null;
  for (const r of rms) {
    const on = r.v > th;
    if (on && start === null) start = r.i;
    if (!on && start !== null) { runs.push([start, r.i]); start = null; }
  }
  if (start !== null) runs.push([start, x.length]);

  console.log(`${path}: ${rate} Hz, ${ch}ch, ${bits}-bit, ${(x.length / rate).toFixed(4)} s, peak RMS ${peak.toFixed(1)}`);
  console.log(`threshold ${th.toFixed(1)} (${frac} of peak), hop ${hopMs} ms -> ${runs.length} burst(s)`);
  for (const [a, b] of runs) {
    let pk = 0;
    for (let i = a; i < b; i++) { const v = Math.abs(x[i]); if (v > pk) pk = v; }
    const best = bestFreq(x, rate, a, b, 200, 8000, 2);
    // refine +-5 Hz at 0.1 Hz
    const fine = bestFreq(x, rate, a, b, Math.max(50, best.f - 6), best.f + 6, 0.1);
    console.log(
      `  ${(a / rate * 1000).toFixed(1)}..${(b / rate * 1000).toFixed(1)} ms` +
      `  dur ${((b - a) / rate * 1000).toFixed(1)} ms  peak ${pk.toFixed(0)}` +
      `  f0 ${fine.f.toFixed(1)} Hz`);
  }
}
main();
