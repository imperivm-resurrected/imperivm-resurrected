// The game's XML files: a parser for checking them and reading attributes, and helpers that change attributes in the text
// itself, so that a converted file keeps the layout of the original.
//
// The game writes attribute values with its own escapes (\n, \', \l for <, \g for >, \a for &) inside double quotes, and
// the translation tables with the usual XML entities. Both are read as they are: values come back undecoded.
'use strict';

// Returns {root: {name, attrs: Map, children, line}, roots: [...]} or throws an Error with the line of the problem.
function parseXml(text) {
	let i = 0;
	let line = 1;
	const roots = [];
	const stack = [];
	const fail = (msg) => {
		throw new Error(`line ${line}: ${msg}`);
	};
	const advance = (to) => {
		for (let k = i; k < to; k++) if (text.charCodeAt(k) === 10) line++;
		i = to;
	};
	if (text.charCodeAt(0) === 0xfeff) i = 1;
	while (i < text.length) {
		const lt = text.indexOf('<', i);
		if (lt < 0) {
			if (text.slice(i).trim()) fail('text after the last element');
			advance(text.length);
			break;
		}
		if (text.slice(i, lt).trim() && !stack.length) fail('text outside an element');
		advance(lt);
		if (text.startsWith('<!--', i)) {
			const end = text.indexOf('-->', i + 4);
			if (end < 0) fail('comment not closed');
			advance(end + 3);
			continue;
		}
		if (text.startsWith('<?', i)) {
			const end = text.indexOf('?>', i + 2);
			if (end < 0) fail('declaration not closed');
			advance(end + 2);
			continue;
		}
		if (text.startsWith('</', i)) {
			const m = /^<\/([A-Za-z_][\w.:-]*)\s*>/.exec(text.slice(i, i + 200));
			if (!m) fail('bad closing tag');
			const open = stack.pop();
			if (!open || open.name !== m[1]) fail(`</${m[1]}> closes <${open ? open.name : 'nothing'}>`);
			advance(i + m[0].length);
			continue;
		}
		const nm = /^<([A-Za-z_][\w.:-]*)/.exec(text.slice(i, i + 200));
		if (!nm) fail('bad tag');
		const el = { name: nm[1], attrs: new Map(), children: [], line };
		advance(i + nm[0].length);
		for (;;) {
			const ws = /^\s*/.exec(text.slice(i, i + 4096))[0];
			advance(i + ws.length);
			if (text.startsWith('/>', i)) {
				advance(i + 2);
				(stack.length ? stack[stack.length - 1].children : roots).push(el);
				break;
			}
			if (text[i] === '>') {
				advance(i + 1);
				(stack.length ? stack[stack.length - 1].children : roots).push(el);
				stack.push(el);
				break;
			}
			const an = /^([A-Za-z_][\w.:-]*)\s*=\s*/.exec(text.slice(i, i + 200));
			if (!an || !ws) fail(`bad attribute in <${el.name}>`);
			advance(i + an[0].length);
			const q = text[i];
			if (q !== '"' && q !== "'") fail(`attribute ${an[1]} without quotes`);
			const end = text.indexOf(q, i + 1);
			if (end < 0) fail(`attribute ${an[1]} not closed`);
			const value = text.slice(i + 1, end);
			if (value.includes('<')) fail(`'<' in attribute ${an[1]}`);
			if (el.attrs.has(an[1])) fail(`attribute ${an[1]} twice`);
			el.attrs.set(an[1], value);
			advance(end + 1);
		}
	}
	if (stack.length) fail(`<${stack[stack.length - 1].name}> not closed`);
	if (!roots.length) fail('no element');
	return { root: roots[0], roots };
}

function* walk(el) {
	yield el;
	for (const c of el.children) yield* walk(c);
}

// Escapes plain text for an attribute of the game's own files (the \-escapes the game writes). Double quotes become \',
// which the script language reads as the same string delimiter.
function gameEscape(s) {
	return s.replace(/\\/g, '\\\\').replace(/\r?\n/g, '\\n').replace(/["']/g, "\\'").replace(/</g, '\\l').replace(/>/g, '\\g').replace(/&/g, '\\a');
}

function gameUnescape(s) {
	const map = { n: '\n', "'": "'", '"': '"', l: '<', g: '>', a: '&', '\\': '\\' };
	return s.replace(/\\(.)/g, (all, c) => (Object.prototype.hasOwnProperty.call(map, c) ? map[c] : all));
}

// Escapes plain text for a translation table (standard XML entities).
function entityEscape(s) {
	return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// Replaces the value of attribute `name` in every element where `test(value)` is true, through `fn(value, elementStart)`.
function mapAttribute(text, name, fn) {
	const re = new RegExp('(\\s' + name.replace(/\./g, '\\.') + '=")([^"]*)(")', 'g');
	let count = 0;
	const out = text.replace(re, (all, a, value, b, offset) => {
		const next = fn(value, offset);
		if (next === undefined || next === value) return all;
		count++;
		return a + next + b;
	});
	return { text: out, count };
}

module.exports = { parseXml, walk, gameEscape, gameUnescape, entityEscape, mapAttribute };
