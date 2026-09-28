// Build the alignment map between two 061 images (old = 061.dat, new = the
// NAND copy) and report the inserted/deleted regions.
// usage: node tools/fw_align_map.js <old.bin> <new.bin> [step]
const fs = require('fs');

const A = fs.readFileSync(process.argv[2]);   // old
const B = fs.readFileSync(process.argv[3]);   // new
const step = process.argv[4] ? parseInt(process.argv[4], 16) : 0x40;
const PROBE = 48;
const LIMIT = Math.min(A.length, B.length);

let prevShift = null;
const transitions = [];
let mismatches = [];

for (let d = 0x0A; d + PROBE <= LIMIT; d += step) {
  const probe = A.slice(d, d + PROBE);
  let best = null;
  for (let k = -0x200; k <= 0x200; k++) {
    const o = d + k;
    if (o < 0 || o + PROBE > B.length) continue;
    let same = 0;
    for (let i = 0; i < PROBE; i++) if (B[o + i] === probe[i]) same++;
    if (!best || same > best.same) best = { k, same };
  }
  const shift = best.same > PROBE * 0.6 ? best.k : null;
  if (shift !== prevShift) {
    transitions.push({ d, shift, pct: (100 * best.same / PROBE).toFixed(0) });
    prevShift = shift;
  }
}

console.log('old=' + process.argv[2] + '  new=' + process.argv[3]);
console.log('alignments (old offset -> shift applied to get the new offset):');
for (const t of transitions) {
  console.log('  old 0x' + t.d.toString(16).padStart(5, '0') + '  shift ' +
    (t.shift === null ? '??' : (t.shift >= 0 ? '+' : '') + '0x' + Math.abs(t.shift).toString(16)) +
    '  (' + t.pct + '% match)');
}
const total = transitions.filter(t => t.shift !== null).reduce((s, t, i, arr) => {
  if (i + 1 < arr.length) return s + (arr[i + 1].d - t.d) * 0;
  return s;
}, 0);
console.log('net size difference: new is ' + (B.length >= A.length ? '+' : '-') +
  (Math.abs(B.length - A.length)) + ' bytes long (' + A.length + ' vs ' + B.length + ')');
