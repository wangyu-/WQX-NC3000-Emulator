// Walk the celp_data descriptor linked list.
//   descriptor at file offset 2V is 5 bytes: [nextV(2)][u2][u3][u4]
//   next V = LE16 at 2V ; chain ends on 0xFFFE / 0xFFFF
// usage: node tools/celp_chain.js <celp_data.bin> <V0> [maxItems] [dumpBytes]
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
let v = parseInt(process.argv[3], 16);
const maxItems = parseInt(process.argv[4] || '40', 10);
const dumpBytes = process.argv[5] === 'dump';

console.log('start V=0x' + v.toString(16) + ' (file offset 0x' + (2 * v).toString(16) + ')');
let total = 0;
for (let k = 0; k < maxItems; k++) {
  const o = 2 * v;
  if (o + 5 > b.length) { console.log('  overrun'); break; }
  const next = b.readUInt16LE(o);
  const u2 = b[o + 2], u3 = b[o + 3], u4 = b[o + 4];
  const len = (u2 & 0x3F) + (u4 ? 1 : 0);
  console.log('  #' + String(k).padStart(3) + ' V=0x' + v.toString(16).padStart(5, '0') +
    ' off=0x' + o.toString(16).padStart(6, '0') +
    '  next=0x' + next.toString(16).padStart(5, '0') +
    '  u2=%02x u3=%02x u4=%02x  len=' + len +
    (dumpBytes ? '  payload@+5: ' + b.slice(o + 5, o + 5 + len).toString('hex') : ''));
  total += len;
  if (next === 0xFFFE || next === 0xFFFF) { console.log('  END marker 0x' + next.toString(16) + ' after ' + (k + 1) + ' items, payload total ' + total + ' bytes'); break; }
  if (next === v) { console.log('  self loop, stop'); break; }
  v = next;
}
