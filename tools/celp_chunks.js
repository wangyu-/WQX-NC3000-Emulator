// Follow the real chunk chain of sysdir/celp_data.
//
// From NC3000 BANK0B $17F0 / $9DA0-$9D63:
//   chunk at 5-byte file offset 5*V ; chunk = [u0 u1 u2 u3 u4]
//   next V  = u0 | (u1<<8) | ((u2 & 0x3F) << 16)      (24-bit, in 5-byte units)
//   next off= 5 * nextV + base(0x0A5A clusters -> file relative 5*nextV)
//   bytes sent to the 061 per chunk = [u3, u2 & 0x3F, u4]
// usage: node tools/celp_chunks.js <celp_data.bin> <V0> <maxChunks> <out.bin> [mode]
//   mode 'bytes' (default) = 3 payload bytes per chunk
//   mode 'raw'             = the raw 5 bytes per chunk
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
let v = parseInt(process.argv[3], 16);
const maxC = parseInt(process.argv[4], 10);
const outPath = process.argv[5];
const mode = process.argv[6] || 'bytes';

const out = [];
let visited = new Set();
for (let k = 0; k < maxC; k++) {
  const o = 5 * v;
  if (o + 5 > b.length) { console.log('  overrun at chunk ' + k); break; }
  const u = [b[o], b[o + 1], b[o + 2], b[o + 3], b[o + 4]];
  if (k < 8) console.log('  #' + k + ' V=0x' + v.toString(16) + ' off=0x' + o.toString(16) +
    ' chunk=' + u.map(x => x.toString(16).padStart(2, '0')).join(' '));
  if (mode === 'raw') out.push(...u); else out.push(u[3], u[2] & 0x3f, u[4]);
  const nextV = u[0] | (u[1] << 8) | ((u[2] & 0x3f) << 16);
  if (nextV === 0xFFFFFF || nextV === 0 || nextV === v || visited.has(nextV)) {
    console.log('  stop at chunk ' + k + ' (nextV=0x' + nextV.toString(16) + ')');
    break;
  }
  visited.add(v);
  v = nextV;
}
const buf = Buffer.from(out);
const f = Buffer.alloc(2 + buf.length);
f.writeUInt16LE(buf.length, 0); buf.copy(f, 2);
fs.writeFileSync(outPath, f);
console.log('chunks -> ' + (buf.length / (mode === 'raw' ? 5 : 3)) + ' chunks, ' + buf.length +
  ' bytes, wrote ' + outPath + ' (' + f.length + ' bytes incl. u16 count)');
