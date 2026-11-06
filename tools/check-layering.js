// Checks the include direction between the folders of src/. From the bottom up:
//   addr < core, i18n < hooks < mods < game < net < ui < app
// A file may include headers of its own folder and of the folders before its own. Two more rules:
//   - core must not include addr (it has to stay free of game addresses);
//   - the reusable files (core, the translation table and the interface kit) may only include reusable files.
// The includes that already break the direction are listed in tools/layering-baseline.txt. The check fails for a new one, and also
// when a listed one is gone (so the list only shrinks: delete the line).
//   node tools/check-layering.js                 check
//   node tools/check-layering.js --write-baseline    rewrite the list from the current state (review the diff!)
'use strict';
const fs = require('fs');
const path = require('path');

const SRC = path.resolve(__dirname, '..', 'src');
const BASELINE = path.join(__dirname, 'layering-baseline.txt');
const RANK = { addr: 0, core: 1, i18n: 1, hooks: 2, mods: 3, game: 4, net: 5, ui: 6, app: 7 };
const REUSABLE = [/^core\//, /^i18n\/tr(_data)?\.(h|cpp)$/, /^ui\/(uikit|widgets|listbox|textedit|richtext|ornaments)\.(h|cpp)$/];
const isReusable = (rel) => REUSABLE.some((re) => re.test(rel));

function walk(dir, rel, out) {
  for (const e of fs.readdirSync(path.join(dir, rel), { withFileTypes: true })) {
    const r = rel ? rel + '/' + e.name : e.name;
    if (e.isDirectory()) walk(dir, r, out);
    else if (/\.(cpp|h|c)$/.test(e.name)) out.push(r);
  }
  return out;
}

const files = walk(SRC, '', []);
const known = new Set(files);
const violations = [];
for (const rel of files) {
  const from = rel.split('/')[0];
  if (!(from in RANK)) { violations.push(rel + ' -> (folder ' + from + ' is not in the layer list)'); continue; }
  const text = fs.readFileSync(path.join(SRC, rel), 'latin1');
  for (const m of text.matchAll(/^[ \t]*#[ \t]*include[ \t]+"([^"]+)"/gm)) {
    const target = m[1];
    const to = target.split('/')[0];
    if (!(to in RANK)) continue;
    if (!known.has(target) && !fs.existsSync(path.join(SRC, target))) { violations.push(rel + ' -> ' + target + '  (file does not exist)'); continue; }
    if (RANK[to] > RANK[from]) violations.push(rel + ' -> ' + target);
    else if (from === 'core' && to === 'addr') violations.push(rel + ' -> ' + target);
    else if (isReusable(rel) && !isReusable(target)) violations.push(rel + ' -> ' + target);
  }
}
const current = [...new Set(violations)].sort();

if (process.argv.includes('--write-baseline')) {
  fs.writeFileSync(BASELINE, current.join('\n') + '\n');
  console.log('baseline written: ' + current.length + ' includes');
  process.exit(0);
}

const baseline = new Set(fs.existsSync(BASELINE) ? fs.readFileSync(BASELINE, 'utf8').split(/\r?\n/).filter(Boolean) : []);
const fresh = current.filter((v) => !baseline.has(v));
const gone = [...baseline].filter((v) => !current.includes(v));
console.log(`${files.length} files, ${current.length} includes against the direction (${baseline.size} in the baseline)`);
if (fresh.length) { console.log('NEW (not allowed):'); for (const v of fresh) console.log('  ' + v); }
if (gone.length) { console.log('FIXED (remove these lines from tools/layering-baseline.txt):'); for (const v of gone) console.log('  ' + v); }
process.exitCode = fresh.length || gone.length ? 1 : 0;
