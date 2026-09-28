// wav_declick.js - repair the full-scale "rail" bursts the S200 DAC path emits
// usage: node tools/wav_declick.js <in.wav> <out.wav> [rate]
const fs = require('fs');

const src = process.argv[2];
const dst = process.argv[3];
const rateArg = process.argv[4] ? parseInt(process.argv[4], 10) : 0;

const b = fs.readFileSync(src);
let off = 12, dOff = -1, dLen = 0, sr = b.readUInt32LE(24);
while (off + 8 <= b.length) {
  const id = b.toString('ascii', off, off + 4);
  const sz = b.readUInt32LE(off + 4);
  if (id === 'data') { dOff = off + 8; dLen = sz; break; }
  off += 8 + sz + (sz & 1);
}
const n = dLen >> 1;
const a = new Int16Array(n);
for (let i = 0; i < n; i++) a[i] = b.readInt16LE(dOff + i * 2);

const TH = 30000;
let repaired = 0, runs = 0;
for (let i = 0; i < n; ) {
  if (Math.abs(a[i]) < TH) { i++; continue; }
  let j = i;
  while (j < n && Math.abs(a[j]) >= TH) j++;
  runs++;
  repaired += j - i;
  const before = i > 0 ? a[i - 1] : 0;
  const after = j < n ? a[j] : before;
  const span = j - i + 1;
  for (let k = i; k < j; k++) a[k] = Math.round(before + (after - before) * (k - i + 1) / span);
  i = j;
}

const rate = rateArg || sr;
const out = Buffer.alloc(44 + n * 2);
out.write('RIFF', 0); out.writeUInt32LE(36 + n * 2, 4); out.write('WAVE', 8);
out.write('fmt ', 12); out.writeUInt32LE(16, 16);
out.writeUInt16LE(1, 20); out.writeUInt16LE(1, 22);
out.writeUInt32LE(rate, 24); out.writeUInt32LE(rate * 2, 28);
out.writeUInt16LE(2, 32); out.writeUInt16LE(16, 34);
out.write('data', 36); out.writeUInt32LE(n * 2, 40);
for (let i = 0; i < n; i++) out.writeInt16LE(a[i], 44 + i * 2);
fs.writeFileSync(dst, out);
console.log(dst + ':', n, 'samples @', rate, 'Hz =', (n / rate).toFixed(1), 's;',
            'repaired', repaired, 'samples in', runs, 'bursts',
            '(' + (100 * repaired / n).toFixed(2) + '%)');
