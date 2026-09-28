// japanese.dic looks partly byte-transposed: try even/odd de-interleaving and
// pair swapping, decode as A1600 and score envelope modulation vs real speech.
// usage: node tools/dic_deint.js <dic> <player.exe> <workDir>
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const b = fs.readFileSync(process.argv[2]);
const player = process.argv[3];
const work = process.argv[4] || 'spce061a/build';

function score(w) {
  const n = (w.length - 44) / 2, win = 100, e = [];
  for (let i = 0; i + win <= n; i += win) {
    let s = 0;
    for (let k = 0; k < win; k++) { const v = w.readInt16LE(44 + 2 * (i + k)); s += v * v; }
    e.push(Math.sqrt(s / win));
  }
  const mean = e.reduce((a, c) => a + c, 0) / e.length;
  const varr = e.reduce((a, c) => a + (c - mean) ** 2, 0) / e.length;
  let best = { lag: 0, v: 0 };
  for (let lag = 8; lag <= 120; lag++) {
    let s = 0, c1 = 0, c2 = 0;
    for (let i = 0; i + lag < e.length; i++) { s += (e[i] - mean) * (e[i + lag] - mean); c1 += (e[i] - mean) ** 2; c2 += (e[i + lag] - mean) ** 2; }
    const c = s / Math.sqrt((c1 * c2) || 1);
    if (c > best.v) best = { lag, v: c };
  }
  return { depth: Math.sqrt(varr) / (mean || 1), corr: best.v, hz: 160 / (best.lag || 1) };
}

function tryOne(tag, payload, param) {
  const f = Buffer.alloc(6 + payload.length);
  f.writeUInt32LE(payload.length + 2, 0); f.writeUInt16LE(param, 4); payload.copy(f, 6);
  const a = path.join(work, tag + '.a16'), w = path.join(work, tag + '.wav');
  fs.writeFileSync(a, f);
  try { execFileSync(player, [a, w]); } catch (e) { /* ignore */ }
  if (!fs.existsSync(w)) { console.log(tag + ': no wav'); return; }
  const sc = score(fs.readFileSync(w));
  console.log(tag + ': envDepth=' + sc.depth.toFixed(2) + ' modCorr=' + sc.corr.toFixed(2) +
    ' @' + sc.hz.toFixed(1) + 'Hz  -> ' + w);
}

const off = parseInt(process.env.DIC_OFF || '0x9000', 16);
const N = 32 * 300;
const raw = b.slice(off, off + N);
const even = Buffer.alloc(Math.floor(N / 2)), odd = Buffer.alloc(Math.floor(N / 2)), sw = Buffer.from(raw);
for (let i = 0; i + 1 < N; i += 2) { even[i / 2] = raw[i]; odd[i / 2] = raw[i + 1]; }
for (let i = 0; i + 1 < N; i += 2) { const t = sw[i]; sw[i] = sw[i + 1]; sw[i + 1] = t; }
for (const p of [0x8007, 0x8009]) {
  tryOne('da_even_p' + p.toString(16), even, p);
  tryOne('da_odd_p' + p.toString(16), odd, p);
  tryOne('da_swap_p' + p.toString(16), sw, p);
}
