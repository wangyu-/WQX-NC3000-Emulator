// Full-image scan for GFFS directory blocks: runs of 16-byte records
// [2B inode id][14B GBK name (00, FF pad)] ending with an FF FF record.
// usage: node tools/nand_dirs.js <nand> [minEntries] [maxPrint]
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
const minEntries = parseInt(process.argv[3] || '3', 10);
const maxPrint = parseInt(process.argv[4] || '200', 10);
const dec = new TextDecoder('gbk');

const good = (nm) => nm && nm.length <= 14 && !/\ufffd/.test(nm) &&
                    /^[\u4e00-\u9fffA-Za-z0-9_\- ./]+$/.test(nm);
const nameOf = (off) => dec.decode(b.slice(off + 2, off + 16)).split('\u0000')[0];

const hits = [];
for (let off = 0; off + 16 <= b.length; off += 2) {
  const id = b.readUInt16LE(off);
  if (id === 0 || id > 1024) continue;
  const nm = nameOf(off);
  if (!good(nm)) continue;
  // walk the run
  const names = [nm];
  const ids = [id];
  let p = off + 16;
  while (p + 16 <= b.length && names.length < 1500) {
    const x = b.readUInt16LE(p);
    if (x === 0xFFFF || x === 0) break;
    if (x > 1024) break;
    const n2 = nameOf(p);
    if (!good(n2)) break;
    names.push(n2); ids.push(x); p += 16;
  }
  if (names.length >= minEntries) hits.push({ off, names, ids });
  if (names.length > 1) off = p - 2;          // skip past this run
}

console.log(`runs with >= ${minEntries} entries: ${hits.length}`);
for (const h of hits.slice(0, maxPrint)) {
  console.log(`\n@0x${h.off.toString(16)} (block 0x${Math.floor(h.off / 16384).toString(16)}, ` +
    `+0x${(h.off % 16384).toString(16)}) ${h.names.length} entries:`);
  console.log('   ' + h.names.slice(0, 60).map((n, i) => `${h.ids[i]}:${n}`).join(' | '));
}
