// Reads the game's "HMMSYS PackFile" packages (.pak): a header, an index of names (each one shares a prefix with the
// previous name) with offset and size, and the files stored whole.
'use strict';
const fs = require('fs');

const MAGIC = 'HMMSYS PackFile';

// Returns a Map of upper-case '\'-separated name -> {offset, size}, and a read(name) that returns the bytes or null.
function openPak(file) {
	const data = fs.readFileSync(file);
	if (data.toString('latin1', 0, MAGIC.length) !== MAGIC) throw new Error(`${file}: not a game package`);
	const count = data.readUInt32LE(0x20);
	const index = new Map();
	let p = 0x28;
	let prev = '';
	for (let i = 0; i < count; i++) {
		const len = data[p];
		const shared = data[p + 1];
		p += 2;
		const name = prev.slice(0, shared) + data.toString('latin1', p, p + len - shared);
		p += len - shared;
		index.set(name.toUpperCase(), { name, offset: data.readUInt32LE(p), size: data.readUInt32LE(p + 4) });
		p += 8;
		prev = name;
	}
	const read = (name) => {
		const e = index.get(name.replace(/\//g, '\\').toUpperCase());
		return e ? data.subarray(e.offset, e.offset + e.size) : null;
	};
	return { index, read };
}

module.exports = { openPak };
