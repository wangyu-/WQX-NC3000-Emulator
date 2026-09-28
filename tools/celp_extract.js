// Emit the "0x11 0x03" record payloads for one syllable of sysdir/celp_data.
// usage: node tools/celp_extract.js <celp_data.bin> <tableBlock> <entryIdx>
//          <dataBlock> <variant> <out.bin>
// variant: 0=[u3,u2&3f,u4] 1=[u0,u1,u2] 2=[u0,u1,u2&3f] 3=[u3,u4,u2&3f]
const fs = require('fs');

const b = fs.readFileSync(process.argv[2]);
const k = parseInt(process.argv[3], 10);
const idx = parseInt(process.argv[4], 10);
const dataBlock = parseInt(process.argv[5], 10);
const variant = parseInt(process.argv[6], 10);
const outPath = process.argv[7];
const TABLES_END = 0x44000, DATA = 0x44000, BLK = 65536;

const blocks = [];
{
  let s = 0, prev = null;
  for (let i = 0; i < TABLES_END; i += 2) {
    const v = b.readUInt16LE(i);
    if (prev !== null && v < prev) { blocks.push({ start: s, end: i }); s = i; }
    prev = v;
  }
  blocks.push({ start: s, end: TABLES_END });
}
const blk = blocks[k];
const from = b.readUInt16LE(blk.start + 2 * (idx - 1));
const to = b.readUInt16LE(blk.start + 2 * idx);
const len = (to - from) * 2;                 // offsets are in 2-byte units
const base = DATA + dataBlock * BLK + from * 2;
const recLen = (variant >= 4) ? 3 : 5;       // 4/5 = raw 3-byte records
const nrec = Math.floor(len / recLen);

const out = Buffer.alloc(2 + nrec * 3);
out.writeUInt16LE(nrec, 0);
for (let r = 0; r < nrec; r++) {
  const o = base + r * recLen;
  const u = [b[o], b[o + 1], b[o + 2], b[o + 3], b[o + 4]];
  const p = [u[3], u[2] & 0x3f, u[4]];       // variant 0 (firmware builder order)
  let pay;
  if (variant === 0) pay = [u[3], u[2] & 0x3f, u[4]];
  else if (variant === 1) pay = [u[0], u[1], u[2]];
  else if (variant === 2) pay = [u[0], u[1], u[2] & 0x3f];
  else pay = [u[3], u[4], u[2] & 0x3f];
  if (variant === 4) pay = [b[o], b[o + 1], b[o + 2]];          // raw, 3-byte records
  else if (variant === 5) pay = [u[3], u[2], u[4]];             // 5-byte units, raw tail 3
  else if (variant === 6) pay = [u[0], u[1], u[2]];             // 5-byte units, head 3
  out[2 + r * 3] = pay[0]; out[3 + r * 3] = pay[1]; out[4 + r * 3] = pay[2];
}
fs.writeFileSync(outPath, out);
console.log('table block ' + k + ' entry ' + idx + ': units ' + from + '..' + to +
  ' -> bytes 0x' + (from * 2).toString(16) + '..0x' + (to * 2).toString(16) +
  ' (' + len + ' B, ' + nrec + ' records), data block ' + dataBlock +
  ' @ file 0x' + (DATA + dataBlock * BLK).toString(16) + ', variant ' + variant);
console.log('  first record bytes: ' + b.slice(base, base + 15).toString('hex'));
console.log('  wrote ' + outPath + ' (' + out.length + ' bytes)');
