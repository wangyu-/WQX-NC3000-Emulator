// Scan the NC3000 NAND dump for directory blocks (16-byte name records) and
// report where each one physically lives, so the inode block addresses can be
// related to the dump's layout.
// usage: node tools/nand_scan.js <nand> [blockSize]
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
const BLK = parseInt(process.argv[3] || '16384', 10);
const dec = new TextDecoder('gbk');
const nameOk = (nm) => nm && !/\ufffd/.test(nm) && /^[\u4e00-\u9fffA-Za-z0-9_\- ./]+$/.test(nm) && nm.length >= 1;

const nblk = Math.floor(b.length / BLK);
const found = [];
for (let k = 0; k < nblk; k++) {
  const off = k * BLK;
  const id = b.readUInt16LE(off);
  if (id === 0 || id === 0xFFFF || id > 1024) continue;
  const nm = dec.decode(b.slice(off + 2, off + 16)).split('\u0000')[0];
  if (!nameOk(nm)) continue;
  let cnt = 0;
  const names = [];
  for (let i = 0; i < BLK / 16; i++) {
    const r = off + i * 16;
    const x = b.readUInt16LE(r);
    if (x === 0xFFFF || x === 0) break;
    const n2 = dec.decode(b.slice(r + 2, r + 16)).split('\u0000')[0];
    if (!nameOk(n2)) break;
    names.push(n2);
    cnt++;
  }
  found.push({ blk: k, id, name: nm, cnt, names });
}

console.log(`block size ${BLK}, blocks ${nblk}, directory-like blocks ${found.length}`);
for (const f of found.slice(0, 60)) {
  console.log(`  block 0x${f.blk.toString(16)} file 0x${(f.blk * BLK).toString(16)} ` +
    `first id=${f.id} entries=${f.cnt} names: ${f.names.slice(0, 8).join(' | ')}`);
}
