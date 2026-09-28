// Find used regions in the NAND dump (runs of non-erased 2 KB sectors) and
// characterise them, so inode extents (count x 2 KB) can be matched to files.
// usage: node tools/nand_regions.js <nand> [minKB]
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
const minKB = parseInt(process.argv[3] || '16', 10);
const SEC = 2048;
const dec = new TextDecoder('gbk');

const nsec = Math.floor(b.length / SEC);
const used = new Uint8Array(nsec);
for (let i = 0; i < nsec; i++) {
  const s = b.slice(i * SEC, (i + 1) * SEC);
  let ff = 0;
  for (const c of s) if (c === 0xFF) ff++;
  used[i] = ff < SEC * 0.9 ? 1 : 0;      // >=10% programmed -> "used"
}

let i = 0;
const out = [];
while (i < nsec) {
  if (!used[i]) { i++; continue; }
  let j = i;
  while (j < nsec && used[j]) j++;
  const from = i * SEC, to = j * SEC;
  const s = b.slice(from, to);
  let ff = 0, ent = 0;
  const freq = new Array(256).fill(0);
  for (const c of s) { freq[c]++; if (c === 0xFF) ff++; }
  for (const f of freq) if (f) { const p = f / s.length; ent -= p * Math.log2(p); }
  const t = dec.decode(s);
  let good = 0, cjk = 0;
  for (const ch of t) {
    const v = ch.codePointAt(0);
    if ((v >= 0x20 && v < 0x7F) || (v >= 0x4E00 && v <= 0x9FFF)) good++;
    if (v >= 0x4E00 && v <= 0x9FFF) cjk++;
  }
  out.push({ sec0: i, sec1: j - 1, bytes: to - from, ent, ff: ff / s.length,
             text: good / [...t].length, cjk: cjk / [...t].length,
             head: dec.decode(s.slice(0, 20)).replace(/[^\x20-\x7e\u4e00-\u9fff]/g, '.') });
  i = j;
}

out.sort((a, c) => c.bytes - a.bytes);
console.log(`used regions: ${out.length} (>= ${minKB} KB shown: ${out.filter((r) => r.bytes / 1024 >= minKB).length})`);
for (const r of out) {
  if (r.bytes / 1024 < minKB) continue;
  console.log(`sectors 0x${r.sec0.toString(16)}..0x${r.sec1.toString(16)} ` +
    `${(r.bytes / 1024).toFixed(0)}KB @0x${(r.sec0 * SEC).toString(16)} ` +
    `H=${r.ent.toFixed(2)} ff=${(r.ff * 100).toFixed(0)}% cjk=${(r.cjk * 100).toFixed(0)}% "${r.head}"`);
}
