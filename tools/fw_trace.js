// Trace the logical->physical mapping of the NAND copy of the 061 firmware.
// usage: node tools/fw_trace.js <datFile> <nandLo> <nandHi> [step]
const fs = require('fs');
const path = require('path');

const dat = fs.readFileSync(process.argv[2]);
const nand = fs.readFileSync(path.join(__dirname, '..', 'info', 'NC3KSYSNAND.nand'));
const lo = parseInt(process.argv[3], 16);
const hi = parseInt(process.argv[4], 16);
const step = process.argv[5] ? parseInt(process.argv[5], 16) : 0x100;

function findAll(pat) {
  const hits = [];
  outer:
  for (let i = lo; i + pat.length <= hi; i++) {
    for (let j = 0; j < pat.length; j++) if (nand[i + j] !== pat[j]) continue outer;
    hits.push(i);
  }
  return hits;
}

let prevNand = null, prevDat = null;
for (let off = 0x400; off + 32 <= dat.length; off += step) {
  const pat = dat.slice(off, off + 32);
  const hits = findAll(pat);
  let line = 'dat 0x' + off.toString(16).padStart(5, '0');
  if (hits.length === 1) {
    const n = hits[0];
    line += '  nand 0x' + n.toString(16) + '  d(nand-dat)=0x' + (n - off).toString(16);
    if (prevNand !== null) {
      const dn = n - prevNand, dd = off - prevDat;
      line += '   jump 0x' + dn.toString(16) + ' per dat 0x' + dd.toString(16) + (dn !== dd ? '   <<< GAP ' + (dn - dd) : '');
    }
    prevNand = n; prevDat = off;
  } else {
    line += '  hits=' + hits.length + (hits.length ? ' [' + hits.map(h => '0x' + h.toString(16)).join(',') + ']' : '');
    if (hits.length !== 1) { prevNand = null; }
  }
  console.log(line);
}
