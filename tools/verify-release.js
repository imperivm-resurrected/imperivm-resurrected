// Verifies the files of a release before (or after) they are published, without the private key:
//   - the version of the tag is the one in src/core/version_info.h (IMPERIVM_VERSION_TEXT, and the same numbers in IMPERIVM_VERSION_NUMBER);
//   - version.json: same version, the link of the zip points to this release, size and SHA-256 of the zip are the real ones, and the
//     signature is valid for the public key that the mod has in src/mods/updater.cpp (the one the game checks before installing);
//   - its build, when it has one, is the SHA-256 of the zip's winmm.dll and its signature is valid for the same key;
//   - the zip holds winmm.dll, steam_api.dll and the imperivm-resurrected\ folder and nothing else, with no path that escapes;
//   - the installer (imperivm-resurrected-setup-<version>.exe), if there is one, carries that same zip byte for byte;
//   - release-notes/<version>.md is the one source of the notes: the notes in version.json and, with --body, the body of the release are the same text;
//   - the SDK zip, if there is one, holds exactly the files of sdk/ at this commit;
//   - SHA256SUMS.txt, if there is one, matches the files.
//   node tools/verify-release.js <folder with the files> [--tag v0.31.54] [--repo THIONG/imperivm-resurrected] [--body release-body.md] [--write-sums]
//   --body        the body of the release as published (gh release view --json body --jq .body), to compare with release-notes/<version>.md
//   --write-sums  after verifying, writes SHA256SUMS.txt in the folder (zips and version.json)
// build-release.js runs it on the folder it prepares; the release workflow runs it on the files downloaded from the release.
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const args = process.argv.slice(2);
const option = (name, fallback) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : fallback; };
const dir = args.find((a, i) => !a.startsWith('--') && !['--tag', '--repo', '--body'].includes(args[i - 1]));
const tag = option('--tag', '');
const repo = option('--repo', 'THIONG/imperivm-resurrected');
const bodyFile = option('--body', '');
if (!dir) { console.error('usage: node tools/verify-release.js <folder> [--tag vX.Y.Z] [--repo owner/name] [--body file] [--write-sums]'); process.exit(2); }

const problems = [];
const bad = (m) => problems.push(m);
const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');
const tarExe = process.platform === 'win32' ? path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'tar.exe') : 'tar';
const listZip = (zip) => execFileSync(tarExe, ['-tf', zip], { encoding: 'utf8', maxBuffer: 1 << 28 }).split(/\r?\n/).map((l) => l.trim()).filter(Boolean);

const versionSource = fs.readFileSync(path.join(ROOT, 'src', 'core', 'version_info.h'), 'utf8');
const version = (/IMPERIVM_VERSION_TEXT\s+"([^"]+)"/.exec(versionSource) || [])[1];
if (!version) { console.error('IMPERIVM_VERSION_TEXT not found in src/core/version_info.h'); process.exit(2); }
const versionNumber = (/IMPERIVM_VERSION_NUMBER\s+([\d, ]+)/.exec(versionSource) || [])[1];
if (versionNumber && versionNumber.replace(/\s/g, '').split(',').slice(0, 3).join('.') !== version) bad(`IMPERIVM_VERSION_NUMBER is ${versionNumber} but IMPERIVM_VERSION_TEXT is ${version}`);
if (tag && tag !== 'v' + version) bad(`the tag is ${tag} but src/core/version_info.h says ${version}`);

const publicKey = () => {
  const text = fs.readFileSync(path.join(ROOT, 'src', 'mods', 'updater.cpp'), 'utf8');
  const body = (/kPubKey\[64\]\s*=\s*\{([^}]*)\}/.exec(text) || [])[1];
  const bytes = body ? (body.match(/0x[0-9a-fA-F]{2}/g) || []).map((h) => parseInt(h, 16)) : [];
  if (bytes.length !== 64) throw new Error('kPubKey not found in src/mods/updater.cpp');
  const spkiPrefix = Buffer.from('3059301306072a8648ce3d020106082a8648ce3d030107034200', 'hex');
  return crypto.createPublicKey({ key: Buffer.concat([spkiPrefix, Buffer.from([4]), Buffer.from(bytes)]), format: 'der', type: 'spki' });
};

const sameText = (a, b) => a.replace(/\r\n/g, '\n').trim() === b.replace(/\r\n/g, '\n').trim();
const notesPath = path.join(ROOT, 'release-notes', `${version}.md`);
const sourceNotes = fs.existsSync(notesPath) ? fs.readFileSync(notesPath, 'utf8') : null;
if (sourceNotes === null) bad(`release-notes/${version}.md is missing: it is the one source of the release notes`);
if (sourceNotes !== null && bodyFile) {
  if (!fs.existsSync(bodyFile)) bad(`${bodyFile} (the body of the release) does not exist`);
  else if (!sameText(fs.readFileSync(bodyFile, 'utf8').replace(/^<!--[^>]*-->\s*/, ''), sourceNotes.replace(/^<!--[^>]*-->\s*/, ''))) bad(`the body of the release is not the text of release-notes/${version}.md (edit the release with gh release edit --notes-file)`);
}

const zipName = `imperivm-resurrected-${version}.zip`;
const zipPath = path.join(dir, zipName);
const jsonPath = path.join(dir, 'version.json');
if (!fs.existsSync(zipPath)) bad(`${zipName} is missing`);
if (!fs.existsSync(jsonPath)) bad('version.json is missing');

let info = null;
if (fs.existsSync(jsonPath)) {
  try { info = JSON.parse(fs.readFileSync(jsonPath, 'utf8')); } catch (e) { bad('version.json is not valid JSON: ' + e.message); }
}

if (fs.existsSync(zipPath) && info) {
  const data = fs.readFileSync(zipPath);
  const hash = sha256(data);
  if (info.version !== version) bad(`version.json says ${info.version}, the code says ${version}`);
  if (!info.zip || info.zip.url !== `https://github.com/${repo}/releases/download/v${version}/${zipName}`) bad(`version.json links to ${info.zip && info.zip.url}, not to this release`);
  if (info.zip && info.zip.bytes !== data.length) bad(`version.json says ${info.zip.bytes} bytes, the zip has ${data.length}`);
  if (info.zip && info.zip.sha256 !== hash) bad('version.json has a different SHA-256 than the zip');
  if (sourceNotes !== null && !sameText(String(info.notes || '').replace(/^<!--[^>]*-->\s*/, ''), sourceNotes.replace(/^<!--[^>]*-->\s*/, ''))) bad(`the notes in version.json are not the text of release-notes/${version}.md`);
  if (info.firma !== info.signature || info.notas !== info.notes || info.fecha !== info.date) bad('the legacy keys (firma, notas, fecha) differ from the current ones');
  try {
    const message = Buffer.from(`imperivm-resurrected\n${version}\n${hash}\n${data.length}`);
    if (!/^[0-9a-f]{128}$/.test(info.signature || '') || !crypto.verify('sha256', message, { key: publicKey(), dsaEncoding: 'ieee-p1363' }, Buffer.from(info.signature, 'hex'))) {
      bad('the signature of version.json is not valid for the public key of the mod');
    }
  } catch (e) { bad('the signature could not be checked: ' + e.message); }
  const entries = listZip(zipPath);
  if (!entries.includes('winmm.dll')) bad(`${zipName} has no winmm.dll`);
  // The build: the SHA-256 of the zip's winmm.dll, signed apart. Releases from before it have none.
  if (info.build !== undefined && entries.includes('winmm.dll')) {
    const dllHash = sha256(execFileSync(tarExe, ['-x', '-O', '-f', zipPath, 'winmm.dll'], { maxBuffer: 1 << 28 }));
    if (!info.build || info.build.sha256 !== dllHash) bad("version.json has another build than the SHA-256 of the zip's winmm.dll");
    try {
      const message = Buffer.from(`imperivm-resurrected-build
${version}
${dllHash}`);
      if (!/^[0-9a-f]{128}$/.test((info.build && info.build.signature) || '') || !crypto.verify('sha256', message, { key: publicKey(), dsaEncoding: 'ieee-p1363' }, Buffer.from(info.build.signature, 'hex'))) {
        bad('the signature of the build in version.json is not valid for the public key of the mod');
      }
    } catch (e) { bad('the signature of the build could not be checked: ' + e.message); }
  }
  if (!entries.includes('steam_api.dll')) bad(`${zipName} has no steam_api.dll`);
  for (const e of entries) {
    if (e !== 'winmm.dll' && e !== 'steam_api.dll' && !e.startsWith('imperivm-resurrected/')) bad(`${zipName} holds ${e}, only winmm.dll, steam_api.dll and imperivm-resurrected/ may go in`);
    if (e.includes('..') || path.isAbsolute(e) || e.includes(':')) bad(`${zipName} has an unsafe path: ${e}`);
  }
}

const setupName = `imperivm-resurrected-setup-${version}.exe`;
const setupPath = path.join(dir, setupName);
if (fs.existsSync(setupPath) && fs.existsSync(zipPath)) {
  const setup = fs.readFileSync(setupPath);
  const zip = fs.readFileSync(zipPath);
  const trailer = setup.subarray(setup.length - 80);
  const size = Number(trailer.readBigUInt64LE(8));
  const inner = setup.subarray(setup.length - 80 - size, setup.length - 80);
  if (trailer.toString('ascii', 0, 8) !== 'IRSETUP1') bad(`${setupName} has no installer trailer`);
  else if (size !== zip.length || !inner.equals(zip)) bad(`${setupName} does not carry ${zipName} byte for byte`);
  else if (trailer.subarray(16, 48).toString('hex') !== sha256(zip)) bad(`${setupName} carries a wrong SHA-256 of the zip`);
  else if (trailer.toString('utf8', 48, 80).replace(/\0+$/, '') !== version) bad(`${setupName} says another version`);
}

const SDK_ROOT = fs.existsSync(path.join(ROOT, 'sdk')) ? path.join(ROOT, 'sdk') : path.resolve(ROOT, '..', 'sdk');
const sdkChangelog = path.join(SDK_ROOT, 'CHANGELOG.md');
const sdkVersion = fs.existsSync(sdkChangelog) ? (/^## (\d+\.\d+(?:\.\d+)?)/m.exec(fs.readFileSync(sdkChangelog, 'utf8')) || [])[1] : '';
const sdkZip = sdkVersion ? path.join(dir, `imperivm-resurrected-sdk-${sdkVersion}.zip`) : '';
if (sdkZip && fs.existsSync(sdkZip)) {
  const walk = (d, rel, out) => {
    for (const e of fs.readdirSync(path.join(d, rel), { withFileTypes: true })) {
      const r = rel ? rel + '/' + e.name : e.name;
      if (e.isDirectory()) walk(d, r, out); else out.push(r);
    }
    return out;
  };
  const expected = new Set(walk(SDK_ROOT, '', []));
  const found = new Set(listZip(sdkZip).filter((e) => !e.endsWith('/')));
  for (const f of expected) if (!found.has(f)) bad(`the SDK zip lacks ${f}`);
  for (const f of found) if (!expected.has(f)) bad(`the SDK zip has ${f}, which is not in sdk/`);
}

const sumsPath = path.join(dir, 'SHA256SUMS.txt');
if (fs.existsSync(sumsPath)) {
  for (const line of fs.readFileSync(sumsPath, 'utf8').split(/\r?\n/).filter(Boolean)) {
    const m = /^([0-9a-f]{64}) [ *](.+)$/.exec(line);
    const file = m && path.join(dir, m[2]);
    if (!m || !fs.existsSync(file)) bad(`SHA256SUMS.txt has a line that does not match a file: ${line}`);
    else if (sha256(fs.readFileSync(file)) !== m[1]) bad(`SHA256SUMS.txt: ${m[2]} has another hash`);
  }
}

if (problems.length) {
  console.log(`${problems.length} problems in ${dir}`);
  for (const p of problems) console.log('  ' + p);
  process.exit(1);
}
if (args.includes('--write-sums')) {
  const files = fs.readdirSync(dir).filter((f) => /\.zip$/.test(f) || f === 'version.json' || f === setupName).sort();
  fs.writeFileSync(sumsPath, files.map((f) => `${sha256(fs.readFileSync(path.join(dir, f)))}  ${f}`).join('\n') + '\n');
  console.log(`SHA256SUMS.txt written (${files.length} files)`);
}
console.log(`release ${version} verified: signature, hashes and contents are right` + (fs.existsSync(sdkZip || '-') ? `, SDK ${sdkVersion} matches sdk/` : ''));
