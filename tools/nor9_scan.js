// nor9_scan.js - find the 061 command sites inside NOR bank 9 (the audio player)
const fs = require('fs');
const b = fs.readFileSync('info/NC3KSYSNOR.nor');
const BANK9 = 9 * 0x8000;         // bank N (N>=1) starts at file offset N*0x8000
const CPU_BASE = 0x4000;          // window for bank >= 1

function cpu(fileOff) { return CPU_BASE + (fileOff - BANK9); }

function scan(lo, hi, pat, label) {
  const hits = [];
  for (let i = lo; i + pat.length <= hi; i++) {
    let ok = true;
    for (let j = 0; j < pat.length; j++) if (b[i + j] !== pat[j]) { ok = false; break; }
    if (ok) hits.push(i);
  }
  if (hits.length)
    console.log(label.padEnd(28), hits.map(h => 'file' + h.toString(16) + '/cpu' + cpu(h).toString(16)).join(' '));
  return hits;
}

const lo = BANK9, hi = BANK9 + 0x8000;
scan(lo, hi, [0x20, 0xa8, 0xe0], 'JSR $E0A8 (0x99 + A)');
scan(lo, hi, [0x20, 0xb4, 0xe0], 'JSR $E0B4 (send byte)');
scan(lo, hi, [0x20, 0xb7, 0xe0], 'JSR $E0B7 (recv byte)');
scan(lo, hi, [0x20, 0x93, 0xe0], 'JSR $E093 (wait ready)');
scan(lo, hi, [0x20, 0xba, 0xe0], 'JSR $E0BA ($FB73)');
scan(lo, hi, [0x20, 0xab, 0xe0], 'JSR $E0AB (link)');
scan(lo, hi, [0x20, 0xa5, 0xe0], 'JSR $E0A5 (stop)');
scan(lo, hi, [0x20, 0xa2, 0xe0], 'JSR $E0A2 (end-ack)');
scan(lo, hi, [0x20, 0xae, 0xe0], 'JSR $E0AE (end)');
scan(lo, hi, [0x20, 0x9c, 0xe0], 'JSR $E09C (sleep)');
scan(lo, hi, [0x20, 0x99, 0xe0], 'JSR $E099 (volume)');
scan(lo, hi, [0x20, 0xc9, 0xe0], 'JSR $E0C9 (speed)');
scan(lo, hi, [0xa9, 0x99], 'LDA #$99');
scan(lo, hi, [0xa9, 0xaa], 'LDA #$AA');
scan(lo, hi, [0xa9, 0x33], 'LDA #$33');
scan(lo, hi, [0xa9, 0x22], 'LDA #$22');

// also list every JSR $E0xx in bank 9 so we see the whole BIOS surface used
const jsr = {};
for (let i = lo; i + 2 < hi; i++) {
  if (b[i] === 0x20 && b[i + 2] === 0xe0) {
    const t = b[i + 1];
    (jsr[t] = jsr[t] || []).push(cpu(i));
  }
}
console.log('\nJSR $E0xx targets used in bank 9:');
for (const t of Object.keys(jsr).sort((x, y) => x - y))
  console.log('  $E0' + Number(t).toString(16).padStart(2, '0'), jsr[t].length, 'calls');
