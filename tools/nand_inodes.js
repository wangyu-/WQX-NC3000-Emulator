// Parse the NC3000 NAND inode table (32-byte records ending in A8 A8).
// usage: node tools/nand_inodes.js <nand> [tableBytes] [filter]
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
const span = parseInt(process.argv[3] || '0x4000', 10) || 0x4000;
const filter = process.argv[4] || '';

const inodes = [];
for (let i = 0; i + 31 < span; i += 2) {
  if (b[i + 30] !== 0xA8 || b[i + 31] !== 0xA8) continue;
  const r = b.slice(i, i + 32);
  const id = r.readUInt16LE(0);
  if (id === 0 || id > 1024) continue;
  inodes.push({
    off: i, id,
    attr: r.readUInt16LE(2),
    status: r[4],
    date: r.readUInt16LE(5),
    count: r.readUInt16LE(14),
    blk: [r.readUInt16LE(18), r.readUInt16LE(20), r.readUInt16LE(22), r.readUInt16LE(24)],
    idx: [r.readUInt16LE(26), r.readUInt16LE(28)],
    raw: r,
  });
  i += 30;
}

console.log(`inode records: ${inodes.length}`);
const kind = (a) => (a & 0xF000) === 0xE000 ? 'DIR ' : (a & 0x00FF) === 0xAB ? 'SYS?' :
                    (a & 0x00FF) === 0x8A ? 'FILE' : '    ';
for (const n of inodes) {
  const size2k = n.count * 2048;
  const line = `#${String(n.id).padStart(3)} @0x${n.off.toString(16).padStart(4, '0')} ` +
    `attr=${n.attr.toString(16).padStart(4, '0')} ${kind(n.attr)} st=${n.status.toString(16)} ` +
    `cnt=${String(n.count).padStart(5)} (${(size2k / 1024).toFixed(0)}KB) ` +
    `blk=[${n.blk.map((x) => x.toString(16)).join(' ')}] idx=[${n.idx.map((x) => x.toString(16)).join(' ')}]`;
  if (!filter || line.includes(filter)) console.log(line);
}

if (!filter) {
  console.log('\n--- inodes whose block list contains 1 or 8 (root dir candidates) ---');
  for (const n of inodes) {
    if (n.blk.includes(1) || n.blk.includes(8) || n.blk.includes(0x4000 / 2048)) {
      console.log(`#${n.id} attr=0x${n.attr.toString(16)} blk=[${n.blk.map((x) => x.toString(16)).join(' ')}] count=${n.count}`);
    }
  }
}
