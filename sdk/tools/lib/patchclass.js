// What the patches of the root of a rules package change of the game's own files (see "The root of a rules package adds, it does not replace" in sdk/rulesets.md).
//
// The root of a rules package may ADD to the game (an XML <add>, an attribute the node lacks, a new INI section, key or +Line, script code with @top, @bottom,
// @before or @after) but may not CHANGE what the game already has: an XML <replace>, <remove>, or <set> of an attribute the game's node has with another value
// (or an unset of it), an INI key the game's file has with another value (or -Key of it), an @replace of a script line. The game applies the changes only when a
// ruleset of that package is chosen and skips them otherwise. This is the same classification as the DLL's (src/mods/patchxml.cpp, PatchPolicy): what a patch added
// counts as added, so an operation on it applies. Operations are counted one per XML operation, INI line (a -Key and the Key = value after it count once) and
// script directive. The patches of the interface (data/interface/...) are exempt.
'use strict';

function parseXml(text) {
	const root = { name: '#doc', attrs: [], kids: [], parent: null, game: true };
	let cur = root;
	const re = /<!--[\s\S]*?-->|<!\[CDATA\[[\s\S]*?\]\]>|<\?[\s\S]*?\?>|<!DOCTYPE[^>]*>|<\/([^\s>]+)\s*>|<([^\s/>!?]+)((?:\s+[^\s=>/]+\s*=\s*(?:"[^"]*"|'[^']*'))*)\s*(\/?)>/g;
	let m;
	while ((m = re.exec(text))) {
		if (m[1] !== undefined) { if (cur.parent) cur = cur.parent; continue; }
		if (m[2] === undefined) continue;
		const node = { name: m[2], attrs: [], kids: [], parent: cur, game: true };
		const ar = /([^\s=]+)\s*=\s*(?:"([^"]*)"|'([^']*)')/g;
		let a;
		while ((a = ar.exec(m[3]))) node.attrs.push({ name: a[1], value: a[2] !== undefined ? a[2] : a[3] });
		cur.kids.push(node);
		if (!m[4]) cur = node;
	}
	return root;
}

// The selector of the patch system: /a/b[@x='v'][2], //a, [@x]
function select(doc, selector) {
	const steps = [];
	const re = /(\/\/|\/)([^/[]+)((?:\[[^\]]*\])*)/g;
	let m;
	while ((m = re.exec(selector.trim()))) {
		const preds = [];
		const pr = /\[([^\]]*)\]/g;
		let p;
		while ((p = pr.exec(m[3]))) {
			const t = p[1].trim();
			let q;
			if ((q = /^@([^=\s]+)\s*=\s*(?:'([^']*)'|"([^"]*)")$/.exec(t))) preds.push({ attr: q[1].toLowerCase(), value: (q[2] !== undefined ? q[2] : q[3]).toLowerCase() });
			else if ((q = /^@([^=\s]+)$/.exec(t))) preds.push({ attr: q[1].toLowerCase() });
			else if (/^\d+$/.test(t)) preds.push({ index: +t });
			else return null;
		}
		steps.push({ deep: m[1] === '//', name: m[2].trim().toLowerCase(), preds });
	}
	if (!steps.length) return null;
	const all = (n, out) => { for (const k of n.kids) { out.push(k); all(k, out); } return out; };
	let ctx = [doc];
	for (const s of steps) {
		const next = [];
		for (const c of ctx) {
			let hit = (s.deep ? all(c, []) : c.kids).filter(n => s.name === '*' || n.name.toLowerCase() === s.name);
			for (const p of s.preds) {
				if (p.index) { const base = hit; hit = hit.filter(n => base.indexOf(n) + 1 === p.index); }
				else if (p.value !== undefined) hit = hit.filter(n => n.attrs.some(a => a.name.toLowerCase() === p.attr && a.value.toLowerCase() === p.value));
				else hit = hit.filter(n => n.attrs.some(a => a.name.toLowerCase() === p.attr));
			}
			next.push(...hit);
		}
		ctx = next;
	}
	return ctx;
}

// A copy of a node of the patch's payload: what a patch adds is not the game's.
function cloneAdded(n, parent) {
	const c = { name: n.name, attrs: n.attrs.map(a => ({ name: a.name, value: a.value })), kids: [], parent, game: false };
	c.kids = n.kids.map(k => cloneAdded(k, c));
	return c;
}

const brief = op => `${op.name} ${(op.attrs.find(a => a.name.toLowerCase() === 'select') || { value: '' }).value.trim()}`.trim();

function xmlSkipped(base, patch) {
	const out = [];
	const doc = parseXml(base);
	const pdoc = parseXml(patch);
	const root = pdoc.kids.find(k => k.name.toLowerCase() === 'patch');
	if (!root) return out;
	const hasAncestorIn = (n, set) => { for (let a = n.parent; a; a = a.parent) if (set.has(a)) return true; return false; };
	for (const op of root.kids) {
		const kind = op.name.toLowerCase();
		if (!['add', 'set', 'remove', 'replace'].includes(kind)) continue;
		const sel = (op.attrs.find(a => a.name.toLowerCase() === 'select') || {}).value;
		if (!sel) continue;
		let targets = select(doc, sel);
		if (!targets || !targets.length) continue;
		let skipped = false;
		if (kind === 'add') {
			const payload = op.kids;
			const pos = ((op.attrs.find(a => a.name.toLowerCase() === 'position') || {}).value || 'end').toLowerCase();
			for (const t of targets) {
				const fresh = payload.map(k => cloneAdded(k, null));
				if (pos === 'before' || pos === 'after') { const par = t.parent; if (!par) continue; fresh.forEach(f => { f.parent = par; }); par.kids.splice(par.kids.indexOf(t) + (pos === 'after' ? 1 : 0), 0, ...fresh); }
				else { fresh.forEach(f => { f.parent = t; }); if (pos === 'start') t.kids.unshift(...fresh); else t.kids.push(...fresh); }
			}
		}
		else if (kind === 'set') {
			const unset = ((op.attrs.find(a => a.name.toLowerCase() === 'unset') || {}).value || '').split(/\s+/).filter(Boolean);
			for (const t of targets) {
				if (t === doc) continue;
				for (const pa of op.attrs) {
					const n = pa.name.toLowerCase();
					if (n === 'select' || n === 'optional' || n === 'unset') continue;
					const have = t.attrs.find(a => a.name.toLowerCase() === n);
					if (have) { if (t.game) { if (have.value !== pa.value) skipped = true; } else have.value = pa.value; }
					else t.attrs.push({ name: pa.name, value: pa.value });
				}
				for (const u of unset) {
					const at = t.attrs.findIndex(a => a.name.toLowerCase() === u.toLowerCase());
					if (at < 0) continue;
					if (t.game) skipped = true; else t.attrs.splice(at, 1);
				}
			}
		}
		else {
			const chosen = new Set(targets);
			targets = targets.filter(t => !hasAncestorIn(t, chosen));
			const mine = targets.filter(t => !t.game);
			skipped = mine.length !== targets.length;
			for (const t of mine) {
				const par = t.parent;
				if (!par) continue;
				const at = par.kids.indexOf(t);
				if (kind === 'remove') par.kids.splice(at, 1);
				else {
					const fresh = op.kids.map(k => cloneAdded(k, par));
					par.kids.splice(at, 1, ...fresh);
				}
			}
		}
		if (skipped) out.push(brief(op));
	}
	return out;
}

const KEY = (section, key) => `[${section.toLowerCase()}]${key.toLowerCase()}`;
const sectionOf = line => { const m = /^\[(.*)\]$/.exec(line); return m ? m[1].trim() : null; };
const isComment = t => !t || t[0] === ';' || t[0] === '#';
const stripComment = v => { const i = v.indexOf(';'); return (i < 0 ? v : v.slice(0, i)).trim(); };

// The keys of the game's INI file: "[section]key" -> value ("" for a line without =)
function iniKeys(text) {
	const keys = new Map();
	let section = '';
	for (const raw of text.replace(/^\uFEFF/, '').split(/\r?\n/)) {
		const t = raw.trim();
		if (isComment(t)) continue;
		const s = sectionOf(t);
		if (s !== null) { section = s; continue; }
		const eq = t.indexOf('=');
		keys.set(KEY(section, eq < 0 ? t : t.slice(0, eq).trim()), eq < 0 ? '' : stripComment(t.slice(eq + 1).trim()));
	}
	return keys;
}

function iniSkipped(base, patch) {
	const out = [];
	const game = iniKeys(base);
	const deleted = new Set();
	let section = null;
	for (const raw of patch.replace(/^\uFEFF/, '').split(/\r?\n/)) {
		const t = raw.trim();
		if (isComment(t)) continue;
		const s = sectionOf(t);
		if (s !== null) { section = s; continue; }
		if (section === null) continue;
		if (t[0] === '+') continue;
		if (t[0] === '-') {
			const key = t.slice(1).trim();
			if (game.has(KEY(section, key))) { deleted.add(KEY(section, key)); out.push(`[${section}] -${key}`); }
			continue;
		}
		const eq = t.indexOf('=');
		if (eq < 0) continue;
		const key = t.slice(0, eq).trim(), value = stripComment(t.slice(eq + 1).trim());
		const k = KEY(section, key);
		if (game.has(k) && game.get(k) !== value && !deleted.has(k)) out.push(`[${section}] ${key}`);
	}
	return out;
}

function vsSkipped(patch) {
	const out = [];
	for (const line of patch.split(/\r?\n/)) {
		const m = /^@replace\b\s*(.*)$/i.exec(line);
		if (m) out.push(`@replace ${m[1].trim()}`);
	}
	return out;
}

// The operations of `patch` (for the game file `file`, a path like data/commands/rbarracks.xml) that change what `base`, the game's own file, has.
function skippedOps(file, base, patch) {
	const f = file.toLowerCase();
	if (/^data\/interface\//.test(f)) return [];
	if (f.endsWith('.xml')) return xmlSkipped(base, patch);
	if (f.endsWith('.ini')) return iniSkipped(base, patch);
	if (f.endsWith('.vs')) return vsSkipped(patch);
	return [];
}

module.exports = { skippedOps, iniKeys };
