// Clones a game civilization as a new civilization in a mod (Imperivm Resurrected SDK).
//
//   node clone-civilization.js <DATA> <mod folder> <id> [letter] [base=Gaul] [index] [--all-orders]
//   node clone-civilization.js <DATA> <mod folder> <id> [--letter Dac] [--base Gaul] [--index 10] [--all-orders]
//
// With --all-orders every order of the base's classes is copied under a name of its own ("<order> <Id>"), not only the ones that name
// a class. Use it when the copy must change what a shared order costs or does without touching the base's.
//
// Writes to the mod folder:
//   civs\<id>.json                     the civilization declaration
//   data\classes\<L>*.sc.xml           a copy of each base class, with a different id and race="<id>"
//   data\commands\<id>.xml             the base orders that name its classes, with the new classes
//   patches\data\commands\*.xml        adds the new classes to the shared orders (repair, upgrades...)
const fs = require('fs'), path = require('path');

const fail = message => { console.error(message); process.exit(1); };
const opt = {}, positional = [];
const args = process.argv.slice(2);
for (let i = 0; i < args.length; i++) {
	const a = args[i];
	if (a === '--all-orders') opt.allOrders = true;
	else if (['--letter', '--base', '--index'].includes(a)) {
		if (!args[i + 1] || args[i + 1].startsWith('--')) fail('missing value for ' + a);
		opt[a.slice(2)] = args[++i];
	}
	else if (a.startsWith('--')) fail('unknown option: ' + a);
	else positional.push(a);
}
const CLONE_ALL = opt.allOrders;
const [dataDir, modDir, id, positionalLetter, positionalBase, positionalIndex] = positional;
if (!dataDir || !modDir || !id || positional.length > 6) {
	fail('usage: node clone-civilization.js <DATA> <mod folder> <id> [letter] [base=Gaul] [index]\n' +
		'       node clone-civilization.js <DATA> <mod folder> <id> [--letter Dac] [--base Gaul] [--index 10] [--all-orders]');
}
if (opt.letter !== undefined && positionalLetter !== undefined) fail('use either a positional letter or --letter, not both');
if (!/^[A-Za-z][A-Za-z0-9_]{0,23}$/.test(id)) fail('invalid id');
const derived = id.replace(/_/g, '').slice(0, 3).toLowerCase();
const letter = opt.letter ?? positionalLetter ?? (derived[0].toUpperCase() + derived.slice(1));
const base = opt.base ?? positionalBase ?? 'Gaul', indexArg = opt.index ?? positionalIndex;
if (!/^(?:[A-Za-z0-9]|[A-Z][A-Za-z0-9]{1,3})$/.test(letter)) fail('invalid letter: use 1 to 4 characters (uppercase letter then letters or digits; a single letter or digit also works)');
if (indexArg !== undefined && (!Number.isInteger(Number(indexArg)) || Number(indexArg) < 10 || Number(indexArg) > 127)) fail('index must be a whole number from 10 to 127');
const L = letter[0].toUpperCase() + letter.slice(1), l = letter.toLowerCase();
const Id = id[0].toUpperCase() + id.slice(1);

// Letter of each game civilization and the name of its generic hero
const BASES = {
	Gaul: { letter: 'G', hero: 'GaulHero' }, RepublicanRome: { letter: 'R', hero: 'RepublicanRomanHero' },
	Carthage: { letter: 'C', hero: 'CarthaginianHero' }, Iberia: { letter: 'I', hero: 'IberianHero' },
	ImperialRome: { letter: 'M', hero: 'ImperialRomanHero' }, Britain: { letter: 'B', hero: 'BritonHero' },
	Egypt: { letter: 'E', hero: 'EgyptianHero' }, Germany: { letter: 'T', hero: 'GermanHero' },
};
if (!BASES[base]) { console.error('unknown base: ' + base); process.exit(1); }
const bl = BASES[base].letter, blow = bl.toLowerCase();
const reserved = Object.entries(BASES).find(([, b]) => b.letter === L);
if (reserved) fail(`prefix "${L}" belongs to ${reserved[0]}: cloning with it would overwrite game classes; choose a different --letter (for example ${L}ac)`);
const registry = fs.readFileSync(path.join(__dirname, '..', 'prefixes.md'), 'utf8');
for (const m of registry.matchAll(/^\| `([^`]+)` \| ([^|]+) \| ([^|]+) \|/gm)) {
	if (m[1].toLowerCase() === l) console.warn(`WARNING: prefix "${L}" is registered to ${m[2].trim()} from ${m[3].trim()}; choose a different --letter before publishing. Keeping "${L}" (no alternative chosen).`);
}

const read = f => fs.readFileSync(f, 'latin1');
const ci = (dir, name) => fs.readdirSync(dir).find(f => f.toLowerCase() === name.toLowerCase());
if (!fs.existsSync(dataDir) || !fs.statSync(dataDir).isDirectory()) fail('DATA folder not found: first run tools/extract-data.js and pass its DATA folder');
const classesName = ci(dataDir, 'classes'), commandsName = ci(dataDir, 'commands');
if (!classesName || !commandsName) fail('the DATA folder must contain classes and commands: use the DATA folder from tools/extract-data.js');
const classesDir = path.join(dataDir, classesName);
const commandsDir = path.join(dataDir, commandsName);

// 1. Base classes: id -> file text. It is copied whole: the game does NOT inherit things like the entity from the parent,
// and a class without an entity is created with no model and disappears
const classes = new Map();
const everyClass = new Map();  // every game class, by id (for those that have no race but that the civilization needs)
for (const f of fs.readdirSync(classesDir)) {
	if (!/\.sc\.xml$/i.test(f)) continue;
	const t = read(path.join(classesDir, f));
	const m = t.match(/<class\b([^>]*)>/);
	if (!m) continue;
	const cid = (m[1].match(/\bid\s*=\s*"([^"]*)"/) || [])[1];
	if (!cid) continue;
	everyClass.set(cid, t);
	if (t.indexOf('race="' + base + '"') >= 0) classes.set(cid, t);
}
// Sentries: the AI, towers, walls and guard posts create "<letter>Sentry", "<letter>Sentry1" and "<letter>GuardPostSentry" with the
// OWNER's letter. They are classes without race (children of <letter>Sentry), so they do not come in through the race: they are requested by name. The first one is mandatory
for (const extra of ['Sentry', 'Sentry1', 'GuardPostSentry']) {
	const id = bl + extra;
	if (everyClass.has(id) && !classes.has(id)) classes.set(id, everyClass.get(id));
}
// The child classes of the base ones that have no race (GHero1..GHero4, children of GaulHero: they inherit the race from the parent) also belong to the
// civilization. Without them there are no heroes to hire (GetRandomHeroClass keeps looking forever for a class with a model)
const parentOf = t => ((t.match(/<class\b[^>]*?\bparent\s*=\s*"([^"]*)"/) || [])[1]);
for (let grew = true; grew;) {
	grew = false;
	for (const [cid, t] of everyClass) {
		if (classes.has(cid) || /<properties\b[^>]*\brace\s*=/.test(t)) continue;
		const p = parentOf(t);
		if (p && classes.has(p)) { classes.set(cid, t); grew = true; }
	}
}
// Which classes get cloned: those starting with the base letter + an uppercase letter (or "house", like Rhouse03 of Republican Rome) and the generic hero
const rename = new Map();
for (const cid of classes.keys()) {
	if (new RegExp('^' + bl + '([A-Z]|house)').test(cid)) rename.set(cid, L + cid.slice(1));
	else if (cid === BASES[base].hero) rename.set(cid, Id + 'Hero');
}
console.log(`${classes.size} classes with race="${base}", ${rename.size} will be cloned`);

// Many orders do not name the class but its alias (altid): <src obj="DruidHouse"/> is the Gaul druid house (GDruidHouse, altid
// "DruidHouse"). Since the copies carry no altid, the orders for those aliases are offered to the copy by its class id
const aliasOf = new Map();  // altid -> id of the original class
for (const [cid, text] of classes) {
	const alias = (text.match(/<class\b[^>]*?\baltid\s*=\s*"([^"]*)"/) || [])[1];
	if (alias && rename.has(cid)) aliasOf.set(alias, cid);
}

if (!rename.size) fail(`no classes to clone for ${base}: check the extracted DATA folder`);

// 2. Orders
const esc = s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const alt = [...rename.keys()].sort((a, b) => b.length - a.length).map(esc).join('|');
const classRe = new RegExp('\\b(' + alt + ')\\b', 'g');
const mapIds = s => s.replace(classRe, m => rename.get(m));
function mapName(n) {
	const r = mapIds(n);
	if (r !== n) return r;
	return n.replace(new RegExp('^(trainpeasant)' + blow + '$'), '$1' + l)
		.replace(new RegExp('^(hirehero)' + bl + '$'), '$1' + L)
		.replace(new RegExp('^' + bl + '(Tribute|AddPop)$'), L + '$1')
		.replace(new RegExp('\\b' + base + '\\b'), Id);
}
const cloned = [];
const aliasOrders = {};  // name of the base order -> name of the cloned order (for the AI)
const shared = new Map();  // file -> [{name, srcs}]
let nShared = 0;
for (const f of fs.readdirSync(commandsDir)) {
	const t = read(path.join(commandsDir, f)).replace(/<!--[\s\S]*?-->/g, '');  // commented-out orders do not exist
	const re = /<cmd\b[\s\S]*?<\/cmd>/g;
	let m;
	while ((m = re.exec(t))) {
		const body = m[0];
		const name = (body.match(/\bname\s*=\s*"([^"]*)"/) || [])[1];
		const srcs = [...body.matchAll(/<src\s+obj\s*=\s*"([^"]*)"/g)].map(x => aliasOf.get(x[1]) || x[1]);
		const mine = srcs.filter(s => rename.has(s));
		// Exclusions (<nsrc>): an order for "all posts except GGuardPost" would offer its buttons to the DGuardPost if it is not excluded
			const nsrcs = [...body.matchAll(/<nsrc\s+obj\s*=\s*"([^"]*)"/g)].map(x => aliasOf.get(x[1]) || x[1]).filter(s => rename.has(s));
			if (nsrcs.length && name) {
				if (!shared.has(f)) shared.set(f, []);
				shared.get(f).push({ name, srcs: [], nsrcs: nsrcs.map(s => rename.get(s)) });
			}
			if (!mine.length || !name) continue;
		const head = body.replace(/<src\b[\s\S]*$/, '');
		const refs = new RegExp(classRe.source).test(head);
		if (refs || CLONE_ALL || mapName(name) !== name || /^trainpeasant|^hirehero/i.test(name)) {
			// the order names base classes: copy it with the new classes
			let nn = mapName(name);
			if (nn === name) nn = name + ' ' + Id;
			let c = body.replace(/<src\b[^>]*\/>\s*/g, '');
			c = c.replace(/(\bname\s*=\s*")[^"]*"/, '$1' + nn + '"');
			const srcTxt = mine.map(s => '\t<src obj="' + rename.get(s) + '"/>').join('\n');
			// Only the param and sclass classes are changed: the images (button, queueicon...), the help and the texts are the base's
			c = c.replace(/(\b(?:param|sclass)\s*=\s*")([^"]*)"/g, (_s, a, v) => a + mapIds(v) + '"');
			c = c.replace(/<\/cmd>$/, srcTxt + '\n</cmd>');
			cloned.push(c);
			aliasOrders[name] = nn;
		}
		else {
			// generic order (repair, upgrades...): the original is patched so that the new classes offer it too
			if (!shared.has(f)) shared.set(f, []);
			shared.get(f).push({ name, srcs: mine.map(s => rename.get(s)) });
			nShared++;
		}
	}
}
const outputs = [
	...Array.from(rename.values(), name => path.join(modDir, 'data', 'classes', name + '.sc.xml')),
	path.join(modDir, 'data', 'commands', Id + '.xml'),
	...Array.from(shared.keys(), name => path.join(modDir, 'patches', 'data', 'commands', name)),
	path.join(modDir, 'civs', id.toLowerCase() + '.json'),
];
const existing = new Set(fs.existsSync(modDir) ? fs.readdirSync(modDir, { recursive: true }).map(name => name.toLowerCase()) : []);
for (const file of outputs) {
	if (existing.has(path.relative(modDir, file).toLowerCase())) fail(`refusing to overwrite ${file}: generate into a fresh folder, then merge your changes manually`);
}

// 3. New classes: copy the entity and image paths unchanged.
fs.mkdirSync(path.join(modDir, 'data', 'classes'), { recursive: true });
for (const [oldId, newId] of rename) {
	let xml = classes.get(oldId);
	xml = xml.replace(/(<class\b[^>]*?\bid\s*=\s*")[^"]*"/, '$1' + newId + '"');
	xml = xml.replace(/(<class\b[^>]*?\bparent\s*=\s*")([^"]*)"/, (_s, p, par) => p + (rename.get(par) || par) + '"');
	xml = xml.split('race="' + base + '"').join('race="' + Id + '"');
	// An altid belongs to one class only; duplicating it hides the original from the AI.
	xml = xml.replace(/(<class\b[^>]*?)\s+altid\s*=\s*"[^"]*"/, '$1');
	xml = xml.replace(/(\bsentry_class_name\s*=\s*")([^"]*)"/g, (_s, a, v) => a + (rename.get(v) || v) + '"');
	fs.writeFileSync(path.join(modDir, 'data', 'classes', newId + '.sc.xml'), xml, 'latin1');
}
fs.mkdirSync(path.join(modDir, 'data', 'commands'), { recursive: true });
fs.writeFileSync(path.join(modDir, 'data', 'commands', Id + '.xml'),
	'<commands>\n\n<!-- ' + `${Id} (cloned from ${base})` + ' -->\n\n' + cloned.join('\n\n') + '\n\n</commands>\n', 'latin1');
for (const [f, list] of shared) {
	const dest = path.join(modDir, 'patches', 'data', 'commands', f);
	fs.mkdirSync(path.dirname(dest), { recursive: true });
	const ops = list.map(o => '\t<add select="/commands/cmd[@name=\'' + o.name + '\']">' +
		o.srcs.map(s => '<src obj="' + s + '"/>').join('') + (o.nsrcs || []).map(s => '<nsrc obj="' + s + '"/>').join('') + '</add>');
	fs.writeFileSync(dest, '<patch>\n' + ops.join('\n') + '\n</patch>\n', 'latin1');
}

// 4. Declaration
fs.mkdirSync(path.join(modDir, 'civs'), { recursive: true });
fs.writeFileSync(path.join(modDir, 'civs', id.toLowerCase() + '.json'), JSON.stringify({
	id: id.toLowerCase(), ...(indexArg === undefined ? {} : { index: Number(indexArg) }), base, name: { spanish: Id, english: Id, italian: Id }, letter: L, ai: true,
		// With "ia": true the AI plays the civilization using the game's scripts, which see the base: these tables translate what they ask for
		// (base classes and orders) into what was cloned. Do not delete them if you enable "ia"
		alias: { classes: Object.fromEntries(rename), commands: aliasOrders },
}, null, 2) + '\n');
console.log(`${rename.size} classes, ${cloned.length} new commands, ${nShared} shared commands patched`);
