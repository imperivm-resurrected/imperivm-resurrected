// Reads the log of a session run with [Debug] MemoryTrace=1 and prints, for every game that started, how much memory the process held and what the
// game's caches held, so five rounds of ruleset switches can be compared with the first game.
//   node tools/memory-report.js "<game>\Logs\imperivm\imperivm-mod.log" [more logs]
// The rows are the points "after LoadGameData" (a game just loaded). A game that came after a switch says which one; "caches emptied" is the line the
// switch writes when it empties the image caches (see sdk/examples/ruleset-clash-example).
'use strict';
const fs = require('fs');

const files = process.argv.slice(2);
if (!files.length) {
	console.log('Usage: node memory-report.js <imperivm-mod.log> [more logs, oldest first]');
	process.exit(1);
}

const rows = [];
let switchText = '';
let emptied = '';
let current = null;

function readLog(file) {
	let text;
	try {
		text = fs.readFileSync(file, 'utf8');
	} catch (e) {
		console.log('Cannot read ' + file + ': ' + e.message);
		process.exit(1);
	}
	return text.split(/\r?\n/);
}

for (const file of files) {
	for (const line of readLog(file)) {
		let m = /ruleset changed without a restart: (\S+) -> (\S+)/.exec(line);
		if (m) {
			switchText = m[1] + ' -> ' + m[2];
			continue;
		}
		m = /image caches emptied in (\d+) ms: files (\d+) entries \((\d+) in use\) -> (\d+) \((\d+) in use\).* (\d+) images change, (\d+) of them still cached/.exec(line);
		if (m) {
			emptied = m[2] + ' -> ' + m[4] + ' file entries, ' + m[6] + ' images change, ' + m[7] + ' still cached';
			continue;
		}
		m = /memory \(after LoadGameData(?:, nothing reloaded)?\): process commit (\d+) MB .*working set (\d+) MB, .* (\d+) heaps, (\d+) failed HeapValidate/.exec(line);
		if (m) {
			current = { commit: +m[1], working: +m[2], failed: +m[4], switched: switchText, emptied, entities: '', files: '', interface: '' };
			rows.push(current);
			switchText = '';
			emptied = '';
			continue;
		}
		if (!current) continue;
		m = /memory \(after LoadGameData[^)]*\): entity cache holds (\d+) entities by name/.exec(line);
		if (m) current.entities = m[1];
		m = /art cache \(after LoadGameData[^)]*\): file cache (\d+) entries \((\d+) in use.*interface cache (\d+) bytes/.exec(line);
		if (m) {
			current.files = m[1] + ' (' + m[2] + ' in use)';
			current.interface = m[3];
		}
	}
}

if (!rows.length) {
	console.log('No "memory (after LoadGameData" lines: was [Debug] MemoryTrace=1 set before the game started?');
	process.exit(1);
}

const pad = (s, n) => String(s).padEnd(n);
console.log(pad('game', 5) + pad('commit MB', 11) + pad('working MB', 12) + pad('failed heaps', 14) + pad('entities', 10) + pad('file cache', 18) + pad('interface bytes', 17) + 'switch before it');
rows.forEach((r, i) => {
	console.log(pad(i + 1, 5) + pad(r.commit, 11) + pad(r.working, 12) + pad(r.failed, 14) + pad(r.entities, 10) + pad(r.files, 18) + pad(r.interface, 17) + (r.switched || '-') + (r.emptied ? ' [' + r.emptied + ']' : ''));
});
const first = rows[0], last = rows[rows.length - 1];
const switches = rows.filter((r) => r.switched).length;
console.log('\n' + rows.length + ' games, ' + switches + ' after a switch. Commit ' + first.commit + ' -> ' + last.commit + ' MB (' + (last.commit - first.commit >= 0 ? '+' : '') + (last.commit - first.commit) + '), working set '
	+ first.working + ' -> ' + last.working + ' MB. Failed heap checks: ' + rows.reduce((n, r) => n + r.failed, 0) + '.');
if (switches) console.log('Per switch: ' + ((last.commit - first.commit) / switches).toFixed(1) + ' MB of commit.');
