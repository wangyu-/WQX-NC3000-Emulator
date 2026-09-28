// Slide the 64 KB reference 061 image over a NAND region and report the best
// word-level alignment (the two files are different builds, so we look for the
// offset with the highest similarity and where they differ).
// usage: node tools/fw_align.js <nand> <refDat> <regionStartHex> <regionEndHex>
const fs = require('fs');
const nand = fs.readFileSync(process.argv[2]);
const ref = fs.readFileSync(process.argv[3]);
const start = parseInt(process.argv[4], 16);
const end = parseInt(process.argv[5], 16);
const N = ref.length >> 1;                      // 32768 words
const step = 8;                                 // sample every 8th word
const probe = [];
for (let i = 0; i < N; i += step) probe.push(i);

let best = null;
for (let off = start; off + ref.length <= end; off += 2) {
  let same = 0;
  for (const i of probe) {
    if (nand.readUInt16LE(off + i * 2) === ref.readUInt16LE(i * 2)) same++;
  }
  const ratio = same / probe.length;
  if (!best || ratio > best.ratio) best = { off, ratio, same, n: probe.length };
}

console.log(`best alignment: NAND 0x${best.off.toString(16)} <- ref 0x0 ` +
  `(similarity ${(best.ratio * 100).toFixed(1)}%, ${best.same}/${best.n} sampled words)`);

// full comparison at that alignment + difference runs
const A = best.off;
let same = 0, runs = [], cur = null;
for (let i = 0; i < N; i++) {
  const a = nand.readUInt16LE(A + i * 2), b = ref.readUInt16LE(i * 2);
  if (a === b) { same++; if (cur) { runs.push(cur); cur = null; } }
  else {
    if (!cur) cur = { from: i, to: i };
    else cur.to = i;
  }
}
if (cur) runs.push(cur);
console.log(`word-identical: ${same}/${N} = ${(same / N * 100).toFixed(1)}%`);
console.log(`difference runs: ${runs.length} (largest:)`);
runs.sort((x, y) => (y.to - y.from) - (x.to - x.from));
for (const r of runs.slice(0, 12)) {
  console.log(`  words 0x${r.from.toString(16)}..0x${r.to.toString(16)} ` +
    `(bytes 0x${(r.from * 2).toString(16)}..0x${(r.to * 2 + 1).toString(16)}) len ${r.to - r.from + 1}`);
}
