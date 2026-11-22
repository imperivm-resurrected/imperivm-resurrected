// Builds the files of a release in one folder and verifies them: the mod zip, version.json signed for the in-game updater and the installer.
//   node tools/build-release.js --dll build/winmm.dll --steam-api <steam_api.dll> --out <folder> [--key <updates-private.pem>] [--repo owner/name]
// The private key is the file of --key or, without it, the PEM text in the environment variable UPDATES_PRIVATE_KEY (the publish workflow
// takes it from the secret of the release environment). The notes are release-notes/<version>.md. The installer needs build/installer-stub.exe
// (installer/build.bat). The publish workflow runs it on GitHub; dll/publish-version.js runs it on the maintainer's PC.
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');
const { packInstaller } = require('../installer/pack.js');

const ROOT = path.resolve(__dirname, '..');
const STEAM_API_SHA256 = '1cb8b7cb77923762d4e49b27e44295369a79e93b5d4282c5e840b268d28fe771';

const args = process.argv.slice(2);
const option = (name, fallback) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : fallback; };
const fail = (message) => { console.error('ERROR: ' + message); process.exit(1); };
const dll = option('--dll');
const steamApi = option('--steam-api');
const out = option('--out');
const keyFile = option('--key');
const repo = option('--repo', 'THIONG/imperivm-resurrected');
if (!dll || !steamApi || !out) fail('usage: node tools/build-release.js --dll <winmm.dll> --steam-api <steam_api.dll> --out <folder> [--key <pem>] [--repo owner/name]');
if (process.platform !== 'win32') fail('it needs Windows (tar.exe makes the zip)');
const outDir = path.resolve(out);
const rel = path.relative(outDir, ROOT);
if (rel === '' || !rel.startsWith('..') || fs.existsSync(path.join(outDir, '.git'))) fail(`--out ${out} holds the repository: give it a folder of its own, it is emptied first`);

const m = fs.readFileSync(path.join(ROOT, 'src', 'core', 'version_info.h'), 'utf8').match(/IMPERIVM_VERSION_TEXT\s+"([^"]+)"/);
if (!m) fail('IMPERIVM_VERSION_TEXT not found in src/core/version_info.h');
const version = m[1];

const notesPath = path.join(ROOT, 'release-notes', `${version}.md`);
if (!fs.existsSync(notesPath)) fail(`release-notes/${version}.md is missing: write the notes of the version first`);
const notes = fs.readFileSync(notesPath, 'utf8').replace(/^<!--[^>]*-->\s*/, '').trim();

const pem = keyFile ? fs.readFileSync(keyFile, 'utf8') : process.env.UPDATES_PRIVATE_KEY;
if (!pem || !pem.trim()) fail('no private key: pass --key or set UPDATES_PRIVATE_KEY');
const key = crypto.createPrivateKey(pem);

if (!fs.existsSync(dll)) fail(dll + ' is missing: build first');
if (!fs.existsSync(steamApi)) fail(steamApi + ' is missing');
const steamHash = crypto.createHash('sha256').update(fs.readFileSync(steamApi)).digest('hex');
if (steamHash !== STEAM_API_SHA256) fail(`steam_api.dll has SHA-256 ${steamHash}, not the one of every release so far: if Valve's file changed on purpose, update STEAM_API_SHA256`);

fs.rmSync(outDir, { recursive: true, force: true });
const stage = path.join(outDir, 'content');
fs.mkdirSync(stage, { recursive: true });
fs.copyFileSync(dll, path.join(stage, 'winmm.dll'));
fs.copyFileSync(steamApi, path.join(stage, 'steam_api.dll'));
fs.cpSync(path.join(ROOT, 'mod'), path.join(stage, 'imperivm-resurrected'), { recursive: true });

const zipName = `imperivm-resurrected-${version}.zip`;
const zip = path.join(out, zipName);
execFileSync(path.join(process.env.SystemRoot, 'System32', 'tar.exe'), ['-a', '-c', '-f', zip, '-C', stage, 'winmm.dll', 'steam_api.dll', 'imperivm-resurrected']);
fs.rmSync(stage, { recursive: true, force: true });

const setupName = `imperivm-resurrected-setup-${version}.exe`;
packInstaller(zip, version, path.join(out, setupName));

const data = fs.readFileSync(zip);
const sha256 = crypto.createHash('sha256').update(data).digest('hex');
const bytes = data.length;
const message = Buffer.from(`imperivm-resurrected\n${version}\n${sha256}\n${bytes}`);
const signature = crypto.sign('sha256', message, { key, dsaEncoding: 'ieee-p1363' }).toString('hex');
if (!crypto.verify('sha256', message, { key: crypto.createPublicKey(key), dsaEncoding: 'ieee-p1363' }, Buffer.from(signature, 'hex'))) fail('the signature could not be verified');

// The build of this release, the SHA-256 of its winmm.dll, signed apart: the league takes the builds of the releases as the official
// ones (imperivm-league src/builds.ts), and every game announces its own in multiplayer rooms.
const dllSha256 = crypto.createHash('sha256').update(fs.readFileSync(dll)).digest('hex');
const buildMessage = Buffer.from(`imperivm-resurrected-build
${version}
${dllSha256}`);
const buildSignature = crypto.sign('sha256', buildMessage, { key, dsaEncoding: 'ieee-p1363' }).toString('hex');

const date = new Date().toISOString().slice(0, 10);
const info = {
  version,
  date,
  notes,
  zip: { url: `https://github.com/${repo}/releases/download/v${version}/${zipName}`, bytes, sha256 },
  signature,
  build: { sha256: dllSha256, signature: buildSignature },
  fecha: date,
  notas: notes,
  firma: signature,
};
fs.writeFileSync(path.join(out, 'version.json'), JSON.stringify(info, null, 2) + '\n');

execFileSync(process.execPath, [path.join(__dirname, 'verify-release.js'), out, '--tag', `v${version}`, '--repo', repo], { stdio: 'inherit' });
console.log(`Version ${version} ready in ${out}`);
console.log(`  ${zipName}  (${(bytes / 1048576).toFixed(1)} MB, sha256 ${sha256.slice(0, 16)}...)`);
console.log(`  version.json (signed; build ${dllSha256.slice(0, 12)})`);
console.log(`  ${setupName}  (installer: the same zip inside)`);
