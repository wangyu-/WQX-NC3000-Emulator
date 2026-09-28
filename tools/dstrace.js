// Trace which code stores into the NC3000 dictionary workspace zeropage cells.
// usage: node tools/dstrace.js <targets...>   e.g. node tools/dstrace.js 0d03 0d0b
const fs = require('fs');
const { disasm } = require('./dis6502.js');

const file = process.argv[2] || 'info/NC3KSYSNOR.nor';
const targets = (process.argv.slice(3).length ? process.argv.slice(3) : ['0d03', '0d0b', '0d16']).map((t) =>
  '$' + t.replace(/^\$/, '').padStart(2, '0')
);
const buf = fs.readFileSync(file);
const ins = disasm(buf, 0, buf.length, 0xc000);

for (let i = 0; i < ins.length; i++) {
  const t = ins[i].text;
  if (!t.startsWith('STA')) continue;
  const hit = targets.find((tg) => t.endsWith(tg) || t.endsWith(tg + ',X') || t.endsWith(tg + ',Y'));
  if (!hit) continue;
  const s = Math.max(0, i - 10);
  console.log('=== ' + t + '   file 0x' + ins[i].off.toString(16) + '  cpu $' + ins[i].cpu.toString(16) + '  bank ' + (ins[i].off >> 15));
  for (let k = s; k <= i; k++) {
    console.log('    $' + ins[k].cpu.toString(16).padStart(4, '0') + '  ' + ins[k].text);
  }
}
