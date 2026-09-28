// find all accesses to zero-page 0x30-0x33 (NC3000 DSP interface) in the NOR dump
const fs = require('fs');
const b = fs.readFileSync(process.argv[2] || 'info/NC3KSYSNOR.nor');
const zp = { 0xa5: 'LDA', 0xa6: 'LDX', 0xa4: 'LDY', 0x85: 'STA', 0x86: 'STX', 0x84: 'STY',
  0xe6: 'INC', 0xc6: 'DEC', 0x06: 'ASL', 0x46: 'LSR', 0x26: 'ROL', 0x66: 'ROR', 0x24: 'BIT',
  0xc5: 'CMP', 0xe5: 'ADC', 0xc4: 'CPY', 0x25: 'AND', 0x05: 'ORA', 0x45: 'EOR',
  0xa1: 'LDA(zp,X)', 0x81: 'STA(zp,X)', 0xb1: 'LDA(zp),Y', 0x91: 'STA(zp),Y',
  0x61: 'ADC(zp,X)', 0x71: 'ADC(zp),Y', 0xc1: 'CMP(zp,X)', 0xd1: 'CMP(zp),Y' };
const abs = { 0x2c: 'BIT', 0x8d: 'STA', 0xad: 'LDA', 0xcd: 'CMP', 0x2d: 'AND', 0x0d: 'ORA',
  0x4d: 'EOR', 0xee: 'INC', 0xce: 'DEC', 0x2e: 'ROL', 0x4e: 'LSR', 0xa9: 'LDA#' };
const hits = [];
for (let i = 0; i + 1 < b.length; i++) {
  const o = b[i], a = b[i + 1];
  if (zp[o] && a >= 0x30 && a <= 0x33) hits.push({ off: i, op: `${zp[o]} $${a.toString(16).padStart(2, '0')}` });
  if (abs[o] && i + 2 < b.length && a >= 0x30 && a <= 0x33 && b[i + 2] === 0x00)
    hits.push({ off: i, op: `${abs[o]} $00${a.toString(16).padStart(2, '0')}` });
}
console.log('accesses to zp $30-$33:', hits.length);
for (const h of hits) {
  const bank = h.off >> 15;
  console.log(`bank ${bank.toString(16).padStart(2, '0')}  file 0x${h.off.toString(16).padStart(6, '0')}  ${h.op}`);
}
