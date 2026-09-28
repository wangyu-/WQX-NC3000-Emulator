// Minimal PDF text extractor for PDFs that use simple /Differences encodings.
// usage: node tools/pdftext.js <file.pdf> [maxPages]
const fs = require('fs');
const zlib = require('zlib');

const file = process.argv[2];
const maxPages = parseInt(process.argv[3] || '999', 10);
const buf = fs.readFileSync(file);
const s = buf.toString('latin1');

// ---------- index objects ----------
const objs = new Map(); // num -> {start, end, text}
const objRe = /(\d+)\s+0\s+obj/g;
let m;
while ((m = objRe.exec(s))) {
  const num = parseInt(m[1], 10);
  const start = m.index + m[0].length;
  const endIdx = s.indexOf('endobj', start);
  objs.set(num, { start, end: endIdx < 0 ? s.length : endIdx });
}

function objText(num) {
  const o = objs.get(num);
  return o ? s.slice(o.start, o.end) : '';
}

function streamData(num) {
  const o = objs.get(num);
  if (!o) return null;
  let k = s.indexOf('stream', o.start);
  if (k < 0 || k > o.end) return null;
  let st = k + 6;
  if (s[st] === '\r') st++;
  if (s[st] === '\n') st++;
  const e = s.indexOf('endstream', st);
  const raw = Buffer.from(buf.slice(st, e < 0 ? o.end : e));
  const dict = s.slice(o.start, k);
  if (/\/FlateDecode/.test(dict)) {
    try { return zlib.inflateSync(raw); } catch (err) { return null; }
  }
  return raw;
}

// ---------- ToUnicode CMaps ----------
const cmaps = new Map(); // objNum -> Map(code -> string)
function parseCMap(text) {
  const map = new Map();
  for (const blk of text.matchAll(/beginbfchar([\s\S]*?)endbfchar/g)) {
    for (const pair of blk[1].matchAll(/<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>/g)) {
      const c = parseInt(pair[1], 16);
      let str = '';
      for (let i = 0; i + 3 < pair[2].length; i += 4) str += String.fromCharCode(parseInt(pair[2].substr(i, 4), 16));
      map.set(c, str);
    }
  }
  for (const blk of text.matchAll(/beginbfrange([\s\S]*?)endbfrange/g)) {
    for (const r of blk[1].matchAll(/<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>/g)) {
      const a = parseInt(r[1], 16), b = parseInt(r[2], 16), d = parseInt(r[3], 16);
      for (let c = a; c <= b && c - a < 65536; c++) map.set(c, String.fromCharCode(d + (c - a)));
    }
  }
  return map;
}
for (const [num, o] of objs) {
  const t = s.slice(o.start, o.end);
  if (!/beginbfchar|beginbfrange/.test(t)) continue;
}

// ---------- font maps ----------
const fontMaps = new Map();   // fontObjNum -> Map(code->glyphName)  (from /Differences)
const fontCMaps = new Map();  // fontObjNum -> Map(code->char)        (from /ToUnicode)
for (const [num, o] of objs) {
  const t = s.slice(o.start, o.end);
  if (!/\/Type\s*\/Font/.test(t)) continue;
  let diffsText = t.match(/\/Differences\s*\[([^\]]*)\]/s);
  if (!diffsText) {
    const encRef = t.match(/\/Encoding\s+(\d+)\s+0\s+R/);
    if (encRef) {
      const et = objText(parseInt(encRef[1], 10));
      diffsText = et.match(/\/Differences\s*\[([\s\S]*?)\]/);
    }
  }
  const map = new Map();
  if (diffsText) {
    let code = 0;
    for (const tok of diffsText[1].match(/\d+|\/[^\s\/\[\]<>()]+/g) || []) {
      if (tok[0] === '/') { map.set(code, tok.slice(1)); code++; }
      else code = parseInt(tok, 10);
    }
  }
  fontMaps.set(num, map);
  // /ToUnicode
  const tu = t.match(/\/ToUnicode\s+(\d+)\s+0\s+R/);
  if (tu) {
    const data = streamData(parseInt(tu[1], 10));
    if (data) fontCMaps.set(num, parseCMap(data.toString('latin1')));
  }
}
// Type0 fonts inherit the descendant's ToUnicode
for (const [num, o] of objs) {
  const t = s.slice(o.start, o.end);
  if (!/\/Subtype\s*\/Type0/.test(t)) continue;
  const d = t.match(/\/DescendantFonts\s*\[\s*(\d+)\s+0\s+R/);
  if (!d) continue;
  const dt = objText(parseInt(d[1], 10));
  const tu = dt.match(/\/ToUnicode\s+(\d+)\s+0\s+R/);
  if (tu) {
    const data = streamData(parseInt(tu[1], 10));
    if (data) fontCMaps.set(num, parseCMap(data.toString('latin1')));
  }
}
// font resource name -> object number, per page
const fontResourceRe = /\/Font\s*<<([^>]*)>>/g;
const globalFontRes = new Map();
let fr;
while ((fr = fontResourceRe.exec(s))) {
  for (const item of fr[1].matchAll(/\/(\w+)\s+(\d+)\s+0\s+R/g)) {
    globalFontRes.set(item[1], parseInt(item[2], 10));
  }
}

const GLYPH = {
  space: ' ', period: '.', comma: ',', colon: ':', semicolon: ';', hyphen: '-',
  slash: '/', parenleft: '(', parenright: ')', bracketleft: '[', bracketright: ']',
  zero: '0', one: '1', two: '2', three: '3', four: '4', five: '5', six: '6',
  seven: '7', eight: '8', nine: '9', equal: '=', plus: '+', percent: '%',
  question: '?', exclam: '!', quotedbl: '"', quotesingle: "'", asterisk: '*',
  ampersand: '&', dollar: '$', numbersign: '#', at: '@', underscore: '_',
  grave: '`', asciitilde: '~', asciicircum: '^', bar: '|', backslash: '\\',
  braceleft: '{', braceright: '}', less: '<', greater: '>',
};
function glyphToChar(name) {
  if (GLYPH[name]) return GLYPH[name];
  if (name.length === 1) return name;
  if (/^uni[0-9A-F]{4}$/.test(name)) return String.fromCharCode(parseInt(name.slice(3), 16));
  return '';
}

// ---------- pages ----------
let pageCount = 0;
for (const [num, o] of objs) {
  const t = s.slice(o.start, o.end);
  if (!/\/Type\s*\/Page[^s]/.test(t)) continue;
  if (++pageCount > maxPages) break;
  // contents
  const cm = t.match(/\/Contents\s+(\d+)\s+0\s+R/);
  if (!cm) continue;
  const data = streamData(parseInt(cm[1], 10));
  if (!data) continue;
  // local font resources
  const localFonts = new Map(globalFontRes);
  const resM = t.match(/\/Resources\s+(\d+)\s+0\s+R/);
  const resText = resM ? objText(parseInt(resM[1], 10)) : t;
  for (const item of (resText.match(/\/Font\s*<<([^>]*)>>/) || ['', ''])[1].matchAll(/\/(\w+)\s+(\d+)\s+0\s+R/g)) {
    localFonts.set(item[1], parseInt(item[2], 10));
  }
  const cs = data.toString('latin1');
  let curFont = null;
  let out = '';
  const tokRe = /\/(\w+)\s+[\d.]+\s+Tf|\((?:\\.|[^\\()])*\)|<([0-9A-Fa-f\s]*)>|(TJ|Tj|T\*|TD|Td)/g;
  let tk;
  while ((tk = tokRe.exec(cs))) {
    if (tk[1]) { curFont = localFonts.get(tk[1]) ?? null; continue; }
    if (tk[3] === 'T*' || tk[3] === 'TD') { out += '\n'; continue; }
    if (tk[3] === 'Td') continue;
    let str = '';
    if (tk[0][0] === '(') {
      str = tk[0].slice(1, -1).replace(/\\([()\\])/g, '$1')
        .replace(/\\([0-7]{1,3})/g, (_, o) => String.fromCharCode(parseInt(o, 8)));
    } else if (tk[2] !== undefined) {
      const hex = tk[2].replace(/\s+/g, '');
      const bytes = [];
      for (let i = 0; i + 1 < hex.length; i += 2) bytes.push(parseInt(hex.substr(i, 2), 16));
      str = bytes.map((x) => String.fromCharCode(x)).join('');
    }
    const cmap = curFont !== null ? fontCMaps.get(curFont) : null;
    const map = curFont !== null ? fontMaps.get(curFont) : null;
    if (cmap && cmap.size && tk[2] !== undefined) {
      // 2-byte codes (Identity-H)
      const hex = tk[2].replace(/\s+/g, '');
      let dec = '';
      for (let i = 0; i + 3 < hex.length; i += 4) {
        const c = parseInt(hex.substr(i, 4), 16);
        dec += cmap.has(c) ? cmap.get(c) : '?';
      }
      str = dec;
    } else if (map && map.size) {
      let dec = '';
      for (const ch of str) {
        const code = ch.charCodeAt(0);
        const name = map.get(code);
        dec += name ? glyphToChar(name) : '';
      }
      str = dec;
    } else if (cmap && cmap.size) {
      let dec = '';
      for (const ch of str) dec += cmap.has(ch.charCodeAt(0)) ? cmap.get(ch.charCodeAt(0)) : '?';
      str = dec;
    }
    out += str;
  }
  console.log(`\n===== page ${pageCount} (obj ${num}) =====\n` + out);
}
