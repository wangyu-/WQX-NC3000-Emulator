// Try decoding regions of japanese.dic as A1600 streams and score the result.
// usage: node tools/dic_try.js <dic> <player.exe> <workDir>
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const dic = process.argv[2];
const player = process.argv[3];
const work = process.argv[4] || 'spce061a/build';
const b = fs.readFileSync(dic);

function stats(wavPath) {
  const w = fs.readFileSync(wavPath);
  const n = (w.length - 44) / 2;
  if (n < 100) return null;
  let sum = 0, prev = 0, cross = 0, sq = 0, silent = 0, win = 1600;
  const env = [];
  for (let i = 0; i < n; i++) {
    const v = w.readInt16LE(44 + 2 * i);
    sum += v * v;
    if (i) cross += v * prev;
    sq += prev * prev;
    prev = v;
    if ((i + 1) % win === 0) {
      let s2 = 0;
      for (let k = i + 1 - win; k <= i; k++) { const x = w.readInt16LE(44 + 2 * k); s2 += x * x; }
      env.push(Math.sqrt(s2 / win));
    }
  }
  const rms = Math.sqrt(sum / n);
  const lag1 = sq ? cross / sq : 0;
  const thr = 0.15 * Math.max(...env);
  for (const e of env) if (e < thr) silent++;
  return { n, rms, lag1, silence: env.length ? silent / env.length : 0, envMax: Math.max(...env) };
}

const offs = [0x2800, 0x9000, 0x1a000, 0x2c000];
const params = [0x8005, 0x8007, 0x8009, 0x800b];
const rows = [];
for (const o of offs) {
  for (const p of params) {
    const payload = b.slice(o, o + 32 * 256);
    const f = Buffer.alloc(6 + payload.length);
    f.writeUInt32LE(payload.length + 2, 0);
    f.writeUInt16LE(p, 4);
    payload.copy(f, 6);
    const inPath = path.join(work, 'dt.a16');
    const wavPath = path.join(work, 'dt.wav');
    fs.writeFileSync(inPath, f);
    let out = '';
    try { out = execFileSync(player, [inPath, wavPath], { encoding: 'utf8' }); } catch (e) { out = String(e.stdout || ''); }
    const m = out.match(/A1600 stream decoded\s+([0-9]+) samples/);
    const st = fs.existsSync(wavPath) ? stats(wavPath) : null;
    rows.push({ off: o, param: p, samples: m ? +m[1] : 0, st });
  }
}
rows.sort((a, c) => (c.st ? c.st.lag1 * (1 - Math.abs(c.st.silence - 0.2)) : 0) - (a.st ? a.st.lag1 * (1 - Math.abs(a.st.silence - 0.2)) : 0));
console.log('off      param  samples   rms     lag1    silence');
for (const r of rows) {
  console.log('0x' + r.off.toString(16).padEnd(6) + ' 0x' + r.param.toString(16) + '  ' +
    String(r.samples).padStart(7) + '  ' +
    (r.st ? r.st.rms.toFixed(0).padStart(6) + '  ' + r.st.lag1.toFixed(3) + '   ' + (100 * r.st.silence).toFixed(0) + '%' : '   -      -      -'));
}
