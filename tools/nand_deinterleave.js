// Extract a NAND region as 528-byte pages (512 data + 16 spare) and compare
// against a logical image, reporting per-page matches.
// usage: node tools/nand_deinterleave.js <startHex> <dataLenHex> <compareFile> [outFile]
const fs = require('fs');
const path = require('path');

const nand = fs.readFileSync(path.join(__dirname, '..', 'info', 'NC3KSYSNAND.nand'));
const start = parseInt(process.argv[2], 16);
const dataLen = parseInt(process.argv[3], 16);
const cmpFile = process.argv[4];
const outFile = process.argv[5];

const PAGE = 528, DATA = 512;
const pages = Math.ceil(dataLen / DATA);
let out = Buffer.alloc(pages * DATA);
for (let p = 0; p < pages; p++) {
  nand.copy(out, p * DATA, start + p * PAGE, start + p * PAGE + DATA);
}
const log = [];
log.push('pages=' + pages + '  region 0x' + start.toString(16) + '..0x' + (start + pages * PAGE).toString(16));

if (cmpFile) {
  const ref = fs.readFileSync(cmpFile);
  const n = Math.min(ref.length, dataLen);
  let worst = [];
  for (let p = 0; p < pages; p++) {
    let same = 0;
    const len = Math.max(0, Math.min(DATA, n - p * DATA));
    for (let i = 0; i < len; i++) if (ref[p * DATA + i] === out[p * DATA + i]) same++;
    const pct = len ? (100 * same / len) : 0;
    if (pct < 99.9) worst.push({ p, pct: pct.toFixed(1) });
  }
  const totalSame = (() => { let s = 0; for (let i = 0; i < n; i++) if (ref[i] === out[i]) s++; return s; })();
  log.push('overall byte match: ' + (100 * totalSame / n).toFixed(2) + '% of ' + n + ' bytes');
  log.push('pages below 99.9%: ' + worst.length);
  for (const w of worst.slice(0, 40)) log.push('   page ' + w.p + ' @dat 0x' + (w.p * DATA).toString(16) + '  ' + w.pct + '%');
}
console.log(log.join('\n'));
if (outFile) { fs.writeFileSync(outFile, out); console.log('wrote ' + outFile + ' (' + out.length + ' bytes)'); }
