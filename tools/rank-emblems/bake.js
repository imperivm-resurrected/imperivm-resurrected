// Bakes the rank emblems of make.js into assets/league/<rank>-256.png and <rank>-64.png, the pictures winmm.dll carries
// (src/app/winmm.rc) and scales to the size each screen shows them at.
// Needs sharp, which is not a dependency of the repository: install it anywhere and point to it.
//   node tools/rank-emblems/bake.js [path to sharp's folder]
//   SHARP=C:\somewhere\node_modules\sharp node tools/rank-emblems/bake.js
'use strict';
const fs = require('fs');
const path = require('path');
const { RANKS, emblem } = require('./make');

function loadSharp() {
	const where = process.argv[2] || process.env.SHARP;
	try {
		return require(where ? path.resolve(where) : 'sharp');
	} catch (e) {
		console.error('sharp was not found: install it in a folder outside the repository (npm install sharp there) and pass its folder,');
		console.error('  node tools/rank-emblems/bake.js <folder>\\node_modules\\sharp');
		process.exit(1);
	}
}

(async () => {
	const sharp = loadSharp();
	const out = path.join(__dirname, '..', '..', 'assets', 'league');
	fs.mkdirSync(out, { recursive: true });
	for (const r of RANKS) {
		const svg = Buffer.from(emblem(r));
		for (const size of [256, 64]) {
			// The SVG is 128 units; librsvg renders it at density / 72 of that, then sharp scales it to the size.
			const density = Math.max(72, (72 * size) / 128);
			await sharp(svg, { density }).resize(size, size).png().toFile(path.join(out, `${r.id}-${size}.png`));
		}
	}
	console.log('baked', RANKS.length * 2, 'PNGs to', path.relative(process.cwd(), out));
})();
