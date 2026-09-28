// Scan the NOR image for references to absolute addresses / byte patterns.
// usage:
//   node tools/nor_scan.js addr 0x0610 0x0617      -> find abs operands pointing there
//   node tools/nor_scan.js bytes 000e00            -> find raw byte pattern
const fs = require('fs');
const path = require('path');
const buf = fs.readFileSync(path.join(__dirname, '..', 'info', 'NC3KSYSNOR.nor'));

function desc(off) {
  const bank = off >> 15;
  const inb = off & 0x7fff;
  const cpu = bank === 0 ? 0x8000 + inb : 0x4000 + inb;
  return 'bank' + bank + ' $' + cpu.toString(16).padStart(4, '0') + ' (file 0x' + off.toString(16) + ')';
}
function findBytes(pat) {
  const hits = [];
  outer:
  for (let i = 0; i + pat.length <= buf.length; i++) {
    for (let j = 0; j < pat.length; j++) if (buf[i + j] !== pat[j]) continue outer;
    hits.push(i);
  }
  return hits;
}

const OPCODES_ABS = new Set([
  0xad, 0x8d, 0xbd, 0x9d, 0x20, 0x4c, 0xcd, 0xec, 0xcc, 0xce, 0xee, 0x2c, 0xae, 0x8e, 0xac, 0x8c,
  0x0d, 0x0e, 0x2d, 0x2e, 0x4d, 0x4e, 0x6d, 0x6e, 0xed, 0xfd, 0xdd, 0xd9, 0xf9, 0x39, 0x19, 0x1d,
]);

const mode = process.argv[2];
if (mode === 'addr') {
  const lo = parseInt(process.argv[3], 16);
  const hi = parseInt(process.argv[4] || process.argv[3], 16);
  for (let a = lo; a <= hi; a++) {
    const hits = [];
    for (let i = 0; i + 2 < buf.length; i++) {
      if (buf[i + 1] === (a & 0xff) && buf[i + 2] === ((a >> 8) & 0xff) && OPCODES_ABS.has(buf[i])) hits.push(i);
    }
    console.log('$' + a.toString(16).padStart(4, '0') + ' -> ' + hits.length + ' hit(s)');
    for (const h of hits.slice(0, 24)) console.log('   ' + desc(h) + '  ' + buf.slice(h, h + 3).toString('hex'));
  }
} else if (mode === 'bytes') {
  for (const h of process.argv.slice(3)) {
    const pat = Buffer.from(h.replace(/[^0-9a-fA-F]/g, ''), 'hex');
    const hits = findBytes(pat);
    console.log(h + ' -> ' + hits.length + ' hit(s)');
    for (const x of hits.slice(0, 24)) console.log('   ' + desc(x));
  }
} else {
  console.log('usage: node tools/nor_scan.js addr <lo> <hi> | bytes <hex>...');
}
