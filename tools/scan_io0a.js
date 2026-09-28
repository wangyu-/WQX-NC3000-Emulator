// scan writes to IO register 0x0A (io_bios_bsw) in the NC3000 NOR dump
const fs = require('fs');
const b = fs.readFileSync(process.argv[2] || 'info/NC3KSYSNOR.nor');

const hits = [];
for (let i = 0; i + 2 < b.length; i++) {
  if (b[i] === 0x85 && b[i + 1] === 0x0a) hits.push({ i, kind: 'STA zp $0A' });
  else if (b[i] === 0x8d && b[i + 1] === 0x0a && b[i + 2] === 0x00) hits.push({ i, kind: 'STA abs $000A' });
  else if (b[i] === 0x9d && b[i + 1] === 0x0a) hits.push({ i, kind: 'STA abs,X $0A??' });
}
console.log('writes to IO $0A (io_bios_bsw):', hits.length);
for (const h of hits) {
  const from = Math.max(0, h.i - 10);
  const ctx = b.slice(from, h.i).toString('hex').replace(/(..)/g, '$1 ').trim();
  console.log(h.kind.padEnd(16), 'file 0x' + h.i.toString(16).padStart(6, '0'), 'bank', (h.i >> 15).toString(16), 'prev:', ctx);
}
