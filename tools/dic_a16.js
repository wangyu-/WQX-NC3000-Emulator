// Build .a16 candidates out of japanese.dic's 5-byte audio chunks.
//   chunk = [u0 u1 u2 u3 u4] ; payload bytes = [u3, u2 & 0x3F, u4]
//   mode seq   : chunks are consecutive (file order)
//   mode chain : follow V24 = u0 | (u1<<8) | ((u2 & 0x3F) << 16), next off = 5*V24
// usage: node tools/dic_a16.js <dic> <mode> <startV|startOff> <chunks> <paramHex> <out.a16>
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
const mode = process.argv[3];
const start = parseInt(process.argv[4], 16);
const nChunks = parseInt(process.argv[5], 10);
const param = parseInt(process.argv[6], 16);
const outPath = process.argv[7];

const pay = [];
let off, v;
if (mode === 'seq') { off = start; }
else { v = start; }
const seen = new Set();
for (let k = 0; k < nChunks; k++) {
  if (mode === 'seq') { if (off + 5 > b.length) break; }
  else {
    off = 5 * v;
    if (off + 5 > b.length || seen.has(v)) break;
    seen.add(v);
  }
  const u = [b[off], b[off + 1], b[off + 2], b[off + 3], b[off + 4]];
  pay.push(u[3], u[2] & 0x3f, u[4]);
  if (mode === 'chain') {
    const nv = u[0] | (u[1] << 8) | ((u[2] & 0x3f) << 16);
    if (nv === 0 || nv === 0xffffff || nv === v) break;
    v = nv;
  } else off += 5;
}
const buf = Buffer.from(pay);
const f = Buffer.alloc(6 + buf.length);
f.writeUInt32LE(buf.length + 2, 0);
f.writeUInt16LE(param, 4);
buf.copy(f, 6);
fs.writeFileSync(outPath, f);
console.log('mode=' + mode + ' start=0x' + start.toString(16) + ' chunks=' + (buf.length / 3) +
  ' payload=' + buf.length + 'B param=0x' + param.toString(16) + ' -> ' + outPath);
