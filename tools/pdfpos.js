// Positional PDF text extractor (keeps glyph/string coordinates so tables can be rebuilt).
// usage: node tools/pdfpos.js <file.pdf> [pageFilterRegex]
const fs = require('fs');
const zlib = require('zlib');

const file = process.argv[2];
const pageFilter = process.argv[3] ? new RegExp(process.argv[3]) : null;
const buf = fs.readFileSync(file);
const s = buf.toString('latin1');

const objs = new Map();
{
  const re = /(\d+)\s+0\s+obj/g;
  let m;
  while ((m = re.exec(s))) {
    const start = m.index + m[0].length;
    const endIdx = s.indexOf('endobj', start);
    objs.set(parseInt(m[1], 10), { start, end: endIdx < 0 ? s.length : endIdx });
  }
}
const objText = (n) => (objs.get(n) ? s.slice(objs.get(n).start, objs.get(n).end) : '');
function streamData(n) {
  const o = objs.get(n);
  if (!o) return null;
  const k = s.indexOf('stream', o.start);
  if (k < 0 || k > o.end) return null;
  let st = k + 6;
  if (s[st] === '\r') st++;
  if (s[st] === '\n') st++;
  const e = s.indexOf('endstream', st);
  const raw = Buffer.from(buf.slice(st, e < 0 ? o.end : e));
  const dict = s.slice(o.start, k);
  if (/\/FlateDecode/.test(dict)) { try { return zlib.inflateSync(raw); } catch (err) { return null; } }
  return raw;
}

function parseCMap(text) {
  const map = new Map();
  for (const blk of text.matchAll(/beginbfchar([\s\S]*?)endbfchar/g)) {
    for (const p of blk[1].matchAll(/<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>/g)) {
      const c = parseInt(p[1], 16);
      let str = '';
      for (let i = 0; i + 3 < p[2].length; i += 4) str += String.fromCharCode(parseInt(p[2].substr(i, 4), 16));
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

const fontMaps = new Map();
const fontCMaps = new Map();
for (const [num, o] of objs) {
  const t = s.slice(o.start, o.end);
  if (!/\/Type\s*\/Font/.test(t)) continue;
  let diffs = t.match(/\/Differences\s*\[([\s\S]*?)\]/);
  if (!diffs) {
    const r = t.match(/\/Encoding\s+(\d+)\s+0\s+R/);
    if (r) diffs = objText(parseInt(r[1], 10)).match(/\/Differences\s*\[([\s\S]*?)\]/);
  }
  const map = new Map();
  if (diffs) {
    let code = 0;
    for (const tok of diffs[1].match(/\d+|\/[^\s\/\[\]<>()]+/g) || []) {
      if (tok[0] === '/') { map.set(code, tok.slice(1)); code++; } else code = parseInt(tok, 10);
    }
  }
  fontMaps.set(num, map);
  const tu = t.match(/\/ToUnicode\s+(\d+)\s+0\s+R/);
  if (tu) {
    const d = streamData(parseInt(tu[1], 10));
    if (d) fontCMaps.set(num, parseCMap(d.toString('latin1')));
  }
}
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

const globalFontRes = new Map();
for (const m of s.matchAll(/\/Font\s*<<([^>]*)>>/g)) {
  for (const it of m[1].matchAll(/\/(\w+)\s+(\d+)\s+0\s+R/g)) globalFontRes.set(it[1], parseInt(it[2], 10));
}

let pageNo = 0;
for (const [num, o] of objs) {
  const t = s.slice(o.start, o.end);
  if (!/\/Type\s*\/Page[^s]/.test(t)) continue;
  pageNo++;
  const contents = [...t.matchAll(/\/Contents\s+(\d+)\s+0\s+R/g)].map((x) => parseInt(x[1], 10));
  if (!contents.length) continue;
  const localFonts = new Map(globalFontRes);
  const resM = t.match(/\/Resources\s+(\d+)\s+0\s+R/);
  const resText = resM ? objText(parseInt(resM[1], 10)) : t;
  const fm = resText.match(/\/Font\s*<<([\s\S]*?)>>/);
  if (fm) for (const it of fm[1].matchAll(/\/(\w+)\s+(\d+)\s+0\s+R/g)) localFonts.set(it[1], parseInt(it[2], 10));

  const items = [];
  for (const cnum of contents) {
    const data = streamData(cnum);
    if (!data) continue;
    const cs = data.toString('latin1');
    let curFont = null, x = 0, y = 0, fontSize = 10;
    const re = /(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+Tm|\/(\w+)\s+([\d.]+)\s+Tf|(\((?:\\.|[^\\()])*\))|<([0-9A-Fa-f\s]*)>|(Tj|TJ|T\*)/g;
    let tk;
    while ((tk = re.exec(cs))) {
      if (tk[1] !== undefined) { x = parseFloat(tk[5]); y = parseFloat(tk[6]); continue; }
      if (tk[7]) { curFont = localFonts.get(tk[7]) ?? null; fontSize = parseFloat(tk[8]); continue; }
      if (tk[11] === 'T*') { y -= 12; continue; }
      let str = '';
      if (tk[9] !== undefined) {
        str = tk[9].slice(1, -1).replace(/\\([()\\])/g, '$1').replace(/\\([0-7]{1,3})/g, (_, o) => String.fromCharCode(parseInt(o, 8)));
      } else if (tk[10] !== undefined) {
        const hex = tk[10].replace(/\s+/g, '');
        for (let i = 0; i + 1 < hex.length; i += 2) str += String.fromCharCode(parseInt(hex.substr(i, 2), 16));
      } else continue;
      const cmap = curFont !== null ? fontCMaps.get(curFont) : null;
      const map = curFont !== null ? fontMaps.get(curFont) : null;
      if (cmap && cmap.size && tk[10] !== undefined) {
        const hex = tk[10].replace(/\s+/g, '');
        let dec = '';
        for (let i = 0; i + 3 < hex.length; i += 4) {
          const c = parseInt(hex.substr(i, 4), 16);
          dec += cmap.has(c) ? cmap.get(c) : '?';
        }
        str = dec;
      } else if (map && map.size) {
        let dec = '';
        for (const ch of str) { const n = map.get(ch.charCodeAt(0)); dec += n ? (n.length === 1 ? n : '') : ''; }
        str = dec;
      }
      if (str.length) items.push({ x, y, str });
    }
  }
  if (!items.length) continue;
  if (pageFilter && !pageFilter.test(String(pageNo))) continue;
  const rotate = process.argv[4] === 'rotate';
  if (process.argv[4] === 'raw') {
    console.log(`\n===== page ${pageNo} (raw items) =====`);
    for (const it of items.sort((a, b) => a.x - b.x || b.y - a.y)) {
      console.log(`x=${Math.round(it.x)}\ty=${Math.round(it.y)}\t${JSON.stringify(it.str)}`);
    }
    continue;
  }
  const rows = [];
  if (!rotate) {
    for (const it of items.sort((a, b) => b.y - a.y || a.x - b.x)) {
      let row = rows.find((r) => Math.abs(r.y - it.y) < 3.5);
      if (!row) { row = { y: it.y, items: [] }; rows.push(row); }
      row.items.push(it);
    }
    rows.sort((a, b) => b.y - a.y);
    console.log(`\n===== page ${pageNo} =====`);
    for (const r of rows) {
      let line = '';
      let lastX = -1e9;
      for (const it of r.items.sort((a, b) => a.x - b.x)) {
        if (lastX > -1e8 && it.x - lastX > 1) line += ` [x=${Math.round(it.x)}] `;
        line += it.str;
        lastX = it.x;
      }
      console.log(`y=${Math.round(r.y)}: ${line}`);
    }
  } else {
    for (const it of items.sort((a, b) => a.x - b.x || b.y - a.y)) {
      let row = rows.find((r) => Math.abs(r.x - it.x) < 3.5);
      if (!row) { row = { x: it.x, items: [] }; rows.push(row); }
      row.items.push(it);
    }
    rows.sort((a, b) => a.x - b.x);
    console.log(`\n===== page ${pageNo} (rotate) =====`);
    for (const r of rows) {
      let line = '';
      let lastY = -1e9;
      for (const it of r.items.sort((a, b) => b.y - a.y)) {
        if (lastY > -1e8 && lastY - it.y > 1) line += ' | ';
        line += it.str;
        lastY = it.y;
      }
      console.log(`x=${Math.round(r.x)}: ${line}`);
    }
  }
}
