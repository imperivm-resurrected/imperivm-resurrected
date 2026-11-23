// The league's rank emblems as SVG: one shield (an imperial scutum seen from the front) for every rank, its material climbing with the
// rank, and one big piece outside the shield that tells the rank apart at any size. 128 units square, transparent around the art.
// Writes build/rank-emblems/svg/<rank>.svg and build/rank-emblems/preview.html (outside git) to look at them; bake.js turns them into
// the PNGs of assets/league that winmm.dll carries.
// Run: node tools/rank-emblems/make.js
const fs = require('fs');
const path = require('path');

// Colours per rank: body (the painted leather), its dark edge, rim metal (light, dark), boss metal (light, dark), accent.
const RANKS = [
	{ id: 'recruit', name: 'Recruit', body: ['#8a6a4c', '#4a3626'], rim: ['#9c958c', '#4b4640'], boss: ['#a8a097', '#4e4842'], extra: 'none' },
	{ id: 'legionary', name: 'Legionary', body: ['#7d8a94', '#3a434a'], rim: ['#d9e0e5', '#5d676e'], boss: ['#eef2f5', '#6c767d'], extra: 'swords' },
	{ id: 'signifer', name: 'Signifer', body: ['#3f8f80', '#1c4a42'], rim: ['#e0b46a', '#7a5524'], boss: ['#f0cd85', '#8a6127'], extra: 'signum' },
	{ id: 'centurion', name: 'Centurion', body: ['#c0742e', '#6a3a14'], rim: ['#f0c27a', '#80501c'], boss: ['#f7d494', '#8d5a22'], extra: 'crest' },
	{ id: 'tribune', name: 'Tribune', body: ['#b9c3cc', '#5e6870'], rim: ['#f4f7f9', '#7c868e'], boss: ['#ffffff', '#8b959c'], extra: 'tribune' },
	{ id: 'legate', name: 'Legate', body: ['#d9a632', '#7a5612'], rim: ['#fff0b8', '#9a7420'], boss: ['#fff6cf', '#a5801f'], extra: 'legate' },
	{ id: 'praetorian', name: 'Praetorian', body: ['#9b2f6c', '#4a0f31'], rim: ['#f4d77a', '#9a7420'], boss: ['#fbe7a0', '#a5801f'], extra: 'praetorian' },
	{ id: 'imperator', name: 'Imperator', body: ['#d11f26', '#5c070b'], rim: ['#ffe38a', '#a47a12'], boss: ['#fff1b8', '#b08417'], extra: 'imperator' },
];

const GOLD = ['#ffe9a0', '#b88a1c'];
const RED = ['#ef3a3a', '#7d0b0e'];

// The shield: straight sides, top and bottom bowed as a curved board seen from the front, rounded corners.
const SHIELD = 'M42 27 Q64 21 86 27 Q92 28.5 92 35 L92 97 Q92 103.5 86 105 Q64 111 42 105 Q36 103.5 36 97 L36 35 Q36 28.5 42 27 Z';

function defs(r) {
	return `<defs>
	<linearGradient id="body" x1="0" x2="1" y1="0" y2="0">
		<stop offset="0" stop-color="${r.body[1]}"/><stop offset=".32" stop-color="${r.body[0]}"/><stop offset=".55" stop-color="${r.body[0]}"/><stop offset="1" stop-color="${r.body[1]}"/>
	</linearGradient>
	<linearGradient id="rim" x1="0" x2="0" y1="0" y2="1">
		<stop offset="0" stop-color="${r.rim[0]}"/><stop offset=".5" stop-color="${r.rim[1]}"/><stop offset="1" stop-color="${r.rim[0]}"/>
	</linearGradient>
	<radialGradient id="boss" cx=".38" cy=".34" r=".75">
		<stop offset="0" stop-color="#ffffff"/><stop offset=".18" stop-color="${r.boss[0]}"/><stop offset="1" stop-color="${r.boss[1]}"/>
	</radialGradient>
	<linearGradient id="spina" x1="0" x2="1" y1="0" y2="0">
		<stop offset="0" stop-color="${r.rim[1]}"/><stop offset=".45" stop-color="${r.rim[0]}"/><stop offset="1" stop-color="${r.rim[1]}"/>
	</linearGradient>
	<linearGradient id="gold" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="${GOLD[0]}"/><stop offset="1" stop-color="${GOLD[1]}"/></linearGradient>
	<linearGradient id="red" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="${RED[0]}"/><stop offset="1" stop-color="${RED[1]}"/></linearGradient>
	<linearGradient id="steel" x1="0" x2="1" y1="0" y2="0"><stop offset="0" stop-color="#8b959c"/><stop offset=".5" stop-color="#f4f7f9"/><stop offset="1" stop-color="#8b959c"/></linearGradient>
	<radialGradient id="glow" cx=".5" cy=".5" r=".5"><stop offset="0" stop-color="#ffd45a" stop-opacity=".55"/><stop offset="1" stop-color="#ffd45a" stop-opacity="0"/></radialGradient>
	<clipPath id="inside"><path d="${SHIELD}"/></clipPath>
</defs>`;
}

// What every shield has: the body, a sheen, the spine, the boss and the rim.
function shield(r, paint = '') {
	return `<g>
	<path d="${SHIELD}" fill="url(#body)"/>
	<g clip-path="url(#inside)">${paint}
		<rect x="61" y="20" width="6" height="92" fill="url(#spina)"/>
		<path d="M40 30 Q52 26 62 26 L62 108 Q50 108 40 104 Z" fill="#ffffff" opacity=".07"/>
	</g>
	<circle cx="64" cy="66" r="13" fill="url(#boss)" stroke="${r.rim[1]}" stroke-width="2"/>
	<circle cx="64" cy="66" r="17" fill="none" stroke="url(#rim)" stroke-width="2.5"/>
	<path d="${SHIELD}" fill="none" stroke="url(#rim)" stroke-width="5" stroke-linejoin="round"/>
	<path d="${SHIELD}" fill="none" stroke="#000" stroke-opacity=".35" stroke-width="1"/>
</g>`;
}

// Two gladii crossed behind the shield: the points out at the top corners, guard, grip and pommel out at the bottom ones.
function swords() {
	// Along the sword: from the point (18,8) to the pommel (111,118); `along` places a point at a share of it, `side` moves it across.
	const p0 = [18, 8], p1 = [111, 118];
	const len = Math.hypot(p1[0] - p0[0], p1[1] - p0[1]);
	const d = [(p1[0] - p0[0]) / len, (p1[1] - p0[1]) / len], n = [-d[1], d[0]];
	const at = (t, s = 0) => `${(p0[0] + d[0] * len * t + n[0] * s).toFixed(1)} ${(p0[1] + d[1] * len * t + n[1] * s).toFixed(1)}`;
	const guard = 0.8, grip = 0.93;
	const one = `
		<path d="M${at(0)} L${at(0.1, 4.2)} L${at(guard, 4.2)} L${at(guard, -4.2)} L${at(0.1, -4.2)} Z" fill="url(#steel)" stroke="#3a434a" stroke-width="1.1"/>
		<path d="M${at(0.06)} L${at(guard - 0.02)}" stroke="#5d676e" stroke-width="1"/>
		<path d="M${at(guard, 8)} L${at(guard, -8)}" stroke="url(#gold)" stroke-width="4.5" stroke-linecap="round"/>
		<path d="M${at(guard + 0.02)} L${at(grip)}" stroke="#5a3a1c" stroke-width="4.5" stroke-linecap="round"/>
		<circle cx="${at(0.97).split(' ')[0]}" cy="${at(0.97).split(' ')[1]}" r="4" fill="url(#gold)" stroke="#6b4c10" stroke-width="1"/>`;
	return `<g>${one}</g><g transform="translate(128 0) scale(-1 1)">${one}</g>`;
}

// The signum: a pole behind the shield carrying stacked discs above it, under a crossbar.
function signum() {
	// A spear point on top, a crossbar with a pendant at each end, and a disc over the shield's rim.
	return `<g>
		<rect x="62" y="8" width="4" height="34" fill="#6b4c22"/>
		<path d="M64 1 L69 9 L64 12 L59 9 Z" fill="url(#gold)" stroke="#6b4c10" stroke-width="1"/>
		<path d="M47 14 L81 14" stroke="url(#gold)" stroke-width="3.5" stroke-linecap="round"/>
		<path d="M49 14 L49 20 M79 14 L79 20" stroke="url(#gold)" stroke-width="2"/>
		<path d="M46 20 L52 20 L49 26 Z M76 20 L82 20 L79 26 Z" fill="url(#gold)" stroke="#6b4c10" stroke-width=".8"/>
		<circle cx="64" cy="20" r="6" fill="url(#gold)" stroke="#6b4c10" stroke-width="1.2"/>
	</g>`;
}

// The crest across the top: red horsehair in a fan, on a gold holder.
function crest(w = 34, h = 22, colors = ['url(#red)', '#b3151a', '#4a0507']) {
	const x0 = 64 - w, x1 = 64 + w, y = 25;
	// A fan of horsehair tufts in two reds, each one bulging a little past the last.
	const at = (a, k) => `${(64 - Math.cos(a) * w * k).toFixed(1)} ${(y - Math.sin(a) * h * k).toFixed(1)}`;
	let fan = '';
	for (let i = 0; i < 9; i++) {
		const a0 = Math.PI * (i / 9), a1 = Math.PI * ((i + 1) / 9);
		fan += `<path d="M64 ${y + 2} L${at(a0, 1)} Q${at((a0 + a1) / 2, 1.14)} ${at(a1, 1)} Z" fill="${i % 2 ? colors[1] : colors[0]}" stroke="${colors[2]}" stroke-width=".9"/>`;
	}
	return `<g>
		${fan}
		<path d="M${x0 + 4} ${y} L${x1 - 4} ${y}" stroke="url(#gold)" stroke-width="4" stroke-linecap="round"/>
	</g>`;
}

// Jupiter's thunderbolt painted on the shield: four zigzag bolts out of the boss.
function bolts() {
	// One bolt out of the boss towards the top right, reflected into the other three corners so the four match.
	const bolt = `<path d="M72 55 L74 46 L69 46 L78 32 L77 42 L82 42 L76 54 Z" fill="url(#gold)" stroke="#6b4c10" stroke-width="1" stroke-linejoin="round"/>`;
	return [
		bolt,
		`<g transform="translate(128 0) scale(-1 1)">${bolt}</g>`,
		`<g transform="translate(0 132) scale(1 -1)">${bolt}</g>`,
		`<g transform="translate(128 132) scale(-1 -1)">${bolt}</g>`,
	].join('');
}

// Wings beside the shield: three rows of feathers.
function wings(fill = 'url(#gold)', stroke = '#6b4c10', reach = 30) {
	const wing = (flip) => {
		let feathers = '';
		for (let i = 0; i < 4; i++) {
			const y = 40 + i * 11, len = reach - i * 5;
			feathers += `<path d="M38 ${y} Q${38 - len * 0.55} ${y - 10} ${38 - len} ${y - 6 + i * 2} Q${38 - len * 0.6} ${y + 2} 38 ${y + 9} Z" fill="${fill}" stroke="${stroke}" stroke-width="1"/>`;
		}
		return `<g transform="${flip ? 'translate(128 0) scale(-1 1)' : ''}">${feathers}</g>`;
	};
	return wing(false) + wing(true);
}

// Two columns beside the shield, the praetorian guard's.
function columns() {
	const col = (x) => `<g>
		<rect x="${x - 6}" y="26" width="12" height="4" rx="1" fill="url(#gold)" stroke="#6b4c10" stroke-width="1"/>
		<rect x="${x - 4}" y="30" width="8" height="72" fill="url(#gold)" stroke="#6b4c10" stroke-width="1"/>
		<path d="M${x - 1.5} 33 L${x - 1.5} 99 M${x + 1.5} 33 L${x + 1.5} 99" stroke="#8a6716" stroke-width="1"/>
		<rect x="${x - 6}" y="102" width="12" height="4" rx="1" fill="url(#gold)" stroke="#6b4c10" stroke-width="1"/>
	</g>`;
	return col(22) + col(106);
}

// A gold wreath painted round the boss.
function wreath() {
	let leaves = '';
	// Two branches round the boss, open at the top and the bottom where the spine runs; the right one is the left one's mirror.
	for (let i = 0; i < 6; i++) {
		const a = ((115 + i * 26) * Math.PI) / 180, x = 64 + Math.cos(a) * 24, y = 66 + Math.sin(a) * 24;
		leaves += `<ellipse cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" rx="3.4" ry="7.5" transform="rotate(${((a * 180) / Math.PI + 25).toFixed(0)} ${x.toFixed(1)} ${y.toFixed(1)})" fill="url(#gold)" stroke="#6b4c10" stroke-width=".8"/>`;
	}
	return `<g>${leaves}</g><g transform="translate(128 0) scale(-1 1)">${leaves}</g>`;
}

// The praetorians' scorpion, painted on the shield over the boss line.
function scorpion() {
	return `<g fill="url(#gold)" stroke="#6b4c10" stroke-width=".8">
		<ellipse cx="64" cy="92" rx="5" ry="7"/>
		<path d="M64 99 Q64 105 60 104 Q56 103 58 99" fill="none" stroke="url(#gold)" stroke-width="2.5"/>
		<path d="M60 88 L50 82 L48 77 M68 88 L78 82 L80 77 M59 93 L50 96 M69 93 L78 96 M60 97 L52 102 M68 97 L76 102" fill="none" stroke="url(#gold)" stroke-width="2"/>
	</g>`;
}

function laurel() {
	const branch = (flip) => {
		let leaves = '';
		for (let i = 0; i < 6; i++) {
			const t = i / 5, y = 100 - t * 62, x = 30 - Math.sin(t * Math.PI) * 6;
			leaves += `<ellipse cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" rx="3.2" ry="7" transform="rotate(${(-25 + t * 20).toFixed(0)} ${x.toFixed(1)} ${y.toFixed(1)})" fill="url(#gold)" stroke="#6b4c10" stroke-width=".8"/>`;
		}
		return `<g transform="${flip ? 'translate(128 0) scale(-1 1)' : ''}"><path d="M36 106 Q24 72 32 36" fill="none" stroke="#8a6716" stroke-width="2"/>${leaves}</g>`;
	};
	return branch(false) + branch(true);
}

function emblem(r) {
	let behind = '', paint = '', front = '';
	switch (r.extra) {
		case 'none': paint = '<path d="M36 47 L92 47 M36 68 L92 68 M36 89 L92 89" stroke="#3a2a1c" stroke-opacity=".45" stroke-width="1.2"/>'; break;
		case 'swords': behind = swords(); break;
		case 'signum': behind = signum(); break;
		case 'crest': behind = crest(); break;
		case 'tribune': behind = crest(30, 17); paint = bolts(); break;
		case 'legate': behind = wings() + crest(30, 17); paint = bolts(); break;
		case 'praetorian': behind = columns() + crest(26, 15); paint = wreath(); break;
		case 'imperator': behind = `<circle cx="64" cy="64" r="62" fill="url(#glow)"/>` + require('./crown').rays() + laurel() + crest(38, 23); paint = bolts(); break;
	}
	return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">
${defs(r)}
${behind}
${shield(r, paint)}
${front}
</svg>
`;
}

module.exports = { RANKS, emblem, crest, laurel, bolts, wings, swords, defs, shield };
if (require.main !== module) return;
const dir = path.join(__dirname, '..', '..', 'build', 'rank-emblems');
const out = path.join(dir, 'svg');
fs.mkdirSync(out, { recursive: true });
for (const r of RANKS) fs.writeFileSync(path.join(out, `${r.id}.svg`), emblem(r));

const sizes = [160, 96, 64, 40, 32, 24, 20];
const rows = RANKS.map(
	(r) => `<tr><th>${r.name}</th>${sizes.map((s) => `<td><img src="svg/${r.id}.svg" width="${s}" height="${s}" alt=""></td>`).join('')}</tr>`,
).join('\n');
fs.writeFileSync(
	path.join(dir, 'preview.html'),
	`<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Rank emblems</title>
<style>body{margin:0;padding:24px;background:#141110;color:#e8e2da;font:14px system-ui,sans-serif}table{border-collapse:collapse}
th{font-weight:600;text-align:left;padding:8px 16px 8px 0;color:#c9bfb4}td{padding:8px 14px;text-align:center;vertical-align:middle;border-left:1px solid #2a2422}
thead th{color:#8a8078;font-weight:400;text-align:center}</style></head><body>
<table><thead><tr><th></th>${sizes.map((s) => `<th>${s} px</th>`).join('')}</tr></thead><tbody>
${rows}
</tbody></table></body></html>
`,
);
console.log('written', RANKS.length, 'emblems to', path.relative(process.cwd(), dir));
