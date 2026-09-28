// Find offset-table signatures in the NAND dump: runs of strictly increasing
// 16-bit little-endian values with small deltas (the "word -> audio offset"
// style tables the CELP banks start with).
// usage: node tools/nand_find_table.js <nand> [minRun] [maxDelta]
const fs = require('fs');

const b = fs.readFileSync(process.argv[2]);
const minRun = parseInt(process.argv[3] || '24', 10);
const maxDelta = parseInt(process.argv[4] || '0x800', 16);

const runs = [];
let start = 0, prev = null, n = 0;
for (let off = 0; off + 1 < b.length; off += 2) {
  const v = b[off] | (b[off + 1] << 8);
  if (prev !== null && v > prev && v - prev <= maxDelta) {
    n++;
  } else {
    if (n >= minRun) runs.push({ start, len: n, from: b[start] | (b[start + 1] << 8), to: prev });
    start = off; n = 0;
  }
  prev = v;
}
if (n >= minRun) runs.push({ start, len: n, from: b[start] | (b[start + 1] << 8), to: prev });

runs.sort((a, c) => c.len - a.len);
console.log('found ' + runs.length + ' runs (>= ' + minRun + ' entries)');
for (const r of runs.slice(0, 30)) {
  console.log('  @0x' + r.start.toString(16).padStart(7, '0') + '  entries=' + r.len +
    '  value 0x' + r.from.toString(16) + ' -> 0x' + r.to.toString(16) +
    '  (' + (r.len * 2) + ' bytes)');
}
