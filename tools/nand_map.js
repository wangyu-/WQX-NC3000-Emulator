// Content map of a NAND region: per 16 KB block, entropy + text ratio, so file
// boundaries (program text vs CELP/compressed audio vs erased) are visible.
// usage: node tools/nand_map.js <nand> <startHex> <endHex> [blockSize]
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
const start = parseInt(process.argv[3], 16);
const end = parseInt(process.argv[4], 16);
const BLK = parseInt(process.argv[5] || '16384', 10);
const dec = new TextDecoder('gbk');

for (let off = start; off < end; off += BLK) {
  const s = b.slice(off, Math.min(off + BLK, b.length));
  const freq = new Array(256).fill(0);
  let ff = 0;
  for (const c of s) { freq[c]++; if (c === 0xFF) ff++; }
  let H = 0;
  for (const f of freq) if (f) { const p = f / s.length; H -= p * Math.log2(p); }
  // text ratio: how many bytes decode into plausible CJK/ASCII when read as GBK
  const t = dec.decode(s);
  let good = 0;
  for (const ch of t) {
    const v = ch.codePointAt(0);
    if (v === 9 || v === 10 || v === 13 || (v >= 0x20 && v < 0x7F) ||
        (v >= 0x4E00 && v <= 0x9FFF) || (v >= 0x3000 && v <= 0x303F)) good++;
  }
  console.log(`0x${off.toString(16).padStart(7, '0')} blk 0x${Math.floor(off / BLK).toString(16).padStart(4, '0')} ` +
    `H=${H.toFixed(2)} ff=${(ff / s.length * 100).toFixed(0)}% text=${(good / [...t].length * 100).toFixed(0)}%  ` +
    `head: ${dec.decode(s.slice(0, 24)).replace(/[^\x20-\x7e\u4e00-\u9fff]/g, '.')}`);
}
