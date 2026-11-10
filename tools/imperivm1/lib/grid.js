// The map grids (Terrain.*.grid): a 20-byte header ("DIRG", cell size in world units, bits per cell, width and height in
// world units) and the cells, row by row. With 4 bits per cell the low nibble is the first cell of the byte.
//
// Imperivm I stores the terrain layer of each cell in 4 bits; Great Battles of Rome uses 8 bits and a longer list of
// layers, so the terrain grid is widened and its layer numbers remapped. The decor grid has 16 bits per cell: the low byte
// is the decor type (the `type =` of decors.ini) and the high byte its variant.
'use strict';

const MAGIC = 'DIRG';
const HEADER = 20;

function readGrid(buf) {
	if (buf.length < HEADER || buf.toString('latin1', 0, 4) !== MAGIC) throw new Error('not a grid');
	const g = { cell: buf.readUInt32LE(4), bits: buf.readUInt32LE(8), width: buf.readUInt32LE(12), height: buf.readUInt32LE(16) };
	if (!g.cell || ![1, 4, 8, 16].includes(g.bits)) throw new Error(`grid with ${g.bits} bits per cell`);
	g.cols = Math.ceil(g.width / g.cell);
	g.rows = Math.ceil(g.height / g.cell);
	g.count = g.cols * g.rows;
	g.dataSize = Math.ceil((g.count * g.bits) / 8);
	if (buf.length !== HEADER + g.dataSize) throw new Error(`grid of ${buf.length} bytes, expected ${HEADER + g.dataSize}`);
	g.data = buf.subarray(HEADER);
	return g;
}

function header(cell, bits, width, height) {
	const h = Buffer.alloc(HEADER);
	h.write(MAGIC, 0, 'latin1');
	h.writeUInt32LE(cell, 4);
	h.writeUInt32LE(bits, 8);
	h.writeUInt32LE(width, 12);
	h.writeUInt32LE(height, 16);
	return h;
}

function cellsOf(g) {
	const out = new Uint16Array(g.count);
	for (let i = 0; i < g.count; i++) {
		if (g.bits === 4) out[i] = (g.data[i >> 1] >> ((i & 1) * 4)) & 15;
		else if (g.bits === 8) out[i] = g.data[i];
		else if (g.bits === 16) out[i] = g.data.readUInt16LE(i * 2);
		else out[i] = (g.data[i >> 3] >> (i & 7)) & 1;
	}
	return out;
}

// 4-bit terrain grid -> 8-bit, with `remap[layer]` giving the new layer. Returns {buf, used: Map(layer -> cells)}.
function widenTerrain(buf, remap) {
	const g = readGrid(buf);
	if (g.bits !== 4 && g.bits !== 8) throw new Error(`terrain grid with ${g.bits} bits`);
	const cells = cellsOf(g);
	const out = Buffer.alloc(g.count);
	const used = new Map();
	for (let i = 0; i < g.count; i++) {
		const v = cells[i];
		const to = remap[v];
		if (to === undefined || to < 0 || to > 255) throw new Error(`terrain layer ${v} has no equivalent`);
		out[i] = to;
		used.set(v, (used.get(v) || 0) + 1);
	}
	return { buf: Buffer.concat([header(g.cell, 8, g.width, g.height), out]), used };
}

// Decor grid: the low byte (decor type) goes through `remap` when it has an entry. Returns {buf, changed}.
function remapDecor(buf, remap) {
	const g = readGrid(buf);
	if (g.bits !== 16) throw new Error(`decor grid with ${g.bits} bits`);
	const out = Buffer.from(buf);
	let changed = 0;
	for (let i = 0; i < g.count; i++) {
		const at = HEADER + i * 2;
		const v = out.readUInt16LE(at);
		const type = v & 255;
		if (!type || remap[type] === undefined || remap[type] === type) continue;
		out.writeUInt16LE((v & 0xff00) | remap[type], at);
		changed++;
	}
	return { buf: out, changed };
}

module.exports = { readGrid, cellsOf, widenTerrain, remapDecor, header };
