// NC3000 NAND (NGFFS) file list + best-effort extraction.
//
// The inode table gives every file's *logical* block extent (count x 16 KB);
// the dump is the *physical* image, so the same file appears at a monotone but
// non-linear physical address (bad/spare blocks are skipped).  We therefore
// locate each file by matching its extent size against the used regions of the
// physical image, in the same order the inodes were allocated.
//
// usage: node tools/nand_extract.js <nand> [outDir] [--list]
const fs = require('fs');
const path = require('path');
const b = fs.readFileSync(process.argv[2]);
const outDir = process.argv[3] && !process.argv[3].startsWith('--') ? process.argv[3] : null;
const BLK = 16384;
const dec = new TextDecoder('gbk');

/* ---------------- inode table (records end with A8 A8) ---------------- */
const inodes = new Map();
for (let i = 0; i + 31 < 0x4000; i += 2) {
  if (b[i + 30] !== 0xA8 || b[i + 31] !== 0xA8) continue;
  const r = b.slice(i, i + 32);
  const id = r.readUInt16LE(0);
  if (id === 0 || id > 1024) continue;
  inodes.set(id, {
    id, attr: r.readUInt16LE(2), status: r[4],
    count: r.readUInt16LE(14),
    blk0: r.readUInt16LE(18),
    blk: [r.readUInt16LE(18), r.readUInt16LE(20), r.readUInt16LE(22), r.readUInt16LE(24)],
    isDir: (r.readUInt16LE(2) & 0xF000) === 0xE000,
  });
  i += 30;
}

/* ---------------- directory blocks (16-byte name records) ------------- */
function readDir(logicalBlk) {
  // directory blocks live in the physical area too; find them by scanning for
  // a record run that starts with an id whose name matches nothing else.
  // Simpler: directories are small (1..n blocks) and were written in order, so
  // we scan the whole image once and index all runs.
  return null;
}
function nameOf(off) { return dec.decode(b.slice(off + 2, off + 16)).split('\u0000')[0]; }
const dirRuns = [];
for (let off = 0; off + 16 <= b.length; off += 16) {
  const id = b.readUInt16LE(off);
  if (id > 1024) continue;
  const nm = nameOf(off);
  if (!nm || /\ufffd/.test(nm) || !/^[\u4e00-\u9fffA-Za-z0-9_\-. /]+$/.test(nm)) continue;
  // try to walk a run
  const names = [], ids = [];
  let p = off;
  while (p + 16 <= b.length) {
    const x = b.readUInt16LE(p);
    if (x === 0xFFFF) break;
    if (x > 1024) break;
    const n2 = nameOf(p);
    if (!n2 || /\ufffd/.test(n2) || !/^[\u4e00-\u9fffA-Za-z0-9_\-. /]+$/.test(n2)) break;
    names.push(n2); ids.push(x); p += 16;
  }
  if (names.length >= 4) {
    dirRuns.push({ off, names, ids });
    off = p - 16;                        // skip past the run
  }
}
// keep only the runs that look like real directories (ids all valid inodes)
const dirs = dirRuns.filter((r) => r.ids.every((x) => x === 0 || inodes.has(x)));

/* ---------------- used regions in the physical image ------------------ */
const SEC = 2048;
const nsec = Math.floor(b.length / SEC);
const used = new Uint8Array(nsec);
for (let i = 0; i < nsec; i++) {
  let ff = 0;
  const s = b.slice(i * SEC, (i + 1) * SEC);
  for (const c of s) if (c === 0xFF) ff++;
  used[i] = ff < SEC * 0.9 ? 1 : 0;
}
const regions = [];
for (let i = 0; i < nsec;) {
  if (!used[i]) { i++; continue; }
  let j = i;
  while (j < nsec && used[j]) j++;
  regions.push({ start: i * SEC, end: j * SEC });
  i = j;
}

if (process.argv.includes('--list')) {
  console.log('=== directories found ===');
  for (const d of dirs) {
    console.log(`@0x${d.off.toString(16)} ${d.names.length} entries`);
    console.log('   ' + d.names.map((n, i) => `${d.ids[i]}:${n}`).join(' | '));
  }
  console.log('\n=== inodes ===');
  for (const n of [...inodes.values()].sort((a, c) => a.id - c.id)) {
    console.log(`#${String(n.id).padStart(3)} ${n.isDir ? 'DIR ' : 'FILE'} attr=0x${n.attr.toString(16)} ` +
      `blocks=${String(n.count).padStart(5)} size=${(n.count * BLK / 1024).toFixed(0)}KB blk0=0x${n.blk0.toString(16)}`);
  }
  console.log(`\n=== physical used regions (${regions.length}) ===`);
  for (const r of regions) console.log(`  0x${r.start.toString(16)}..0x${r.end.toString(16)} ` +
    `${((r.end - r.start) / 1024).toFixed(0)}KB`);
}

if (outDir) {
  fs.mkdirSync(outDir, { recursive: true });
  // names by inode (first directory run wins)
  const nameById = new Map();
  for (const d of dirs) d.names.forEach((n, i) => { if (!nameById.has(d.ids[i])) nameById.set(d.ids[i], n); });
  // greedy size match, in inode order, over the regions in physical order
  const pool = regions.slice().sort((a, c) => a.start - c.start);
  const manifest = [];
  for (const n of [...inodes.values()].sort((a, c) => a.id - c.id)) {
    const want = n.count * BLK;
    if (n.isDir || want < BLK) continue;
    const nm = nameById.get(n.id) || `inode_${n.id}`;
    /* pick the region whose size matches best (exact size wins, and the
     * "used" detector can cut the last sector, so allow -3%) */
    let best = null, bestErr = Infinity;
    for (const r of pool) {
      const size = r.end - r.start;
      if (size < want * 0.97) continue;
      const err = Math.abs(size - want);
      if (err < bestErr) { bestErr = err; best = r; }
    }
    if (!best) continue;
    const from = best.start;
    const to = from + want;
    const safe = nm.replace(/[^\w\u4e00-\u9fff.\-]/g, '_');
    fs.writeFileSync(path.join(outDir, `${String(n.id).padStart(3, '0')}_${safe}.bin`), b.slice(from, to));
    manifest.push({ id: n.id, name: nm, blocks: n.count, size: want, from, to });
    best.start = to;                     // consume
  }
  fs.writeFileSync(path.join(outDir, 'manifest.json'), JSON.stringify(manifest, null, 1));
  console.log(`extracted ${manifest.length} files to ${outDir}`);
  for (const m of manifest) {
    console.log(`  #${String(m.id).padStart(3)} ${m.name.padEnd(18)} ${(m.size / 1024).toFixed(0).padStart(6)}KB ` +
      `@0x${m.from.toString(16)}`);
  }
}
