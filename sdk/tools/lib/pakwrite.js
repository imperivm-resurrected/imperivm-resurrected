'use strict';
// Writes a game .pak (HMMSYS PackFile): the container the game reads its data from, and the one a ruleset can bring in packs\ (or local\ for a language).
//   header (0x28 bytes): "HMMSYS PackFile\n" 0x1a, zeros, the number of members (u32 at 0x20), the length of the directory (u32 at 0x24)
//   directory: per member, in order of name: the length of the name (u8), how many characters it shares with the previous name (u8), the rest of the name,
//              the offset (u32) and the size (u32) of its bytes in the file
//   data: the members, one after another (not compressed)
// Names are upper case with backslashes, as the game's own paks have them ("UI\ICONS\RHASTATUS.BMP"). The reader of the game finds a file by that name.
const MAX_NAME = 255;

function packName(name) {
	const n = name.replace(/\//g, '\\').replace(/^\\+/, '').toUpperCase();
	if (!n || n.length > MAX_NAME || /[^\x20-\x7e]/.test(n)) throw new Error(`the name "${name}" cannot be in a pak (1 to ${MAX_NAME} printable ASCII characters)`);
	return n;
}

// files: [{ name, data: Buffer }]. Returns the pak as a Buffer.
function buildPak(files) {
	const members = files.map(f => ({ name: packName(f.name), data: f.data }));
	members.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
	for (let i = 1; i < members.length; i++) if (members[i].name === members[i - 1].name) throw new Error(`two members are named ${members[i].name}`);
	let dirLength = 0, prev = '';
	const shared = [];
	for (const m of members) {
		let s = 0;
		while (s < prev.length && s < m.name.length && prev[s] === m.name[s]) s++;
		shared.push(Math.min(s, 255));
		dirLength += 2 + m.name.length - shared[shared.length - 1] + 8;
		prev = m.name;
	}
	const dataStart = (0x28 + dirLength + 15) & ~15;
	const head = Buffer.alloc(0x28);
	head.write('HMMSYS PackFile\n', 0, 'latin1');
	head[16] = 0x1a;
	head.writeUInt32LE(members.length, 0x20);
	head.writeUInt32LE(dirLength, 0x24);
	const dir = Buffer.alloc(dirLength);
	let p = 0, offset = dataStart;
	members.forEach((m, i) => {
		dir[p++] = m.name.length;
		dir[p++] = shared[i];
		p += dir.write(m.name.slice(shared[i]), p, 'latin1');
		dir.writeUInt32LE(offset, p);
		dir.writeUInt32LE(m.data.length, p + 4);
		p += 8;
		offset += m.data.length;
	});
	const pad = Buffer.alloc(dataStart - 0x28 - dirLength);
	return Buffer.concat([head, dir, pad, ...members.map(m => m.data)]);
}

module.exports = { buildPak, packName };
