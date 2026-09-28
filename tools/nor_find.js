// Search NOR image for ASCII strings and 16-bit little-endian pointers.
// Usage: node tools/nor_find.js str "SPCE061" "Upgrade 061"
//        node tools/nor_find.js ptr 0x9280
const fs = require('fs');
const path = require('path');

const NOR = path.join(__dirname, '..', 'info', 'NC3KSYSNOR.nor');
const buf = fs.readFileSync(NOR);

function bankOf(off) { return off >> 15; }
function cpuOf(off) {
  const bank = off >> 15;
  const inb = off & 0x7fff;
  return bank === 0 ? 0x8000 + inb : 0x4000 + inb;
}
function desc(off) {
  return 'bank' + bankOf(off) + ' $' + cpuOf(off).toString(16) + ' (file 0x' + off.toString(16) + ')';
}
function findBytes(pat, max) {
  const hits = [];
  outer:
  for (let i = 0; i + pat.length <= buf.length; i++) {
    for (let j = 0; j < pat.length; j++) if (buf[i + j] !== pat[j]) continue outer;
    hits.push(i);
    if (max && hits.length >= max) break;
  }
  return hits;
}

const mode = process.argv[2];
if (mode === 'str') {
  for (const s of process.argv.slice(3)) {
    const pat = Buffer.from(s, 'latin1');
    const hits = findBytes(pat);
    console.log('"' + s + '" -> ' + hits.length + ' hit(s)');
    for (const h of hits.slice(0, 20)) console.log('   ' + desc(h));
  }
} else if (mode === 'ptr') {
  for (const a of process.argv.slice(3)) {
    const addr = parseInt(a, 16);
    const pat = Buffer.from([addr & 0xff, (addr >> 8) & 0xff]);
    const hits = findBytes(pat);
    console.log('$' + addr.toString(16) + ' -> ' + hits.length + ' hit(s)');
    for (const h of hits) console.log('   ' + desc(h));
  }
} else {
  console.log('usage: node tools/nor_find.js str <text>... | ptr <hexaddr>...');
}
