// Build a full INT (BRK) service table for NC3KSYSNOR.nor
// usage: node tools/gen_int_table.js > out/nc3000_int_table.tsv
const fs = require('fs');
const rom = fs.readFileSync('info/NC3KSYSNOR.nor');

// ---- names from common.txt (UTF-16LE):  A<idx><bank>\t__name
const names = new Map(); // key "bank:idx" -> {name, src}
for (const line of fs.readFileSync('info/common.txt', 'utf16le').split(/\r?\n/)) {
  const m = line.match(/^\s*A([0-9a-fA-F]{4})\s+(__\S+)\s*$/);
  if (!m) continue;
  const idx = parseInt(m[1].slice(0, 2), 16);
  const bank = parseInt(m[1].slice(2, 4), 16);
  names.set(bank + ':' + idx, { name: m[2].replace(/^__/, ''), src: 'common' });
}
// ---- names from NC3000_trap.txt (GBK): "<bank><idx> <addr> //name"
const trap = new TextDecoder('gbk').decode(fs.readFileSync('info/NC3000_trap.txt'));
for (const line of trap.split(/\r?\n/)) {
  const m = line.match(/^([0-9A-Fa-f]{4})\s+([0-9A-Fa-f]{4})\s*(?:\/\/\s*(.*))?$/);
  if (!m) continue;
  const code = parseInt(m[1], 16);
  const bank = (code >> 8) & 0xff, idx = code & 0xff;
  const key = bank + ':' + idx;
  const nm = (m[3] || '').trim().split(/\s{2,}/)[0];
  const prev = names.get(key);
  if (!prev) names.set(key, { name: nm, src: 'trap' });
  else if (!prev.name) prev.name = nm;
}

const banks = [];
for (let k = 0; k < rom.length / 0x8000; k++) banks.push(k);

console.log(['int', 'bank', 'idx', 'bank_base', 'handler_addr', 'cpu_addr', 'name', 'name_src'].join('\t'));
for (const bank of banks) {
  const base = bank === 0 ? 0x8000 : 0x4000;
  const lo = bank === 0 ? 0x8000 : 0x4000, hi = 0xbfff;
  const s = bank * 0x8000;
  for (let idx = 1; idx < 0x80; idx++) {
    const v = rom.readUInt16LE(s + 2 * idx);
    const info = names.get(bank + ':' + idx);
    const ok = v >= lo && v <= hi;
    if (!ok && !info) break;               // table ends here
    if (!ok && info) continue;             // known name but table slot unusable
    console.log([
      '$' + ((bank << 8) | idx).toString(16).padStart(4, '0'),
      bank.toString(16).padStart(2, '0'),
      idx.toString(16).padStart(2, '0'),
      '0x' + base.toString(16),
      '0x' + v.toString(16),
      '0x' + (base + 2 * idx).toString(16),
      info ? info.name : '',
      info ? info.src : '',
    ].join('\t'));
  }
}
// ---- 8KB images (bank code 0xC0-0xCF) living at chip offset 0x2000*n
console.error('--- 8KB image tables ---');
for (let n = 0; n < 16; n++) {
  const s = 0x2000 * n;
  if (rom.readUInt16LE(s) !== 0xea60) continue;
  let cnt = 0, max = 0;
  for (let idx = 1; idx < 0x80; idx++) {
    const v = rom.readUInt16LE(s + 2 * idx);
    if (v >= 0xc000 && v <= 0xdfff) { cnt++; if (v > max) max = v; } else break;
  }
  console.error(`  image 0xC${n.toString(16)}: chip 0x${s.toString(16)} (bank ${(s >> 15).toString(16)} off 0x${(s % 0x8000).toString(16).padStart(4, '0')}), ` +
    `services=${cnt}, targets 0xC000-0x${max.toString(16)}, spans 0x${(max - 0xc000 + 1).toString(16)} bytes`);
}
