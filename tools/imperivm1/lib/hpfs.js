// Reads and writes HPFS packages (.bfhp): the paged file system the game uses for maps and adventures.
//
// Page 0 is the header: "HPFS", the page size, the page count, the free list (none) and, at 0x1c, the page of the root folder.
// A stream (file or folder) starts on a root page: its length (u32), the depth of its index tree (u16), a reserved u16 and then
// the page numbers of its data (depth 0) or of its index pages. A folder is a stream of entries: page of the entry (u32),
// attributes (u16, bit 0 = folder), name length (u16) and the name.
'use strict';

const MAGIC = 'HPFS';
const ROOT_AT = 0x1c;
const MAX_DEPTH = 4;

function readStream(buf, pageSize, handle, limit = 256 << 20) {
	const rootAt = handle * pageSize;
	if (rootAt + 8 > buf.length) throw new Error(`stream ${handle} out of the file`);
	const length = buf.readUInt32LE(rootAt);
	const depth = buf.readUInt16LE(rootAt + 4);
	if (length > limit || depth > MAX_DEPTH) throw new Error(`stream ${handle}: bad length or depth`);
	const out = Buffer.alloc(length);
	const perPage = pageSize / 4;
	const pages = Math.ceil(length / pageSize);
	const word = (page, index) => {
		const at = page * pageSize + index * 4;
		if (at + 4 > buf.length) throw new Error(`stream ${handle}: index out of the file`);
		return buf.readUInt32LE(at);
	};
	for (let p = 0; p < pages; p++) {
		const idx = new Array(depth + 1).fill(0);
		let q = p;
		for (let i = depth; i >= 1; i--) {
			idx[i] = q % perPage;
			q = Math.floor(q / perPage);
		}
		if (q + 2 >= perPage) throw new Error(`stream ${handle}: too long for its depth`);
		idx[0] = q + 2;
		let cur = handle;
		for (let l = 0; l < depth; l++) cur = word(cur, idx[l]);
		const dataPage = word(cur, idx[depth]);
		const from = dataPage * pageSize;
		const n = Math.min(pageSize, length - p * pageSize, Math.max(0, buf.length - from));
		buf.copy(out, p * pageSize, from, from + n);
	}
	return out;
}

// Returns {pageSize, entries: [{path, directory, data}]} with '/' separated paths, folders before their contents.
function readHpfs(buf) {
	if (buf.length < 0x20 || buf.toString('latin1', 0, 4) !== MAGIC) throw new Error('not an HPFS file');
	const pageSize = buf.readUInt32LE(4);
	if (pageSize < 64 || pageSize > 1 << 20 || (pageSize & (pageSize - 1)) !== 0) throw new Error('bad page size');
	const entries = [];
	const visited = new Set();
	const list = (handle, prefix, depth) => {
		if (depth > 12 || visited.has(handle)) throw new Error('folder loop');
		visited.add(handle);
		const dir = readStream(buf, pageSize, handle, 16 << 20);
		let at = 0;
		while (at + 8 <= dir.length) {
			const entryHandle = dir.readUInt32LE(at);
			const attributes = dir.readUInt16LE(at + 4);
			const nameLength = dir.readUInt16LE(at + 6);
			if (!nameLength && !entryHandle) break;
			if (at + 8 + nameLength > dir.length) throw new Error('bad folder entry');
			const name = dir.toString('latin1', at + 8, at + 8 + nameLength);
			at += 8 + nameLength;
			const path = prefix + name;
			if (attributes & 1) {
				entries.push({ path, directory: true, attributes });
				list(entryHandle, path + '/', depth + 1);
			} else {
				entries.push({ path, directory: false, attributes, data: readStream(buf, pageSize, entryHandle) });
			}
		}
	};
	list(buf.readUInt32LE(ROOT_AT), '', 0);
	return { pageSize, entries };
}

// files: Map or array of [path, Buffer] with '/' separated paths. Folders are created from the paths. The root folder is
// the first stream (page 1), as in the game's packages.
// `dirs` lists folders to create even when no file is in them ("Conversations", "Maps/1/Sequences").
function writeHpfs(files, pageSize = 512, dirs = []) {
	const perPage = pageSize / 4;
	const root = { children: new Map() };
	for (const d of dirs) {
		let node = root;
		for (const part of d.split('/').filter(Boolean)) {
			const key = part.toLowerCase();
			if (!node.children.has(key)) node.children.set(key, { name: part, children: new Map() });
			node = node.children.get(key);
			if (!node.children) throw new Error(`${d}: a file is in the way`);
		}
	}
	// Names are matched without case, as the game looks them up: "Maps" and "maps" are one folder.
	for (const [path, data] of files instanceof Map ? files : new Map(files)) {
		const parts = path.split('/');
		let node = root;
		for (let i = 0; i < parts.length - 1; i++) {
			const key = parts[i].toLowerCase();
			if (!node.children.has(key)) node.children.set(key, { name: parts[i], children: new Map() });
			node = node.children.get(key);
			if (!node.children) throw new Error(`${path}: a file is in the way`);
		}
		const key = parts[parts.length - 1].toLowerCase();
		if (node.children.has(key)) throw new Error(`${path}: twice`);
		node.children.set(key, { name: parts[parts.length - 1], data: Buffer.from(data) });
	}
	const pages = [Buffer.alloc(pageSize)];
	const newPage = () => {
		pages.push(Buffer.alloc(pageSize));
		return pages.length - 1;
	};
	const fillStream = (rootPage, data) => {
		let level = [];
		for (let at = 0; at < data.length; at += pageSize) {
			const page = newPage();
			data.copy(pages[page], 0, at, Math.min(at + pageSize, data.length));
			level.push(page);
		}
		let depth = 0;
		while (level.length > perPage - 2) {
			const next = [];
			for (let at = 0; at < level.length; at += perPage) {
				const page = newPage();
				for (let k = 0; k < perPage && at + k < level.length; k++) pages[page].writeUInt32LE(level[at + k], k * 4);
				next.push(page);
			}
			level = next;
			depth++;
			if (depth > MAX_DEPTH) throw new Error('stream too long');
		}
		pages[rootPage].writeUInt32LE(data.length, 0);
		pages[rootPage].writeUInt16LE(depth, 4);
		level.forEach((page, k) => pages[rootPage].writeUInt32LE(page, 8 + k * 4));
	};
	const addNode = (node) => {
		const rootPage = newPage();
		if (!node.children) {
			fillStream(rootPage, node.data);
			return rootPage;
		}
		const parts = [];
		// Sorted without case, as the game's own packages are.
		const keys = [...node.children.keys()].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
		for (const key of keys) {
			const child = node.children.get(key);
			const nameBytes = Buffer.from(child.name, 'latin1');
			if (nameBytes.toString('latin1') !== child.name) throw new Error(`${child.name}: the name is not Latin-1`);
			const head = Buffer.alloc(8);
			head.writeUInt32LE(addNode(child), 0);
			head.writeUInt16LE(child.children ? 1 : 0, 4);
			head.writeUInt16LE(nameBytes.length, 6);
			parts.push(head, nameBytes);
		}
		fillStream(rootPage, Buffer.concat(parts));
		return rootPage;
	};
	const rootHandle = addNode(root);
	const header = pages[0];
	header.write(MAGIC, 0, 'latin1');
	header.writeUInt32LE(pageSize, 4);
	header.writeUInt32LE(pages.length, 8);
	header.writeUInt32LE(0xffffffff, 0x0c);
	header.writeUInt32LE(8, 0x18);
	header.writeUInt32LE(rootHandle, ROOT_AT);
	return Buffer.concat(pages);
}

module.exports = { readHpfs, writeHpfs };
