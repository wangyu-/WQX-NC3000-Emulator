// Minimal 6502 disassembler for NC3000 firmware analysis.
// usage: node tools/dis6502.js <file> <startOffset> <endOffset> [baseAddr]
const fs = require('fs');

const MODES = {
  imp: (b, a) => '',
  acc: (b, a) => '',
  imm: (b, a) => '#$' + b[a + 1].toString(16).padStart(2, '0'),
  zp: (b, a) => '$' + b[a + 1].toString(16).padStart(2, '0'),
  zpx: (b, a) => '$' + b[a + 1].toString(16).padStart(2, '0') + ',X',
  zpy: (b, a) => '$' + b[a + 1].toString(16).padStart(2, '0') + ',Y',
  izx: (b, a) => '($' + b[a + 1].toString(16).padStart(2, '0') + ',X)',
  izy: (b, a) => '($' + b[a + 1].toString(16).padStart(2, '0') + '),Y',
  abs: (b, a) => '$' + (b[a + 1] | (b[a + 2] << 8)).toString(16).padStart(4, '0'),
  absx: (b, a) => '$' + (b[a + 1] | (b[a + 2] << 8)).toString(16).padStart(4, '0') + ',X',
  absy: (b, a) => '$' + (b[a + 1] | (b[a + 2] << 8)).toString(16).padStart(4, '0') + ',Y',
  ind: (b, a) => '($' + (b[a + 1] | (b[a + 2] << 8)).toString(16).padStart(4, '0') + ')',
  rel: (b, a, base) => {
    let d = b[a + 1];
    if (d > 127) d -= 256;
    return '$' + ((base + a + 2 + d) & 0xffff).toString(16).padStart(4, '0');
  },
};
const LEN = { imp: 1, acc: 1, imm: 2, zp: 2, zpx: 2, zpy: 2, izx: 2, izy: 2, abs: 3, absx: 3, absy: 3, ind: 3, rel: 2 };

const OP = {};
function d(code, name, mode) { OP[code] = { name, mode }; }
// load/store
d(0xa9,'LDA','imm');d(0xa5,'LDA','zp');d(0xb5,'LDA','zpx');d(0xad,'LDA','abs');d(0xbd,'LDA','absx');d(0xb9,'LDA','absy');d(0xa1,'LDA','izx');d(0xb1,'LDA','izy');
d(0xa2,'LDX','imm');d(0xa6,'LDX','zp');d(0xb6,'LDX','zpy');d(0xae,'LDX','abs');d(0xbe,'LDX','absy');
d(0xa0,'LDY','imm');d(0xa4,'LDY','zp');d(0xb4,'LDY','zpx');d(0xac,'LDY','abs');d(0xbc,'LDY','absx');
d(0x85,'STA','zp');d(0x95,'STA','zpx');d(0x8d,'STA','abs');d(0x9d,'STA','absx');d(0x99,'STA','absy');d(0x81,'STA','izx');d(0x91,'STA','izy');
d(0x86,'STX','zp');d(0x96,'STX','zpy');d(0x8e,'STX','abs');
d(0x84,'STY','zp');d(0x94,'STY','zpx');d(0x8c,'STY','abs');
// transfers
d(0xaa,'TAX','imp');d(0xa8,'TAY','imp');d(0x8a,'TXA','imp');d(0x98,'TYA','imp');d(0xba,'TSX','imp');d(0x9a,'TXS','imp');
// stack
d(0x48,'PHA','imp');d(0x68,'PLA','imp');d(0x08,'PHP','imp');d(0x28,'PLP','imp');
// arithmetic
d(0x69,'ADC','imm');d(0x65,'ADC','zp');d(0x75,'ADC','zpx');d(0x6d,'ADC','abs');d(0x7d,'ADC','absx');d(0x79,'ADC','absy');d(0x61,'ADC','izx');d(0x71,'ADC','izy');
d(0xe9,'SBC','imm');d(0xe5,'SBC','zp');d(0xf5,'SBC','zpx');d(0xed,'SBC','abs');d(0xfd,'SBC','absx');d(0xf9,'SBC','absy');d(0xe1,'SBC','izx');d(0xf1,'SBC','izy');
// inc/dec
d(0xe6,'INC','zp');d(0xf6,'INC','zpx');d(0xee,'INC','abs');d(0xfe,'INC','absx');
d(0xc6,'DEC','zp');d(0xd6,'DEC','zpx');d(0xce,'DEC','abs');d(0xde,'DEC','absx');
d(0xe8,'INX','imp');d(0xc8,'INY','imp');d(0xca,'DEX','imp');d(0x88,'DEY','imp');
// logic
d(0x29,'AND','imm');d(0x25,'AND','zp');d(0x35,'AND','zpx');d(0x2d,'AND','abs');d(0x3d,'AND','absx');d(0x39,'AND','absy');d(0x21,'AND','izx');d(0x31,'AND','izy');
d(0x09,'ORA','imm');d(0x05,'ORA','zp');d(0x15,'ORA','zpx');d(0x0d,'ORA','abs');d(0x1d,'ORA','absx');d(0x19,'ORA','absy');d(0x01,'ORA','izx');d(0x11,'ORA','izy');
d(0x49,'EOR','imm');d(0x45,'EOR','zp');d(0x55,'EOR','zpx');d(0x4d,'EOR','abs');d(0x5d,'EOR','absx');d(0x59,'EOR','absy');d(0x41,'EOR','izx');d(0x51,'EOR','izy');
d(0x24,'BIT','zp');d(0x2c,'BIT','abs');
// shifts
d(0x0a,'ASL','acc');d(0x06,'ASL','zp');d(0x16,'ASL','zpx');d(0x0e,'ASL','abs');d(0x1e,'ASL','absx');
d(0x4a,'LSR','acc');d(0x46,'LSR','zp');d(0x56,'LSR','zpx');d(0x4e,'LSR','abs');d(0x5e,'LSR','absx');
d(0x2a,'ROL','acc');d(0x26,'ROL','zp');d(0x36,'ROL','zpx');d(0x2e,'ROL','abs');d(0x3e,'ROL','absx');
d(0x6a,'ROR','acc');d(0x66,'ROR','zp');d(0x76,'ROR','zpx');d(0x6e,'ROR','abs');d(0x7e,'ROR','absx');
// compare
d(0xc9,'CMP','imm');d(0xc5,'CMP','zp');d(0xd5,'CMP','zpx');d(0xcd,'CMP','abs');d(0xdd,'CMP','absx');d(0xd9,'CMP','absy');d(0xc1,'CMP','izx');d(0xd1,'CMP','izy');
d(0xe0,'CPX','imm');d(0xe4,'CPX','zp');d(0xec,'CPX','abs');
d(0xc0,'CPY','imm');d(0xc4,'CPY','zp');d(0xcc,'CPY','abs');
// branches
d(0x10,'BPL','rel');d(0x30,'BMI','rel');d(0x50,'BVC','rel');d(0x70,'BVS','rel');d(0x90,'BCC','rel');d(0xb0,'BCS','rel');d(0xd0,'BNE','rel');d(0xf0,'BEQ','rel');
// jumps
d(0x4c,'JMP','abs');d(0x6c,'JMP','ind');d(0x20,'JSR','abs');d(0x60,'RTS','imp');d(0x40,'RTI','imp');
// flags
d(0x18,'CLC','imp');d(0x38,'SEC','imp');d(0x58,'CLI','imp');d(0x78,'SEI','imp');d(0xb8,'CLV','imp');d(0xd8,'CLD','imp');d(0xf8,'SED','imp');
// misc
d(0x00,'BRK','imp');d(0xea,'NOP','imp');
// 65C02 additions
d(0x64,'STZ','zp');d(0x74,'STZ','zpx');d(0x9c,'STZ','abs');d(0x9e,'STZ','absx');
d(0x12,'ORA','izy');d(0x32,'AND','izy');d(0x52,'EOR','izy');d(0x72,'ADC','izy');d(0x92,'STA','izy');d(0xb2,'LDA','izy');d(0xd2,'CMP','izy');d(0xf2,'SBC','izy');
d(0x80,'BRA','rel');

function disasm(buf, start, end, base) {
  const out = [];
  const b = buf;
  for (let a = start; a < end;) {
    const op = b[a];
    const cpu = (base + a) & 0xffff;
    const e = OP[op];
    if (!e) { out.push({ off: a, cpu, bytes: [op], text: '??? .byte $' + op.toString(16).padStart(2, '0') }); a += 1; continue; }
    const len = LEN[e.mode];
    const bytes = [];
    for (let i = 0; i < len; i++) bytes.push(b[a + i]);
    out.push({ off: a, cpu, bytes, text: e.name + (e.mode === 'acc' ? ' A' : '') + ' ' + MODES[e.mode](b, a, base) });
    a += len;
  }
  return out;
}

if (require.main === module) {
  const [file, s, e, baseArg] = process.argv.slice(2);
  const buf = fs.readFileSync(file);
  const start = parseInt(s, 16), end = parseInt(e, 16);
  const base = baseArg ? parseInt(baseArg, 16) : 0;
  for (const ins of disasm(buf, start, end, base)) {
    const bs = ins.bytes.map(x => x.toString(16).padStart(2, '0')).join(' ').padEnd(9);
    console.log(ins.off.toString(16).padStart(6, '0') + '  ' + ins.cpu.toString(16).padStart(4, '0') + '  ' + bs + ' ' + ins.text);
  }
}

module.exports = { disasm };
