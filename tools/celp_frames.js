// Slice 18-byte S600 frames out of the NC3000's sysdir/celp_data (inode 26).
//
// Why this exists: the NAND is 528-byte pages (512 data + 16 spare), so the
// deinterleave must start on a page boundary.  The older extraction
// (docs/NC3000_单词发音数据.md §1) started at physical 0x2AAB000, which is
// 400 bytes into a page, so every 512-byte window carried 16 bytes of spare
// area - celp_data then looked like "offset tables + 5-byte records" and no
// amount of host-side work made it audible.  Page-aligned, celp_data is one
// continuous 18-byte/frame S600 stream (each frame's first byte has high
// nibble 0xF) that starts 10 bytes into the file; the frame phase drifts by
// 16384 mod 18 = 4 per 16 KB block, which is exactly what a continuous
// stream does.
//
// usage: node tools/celp_frames.js <frameFrom> <frameCount> <outFile> [inode]
//   e.g. node tools/celp_frames.js 0 300 spce061a/build/celp/a_start.bin
const fs = require('fs');
const path = require('path');

const NAND = path.join(__dirname, '..', 'info', 'NC3KSYSNAND.nand');
const PAGE = 528, DATA = 512, BLK = 0x4000;
const HEAD = 10;                       // header bytes before frame 0

const raw = fs.readFileSync(NAND);
const pages = Math.floor(raw.length / PAGE);
const img = Buffer.alloc(pages * DATA);
for (let p = 0; p < pages; p++) raw.copy(img, p * DATA, p * PAGE, p * PAGE + DATA);

function inode(id) {
  for (let i = 0; i + 31 < 0x4000; i += 2) {
    if (img[i + 30] !== 0xA8 || img[i + 31] !== 0xA8) continue;
    if (img.readUInt16LE(i) !== id) continue;
    const blk = [];
    for (let j = 0; j < 4; j++) blk.push(img.readUInt16LE(i + 18 + 2 * j));
    return { cnt: img.readUInt16LE(i + 14), blk, idx: [img.readUInt16LE(i + 26), img.readUInt16LE(i + 28)] };
  }
  return null;
}

// GFFS: blk[0..2] are the first three blocks.  When a file needs more than
// that, the 4th word normally points at a block list; on this image the big
// files are allocated as one contiguous run instead (celp_data's stream is
// byte-continuous across its 16 KB blocks, which is how we know), and the
// "block list" block then holds unrelated bookkeeping.  So: accept the
// indirect list only if it is self-consistent, else assume a contiguous run.
function fileBytes(id) {
  const ino = inode(id);
  if (!ino) throw new Error('inode ' + id + ' not found');
  const direct = ino.cnt <= 4 ? ino.cnt : 3;
  const head = ino.blk.slice(0, direct).filter(b => b !== 0xFFFF);
  let list = head.slice();
  if (ino.cnt > 4) {
    const extra = [];
    const n = Math.min(ino.cnt - list.length, BLK / 2);
    for (let k = 0; k < n; k++) {
      const b = img.readUInt16LE(ino.blk[3] * BLK + 2 * k);
      if (b === 0xFFFF) continue;
      extra.push(b);
    }
    const ok = extra.length === ino.cnt - head.length && extra.every(b => b * BLK + BLK <= img.length);
    if (ok) list = list.concat(extra);
    else for (let i = head.length; i < ino.cnt; i++) list.push(ino.blk[0] + i);
  }
  return Buffer.concat(list.slice(0, ino.cnt).map(b => img.slice(b * BLK, (b + 1) * BLK)));
}

const from = parseInt(process.argv[2] || '0', 10);
const count = parseInt(process.argv[3] || '300', 10);
const out = process.argv[4] || path.join(__dirname, '..', 'spce061a', 'build', 'celp', 'slice.bin');
const id = parseInt(process.argv[5] || '26', 10);
// celp_data carries a 10-byte header before frame 0; the other sysdir files
// (word_ID, ahd_celp, ...) start straight in.
const head = process.argv[6] !== undefined ? parseInt(process.argv[6], 10) : (id === 26 ? HEAD : 0);

const f = fileBytes(id);
const off = head + from * 18;
const seg = f.slice(off, off + count * 18);
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, seg);
console.log(`inode ${id}: ${f.length} bytes; frames ${from}..${from + Math.floor(seg.length / 18)} @ ${off} -> ${out} (${seg.length} B)`);
console.log('  first frame: ' + [...seg.slice(0, 18)].map(b => b.toString(16).padStart(2, '0')).join(' '));
