// Render candidate .a16 (from japanese.dic) and score the envelope modulation.
// usage: node tools/dic_render.js <dic> <player.exe> <workDir>
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const b = fs.readFileSync(process.argv[2]);
const player = process.argv[3];
const work = process.argv[4] || 'spce061a/build';

function envelope(w) {
  const n = (w.length - 44) / 2, win = 100, e = [];
  for (let i = 0; i + win <= n; i += win) {
    let s = 0;
    for (let k = 0; k < win; k++) { const v = w.readInt16LE(44 + 2 * (i + k)); s += v * v; }
    e.push(Math.sqrt(s / win));
  }
  return e;
}
function modDepth(e) {                    // envelope modulation depth + dominant Hz (env rate 160 Hz)
  const mean = e.reduce((a, c) => a + c, 0) / e.length;
  const varr = e.reduce((a, c) => a + (c - mean) * (c - mean), 0) / e.length;
  let best = { lag: 0, v: 0 };
  for (let lag = 8; lag <= 120; lag++) {
    let s = 0, c1 = 0, c2 = 0;
    for (let i = 0; i + lag < e.length; i++) { s += (e[i] - mean) * (e[i + lag] - mean); c1 += (e[i] - mean) ** 2; c2 += (e[i + lag] - mean) ** 2; }
    const c = s / Math.sqrt((c1 * c2) || 1);
    if (c > best.v) best = { lag, v: c };
  }
  return { depth: Math.sqrt(varr) / (mean || 1), lag: best.lag, corr: best.v, hz: 160 / (best.lag || 1) };
}

const cases = [
  ['jp_a', 0x9000, 0x8009, 'seq'],
  ['jp_b', 0x2800, 0x8007, 'seq'],
  ['jp_c', 0x796, 0x8007, 'seq'],
  ['jp_d', 0x1a000, 0x8007, 'seq'],
];
for (const [name, off, param, mode] of cases) {
  const payload = b.slice(off, off + 32 * 400);
  const f = Buffer.alloc(6 + payload.length);
  f.writeUInt32LE(payload.length + 2, 0); f.writeUInt16LE(param, 4); payload.copy(f, 6);
  const inPath = path.join(work, name + '.a16'), wavPath = path.join(work, name + '.wav');
  fs.writeFileSync(inPath, f);
  let out = '';
  try { out = execFileSync(player, [inPath, wavPath], { encoding: 'utf8' }); } catch (e) { out = String(e.stdout || ''); }
  const m = out.match(/A1600 stream decoded\s+([0-9]+) samples/);
  const w = fs.existsSync(wavPath) ? fs.readFileSync(wavPath) : null;
  if (!w) { console.log(name + ': no wav'); continue; }
  const e = envelope(w);
  const md = modDepth(e);
  console.log(name + ': off=0x' + off.toString(16) + ' param=0x' + param.toString(16) +
    ' samples=' + (m ? m[1] : '?') + '  envDepth=' + md.depth.toFixed(2) +
    '  modCorr=' + md.corr.toFixed(2) + ' @' + md.hz.toFixed(1) + 'Hz  -> ' + wavPath);
}
