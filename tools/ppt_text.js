// Extract readable text (UTF-16LE and CP936 runs) from a legacy .ppt (OLE2).
// usage: node tools/ppt_text.js <file.ppt> [out.txt]
const fs = require('fs');

const file = process.argv[2];
const out = process.argv[3];
const buf = fs.readFileSync(file);

function utf16Runs(b) {
  const runs = [];
  let cur = '';
  for (let i = 0; i + 1 < b.length; i += 2) {
    const c = b[i] | (b[i + 1] << 8);
    const ok = (c >= 0x20 && c !== 0x7F) || c === 10 || c === 13 || c === 9 ||
               (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3000 && c <= 0x303F) ||
               (c >= 0xFF00 && c <= 0xFFEF);
    if (ok) cur += String.fromCharCode(c);
    else { if (cur.trim().length >= 2) runs.push(cur); cur = ''; }
  }
  if (cur.trim().length >= 2) runs.push(cur);
  return runs;
}

function cp936Runs(b) {
  const dec = new TextDecoder('gbk');
  const runs = [];
  let start = -1;
  const isText = (c) => c === 9 || c === 10 || c === 13 || (c >= 0x20 && c < 0x7F) || c >= 0x81;
  for (let i = 0; i < b.length; i++) {
    if (isText(b[i])) { if (start < 0) start = i; }
    else {
      if (start >= 0 && i - start >= 4) runs.push(dec.decode(b.slice(start, i)));
      start = -1;
    }
  }
  if (start >= 0 && b.length - start >= 4) runs.push(dec.decode(b.slice(start)));
  return runs;
}

const u16 = utf16Runs(buf).filter((s) => /[\u4e00-\u9fff]/.test(s) || /[A-Za-z]{3}/.test(s));
const gbk = cp936Runs(buf).filter((s) => /[\u4e00-\u9fff]/.test(s) && s.length > 3);

const text = [
  '==== UTF-16LE runs ====',
  ...u16.map((s) => s.replace(/[\u0000-\u001f]/g, ' ')),
  '',
  '==== CP936 runs ====',
  ...gbk.map((s) => s.replace(/[\u0000-\u001f]/g, ' ')),
].join('\n');

if (out) { fs.writeFileSync(out, text); console.log(`wrote ${out} (${text.length} chars)`); }
else console.log(text);
