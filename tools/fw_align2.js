// Align an extracted 061 firmware image against a window of the NAND dump.
// usage: node tools/fw_align2.js <datFile> <nandLo> <nandHi> [baseHint]
const fs = require('fs');
const path = require('path');

const dat = fs.readFileSync(process.argv[2]);
const nand = fs.readFileSync(path.join(__dirname, '..', 'info', 'NC3KSYSNAND.nand'));
const lo = parseInt(process.argv[3], 16);
const hi = parseInt(process.argv[4], 16);

function findAll(pat, start, end) {
  const hits = [];
  outer:
  for (let i = start; i + pat.length <= end; i++) {
    for (let j = 0; j < pat.length; j++) if (nand[i + j] !== pat[j]) continue outer;
    hits.push(i);
  }
  return hits;
}

const probes = [];
for (const off of [0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000, 0x40000, 0x60000, 0x80000 - 32, 0xa0000, 0xc0000, 0xf0000, 0x100000, 0x110000, 0x120000]) {
  if (off + 32 > dat.length) continue;
  probes.push({ off, pat: dat.slice(off, off + 32) });
}

console.log('dat length = ' + dat.length + ' (0x' + dat.length.toString(16) + ')');
for (const p of probes) {
  const hits = findAll(p.pat, lo, hi);
  const deltas = hits.map(h => '0x' + (h - p.off).toString(16));
  console.log('dat 0x' + p.off.toString(16).padStart(6, '0') + ' (' + p.pat.slice(0, 8).toString('hex') + ') -> ' + hits.length + ' hit(s)  delta=' + [...new Set(deltas)].join(','));
  for (const h of hits.slice(0, 6)) console.log('     nand 0x' + h.toString(16) + '   delta 0x' + (h - p.off).toString(16));
}
