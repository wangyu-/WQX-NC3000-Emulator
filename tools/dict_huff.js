// Decode the NC3000/WQX dictionary "compressed word data" bit-stream.
//
// Code table (from src_nc2000/nc2000f/dict_packet/h/doc_format.txt, chapter 4):
//   0      + 3bit   -> level-1 high-frequency word  (8 entries,   0.5 byte)
//   10     + 10bit  -> level-2 high-frequency word  (1024 entries,1.5 byte)
//   110    + 13bit  -> level-3 high-frequency word  (8192 entries,2.0 byte)
//   1110   + 16bit  -> level-4 high-frequency word  (65536,      2.5 byte)
//   1111000+ 8bit   -> literal ASCII byte
//   11111  + 16bit  -> literal GBK (double byte)
//
// usage: node tools/dict_huff.js <file> <startOffsetHex> <bytes> [lsb|msb] [tables.json]
const fs = require('fs');

const file = process.argv[2];
const start = parseInt(process.argv[3] || '0', 16);
const count = parseInt(process.argv[4] || '256', 10);
const bitOrder = (process.argv[5] || 'msb').toLowerCase();
const tablesFile = process.argv[6];

const tables = tablesFile ? JSON.parse(fs.readFileSync(tablesFile, 'utf8')) : { t1: [], t2: [], t3: [], t4: [] };
const buf = fs.readFileSync(file);

let pos = start;
let bit = 0;
function nextBit() {
  if (pos >= buf.length) throw new Error('eof');
  const byte = buf[pos];
  const v = bitOrder === 'msb' ? (byte >> (7 - bit)) & 1 : (byte >> bit) & 1;
  bit++;
  if (bit === 8) { bit = 0; pos++; }
  return v;
}
function nextBits(n) {
  let v = 0;
  for (let i = 0; i < n; i++) v = (v << 1) | nextBit();
  return v;
}

const out = [];
let bad = 0;
try {
  while (out.length < count) {
    const at = { off: pos, bit };
    if (nextBit() === 0) {
      const i = nextBits(3);
      out.push({ at, kind: 'T1', i, s: tables.t1[i] });
      continue;
    }
    if (nextBit() === 0) {
      const i = nextBits(10);
      out.push({ at, kind: 'T2', i, s: tables.t2[i] });
      continue;
    }
    if (nextBit() === 0) {
      const i = nextBits(13);
      out.push({ at, kind: 'T3', i, s: tables.t3[i] });
      continue;
    }
    if (nextBit() === 0) {
      const i = nextBits(16);
      out.push({ at, kind: 'T4', i, s: tables.t4[i] });
      continue;
    }
    // 1111 x... : 1111000 = ASCII (padded to 16 bits), 11111 = GBK
    if (nextBit() === 0) {
      if (nextBit() !== 0 || nextBit() !== 0) { out.push({ at, kind: 'BAD1' }); bad++; continue; }
      nextBit(); // pad bit
      const b = nextBits(8);
      out.push({ at, kind: 'ASC', b, s: String.fromCharCode(b) });
      continue;
    }
    const hi = nextBits(8);
    const lo = nextBits(8);
    out.push({ at, kind: 'GBK', b: [hi, lo], s: Buffer.from([hi, lo]).toString('latin1') });
  }
} catch (e) {
  out.push({ kind: 'EOF' });
}

if (process.env.HUFF_PLAIN) {
  let s = '';
  for (const t of out) {
    if (t.kind === 'EOF') break;
    if (t.kind === 'ASC' || t.kind === 'GBK') s += t.s;
    else if (t.s !== undefined) s += t.s;
    else s += '{%' + t.kind + '#' + t.i + '}';
  }
  console.log(s);
  console.log('# tokens=' + out.length + ' bad=' + bad);
} else {

for (const t of out) {
  const at = t.at ? `${t.at.off.toString(16).padStart(6, '0')}.${t.at.bit}` : '------';
  if (t.kind === 'EOF') { console.log(at + '  <eof>'); break; }
  if (t.kind === 'T1' || t.kind === 'T2' || t.kind === 'T3' || t.kind === 'T4') {
    console.log(`${at}  ${t.kind}#${t.i}  ${t.s === undefined ? '<no-table>' : t.s}`);
  } else if (t.kind === 'ASC') {
    console.log(`${at}  ASC  '${t.s}'`);
  } else if (t.kind === 'GBK') {
    console.log(`${at}  GBK  ${t.b.map((x) => x.toString(16)).join(' ')}`);
  } else {
    console.log(`${at}  ${t.kind}`);
  }
}
}
