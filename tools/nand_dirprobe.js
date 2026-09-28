// Probe candidate (block address, unit) pairs for GFFS directory content:
// a directory block holds 16-byte records [2B inode id][14B GBK name, 00,
// FF padding] terminated by an FF FF record.
// usage: node tools/nand_dirprobe.js <nand> <blkAddr> [units...]
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
const addr = parseInt(process.argv[3], 16);
const units = process.argv.slice(4).map((s) => parseInt(s, 10));
const dec = new TextDecoder('gbk');

function nameOf(rec) {
  return dec.decode(rec.slice(2, 16)).split('\u0000')[0];
}
function goodName(nm) {
  return nm && nm.length <= 14 && !/\ufffd/.test(nm) &&
         /^[\u4e00-\u9fffA-Za-z0-9_\- ./]+$/.test(nm);
}
/* find the first run of >=min consecutive plausible records inside [base, base+span) */
function probe(base, span) {
  for (let off = base; off + 16 <= base + span; off += 2) {
    let run = 0, names = [];
    for (let i = 0; i < 64; i++) {
      const r = b.slice(off + i * 16, off + i * 16 + 16);
      if (r.length < 16) break;
      const id = r.readUInt16LE(0);
      if (id === 0xFFFF) { run = i; break; }
      if (id === 0 || id > 1024) break;
      const nm = nameOf(r);
      if (!goodName(nm)) break;
      names.push(nm);
      run = i + 1;
    }
    if (run >= 3) return { off, run, names };
  }
  return null;
}

for (const unit of units) {
  const base = addr * unit;
  if (base + 32 > b.length) { console.log(`unit ${unit}: 0x${base.toString(16)} beyond dump`); continue; }
  const r = probe(base, 65536);
  console.log(`unit ${String(unit).padStart(5)} base 0x${base.toString(16).padStart(7, '0')} -> ` +
    (r ? `FOUND at +0x${(r.off - base).toString(16)}: ${r.run} entries: ${r.names.slice(0, 10).join(' | ')}`
       : 'no directory run'));
}
