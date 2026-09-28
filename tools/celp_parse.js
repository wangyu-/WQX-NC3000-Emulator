// Parse the NC3000 word-pronunciation library (sysdir/celp_data).
//
// Structure (verified against the file size):
//   [0x00000..0x44000)  75 offset-table blocks, each = 16-bit values that
//                       restart at 0 and grow to <0x8000
//   [0x44000..0x4E4000) 74 x 64 KB data blocks
// usage: node tools/celp_parse.js <celp_data.bin> [info|biggest|dump <block> <idx>]
const fs = require('fs');

const b = fs.readFileSync(process.argv[2]);
const TABLES_END = 0x44000;
const DATA = 0x44000;

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
for (const blk of blocks) {
  blk.count = (blk.end - blk.start) / 2;
  blk.entry = (i) => b.readUInt16LE(blk.start + 2 * i);
}

const cmd = process.argv[3] || 'info';
if (cmd === 'info') {
  console.log('celp_data.bin ' + b.length + ' bytes; table blocks: ' + blocks.length);
  for (let k = 0; k < blocks.length; k++) {
    const blk = blocks[k];
    let max = 0, big = 0;
    for (let i = 0; i < blk.count; i++) { const v = blk.entry(i); if (v > max) max = v; }
    for (let i = 1; i < blk.count; i++) if (blk.entry(i) - blk.entry(i - 1) > 0x100) big++;
    console.log('  block ' + String(k).padStart(2) + ' @0x' + blk.start.toString(16).padStart(5, '0') +
      '  entries=' + String(blk.count).padStart(5) + '  max=0x' + max.toString(16) +
      '  (>0x100 deltas: ' + big + ')');
  }
} else if (cmd === 'biggest') {
  let best = null;
  for (let k = 0; k < blocks.length; k++) {
    const blk = blocks[k];
    for (let i = 1; i < blk.count; i++) {
      const d = blk.entry(i) - blk.entry(i - 1);
      if (!best || d > best.d) best = { k, i, d, from: blk.entry(i - 1) };
    }
  }
  console.log(JSON.stringify(best));
} else if (cmd === 'dump') {
  const k = parseInt(process.argv[4], 10), i = parseInt(process.argv[5], 10);
  const blk = blocks[k];
  const from = blk.entry(i - 1), to = blk.entry(i);
  console.log('block ' + k + ' entry ' + i + ': units ' + from + '..' + to +
    '  delta=' + (to - from) + '  byte span 0x' + (from * 2).toString(16) + '..0x' + (to * 2).toString(16));
  const base = DATA;  // data block guess = k-1 -> adjust outside
  for (let u = from; u < to && u < from + 24; u++) {
    const o = base + u * 2;
    let s = '';
    for (let x = 0; x < 5; x++) s += (b[o + x] === undefined ? '--' : b[o + x].toString(16).padStart(2, '0')) + ' ';
    console.log('   unit ' + u + ' @0x' + o.toString(16) + ': ' + s);
  }
}
