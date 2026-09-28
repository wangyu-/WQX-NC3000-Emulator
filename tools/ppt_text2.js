// Extract text from a legacy .ppt by walking PPT records:
//   4000 (0x0FA0) TextCharsAtom  -> UTF-16LE
//   4008 (0x0FA8) TextBytesAtom  -> CP936
// usage: node tools/ppt_text2.js <file.ppt> [out.txt]
const fs = require('fs');
const buf = fs.readFileSync(process.argv[2]);
const out = process.argv[3];
const pieces = [];

for (let i = 0; i + 8 <= buf.length; i++) {
  const type = buf.readUInt16LE(i + 2);
  if (type !== 0x0FA0 && type !== 0x0FA8) continue;
  const len = buf.readUInt32LE(i + 4);
  if (len === 0 || len > 65536 || i + 8 + len > buf.length) continue;
  const data = buf.slice(i + 8, i + 8 + len);
  let s;
  try {
    s = type === 0x0FA0 ? new TextDecoder('utf-16le').decode(data)
                        : new TextDecoder('gbk').decode(data);
  } catch (e) { continue; }
  // keep it only if it looks like speech: mostly printable / CJK
  const cps = [...s];
  const good = cps.filter((c) => {
    const v = c.codePointAt(0);
    return v === 9 || v === 10 || v === 13 || (v >= 0x20 && v < 0x7F) ||
           (v >= 0x4E00 && v <= 0x9FFF) || (v >= 0x3000 && v <= 0x303F) ||
           (v >= 0xFF00 && v <= 0xFFEF);
  }).length;
  if (cps.length && good / cps.length > 0.85 && good >= 4) pieces.push(s);
  i += 7 + len;                     // records do not nest; skip the payload
}

const text = pieces.join('\n');
if (out) { fs.writeFileSync(out, text); console.log(`wrote ${out}: ${pieces.length} records, ${text.length} chars`); }
else console.log(text);
