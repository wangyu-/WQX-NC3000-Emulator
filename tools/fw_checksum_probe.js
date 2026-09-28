// Try to recover the SPCE061A firmware checksum algorithm from the known
// ID pairs: 061.dat stores (0x10F7,0x303B) twice; the NAND copy stores
// 0x0000,0x0000 / (0x793B,0x3131).
// usage: node tools/fw_checksum_probe.js <image.bin> [lenHex]
const fs = require('fs');

const img = fs.readFileSync(process.argv[2]);
const len = process.argv[3] ? parseInt(process.argv[3], 16) : img.length;
const buf = img.slice(0, len);

function words(buf, start, end, swap) {
  const out = [];
  for (let i = start; i + 1 < end; i += 2) {
    out.push(swap ? (buf[i] << 8) | buf[i + 1] : buf[i] | (buf[i + 1] << 8));
  }
  return out;
}

const results = [];
function report(name, lo, hi) {
  results.push({ name, lo: lo & 0xffff, hi: hi === undefined ? null : hi & 0xffff });
}

for (const start of [0, 2, 4, 6, 8, 10, 12]) {
  const w = words(buf, start, buf.length, false);
  let s16 = 0, s32 = 0, x16 = 0, sb = 0, sbx = 0;
  for (let i = 0; i < w.length; i++) {
    s16 = (s16 + w[i]) & 0xffff;
    s32 = (s32 + w[i]) >>> 0;
    x16 ^= w[i];
  }
  for (let i = start; i < buf.length; i++) { sb = (sb + buf[i]) & 0xffff; sbx ^= buf[i]; }
  report('skip' + start + ' sum16', s16);
  report('skip' + start + ' sum32', s32 & 0xffff, (s32 >>> 16) & 0xffff);
  report('skip' + start + ' xor16', x16);
  report('skip' + start + ' bytesum', sb);
  report('skip' + start + ' bytexor', sbx);

  // end-around carry sum
  let e = s32;
  while (e > 0xffff) e = (e & 0xffff) + (e >>> 16);
  report('skip' + start + ' sum32_fold', e);

  // sum of ~w
  let sn = 0;
  for (let i = 0; i < w.length; i++) sn = (sn + ((~w[i]) & 0xffff)) & 0xffff;
  report('skip' + start + ' sum_not', sn);

  // word sum with odd/even split
  let se = 0, so = 0;
  for (let i = 0; i < w.length; i++) (i & 1 ? (so = (so + w[i]) & 0xffff) : (se = (se + w[i]) & 0xffff));
  report('skip' + start + ' even_sum', se, so);
}

console.log('file ' + process.argv[2] + '  length used 0x' + buf.length.toString(16));
for (const r of results) {
  console.log('  ' + r.name.padEnd(22) + ' 0x' + r.lo.toString(16).padStart(4, '0') +
    (r.hi === null ? '' : ' 0x' + r.hi.toString(16).padStart(4, '0')));
}
