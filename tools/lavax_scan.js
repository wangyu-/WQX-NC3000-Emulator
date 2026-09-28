// lavax_scan.js - look for 061-link / sound code inside the LAVA X binaries
const fs = require('fs');

const files = [
  ['LVM.obj',           'info/NC3k_LAVAX/LVM.obj'],
  ['GVmaker.obj',       'info/NC3k_LAVAX/备份/GVmaker.obj'],
  ['GVmaker.dat',       'info/NC3k_LAVAX/备份/GVmaker.dat'],
  ['LVM.asm',           'info/NC3k_LAVAX/LVM.asm'],
  ['System_Code.asm',   'info/NC3k_LAVAX/System_Code.asm'],
  ['yinwen16.txt',      'info/音文16.txt'],
  ['yinwen.lav',        'info/音文同步.lav'],
];

const pats = [
  ['JSR $FB1C (send byte)', [0x20, 0x1c, 0xfb]],
  ['LDA #$99',              [0xa9, 0x99]],
  ['LDA #$AA',              [0xa9, 0xaa]],
  ['LDA #$33',              [0xa9, 0x33]],
  ['LDA #$22',              [0xa9, 0x22]],
  ['LDA #$44',              [0xa9, 0x44]],
  ['LDA #$55',              [0xa9, 0x55]],
  ['LDA #$10',              [0xa9, 0x10]],
  ['LDA #$40',              [0xa9, 0x40]],
  ['STA $3A',               [0x85, 0x3a]],
  ['JSR $7AE3',             [0x20, 0xe3, 0x7a]],
  ['JSR $FAE3',             [0x20, 0xe3, 0xfa]],
  ['JSR $FE2A',             [0x20, 0x2a, 0xfe]],
  ['INT $0B1E',             [0x00, 0x1e, 0x0b]],
  ['bytes 0x20000',         [0x00, 0x00, 0x02]],
];

for (const [nm, p] of files) {
  let b;
  try { b = fs.readFileSync(p); } catch (e) { console.log(nm, 'missing'); continue; }
  const res = [];
  for (const [pn, pat] of pats) {
    const c = [];
    let i = b.indexOf(Buffer.from(pat));
    while (i >= 0 && c.length < 6) { c.push(i.toString(16)); i = b.indexOf(Buffer.from(pat), i + 1); }
    if (c.length) res.push(pn + ' @' + c.join(','));
  }
  console.log(nm.padEnd(16), String(b.length).padStart(7), '|', res.join(' | ') || '(none)');
}

// textual greps in the asm sources
for (const [nm, p] of files) {
  if (!p.endsWith('.asm') && !p.endsWith('.txt')) continue;
  const t = fs.readFileSync(p, 'latin1');
  const hits = {};
  for (const kw of ['Snd', 'snd', 'Sound', 'PlayFile', '$3a', '$3b', '$3c', '$3d', 'Music', 'MIDI', 'midi']) {
    const n = (t.match(new RegExp(kw.replace(/\$/g, '\\$'), 'g')) || []).length;
    if (n) hits[kw] = n;
  }
  console.log('text', nm, JSON.stringify(hits));
}
