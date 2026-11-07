// Rewrites the game's scripts (.vs) from Imperivm I to Great Battles of Rome, and lists what they call.
//
// The scanner knows the script language only as far as the rewrites need it: comments, string literals, identifiers and
// the characters around them. Everything else passes through unchanged.
'use strict';

// Splits a script into tokens: {type: 'comment' | 'string' | 'ident' | 'space' | 'other', text}.
function tokenize(src) {
	const out = [];
	let i = 0;
	const push = (type, from) => out.push({ type, text: src.slice(from, i) });
	while (i < src.length) {
		const c = src[i];
		const from = i;
		if (c === '/' && src[i + 1] === '/') {
			while (i < src.length && src[i] !== '\n') i++;
			push('comment', from);
		} else if (c === '/' && src[i + 1] === '*') {
			const end = src.indexOf('*/', i + 2);
			i = end < 0 ? src.length : end + 2;
			push('comment', from);
		} else if (c === '"' || c === "'") {
			i++;
			while (i < src.length && src[i] !== c && src[i] !== '\n') i += src[i] === '\\' ? 2 : 1;
			i = Math.min(i + 1, src.length);
			push('string', from);
		} else if (/[A-Za-z_]/.test(c)) {
			while (i < src.length && /[A-Za-z0-9_]/.test(src[i])) i++;
			push('ident', from);
		} else if (/\s/.test(c)) {
			while (i < src.length && /\s/.test(src[i])) i++;
			push('space', from);
		} else {
			i++;
			push('other', from);
		}
	}
	return out;
}

const join = (tokens) => tokens.map((t) => t.text).join('');

// Index of the next token that is not space or comment, from `i` on.
function nextCode(tokens, i) {
	while (i < tokens.length && (tokens[i].type === 'space' || tokens[i].type === 'comment')) i++;
	return i;
}

function prevCode(tokens, i) {
	while (i >= 0 && (tokens[i].type === 'space' || tokens[i].type === 'comment')) i--;
	return i;
}

// Index of the ')' that closes the '(' at `open`, or -1.
function closingParen(tokens, open) {
	let depth = 0;
	for (let i = open; i < tokens.length; i++) {
		if (tokens[i].type !== 'other') continue;
		if (tokens[i].text === '(') depth++;
		else if (tokens[i].text === ')' && --depth === 0) return i;
	}
	return -1;
}

// Calls in the script: [{name, method: bool}] for every identifier followed by '(' or, for methods, after a '.'.
function listCalls(src) {
	const tokens = tokenize(src);
	const calls = [];
	for (let i = 0; i < tokens.length; i++) {
		if (tokens[i].type !== 'ident') continue;
		const p = prevCode(tokens, i - 1);
		const n = nextCode(tokens, i + 1);
		const method = p >= 0 && tokens[p].text === '.';
		const call = n < tokens.length && tokens[n].text === '(';
		if (method || call) calls.push({ name: tokens[i].text, method });
	}
	return calls;
}

// Renames free functions (not methods): {OldName: 'NewName'}.
function renameFunctions(src, renames) {
	const tokens = tokenize(src);
	let count = 0;
	for (let i = 0; i < tokens.length; i++) {
		const t = tokens[i];
		if (t.type !== 'ident' || !Object.prototype.hasOwnProperty.call(renames, t.text)) continue;
		const p = prevCode(tokens, i - 1);
		const n = nextCode(tokens, i + 1);
		if (p >= 0 && tokens[p].text === '.') continue;
		if (n >= tokens.length || tokens[n].text !== '(') continue;
		t.text = renames[t.text];
		count++;
	}
	return { src: join(tokens), count };
}

// Replaces string literals that are exactly a name in `map` ({"OldClass": "NewClass"}), keeping the quote style.
function replaceStringLiterals(src, map) {
	const tokens = tokenize(src);
	const hits = {};
	for (const t of tokens) {
		if (t.type !== 'string' || t.text.length < 2) continue;
		const q = t.text[0];
		const body = t.text.slice(1, -1);
		if (t.text[t.text.length - 1] !== q || !Object.prototype.hasOwnProperty.call(map, body)) continue;
		t.text = q + map[body] + q;
		hits[body] = (hits[body] || 0) + 1;
	}
	return { src: join(tokens), hits };
}

const REDUCE_VARS = 'ObjList i1rg_list;\nint i1rg_i, i1rg_n;\n';

// Imperivm I's ReduceGroup(group) erases part of a group that a script has just spawned: two thirds on easy, one third on
// normal and none on hard (its code in Imperivm.exe), skipping what is already dead. Great Battles of Rome does not have it,
// so each call becomes the same loop in script, with its variables declared after the signature line.
function expandReduceGroup(src) {
	const tokens = tokenize(src);
	let count = 0;
	for (let i = 0; i < tokens.length; i++) {
		if (tokens[i].type !== 'ident' || tokens[i].text !== 'ReduceGroup') continue;
		const p = prevCode(tokens, i - 1);
		if (p >= 0 && tokens[p].text === '.') continue;
		const open = nextCode(tokens, i + 1);
		if (open >= tokens.length || tokens[open].text !== '(') continue;
		const close = closingParen(tokens, open);
		if (close < 0) continue;
		const semi = nextCode(tokens, close + 1);
		if (semi >= tokens.length || tokens[semi].text !== ';') continue;
		const arg = join(tokens.slice(open + 1, close)).trim();
		const code =
			'{ i1rg_list = Group(' + arg + ').GetObjList(); ' +
			'i1rg_n = (i1rg_list.count * (2 - GetDifficulty())) / 3; ' +
			'for (i1rg_i = 0; i1rg_i < i1rg_list.count && i1rg_n > 0; i1rg_i += 1) { ' +
			'if (!i1rg_list[i1rg_i].IsDead()) { i1rg_list[i1rg_i].Erase(); i1rg_n = i1rg_n - 1; } } }';
		tokens.splice(i, semi - i + 1, { type: 'other', text: code });
		count++;
	}
	if (!count) return { src, count };
	return { src: insertDeclarations(join(tokens), REDUCE_VARS), count };
}

// Declarations go after the leading comment lines (the signature), before the script's own declarations.
function insertDeclarations(src, decl) {
	const eol = src.includes('\r\n') ? '\r\n' : '\n';
	const lines = src.split(/\r?\n/);
	let at = 0;
	while (at < lines.length && /^\s*\/\//.test(lines[at])) at++;
	lines.splice(at, 0, ...decl.trimEnd().split('\n'));
	return lines.join(eol);
}

// Index of the first token of the receiver of the method whose '.' is at `dot`: identifiers, dots and balanced (...) groups.
function receiverStart(tokens, dot) {
	let start = dot;
	let j = prevCode(tokens, dot - 1);
	while (j >= 0) {
		const t = tokens[j];
		if (t.type === 'ident') {
			start = j;
			const before = prevCode(tokens, j - 1);
			if (before >= 0 && tokens[before].text === '.') {
				j = prevCode(tokens, before - 1);
				continue;
			}
			break;
		}
		if (t.text === ')') {
			let depth = 0;
			let k = j;
			for (; k >= 0; k--) {
				if (tokens[k].text === ')') depth++;
				else if (tokens[k].text === '(' && --depth === 0) break;
			}
			if (k < 0) break;
			j = prevCode(tokens, k - 1);
			continue;
		}
		break;
	}
	return start;
}

// Finds `<receiver>.<method>(...)` calls: [{start, dot, open, close}] from the last to the first, so they can be replaced.
function methodCalls(tokens, method) {
	const out = [];
	for (let i = 0; i < tokens.length; i++) {
		if (tokens[i].type !== 'ident' || tokens[i].text !== method) continue;
		const dot = prevCode(tokens, i - 1);
		const open = nextCode(tokens, i + 1);
		if (dot < 0 || tokens[dot].text !== '.' || open >= tokens.length || tokens[open].text !== '(') continue;
		const close = closingParen(tokens, open);
		if (close < 0) continue;
		out.push({ start: receiverStart(tokens, dot), dot, open, close });
	}
	return out.reverse();
}

const ritualKey = (receiver) => '/I1Ritual/' + /^[A-Za-z_]\w*/.exec(receiver)[0];

// Stonehenge rituals of Imperivm I that Great Battles of Rome's Stonehenge cannot cast: {command: duration in ms}.
// Building.ExecCmd("<ritual>", ...) becomes a note of when the ritual would end, and Building.IsSpellCasted() a check of
// that time; for the other rituals IsSpellCasted() becomes a check of the building's command, since in Great Battles of
// Rome the ritual's method runs as the building's command until it ends (the game's own verifiers test it like this).
function rewriteRituals(src, timedRituals) {
	let tokens = tokenize(src);
	const timed = new Set();
	let rituals = 0;
	for (const c of methodCalls(tokens, 'ExecCmd')) {
		const first = nextCode(tokens, c.open + 1);
		if (tokens[first].type !== 'string') continue;
		const cmd = tokens[first].text.slice(1, -1);
		if (!Object.prototype.hasOwnProperty.call(timedRituals, cmd)) continue;
		const receiver = join(tokens.slice(c.start, c.dot)).trim();
		const key = ritualKey(receiver);
		timed.add(key);
		const original = join(tokens.slice(c.start, c.close + 1)).replace(/\*\//g, '* /');
		tokens.splice(c.start, c.close - c.start + 1, { type: 'other', text: `EnvWriteInt("${key}", GetTime() + ${timedRituals[cmd]}) /* was ${original} */` });
		rituals++;
	}
	tokens = tokenize(join(tokens));
	let checks = 0;
	for (const c of methodCalls(tokens, 'IsSpellCasted')) {
		if (c.close !== nextCode(tokens, c.open + 1)) continue;
		const receiver = join(tokens.slice(c.start, c.dot)).trim();
		const key = ritualKey(receiver);
		const text = timed.has(key) ? `(GetTime() < EnvReadInt("${key}"))` : `(${receiver}.command != "idle")`;
		tokens.splice(c.start, c.close - c.start + 1, { type: 'other', text });
		checks++;
	}
	return { src: join(tokens), rituals, checks };
}

// Druid commands of Imperivm I that Great Battles of Rome's Gaul druid does not have (there every druid could; here only
// the Briton druid hides and only priests cast mist). A druid told to do them would do nothing, and a script waiting for
// the result would wait forever, so each call becomes what the command ends up doing:
//   X.SetCommand("hide", target)  ->  target.SetVisible(false)
//   X.SetCommand("mist", point)   ->  Place("Mist", point, X.player).SetStamina(10)   (as the priests' mist does)
// `decide(receiver, command)` says whether the receiver cannot do the command (only then the call is rewritten).
function rewriteDruidCommands(src, decide) {
	const tokens = tokenize(src);
	const namedObjs = new Set();
	for (const m of src.matchAll(/\bNamedObj\b([^;]*);/g)) for (const n of m[1].split(',')) namedObjs.add(n.trim());
	const counts = { hide: 0, mist: 0 };
	const skipped = [];
	for (const c of methodCalls(tokens, 'SetCommand')) {
		const first = nextCode(tokens, c.open + 1);
		const cmd = tokens[first].type === 'string' ? tokens[first].text.slice(1, -1) : '';
		if (cmd !== 'hide' && cmd !== 'mist') continue;
		const comma = nextCode(tokens, first + 1);
		if (tokens[comma].text !== ',') continue;
		const arg = join(tokens.slice(comma + 1, c.close)).trim();
		const receiver = join(tokens.slice(c.start, c.dot)).trim();
		if (!decide(receiver, cmd)) continue;
		const semi = nextCode(tokens, c.close + 1);
		const original = join(tokens.slice(c.start, c.close + 1));
		const statement = semi < tokens.length && tokens[semi].text === ';';
		let text = null;
		if (cmd === 'hide' && statement && /^[A-Za-z_][\w.()]*$/.test(arg)) text = `${arg}.SetVisible(false)`;
		if (cmd === 'mist' && statement) {
			let obj = null;
			if (/\.obj$/.test(receiver)) obj = receiver;
			else if (/^GetNamedObj\s*\(/.test(receiver) || namedObjs.has(receiver)) obj = receiver + '.obj';
			if (obj) text = `Place("Mist", ${arg}, ${obj}.player).SetStamina(10)`;
		}
		if (!text) {
			skipped.push(original);
			continue;
		}
		tokens.splice(c.start, c.close - c.start + 1, { type: 'other', text: `${text} /* was ${original.replace(/\*\//g, '* /')} */` });
		counts[cmd]++;
	}
	return { src: join(tokens), counts, skipped };
}

// Conversation.Run() statements wait until no other conversation is running and mark one as running while they play.
// The sequences of a map run together; Imperivm I plays their conversations one after another, while in Great Battles
// of Rome a conversation started by one sequence cuts the one another sequence is playing (in Kormaris, seq15 starts
// its opening conversation 3.5 s into seq0's goddess). The wait gives up after 60 s, so a conversation that never
// clears the mark cannot block the others for good.
const QUEUE_KEY = '/I1/ConversationBusy';
const QUEUE_VARS = 'int i1cv_w;\n';
function queueConversations(src) {
	const conversations = new Set();
	for (const m of src.matchAll(/\bConversation\b([^;(]*);/g)) for (const n of m[1].split(',')) if (n.trim()) conversations.add(n.trim());
	if (!conversations.size) return { src, count: 0 };
	const tokens = tokenize(src);
	let count = 0;
	for (const c of methodCalls(tokens, 'Run')) {
		const receiver = join(tokens.slice(c.start, c.dot)).trim();
		const semi = nextCode(tokens, c.close + 1);
		if (!conversations.has(receiver) || c.close !== nextCode(tokens, c.open + 1) || semi >= tokens.length || tokens[semi].text !== ';') continue;
		const code =
			`{ i1cv_w = 0; while (EnvReadInt("${QUEUE_KEY}") == 1 && i1cv_w < 600) { Sleep(100); i1cv_w += 1; } ` +
			`EnvWriteInt("${QUEUE_KEY}", 1); ${receiver}.Run(); EnvWriteInt("${QUEUE_KEY}", 0); }`;
		tokens.splice(c.start, semi - c.start + 1, { type: 'other', text: code });
		count++;
	}
	if (!count) return { src, count };
	return { src: insertDeclarations(join(tokens), QUEUE_VARS), count };
}

// Calls of a free function with one argument: `replace(arg)` returns the new argument text, or null to turn the statement
// into a comment.
function rewriteCall(src, name, replace) {
	const tokens = tokenize(src);
	const found = [];
	for (let i = 0; i < tokens.length; i++) {
		if (tokens[i].type !== 'ident' || tokens[i].text !== name) continue;
		const p = prevCode(tokens, i - 1);
		if (p >= 0 && tokens[p].text === '.') continue;
		const open = nextCode(tokens, i + 1);
		if (open >= tokens.length || tokens[open].text !== '(') continue;
		const close = closingParen(tokens, open);
		if (close < 0) continue;
		const arg = join(tokens.slice(open + 1, close)).trim();
		const next = replace(arg);
		found.push({ arg, next });
		if (next === arg) continue;
		if (next !== null) {
			tokens.splice(open + 1, close - open - 1, { type: 'other', text: next });
		} else {
			// A statement keeps its ';' (it may be the body of an if); a call inside an expression becomes false.
			const semi = nextCode(tokens, close + 1);
			const before = prevCode(tokens, i - 1);
			const starts = before < 0 || [';', '{', '}', ')'].includes(tokens[before].text) || tokens[before].text === 'else';
			const statement = starts && semi < tokens.length && tokens[semi].text === ';';
			const text = join(tokens.slice(i, close + 1));
			const comment = '/* ' + text.replace(/\*\//g, '* /') + ' (Imperivm I only) */';
			tokens.splice(i, close - i + 1, { type: 'comment', text: statement ? comment : comment + ' false' });
		}
	}
	return { src: join(tokens), found };
}

module.exports = {
	tokenize,
	listCalls,
	renameFunctions,
	replaceStringLiterals,
	expandReduceGroup,
	rewriteRituals,
	rewriteDruidCommands,
	queueConversations,
	rewriteCall,
	insertDeclarations,
};
