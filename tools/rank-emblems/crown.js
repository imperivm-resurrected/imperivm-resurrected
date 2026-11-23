// The Imperator's marks: the laurel crown of a triumphant general over the shield, with a red gem at its front and its ribbons, and
// rays of light behind. Loaded by make.js.
const ink = '#6b4c10';

// The wreath seen from the front: two branches round a flattened ring, meeting at the gem; each leaf points along the branch.
function crown() {
	const cx = 64, cy = 15, rx = 30, ry = 8;
	let back = '', front = '';
	// Leaves in pairs along the ring, each one along the branch and leaning out of it, one to each side; from the gem at the front
	// (bottom) round to the back (top), where the two branches meet.
	for (let i = 0; i < 9; i++) {
		for (const side of [-1, 1]) {
			const t = (i + 0.5) / 9, a = Math.PI / 2 + side * t * Math.PI;
			const x = cx + Math.cos(a) * rx, y = cy + Math.sin(a) * ry;
			const tangent = (Math.atan2(Math.cos(a) * ry, -Math.sin(a) * rx) * 180) / Math.PI;
			const behind = t > 0.5;
			for (const lean of [-32, 32]) {
				const leaf = `<ellipse cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" rx="2.6" ry="6.5" transform="rotate(${(tangent - 90 + lean).toFixed(0)} ${x.toFixed(1)} ${y.toFixed(1)}) translate(0 -3)" fill="${behind ? '#b98d24' : 'url(#gold)'}" stroke="${ink}" stroke-width=".8"/>`;
				if (behind) back += leaf;
				else front += leaf;
			}
		}
	}
	return {
		back: `<g>${back}</g>`,
		front: `<g>
			${front}
			<circle cx="64" cy="${cy + ry}" r="5" fill="url(#red)" stroke="url(#gold)" stroke-width="2.2"/>
			<circle cx="62.6" cy="${cy + ry - 1.5}" r="1.5" fill="#ffffff" opacity=".6"/>
		</g>`,
	};
}

// Rays of light behind the shield, longer and shorter in turn.
function rays() {
	let out = '';
	for (let i = 0; i < 24; i++) {
		const a = (i / 24) * Math.PI * 2, long = i % 2 === 0, r = long ? 63 : 52, w = long ? 0.07 : 0.05;
		const p = (ang, rr) => `${(64 + Math.cos(ang) * rr).toFixed(1)} ${(64 + Math.sin(ang) * rr).toFixed(1)}`;
		out += `<path d="M${p(a - w, 20)} L${p(a, r)} L${p(a + w, 20)} Z" fill="#ffd45a" opacity="${long ? 0.5 : 0.3}"/>`;
	}
	return `<g>${out}</g>`;
}

// The radiate crown of the emperors on their coins: a gold band with sharp rays rising from it and a red gem at the front. Clean
// shapes, so it reads the same at 20 pixels as at 160.
function radiate() {
	let spikes = '';
	const n = 7, x0 = 36, x1 = 92, base = 24;
	for (let i = 0; i < n; i++) {
		const x = x0 + ((x1 - x0) * (i + 0.5)) / n, mid = Math.abs(i - (n - 1) / 2), h = 20 - mid * 3.2, w = 3.6;
		spikes += `<path d="M${(x - w).toFixed(1)} ${base} L${x.toFixed(1)} ${(base - h).toFixed(1)} L${(x + w).toFixed(1)} ${base} Z" fill="url(#gold)" stroke="${ink}" stroke-width="1" stroke-linejoin="round"/>`;
	}
	return `<g>
		${spikes}
		<path d="M34 23 Q64 17 94 23 L94 30 Q64 24 34 30 Z" fill="url(#gold)" stroke="${ink}" stroke-width="1.2" stroke-linejoin="round"/>
		<path d="M38 26.5 Q64 21 90 26.5" fill="none" stroke="#8a6716" stroke-width=".9" stroke-dasharray="2 2.5"/>
		<circle cx="64" cy="25.5" r="4.6" fill="url(#red)" stroke="${ink}" stroke-width="1"/>
		<circle cx="62.8" cy="24.3" r="1.3" fill="#ffffff" opacity=".65"/>
	</g>`;
}

module.exports = { crown, rays, radiate };
