// Enumerate every byte the NC3000 sends to the SPCE061A (via the byte-send
// helpers $FB1C / $9DAA / $9D5x) and every byte it expects back.
const fs = require('fs');
const b = fs.readFileSync(process.argv[2] || 'info/NC3KSYSNOR.nor');

function calls(pat) {                     // find JSR to the given 16-bit target
  const out = [];
  for (let i = 0; i + 2 < b.length; i++) {
    if (b[i] === 0x20 && b[i + 1] === (pat & 0xff) && b[i + 2] === (pat >> 8)) out.push(i);
  }
  return out;
}

for (const [name, target] of [['$FB1C (BIOS send)', 0xfb1c], ['$9DAA (bank0B send)', 0x9daa],
                              ['$FB49 (BIOS recv)', 0xfb49], ['$E0B7 (=recv)', 0xe0b7]]) {
  const sites = calls(target);
  console.log(`\n=== JSR ${name}: ${sites.length} sites ===`);
  const imm = [];
  for (const s of sites) {
    // look back for "LDA #imm" (A9 xx) or "LDX #imm" (A2 xx) etc. within 8 bytes
    let what = '?';
    for (let k = 1; k <= 8 && s - k - 1 >= 0; k++) {
      const op = b[s - k - 1], arg = b[s - k];
      if (op === 0xa9) { what = `LDA #$${arg.toString(16).padStart(2, '0')}`; break; }
      if (op === 0xa2) { what = `LDX #$${arg.toString(16).padStart(2, '0')}`; break; }
      if (op === 0xa0) { what = `LDY #$${arg.toString(16).padStart(2, '0')}`; break; }
      if (op === 0xbd || op === 0xb9) { what = 'LDA tbl,X/Y'; break; }
      if (op === 0xa5 || op === 0xb5) { what = 'LDA zp'; break; }
      if (op === 0x68 || op === 0x60 || op === 0x20) break;
    }
    if (what !== '?' ) imm.push(`${what}@0x${s.toString(16)}`);
  }
  console.log(imm.join('  '));
}
