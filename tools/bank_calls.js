// List the "send byte / wait handshake / read byte" call sites in one bank,
// showing the byte that was loaded into A just before each call.
// usage: node tools/bank_calls.js <bankHex> <loCpu> <hiCpu>
const fs = require('fs');
const path = require('path');
const b = fs.readFileSync(path.join(__dirname, '..', 'info', 'NC3KSYSNOR.nor'));

const bank = parseInt(process.argv[2], 16);
const bankBase = bank * 0x8000;
const winBase = bank === 0 ? 0x8000 : 0x4000;
const lo = parseInt(process.argv[3], 16) - winBase + bankBase;
const hi = parseInt(process.argv[4], 16) - winBase + bankBase;

const targets = {
  0x9daa: 'send byte  ($FB1C)',
  0x9e3d: 'wait ready ($FABE)',
};
for (let o = lo; o < hi; o++) {
  if (b[o] !== 0x20) continue;
  const tgt = b[o + 1] | (b[o + 2] << 8);
  let what = targets[tgt];
  if (what === undefined) continue;
  const cpu = winBase + (o - bankBase);
  let prev = '';
  for (let k = Math.max(lo, o - 6); k < o; k += 1) prev += b[k].toString(16).padStart(2, '0') + ' ';
  console.log('  ' + cpu.toString(16).padStart(4, '0') + '  JSR $' + tgt.toString(16) +
    ' ' + what + '   prev: ' + prev.trim() + '   A=#' + b[o - 1].toString(16).padStart(2, '0'));
}
