// vnt_probe.js - inspect the 视听教材 .vnt containers (高/一般品质)
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..', 'info', '视听教材');

function walk(dir, acc) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, acc);
    else if (e.name.toLowerCase().endsWith('.vnt')) acc.push(p);
  }
  return acc;
}

const all = walk(ROOT, []);
const lo = [], hi = [];
for (const f of all) {
  const b = fs.readFileSync(f);
  if (b.length < 16) continue;
  const w = b.readUInt16LE(0);
  if (w === 0x8007) hi.push(f);
  else if (b.readUInt16LE(4) === 0x0008 && b.readUInt32LE(0) === b.length - 4) lo.push(f);
}

function commonPrefix(files, cap) {
  const bufs = files.slice(0, cap).map(f => fs.readFileSync(f));
  let n = Infinity;
  for (const b of bufs) n = Math.min(n, b.length);
  let i = 0;
  outer:
  for (; i < n; i++) {
    const v = bufs[0][i];
    for (const b of bufs) if (b[i] !== v) break outer;
  }
  return i;
}

console.log('files', all.length, '一般品质', lo.length, '高品质', hi.length);
console.log('一般品质 common prefix (40 files):', commonPrefix(lo, 40));
console.log('高品质   common prefix (40 files):', commonPrefix(hi, 40));

function dump(p, n) {
  const b = fs.readFileSync(p);
  const s = [];
  for (let i = 0; i < Math.min(n, b.length); i++) s.push(b[i].toString(16).padStart(2, '0'));
  console.log(path.basename(p), b.length, s.join(' '));
}

console.log('\n--- 一般品质 samples ---');
for (const f of lo.slice(0, 3)) dump(f, 72);
console.log('\n--- 高品质 samples ---');
for (const f of hi.slice(0, 3)) dump(f, 72);

// Does the 一般品质 u16 header byte-pair 0x0008 appear as u16 somewhere in 高品质?
console.log('\nhi header words of first 12 files:');
for (const f of hi.slice(0, 12)) {
  const b = fs.readFileSync(f);
  const ws = [];
  for (let i = 0; i < 10; i++) ws.push(b.readUInt16LE(i * 2).toString(16).padStart(4, '0'));
  console.log(path.basename(f), b.length, ws.join(' '));
}
