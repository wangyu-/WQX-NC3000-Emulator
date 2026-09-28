// Find GFFS directory records whose name field equals the given name, then show
// the surrounding 16-byte records (i.e. the directory block they live in).
// usage: node tools/nand_findname.js <nand> <name>
const fs = require('fs');
const b = fs.readFileSync(process.argv[2]);
const want = process.argv[3];
const dec = new TextDecoder('gbk');
const pat = Buffer.concat([Buffer.from(want, 'latin1'), Buffer.from([0])]);

const show = (base, k0, k1) => {
  for (let k = k0; k <= k1; k++) {
    const o = base + k * 16;
    if (o < 0 || o + 16 > b.length) continue;
    const rec = b.slice(o, o + 16);
    const id = rec.readUInt16LE(0);
    const nm = dec.decode(rec.slice(2, 16)).split('\u0000')[0];
    console.log(`   0x${o.toString(16)}  ${rec.toString('hex').match(/../g).join(' ')}  ` +
      `id=${id} name="${nm}"`);
  }
};

let hits = [], i = 0;
while ((i = b.indexOf(pat, i)) !== -1) {
  const recOff = i - 2;
  if (recOff >= 0) {
    const id = b.readUInt16LE(recOff);
    if (id > 0 && id < 1024) hits.push(recOff);
  }
  i++;
}
console.log(`records for name "${want}": ${hits.length}`,
  hits.slice(0, 12).map((x) => '0x' + x.toString(16)).join(' '));
for (const r of hits.slice(0, 3)) {
  const base = r - (r % 16);
  console.log(`--- context around 0x${r.toString(16)} (16-aligned 0x${base.toString(16)}) ---`);
  show(base, -4, 12);
}
