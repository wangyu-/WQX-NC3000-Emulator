// Explore the NC3000 NAND dump with the NGFFS layout from NGFFS.ppt:
//   - 32-byte inode records (Inode#, Attribute, Status, 3x date, 3B size,
//     3+2 index pointers, + sector list)
//   - directory content = 16-byte records: [2B inode index][14B name, GBK/FF]
// usage: node tools/nand_ngffs.js <nand> [cmd] [args]
const fs = require('fs');

const buf = fs.readFileSync(process.argv[2]);
const cmd = process.argv[3] || 'summary';
const UNIT = parseInt(process.env.NAND_UNIT || '2048', 10);   // bytes per block address

const dec = new TextDecoder('gbk');
function gbk(b) {
  const end = b.indexOf(0);
  const s = b.slice(0, end < 0 ? b.length : end);
  let out = '';
  for (const ch of dec.decode(s)) if (ch !== '\ufffd' && ch >= ' ') out += ch;
  return out;
}
function name14(rec) {
  const nm = gbk(rec.slice(2, 16));
  return nm.length ? nm : '<' + rec.slice(2, 16).toString('hex') + '>';
}

function inode(i) {            // 32-byte record, 1-based index
  const off = (i - 1) * 32;
  if (off + 32 > buf.length) return null;
  const r = buf.slice(off, off + 32);
  const attr = r.readUInt16LE(2);
  const list = [];
  for (let k = 0; k < 4; k++) list.push(r.readUInt16LE(18 + 2 * k));
  return {
    index: i, off, attr, status: r[4],
    size: r[13] | (r[14] << 8) | (r[15] << 16),
    nblocks: r.readUInt16LE(16),
    list,
    idx1: r.readUInt16LE(26), idx2: r.readUInt16LE(28), idx3: r.readUInt16LE(30),
    raw: r,
  };
}
function dirBlock(blkAddr, max = 20) {
  // a directory block holds 16-byte records until a record starts with FF FF
  const base = blkAddr * UNIT;
  const out = [];
  for (let i = 0; i < 4096; i += 16) {
    const rec = buf.slice(base + i, base + i + 16);
    if (rec.length < 16) break;
    const idx = rec.readUInt16LE(0);
    if (idx === 0xFFFF || idx === 0) break;
    out.push({ index: idx, name: name14(rec) });
    if (out.length >= max) break;
  }
  return out;
}

if (cmd === 'summary') {
  console.log('NAND size', buf.length, '=', (buf.length / 1048576).toFixed(1), 'MB');
  console.log('block-address unit =', UNIT, 'bytes');
  console.log('\n--- inodes 1..12 ---');
  for (let i = 1; i <= 12; i++) {
    const n = inode(i);
    console.log(`#${String(i).padStart(3)} attr=${n.attr.toString(16).padStart(4, '0')} ` +
      `st=${n.status.toString(16)} size=${n.size} nblk=${n.nblocks} list=[${n.list.map(x => x.toString(16)).join(' ')}] ` +
      `idx=${n.idx1.toString(16)},${n.idx2.toString(16)},${n.idx3.toString(16)}`);
  }
  console.log('\n--- directory at NAND offset 0x4000 (first block of the dump) ---');
  for (const e of dirBlock(0x4000 / UNIT, 80)) console.log(`  inode=${e.index}  ${e.name}`);
}

if (cmd === 'inode') {
  const i = parseInt(process.argv[4], 10);
  const n = inode(i);
  console.log(JSON.stringify({ ...n, raw: n.raw.toString('hex') }, null, 1));
}

if (cmd === 'dir') {                       // dir <inode>
  const i = parseInt(process.argv[4], 10);
  const n = inode(i);
  console.log(`inode ${i}: nblk=${n.nblocks} list=[${n.list.map(x => x.toString(16)).join(' ')}]`);
  for (const blk of n.list) {
    if (blk === 0xFFFF) break;
    console.log(` -- block 0x${blk.toString(16)} (${blk * UNIT} = file 0x${(blk * UNIT).toString(16)}) --`);
    for (const e of dirBlock(blk, 200)) console.log(`    inode=${e.index}  ${e.name}`);
  }
}

if (cmd === 'dump') {                      // dump <hexOffset> [len]
  const off = parseInt(process.argv[4], 16);
  const len = parseInt(process.argv[5] || '256', 10);
  for (let i = 0; i + 16 <= len; i += 16) {
    const r = buf.slice(off + i, off + i + 16);
    const id = r.readUInt16LE(0);
    const nm = gbk(r.slice(2, 16));
    console.log(('0x' + (off + i).toString(16)).padStart(12) + '  ' +
      r.toString('hex').match(/../g).join(' ') + '   id=' + id + ' name="' + nm + '"');
  }
}
