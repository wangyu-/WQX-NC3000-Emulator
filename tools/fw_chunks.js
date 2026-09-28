// Per-chunk alignment: for each 4 KB chunk of the reference image, find the best
// match inside a NAND region (tries normal and byte-swapped comparison), so we
// can tell whether the NAND copy is contiguous, shifted, or byte-swapped.
// usage: node tools/fw_chunks.js <nand> <refDat> <regionStartHex> <regionEndHex>
const fs = require('fs');
const nand = fs.readFileSync(process.argv[2]);
const ref = fs.readFileSync(process.argv[3]);
const start = parseInt(process.argv[4], 16);
const end = parseInt(process.argv[5], 16);
const CH = 0x1000;

function bestIn(needle, swap) {
  let best = { off: 0, ratio: -1 };
  const sample = [];
  for (let i = 0; i < CH; i += 4) sample.push(i);
  for (let off = start; off + CH <= end; off += 2) {
    let same = 0;
    for (const i of sample) {
      let a = nand.readUInt16LE(off + i);
      let b = ref.readUInt16LE(needle.base + i);
      if (swap) { a = ((a & 0xFF) << 8) | (a >> 8); b = ((b & 0xFF) << 8) | (b >> 8); }
      if (a === b) same++;
    }
    const ratio = same / sample.length;
    if (ratio > best.ratio) best = { off, ratio };
  }
  return best;
}

console.log('chunk   best-offset      similarity   (normal / byte-swapped)');
for (let c = 0; c + CH <= ref.length; c += CH) {
  const needle = { base: c };
  const n = bestIn(needle, false);
  const s = bestIn(needle, true);
  console.log(`0x${c.toString(16).padStart(4, '0')}  0x${n.off.toString(16).padStart(8, '0')}  ` +
    `${(n.ratio * 100).toFixed(1)}%  /  0x${s.off.toString(16)} ${(s.ratio * 100).toFixed(1)}%`);
}
