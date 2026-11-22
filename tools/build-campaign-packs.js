// Builds the archives of the official Imperivm I and II campaigns for GameBanana, from the content packs the converters wrote
// (tools/imperivm1/convert.js, tools/imperivm2/convert.js). Each archive holds several content packs, one folder per mod.json id,
// which the mod installs with DlInstallBundle (src/mods/campaignpacks.cpp), and a NOTICE.txt: the campaigns are the old games'
// content, distributed with the rights holder's permission, and not covered by the GPL.
//
//   node tools/build-campaign-packs.js --mods "<folder with the converted content packs>" --out "<new folder>"
//
// It prints the size and MD5 of each archive and the line of the table in src/mods/campaignpacks.cpp; the GameBanana ids are filled
// in once the files are uploaded. Maintainer only: it needs the conversions of the old games.
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');

const NOTICE = [
	'Imperivm I and Imperivm II campaigns for Imperivm Resurrected',
	'',
	'The maps, scripts, texts, voices, pictures and movies of these campaigns come from Imperivm I (La guerra de las Galias) and',
	'Imperivm II (La Conquista de Hispania), and belong to their rights holders (FX Interactive / Dinamic Software). They were',
	'converted for Imperivm: Great Battles of Rome HD and are distributed with the rights holder\'s permission, as part of',
	'Imperivm Resurrected.',
	'',
	'This content is not covered by the GPL license of Imperivm Resurrected\'s code. Do not redistribute it outside the',
	'official downloads.',
	'',
	'---',
	'',
	'Las campañas de Imperivm I e Imperivm II para Imperivm Resurrected',
	'',
	'Los mapas, scripts, textos, voces, imágenes y vídeos de estas campañas vienen de Imperivm I (La guerra de las Galias) e',
	'Imperivm II (La Conquista de Hispania), y pertenecen a sus titulares (FX Interactive / Dinamic Software). Se han convertido',
	'para Imperivm: Great Battles of Rome HD y se distribuyen con el permiso del titular de los derechos, como parte de',
	'Imperivm Resurrected.',
	'',
	'Este contenido no está cubierto por la licencia GPL del código de Imperivm Resurrected. No lo redistribuyas fuera de las',
	'descargas oficiales.',
	''
].join('\r\n');

const NEEDS = {
	spanish: 'Necesita «%s», que viene en la misma descarga.',
	english: 'It needs "%s", which comes in the same download.',
	italian: 'Richiede «%s», incluso nello stesso download.'
};

// game, version and ids must match src/mods/campaignpacks.cpp.
const PACKS = [
	{
		game: 1, key: 'imperivm1', version: '1.0', file: 'imperivm1-la-guerra-de-las-galias',
		name: 'Imperivm I: La guerra de las Galias',
		mods: {
			'imperivm1-campaign': {
				spanish: 'La campaña de Imperivm I, La guerra de las Galias, con sus mapas, misiones, conversaciones y voces originales.',
				english: 'The campaign of Imperivm I, The Gallic War, with its original maps, missions, conversations and voices.',
				italian: 'La campagna di Imperivm I, La guerra gallica, con le mappe, le missioni, i dialoghi e le voci originali.',
				needs: 'imperivm1-campaign-data'
			},
			'imperivm1-campaign-data': {
				spanish: 'Las unidades, héroes y edificios de Imperivm I con su comportamiento original, solo para su campaña.',
				english: 'The units, heroes and buildings of Imperivm I with their original behaviour, for its campaign only.',
				italian: 'Le unità, gli eroi e gli edifici di Imperivm I con il loro comportamento originale, solo per la sua campagna.'
			}
		}
	},
	{
		game: 2, key: 'imperivm2', version: '0.2.0', file: 'imperivm2-las-campanas',
		name: 'Imperivm II: the campaigns',
		mods: {
			'imperivm2-data': {
				spanish: 'Las unidades, héroes y edificios de Imperivm II con su comportamiento original, y sus vídeos, solo para sus campañas.',
				english: 'The units, heroes and buildings of Imperivm II with their original behaviour, and its movies, for its campaigns only.',
				italian: 'Le unità, gli eroi e gli edifici di Imperivm II con il loro comportamento originale, e i suoi video, solo per le sue campagne.'
			},
			'imperivm2-hannibals-campaigns': {
				spanish: 'Las campañas de Aníbal, de Imperivm II, con sus mapas, misiones y conversaciones originales.',
				english: 'Hannibal\'s Campaigns, from Imperivm II, with their original maps, missions and conversations.',
				italian: 'Le campagne di Annibale, da Imperivm II, con le mappe, le missioni e i dialoghi originali.',
				needs: 'imperivm2-data'
			},
			'imperivm2-the-punic-wars': {
				spanish: 'Las guerras púnicas, de Imperivm II, con sus mapas, misiones y conversaciones originales.',
				english: 'The Punic Wars, from Imperivm II, with their original maps, missions and conversations.',
				italian: 'Le guerre puniche, da Imperivm II, con le mappe, le missioni e i dialoghi originali.',
				needs: 'imperivm2-data'
			},
			'imperivm2-numantia': {
				spanish: 'Numancia, de Imperivm II, con sus mapas, misiones y conversaciones originales.',
				english: 'Numantia, from Imperivm II, with its original maps, missions and conversations.',
				italian: 'Numanzia, da Imperivm II, con le mappe, le missioni e i dialoghi originali.',
				needs: 'imperivm2-data'
			}
		}
	}
];

// What the converters leave next to a content pack for whoever ran them, and the mod's own cache (.fingerprint), not for players.
const LEFT_OUT = new Set(['report.txt', 'audit.txt', 'unpacked', '.fingerprint']);

function fail(message) {
	console.error(`build-campaign-packs: ${message}`);
	process.exit(1);
}

function parseArgs(argv) {
	const opts = {};
	for (let i = 0; i < argv.length; i++) {
		if (argv[i] === '--mods') opts.mods = argv[++i];
		else if (argv[i] === '--out') opts.out = argv[++i];
		else fail(`unknown argument ${argv[i]}`);
	}
	if (!opts.mods || !opts.out) fail('usage: --mods <folder> --out <new folder>');
	return opts;
}

function readJson(file) {
	return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^﻿/, ''));
}

// The folder of each content pack under `root`, by its mod.json id.
function modFolders(root) {
	const found = new Map();
	for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
		const manifest = path.join(root, entry.name, 'mod.json');
		if (!entry.isDirectory() || !fs.existsSync(manifest)) continue;
		const id = readJson(manifest).id;
		if (id) found.set(id.toLowerCase(), path.join(root, entry.name));
	}
	return found;
}

function copyTree(from, to) {
	fs.mkdirSync(to, { recursive: true });
	for (const entry of fs.readdirSync(from, { withFileTypes: true })) {
		if (LEFT_OUT.has(entry.name.toLowerCase())) continue;
		const a = path.join(from, entry.name), b = path.join(to, entry.name);
		if (entry.isDirectory()) copyTree(a, b);
		else fs.copyFileSync(a, b);
	}
}

function displayName(manifest, language) {
	return typeof manifest.name === 'string' ? manifest.name : manifest.name[language] || manifest.name.english || manifest.id;
}

function main() {
	if (process.platform !== 'win32') fail('it needs Windows (tar.exe makes the zip)');
	const opts = parseArgs(process.argv.slice(2));
	if (fs.existsSync(opts.out) && fs.readdirSync(opts.out).length) fail(`${opts.out} is not empty`);
	const folders = modFolders(opts.mods);
	const stageRoot = path.join(opts.out, 'stage');
	const tar = path.join(process.env.SystemRoot, 'System32', 'tar.exe');
	const table = [];
	for (const pack of PACKS) {
		const stage = path.join(stageRoot, pack.key);
		const ids = Object.keys(pack.mods);
		const manifests = {};
		for (const id of ids) {
			const from = folders.get(id);
			if (!from) fail(`${pack.key}: no content pack with the id ${id} in ${opts.mods}`);
			copyTree(from, path.join(stage, id));
			manifests[id] = readJson(path.join(stage, id, 'mod.json'));
		}
		for (const id of ids) {
			const m = manifests[id], texts = pack.mods[id];
			m.version = pack.version;
			m.authors = ['Imperivm Resurrected'];
			m.redistributable = true;
			m.license = 'NOTICE.txt';
			m.description = {};
			for (const language of ['spanish', 'english', 'italian']) {
				const needs = texts.needs ? ' ' + NEEDS[language].replace('%s', displayName(manifests[texts.needs], language)) : '';
				m.description[language] = texts[language] + needs;
			}
			fs.writeFileSync(path.join(stage, id, 'mod.json'), JSON.stringify(m, null, '\t') + '\n');
			fs.writeFileSync(path.join(stage, id, 'NOTICE.txt'), NOTICE);
		}
		fs.writeFileSync(path.join(stage, 'NOTICE.txt'), NOTICE);
		const zip = path.join(opts.out, `${pack.file}-${pack.version}.zip`);
		execFileSync(tar, ['-a', '-c', '-f', zip, '-C', stage, 'NOTICE.txt', ...ids]);
		const data = fs.readFileSync(zip);
		const md5 = crypto.createHash('md5').update(data).digest('hex');
		console.log(`${path.basename(zip)}  ${(data.length / 1048576).toFixed(1)} MB  md5 ${md5}`);
		const idList = ids.map(id => `"${id}"`).join(', ');
		table.push(`{${pack.game}, "${pack.name}", "${pack.version}", <gbId>, <gbFileId>, "${md5}", ${data.length}, {${idList}}},`);
	}
	fs.rmSync(stageRoot, { recursive: true, force: true });
	console.log('\nsrc/mods/campaignpacks.cpp, once the files are on GameBanana:');
	for (const line of table) console.log('\t\t' + line);
}

main();
