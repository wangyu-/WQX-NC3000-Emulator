// Summarise a Codex rollout JSONL: user messages in full, assistant messages truncated.
// usage: node tools/read_rollout.js <rollout.jsonl> [assistantChars] [outFile]
const fs = require('fs');
const readline = require('readline');

const src = process.argv[2];
const aChars = parseInt(process.argv[3] || '600', 10);
const outPath = process.argv[4] || null;
const out = [];

function textOf(content) {
  if (typeof content === 'string') return content;
  if (!Array.isArray(content)) return '';
  return content.map((c) => c && (c.text || c.input_text || '')).join('');
}

const rl = readline.createInterface({ input: fs.createReadStream(src) });
let n = 0;
rl.on('line', (l) => {
  n++;
  let o;
  try { o = JSON.parse(l); } catch (e) { return; }
  if (o.type !== 'response_item') return;
  const p = o.payload || {};
  if (p.type !== 'message') return;
  const role = p.role;
  const txt = textOf(p.content).trim();
  if (!txt) return;
  if (role === 'user') {
    out.push(`\n\n########## USER (ordinal ${o.ordinal}) ##########\n${txt}`);
  } else if (role === 'assistant') {
    out.push(`\n---------- assistant (ordinal ${o.ordinal}) ----------\n${txt.slice(0, aChars)}${txt.length > aChars ? ' …[truncated]' : ''}`);
  }
});
rl.on('close', () => {
  const text = out.join('\n');
  if (outPath) { fs.writeFileSync(outPath, text); console.log(`wrote ${outPath} (${text.length} chars, ${n} jsonl lines)`); }
  else console.log(text);
});
