// Find "STA <ioA>" instructions that have a "LDA <ioB>" nearby - used to locate
// the keypad scan routines in the NC3000 firmware.
// usage: node tools/scan_pair.js <file> <staAddrHex> <ldaAddrHex> [window]
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
const sta = parseInt(process.argv[3], 16);
const lda = parseInt(process.argv[4], 16);
const win = process.argv[5] ? parseInt(process.argv[5], 10) : 40;

const hits = [];
for (let i = 0; i < b.length - win; i++) {
  if (b[i] !== 0x85 || b[i + 1] !== sta) continue;
  let near = false;
  for (let j = i; j < i + win; j++) {
    if (b[j] === 0xA5 && b[j + 1] === lda) { near = true; break; }
  }
  if (near) {
    const bank = Math.floor(i / 0x8000);
    const base = bank === 0 ? 0x8000 : 0x4000;
    hits.push(`bank ${bank.toString(16).padStart(2, '0')} @ $${(base + (i % 0x8000)).toString(16)}`);
  }
}
console.log(`STA $${sta.toString(16)} with LDA $${lda.toString(16)} within ${win} bytes: ${hits.length} hit(s)`);
for (const h of hits.slice(0, 40)) console.log('   ' + h);
