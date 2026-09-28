// Scan a raw NOR dump for 6502 JSR (0x20) instructions targeting a given address.
// The dump format used by this project is "CPU window order":
//   bank 0   : file 0x000000 + n  ->  CPU 0x8000 + n   (n < 0x8000)
//   bank 1-31: file 0x8000*bank + n -> CPU 0x4000 + n   (n < 0x8000)
// usage: node tools/scan_jsr.js <file> <targetHex> [lastTargetHex]
const fs = require('fs');
const file = process.argv[2];
const lo = parseInt(process.argv[3], 16);
const hi = process.argv[4] ? parseInt(process.argv[4], 16) : lo;
const b = fs.readFileSync(file);

function cpuOf(off) {
  const bank = Math.floor(off / 0x8000);
  const base = bank === 0 ? 0x8000 : 0x4000;
  return { bank, cpu: base + (off % 0x8000) };
}

for (let t = lo; t <= hi; t++) {
  const hits = [];
  for (let i = 0; i + 2 < b.length; i++) {
    if (b[i] !== 0x20) continue;
    if (b[i + 1] === (t & 0xff) && b[i + 2] === ((t >> 8) & 0xff)) {
      const c = cpuOf(i);
      hits.push(`bank ${c.bank.toString(16).padStart(2, '0')} @ $${c.cpu.toString(16).padStart(4, '0')}`);
    }
  }
  console.log(`JSR $${t.toString(16).padStart(4, '0')}: ${hits.length} hit(s)`);
  for (const h of hits.slice(0, 12)) console.log('   ' + h);
}
