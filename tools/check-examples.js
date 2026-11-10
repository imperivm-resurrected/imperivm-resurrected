// Runs the validator of the SDK on every example and every template: one that stops validating is broken.
// No game files are needed.
//   node tools/check-examples.js
'use strict';
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const SDK = path.resolve(__dirname, '..', 'sdk');
const validator = path.join(SDK, 'tools', 'validate-mod.js');
let checked = 0;
const problems = [];
for (const group of ['examples', 'templates']) {
  const root = path.join(SDK, group);
  if (!fs.existsSync(root)) continue;
  for (const name of fs.readdirSync(root)) {
    if (!fs.statSync(path.join(root, name)).isDirectory()) continue;
    // An example of several packages has no mod.json of its own: each folder with one is a package.
    const dir = path.join(root, name);
    const packs = fs.existsSync(path.join(dir, 'mod.json')) ? [''] : fs.readdirSync(dir).filter(d => fs.existsSync(path.join(dir, d, 'mod.json')));
    for (const pack of packs.length ? packs : ['']) {
      const r = spawnSync(process.execPath, [validator, path.join(dir, pack)], { encoding: 'utf8' });
      checked++;
      if (r.status !== 0) problems.push(`${group}/${name}${pack ? '/' + pack : ''}: ${(r.stdout + r.stderr).trim().split('\n').slice(0, 4).join(' | ')}`);
    }
  }
}
console.log(`${checked} examples and templates validated, ${problems.length} with problems`);
for (const p of problems) console.log('  ' + p);
process.exitCode = problems.length ? 1 : 0;
