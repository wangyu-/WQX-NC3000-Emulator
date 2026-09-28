// Find a byte pattern in the NC3000 NOR dump and report CPU addresses
// (the dump is in "CPU window order": bank0 base 0x8000, others 0x4000).
// usage: node tools/scan_bytes.js <file> <hex bytes e.g. "a5 08 29 01">
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
const pat = process.argv[3].trim().split(/\s+/).map((h) => parseInt(h, 16));
const hits = [];
for (let i = 0; i + pat.length <= b.length; i++) {
  let ok = true;
  for (let j = 0; j < pat.length; j++) if (b[i + j] !== pat[j]) { ok = false; break; }
  if (!ok) continue;
  const bank = Math.floor(i / 0x8000);
  const base = bank === 0 ? 0x8000 : 0x4000;
  hits.push(`bank ${bank.toString(16).padStart(2, '0')} @ $${(base + (i % 0x8000)).toString(16).padStart(4, '0')}`);
}
console.log(`${process.argv[3]}: ${hits.length} hit(s)`);
for (const h of hits.slice(0, 30)) console.log('   ' + h);
