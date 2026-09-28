// lavax_snd.js - locate the Snd* system implementations in the LVM builds
const fs = require('fs');

const files = [
  ['LVM.obj', 'info/NC3k_LAVAX/LVM.obj'],
  ['GVmaker.obj', 'info/NC3k_LAVAX/备份/GVmaker.obj'],
];

const pats = [
  ['sta $3e9 + int $0951', [0x8d, 0xe9, 0x03, 0x00, 0x51, 0x09]],
  ['int $0951', [0x00, 0x51, 0x09]],
  ['lda $3e9', [0xad, 0xe9, 0x03]],
  ['sta $3e8', [0x8d, 0xe8, 0x03]],
  ['int $0508 (disk)', [0x00, 0x08, 0x05]],
];

for (const [nm, p] of files) {
  const b = fs.readFileSync(p);
  for (const [pn, pat] of pats) {
    const h = [];
    let i = b.indexOf(Buffer.from(pat));
    while (i >= 0 && h.length < 5) { h.push(i.toString(16)); i = b.indexOf(Buffer.from(pat), i + 1); }
    if (h.length) console.log(nm, pn, h.join(','));
  }
}

// dump the tail of the system code with the calibrated base (file = cpu - 0x80e9)
const b = fs.readFileSync('info/NC3k_LAVAX/LVM.obj');
const BASE = 0x80e9;
const names = ['GetPID', 'SetBright', 'GetBright', 'ComOpen', 'ComClose', 'ComWaitReady',
  'ComSetTimer', 'ComGetc', 'ComPutc', 'ComRead', 'ComWrite', 'ComXor', 'RamRead',
  'DiskReclaim', 'DiskCheck', 'FlmDecode', 'SndPlay', 'SndOpen', 'SndClose', 'SndIfEnd',
  'SndPlayFile', 'SndSetVolume', 'SndGetVolume', 'SndStop', 'SndPause', 'SndResume', 'SndGetPlay'];
const ent = [0xbc50, 0xbc5d, 0xbc66, 0xbc93, 0xbd67, 0xbd80, 0xbd9a, 0xbdbd, 0xbdef,
  0xbdf9, 0xbe26, 0xbe52, 0xbe8a, 0xbeb4, 0xbebb, 0xbf45, 0xbf4c, 0xbf53, 0xbf54,
  0xbf55, 0xbf56, 0xbf63, 0xbf6d, 0xbf76, 0xbf84, 0xbf85, 0xbf86];

for (let i = 0; i < ent.length; i++) {
  const o = ent[i] - BASE;
  const next = i + 1 < ent.length ? ent[i + 1] - BASE : o + 14;
  const len = Math.max(1, Math.min(24, next - o));
  const hex = [...b.subarray(o, o + len)].map(x => x.toString(16).padStart(2, '0')).join(' ');
  console.log(String(i).padStart(2), names[i].padEnd(13), 'cpu=' + ent[i].toString(16), 'file=' + o.toString(16), 'len=' + String(next - o).padStart(3), hex);
}
