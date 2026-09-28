// Dump words of a generated firmware C array (firmware_061.c style) by address.
// usage: node tools/fw_words.js <file.c> <addrHex> <count>   |  node tools/fw_words.js <file.c> find fbbb
const fs = require('fs');

const txt = fs.readFileSync(process.argv[2], 'utf8');
const words = [];
const re = /0x([0-9A-Fa-f]{1,4})/g;
let m, started = false;
for (const line of txt.split(/\r?\n/)) {
  if (!started) { if (line.indexOf('= {') >= 0) started = true; continue; }
  if (line.trim() === '};') break;
  let mm;
  const r = /0x([0-9A-Fa-f]{1,4})/g;
  while ((mm = r.exec(line))) words.push(parseInt(mm[1], 16));
}
const BASE = 0x8200;

if (process.argv[3] === 'find') {
  const pat = parseInt(process.argv[4], 16);
  for (let i = 0; i < words.length; i++)
    if (words[i] === pat) console.log('0x' + (BASE + i).toString(16) + '  idx ' + i);
} else {
  const addr = parseInt(process.argv[3], 16);
  const count = parseInt(process.argv[4] || '32', 16);
  for (let i = 0; i < count; i++) {
    const a = addr - BASE + i;
    if (a < 0 || a >= words.length) break;
    console.log('0x' + (BASE + a).toString(16) + ': ' + words[a].toString(16).padStart(4, '0'));
  }
}
