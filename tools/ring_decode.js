// Decode a list of CPU addresses (one per line, as produced by
// nc3000_headless.exe --ring-out) into 6502 instructions.
//
// usage: node tools/ring_decode.js <norFile> <pcListFile> [--window-bank HH]
//                                   [--ram <ramdump.bin>] [--ram-base 0x4000]
//
// Address mapping used by the NC3000 (see docs/NC3000模拟器_改造方案):
//   $0000-$3FFF  RAM (code here is copied at run time -> not decodable)
//   $4000-$BFFF  switchable bank window (--window-bank, default 00)
//   $C000-$DFFF  BBS window  -> bank 0 file offset $4000+n
//   $E000-$FFFF  BIOS window -> bank 0 file offset $6000+n
const fs = require('fs');
const { disasm } = require('./dis6502.js');

const file = process.argv[2];
const listFile = process.argv[3];
let windowBank = 0;
const wbIdx = process.argv.indexOf('--window-bank');
if (wbIdx > 0) windowBank = parseInt(process.argv[wbIdx + 1], 16);
let ram = null;
const ramIdx = process.argv.indexOf('--ram');
if (ramIdx > 0) ram = fs.readFileSync(process.argv[ramIdx + 1]);

const buf = fs.readFileSync(file);
const lines = fs.readFileSync(listFile, 'utf8').split(/\r?\n/).filter((s) => s.trim());

function fileOffset(pc) {
  if (pc < 0xC000) {
    const inBank = pc & 0x7fff;
    let offInBank;
    if (inBank < 0x2000) offInBank = 0x0000 + inBank;        // $8000-$9FFF
    else if (inBank < 0x4000) offInBank = 0x2000 + (inBank - 0x2000); // $A000-$BFFF
    else if (inBank < 0x6000) offInBank = 0x4000 + (inBank - 0x4000); // $4000-$5FFF
    else offInBank = 0x6000 + (inBank - 0x6000);             // $6000-$7FFF
    return windowBank * 0x8000 + offInBank;
  }
  if (pc < 0xE000) return 0x4000 + (pc - 0xC000);
  return 0x6000 + (pc - 0xE000);
}

for (const line of lines) {
  const m = line.trim().match(/^([0-9a-fA-F]{1,4})(?:\s+x(\d+))?$/);
  if (!m) { console.log('   ?? ' + line); continue; }
  const pc = parseInt(m[1], 16);
  const rep = m[2] ? parseInt(m[2], 10) : 1;
  if (ram) {
    const off = (pc >> 13) * 0x2000 + (pc & 0x1fff);
    const text = ram[off] === undefined ? '??' :
      (disasm(ram, off, off + 3, pc - off)[0] || {}).text;
    console.log(`${m[1].toUpperCase()} : ${text}   x${rep}`);
    continue;
  }
  if (pc < 0x4000) { console.log(`$${m[1]} : [RAM] x${rep}`); continue; }
  const off = fileOffset(pc);
  const ins = disasm(buf, off, off + 3, pc - off);
  const text = ins.length ? ins[0].text : '?';
  console.log(`${m[1].toUpperCase()} : ${text}   x${rep}`);
}
