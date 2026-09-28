// rom061.js - word-addressed access to the SPCE061A image (061.dat)
// The u'nSP address space is word addressed: word A lives at byte 2*(A-base).
const fs = require('fs');
const path = require('path');

const FILE = process.argv[2] && process.argv[2] !== '-'
  ? process.argv[2]
  : path.join(__dirname, '..', 'spce061a', 'rom', '061.dat');
const buf = fs.readFileSync(FILE);
const BASE = 0x8200;

function w(a) { return buf.readUInt16LE((a - BASE) * 2); }

module.exports = { buf, BASE, w, len: buf.length / 2 };

if (require.main === module) {
  const args = process.argv.slice(3);
  const lo = parseInt(args[0] || '8200', 16);
  const n = parseInt(args[1] || '64');
  for (let i = 0; i < n; i += 8) {
    const parts = [];
    for (let j = 0; j < 8 && i + j < n; j++) {
      parts.push(w(lo + i + j).toString(16).padStart(4, '0'));
    }
    console.log((lo + i).toString(16).padStart(4, '0') + ': ' + parts.join(' '));
  }
}
