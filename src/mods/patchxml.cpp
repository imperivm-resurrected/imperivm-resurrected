// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/patchxml.h"

#include <algorithm>
#include <set>
#include <cctype>
#include <cstring>
#include <map>
#include <memory>

#include "core/legacy.h"
#include "i18n/tr.h"
#include "mods/modtext.h"

namespace {

struct Attr {
	std::string name, value;
	char quote = '"';
};

struct Node {
	enum Kind { kElement, kText, kRaw } kind = kElement;
	std::string name;
	std::vector<Attr> attrs;
	std::string text;
	std::vector<std::unique_ptr<Node>> kids;
	Node *parent = nullptr;
	bool selfClose = false;
	bool removed = false;
	std::string key;
};

typedef std::unique_ptr<Node> NodePtr;

using modtext::Lower;

bool IEq(const std::string &a, const std::string &b)
{
	return a.size() == b.size() && Lower(a) == Lower(b);
}

std::string Trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && isspace(static_cast<unsigned char>(s[a]))) a++;
	while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) b--;
	return s.substr(a, b - a);
}

bool BlankText(const std::string &s)
{
	return std::all_of(s.begin(), s.end(), [](char c) { return isspace(static_cast<unsigned char>(c)) != 0; });
}

int LineAt(const std::string &s, size_t pos)
{
	return 1 + static_cast<int>(std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>((std::min)(pos, s.size())), '\n'));
}

std::string Unescape(const std::string &in)
{
	if (in.find('&') == std::string::npos) return in;
	static const struct {
		const char *ent;
		char ch;
	} kEnt[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
	std::string out;
	for (size_t i = 0; i < in.size(); i++) {
		bool done = false;
		if (in[i] == '&') {
			for (const auto &e : kEnt) {
				const size_t n = strlen(e.ent);
				if (in.compare(i, n, e.ent) == 0) {
					out += e.ch;
					i += n - 1;
					done = true;
					break;
				}
			}
		}
		if (!done) out += in[i];
	}
	return out;
}

class Parser {
public:
	explicit Parser(const std::string &text) : s(text) {}

	NodePtr Parse(std::string &error)
	{
		auto doc = std::make_unique<Node>();
		if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) i = 3;
		std::string closed;
		Nodes(doc.get(), closed);
		if (err.empty() && !closed.empty()) Fail(TrFmt("closing tag </%s> was never opened", {closed}));
		if (!err.empty()) {
			error = err;
			return nullptr;
		}
		return doc;
	}

private:
	const std::string &s;
	size_t i = 0;
	std::string err;

	void Fail(const std::string &m)
	{
		if (err.empty()) err = TrFmt("line %s: %s", {std::to_string(LineAt(s, i)), m});
	}

	static bool NameStart(char c) { return isalpha(static_cast<unsigned char>(c)) || c == '_' || c == ':'; }
	static bool NameChar(char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '-' || c == '.'; }

	void AddRaw(Node *parent, size_t from, size_t to)
	{
		auto n = std::make_unique<Node>();
		n->kind = Node::kRaw;
		n->text = s.substr(from, to - from);
		n->parent = parent;
		parent->kids.push_back(std::move(n));
	}

	void Nodes(Node *parent, std::string &closed)
	{
		while (i < s.size() && err.empty()) {
			if (s[i] != '<') {
				const size_t end = s.find('<', i);
				auto t = std::make_unique<Node>();
				t->kind = Node::kText;
				t->text = s.substr(i, end == std::string::npos ? std::string::npos : end - i);
				t->parent = parent;
				parent->kids.push_back(std::move(t));
				i = end == std::string::npos ? s.size() : end;
				continue;
			}
			if (s.compare(i, 4, "<!--") == 0) {
				const size_t end = s.find("-->", i + 4);
				if (end == std::string::npos) return Fail(Tr("unterminated comment"));
				AddRaw(parent, i, end + 3);
				i = end + 3;
			}
			else if (s.compare(i, 9, "<![CDATA[") == 0) {
				const size_t end = s.find("]]>", i + 9);
				if (end == std::string::npos) return Fail(Tr("unterminated CDATA"));
				AddRaw(parent, i, end + 3);
				i = end + 3;
			}
			else if (s.compare(i, 2, "<?") == 0) {
				const size_t end = s.find("?>", i + 2);
				if (end == std::string::npos) return Fail(Tr("unterminated <?"));
				AddRaw(parent, i, end + 2);
				i = end + 2;
			}
			else if (s.compare(i, 2, "<!") == 0) {
				size_t k = i + 2;
				int depth = 0;
				while (k < s.size() && !(s[k] == '>' && depth == 0)) {
					if (s[k] == '[') depth++;
					else if (s[k] == ']') depth--;
					k++;
				}
				if (k >= s.size()) return Fail(Tr("unterminated <!"));
				AddRaw(parent, i, k + 1);
				i = k + 1;
			}
			else if (s.compare(i, 2, "</") == 0) {
				size_t k = i + 2;
				while (k < s.size() && NameChar(s[k])) k++;
				closed = s.substr(i + 2, k - i - 2);
				while (k < s.size() && isspace(static_cast<unsigned char>(s[k]))) k++;
				if (k >= s.size() || s[k] != '>') return Fail(Tr("malformed closing tag"));
				i = k + 1;
				return;
			}
			else {
				Element(parent);
			}
		}
	}

	int depth = 0;

	void Element(Node *parent)
	{
		struct DepthGuard {
			int &d;
			explicit DepthGuard(int &x) : d(x) { ++d; }
			~DepthGuard() { --d; }
		} depthGuard(depth);
		if (depth > 128) return Fail(Tr("the file is nested too deeply"));
		const size_t start = i;
		i++;
		if (i >= s.size() || !NameStart(s[i])) {
			i = start;
			return Fail(Tr("missing tag name (a stray <? it is written &lt;)"));
		}
		auto e = std::make_unique<Node>();
		e->parent = parent;
		size_t k = i;
		while (k < s.size() && NameChar(s[k])) k++;
		e->name = s.substr(i, k - i);
		i = k;
		for (;;) {
			while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) i++;
			if (i >= s.size()) return Fail(TrFmt("the tag <%s> is not closed", {e->name}));
			if (s.compare(i, 2, "/>") == 0) {
				i += 2;
				e->selfClose = true;
				parent->kids.push_back(std::move(e));
				return;
			}
			if (s[i] == '>') {
				i++;
				break;
			}
			if (!NameStart(s[i])) return Fail(TrFmt("malformed attribute in <%s>", {e->name}));
			Attr a;
			k = i;
			while (k < s.size() && NameChar(s[k])) k++;
			a.name = s.substr(i, k - i);
			i = k;
			while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) i++;
			if (i >= s.size() || s[i] != '=') return Fail(TrFmt("the attribute %s is missing the =", {a.name}));
			i++;
			while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) i++;
			if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) return Fail(TrFmt("the value of %s is not in quotes", {a.name}));
			a.quote = s[i];
			const size_t end = s.find(a.quote, i + 1);
			if (end == std::string::npos) return Fail(TrFmt("missing closing quotes for %s", {a.name}));
			a.value = s.substr(i + 1, end - i - 1);
			if (a.value.find('<') != std::string::npos) {
				i++;
				return Fail(TrFmt("the value of %s contains a < (it is written &lt;)", {a.name}));
			}
			i = end + 1;
			e->attrs.push_back(a);
		}
		Node *raw = e.get();
		parent->kids.push_back(std::move(e));
		std::string closed;
		Nodes(raw, closed);
		if (!err.empty()) return;
		if (closed.empty()) {
			i = s.size();
			return Fail(TrFmt("the tag <%s> is not closed", {raw->name}));
		}
		if (!IEq(closed, raw->name)) return Fail(TrFmt("expected </%s> but found </%s>", {raw->name, closed}));
	}
};

void Write(const Node &n, std::string &out)
{
	switch (n.kind) {
	case Node::kText:
	case Node::kRaw: out += n.text; break;
	case Node::kElement:
		out += "<" + n.name;
		for (const Attr &a : n.attrs) out += " " + a.name + "=" + a.quote + a.value + a.quote;
		if (n.kids.empty() && n.selfClose) {
			out += "/>";
			break;
		}
		out += ">";
		for (const auto &k : n.kids) Write(*k, out);
		out += "</" + n.name + ">";
		break;
	}
}

NodePtr Clone(const Node &n, Node *parent, const std::string &key, std::map<const Node *, Node *> *twins = nullptr)
{
	auto c = std::make_unique<Node>();
	c->kind = n.kind;
	c->name = n.name;
	c->attrs = n.attrs;
	c->text = n.text;
	c->selfClose = n.selfClose;
	c->parent = parent;
	c->key = key;
	if (twins && n.kind == Node::kElement) (*twins)[&n] = c.get();
	for (size_t i = 0; i < n.kids.size(); i++) c->kids.push_back(Clone(*n.kids[i], c.get(), key + "/" + std::to_string(i), twins));
	return c;
}

struct Pred {
	std::string attr, value;
	bool hasValue = false;
};

struct Step {
	bool deep = false;
	std::string name;
	std::vector<Pred> preds;
	int index = 0;
};

bool ParseSelector(const std::string &sel, std::vector<Step> &steps, std::string &error)
{
	size_t i = 0;
	const std::string s = Trim(sel);
	if (s.empty()) {
		error = Tr("the selector is empty");
		return false;
	}
	while (i < s.size()) {
		Step st;
		if (s[i] == '/') {
			i++;
			if (i < s.size() && s[i] == '/') {
				st.deep = true;
				i++;
			}
		}
		size_t k = i;
		while (k < s.size() && s[k] != '/' && s[k] != '[') k++;
		st.name = Trim(s.substr(i, k - i));
		if (st.name.empty()) {
			error = Tr("a selector step has no name");
			return false;
		}
		i = k;
		while (i < s.size() && s[i] == '[') {
			const size_t close = s.find(']', i);
			if (close == std::string::npos) {
				error = Tr("missing ] in the selector");
				return false;
			}
			const std::string body = Trim(s.substr(i + 1, close - i - 1));
			if (!body.empty() && body[0] == '@') {
				Pred p;
				const size_t eq = body.find('=');
				if (eq == std::string::npos) {
					p.attr = Trim(body.substr(1));
				}
				else {
					p.attr = Trim(body.substr(1, eq - 1));
					std::string v = Trim(body.substr(eq + 1));
					if (v.size() < 2 || (v[0] != '\'' && v[0] != '"') || v.back() != v[0]) {
						error = TrFmt("the value of @%s must be quoted", {p.attr});
						return false;
					}
					p.value = v.substr(1, v.size() - 2);
					p.hasValue = true;
				}
				st.preds.push_back(p);
			}
			else if (!body.empty() && std::all_of(body.begin(), body.end(), [](char c) { return isdigit(static_cast<unsigned char>(c)) != 0; })) {
				st.index = atoi(body.c_str());
			}
			else {
				error = TrFmt("unsupported condition [%s] ([@attribute='value'], [@attribute] and [n] are valid)", {body});
				return false;
			}
			i = close + 1;
		}
		steps.push_back(st);
		if (i < s.size() && s[i] != '/') {
			error = TrFmt("malformed selector near \"%s\"", {s.substr(i, 12)});
			return false;
		}
	}
	return true;
}

bool Matches(const Node &n, const Step &st)
{
	if (n.kind != Node::kElement) return false;
	if (st.name != "*" && !IEq(n.name, st.name)) return false;
	for (const Pred &p : st.preds) {
		bool ok = false;
		for (const Attr &a : n.attrs) {
			if (IEq(a.name, p.attr) && (!p.hasValue || IEq(Unescape(a.value), p.value))) ok = true;
		}
		if (!ok) return false;
	}
	return true;
}

void Descendants(Node *n, std::vector<Node *> &out)
{
	for (auto &k : n->kids) {
		if (k->kind != Node::kElement) continue;
		out.push_back(k.get());
		Descendants(k.get(), out);
	}
}

std::vector<Node *> Select(Node *doc, const std::vector<Step> &steps)
{
	std::vector<Node *> cur = {doc};
	for (const Step &st : steps) {
		std::vector<Node *> next;
		for (Node *c : cur) {
			std::vector<Node *> cand;
			if (st.deep) {
				Descendants(c, cand);
			}
			else {
				for (auto &k : c->kids) {
					if (k->kind == Node::kElement) cand.push_back(k.get());
				}
			}
			int count = 0;
			for (Node *n : cand) {
				if (!Matches(*n, st)) continue;
				count++;
				if (st.index == 0 || st.index == count) {
					if (std::find(next.begin(), next.end(), n) == next.end()) next.push_back(n);
				}
			}
		}
		cur = next;
	}
	return cur;
}

std::string Attribute(const Node &n, const char *name)
{
	for (const Attr &a : n.attrs) {
		if (IEq(a.name, name)) return a.value;
	}
	return "";
}

bool Truthy(const std::string &v)
{
	const std::string l = Lower(Trim(v));
	return l == "1" || l == "yes" || LegacyIsYes(l) || l == "true";
}

std::vector<std::string> NameList(const std::string &list)
{
	std::vector<std::string> out;
	size_t p = 0;
	while (p < list.size()) {
		while (p < list.size() && (isspace(static_cast<unsigned char>(list[p])) || list[p] == ',')) p++;
		size_t q = p;
		while (q < list.size() && !isspace(static_cast<unsigned char>(list[q])) && list[q] != ',') q++;
		if (q > p) out.push_back(list.substr(p, q - p));
		p = q;
	}
	return out;
}

std::string CanonicalSelector(const std::vector<Step> &steps)
{
	std::string out;
	for (const Step &st : steps) {
		std::vector<std::string> preds;
		for (const Pred &p : st.preds) preds.push_back("[@" + Lower(p.attr) + (p.hasValue ? "='" + Lower(p.value) + "'" : "") + "]");
		std::sort(preds.begin(), preds.end());
		preds.erase(std::unique(preds.begin(), preds.end()), preds.end());
		out += (st.deep ? "//" : "/") + Lower(st.name);
		for (const std::string &p : preds) out += p;
		if (st.index) out += "[" + std::to_string(st.index) + "]";
	}
	return out;
}

size_t IndexIn(Node *n)
{
	Node *p = n->parent;
	for (size_t i = 0; i < p->kids.size(); i++) {
		if (p->kids[i].get() == n) return i;
	}
	return p->kids.size();
}

std::vector<const Node *> Payload(const Node &op)
{
	std::vector<const Node *> out;
	for (const auto &k : op.kids) {
		if (k->kind == Node::kText && BlankText(k->text)) continue;
		out.push_back(k.get());
	}
	return out;
}

std::string IndentOf(Node *n)
{
	Node *p = n->parent;
	const size_t at = IndexIn(n);
	if (p && at > 0 && p->kids[at - 1]->kind == Node::kText) {
		const std::string &t = p->kids[at - 1]->text;
		const size_t nl = t.rfind('\n');
		if (nl != std::string::npos && BlankText(t.substr(nl + 1))) return t.substr(nl);
	}
	return "\n";
}

void InsertNodes(Node *parent, size_t at, const std::vector<const Node *> &payload, const std::string &lineStart, int &added, bool spaceAfter = false)
{
	for (const Node *src : payload) {
		auto ws = std::make_unique<Node>();
		ws->kind = Node::kText;
		ws->text = lineStart;
		ws->parent = parent;
		if (!spaceAfter) parent->kids.insert(parent->kids.begin() + static_cast<std::ptrdiff_t>(at++), std::move(ws));
		parent->kids.insert(parent->kids.begin() + static_cast<std::ptrdiff_t>(at++), Clone(*src, parent, "+" + std::to_string(++added)));
		if (spaceAfter) parent->kids.insert(parent->kids.begin() + static_cast<std::ptrdiff_t>(at++), std::move(ws));
	}
}

void Normalize(Node &n)
{
	if (!n.kids.empty()) n.selfClose = false;
	for (size_t i = 0; i < n.kids.size();) {
		if (i > 0 && n.kids[i]->kind == Node::kText && n.kids[i - 1]->kind == Node::kText) {
			n.kids[i - 1]->text += n.kids[i]->text;
			n.kids.erase(n.kids.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		Normalize(*n.kids[i]);
		i++;
	}
}

}

struct XmlPatchRun::State {
	NodePtr original;
	NodePtr live;
	std::map<const Node *, Node *> twins;
	std::vector<NodePtr> graveyard;
	std::string error;
	bool bom = false;
	int added = 0;

	bool RemovedBefore(const std::vector<Step> &steps)
	{
		const std::vector<Node *> was = Select(original.get(), steps);
		if (was.empty()) return false;
		for (const Node *o : was) {
			const auto it = twins.find(o);
			if (it == twins.end()) return false;
			bool gone = false;
			for (const Node *n = it->second; n && !gone; n = n->parent) gone = n->removed;
			if (!gone) return false;
		}
		return true;
	}
};

XmlPatchRun::XmlPatchRun(const std::string &base) : state(std::make_unique<State>())
{
	state->original = Parser(base).Parse(state->error);
	if (!state->original) return;
	state->live = Clone(*state->original, nullptr, "", &state->twins);
	state->bom = base.compare(0, 3, "\xEF\xBB\xBF") == 0;
}

XmlPatchRun::~XmlPatchRun() = default;

bool XmlPatchRun::Ok() const
{
	return state->live != nullptr;
}

std::string XmlPatchRun::Text() const
{
	std::string out;
	if (state->bom) out += "\xEF\xBB\xBF";
	if (state->live) {
		for (const auto &k : state->live->kids) Write(*k, out);
	}
	return out;
}

bool XmlPatchRun::Apply(const std::string &patch, std::vector<std::string> &errors, std::vector<PatchedValue> *values)
{
	if (!state->live) {
		errors.push_back(TrFmt("the game file cannot be read as XML (%s)", {state->error}));
		return false;
	}
	Node *doc = state->live.get();
	std::string err;
	NodePtr pdoc = Parser(patch).Parse(err);
	if (!pdoc) {
		errors.push_back(TrFmt("the patch is not well-formed XML (%s)", {err}));
		return false;
	}
	const Node *root = nullptr;
	for (const auto &k : pdoc->kids) {
		if (k->kind == Node::kElement) root = k.get();
	}
	if (!root) {
		errors.push_back(Tr("the patch has no <patch> tag"));
		return false;
	}
	int applied = 0;
	for (const auto &opPtr : root->kids) {
		const Node &op = *opPtr;
		if (op.kind != Node::kElement) continue;
		const std::string kind = Lower(op.name);
		auto fail = [&](const std::string &m) { errors.push_back("<" + op.name + " select=\"" + Attribute(op, "select") + "\">: " + m); };
		if (kind != "add" && kind != "set" && kind != "remove" && kind != "replace") {
			fail(Tr("unknown operation (add, set, remove and replace are valid)"));
			continue;
		}
		std::vector<Step> steps;
		std::string selError;
		if (!ParseSelector(Attribute(op, "select"), steps, selError)) {
			fail(selError.empty() ? Tr("missing select") : selError);
			continue;
		}
		std::vector<Node *> targets = Select(doc, steps);
		if (targets.empty()) {
			const bool removedBefore = kind == "remove" && state->RemovedBefore(steps);
			if (!removedBefore && !Truthy(Attribute(op, "optional"))) fail(Tr("cannot find that element in the game file"));
			continue;
		}
		if (kind == "remove" || kind == "replace") {
			const std::set<Node *> chosen(targets.begin(), targets.end());
			targets.erase(std::remove_if(targets.begin(), targets.end(),
							  [&](Node *t) {
								  for (Node *a = t->parent; a; a = a->parent) {
									  if (chosen.count(a)) return true;
								  }
								  return false;
							  }),
				targets.end());
		}
		if (kind == "set") {
			const std::string shown = Trim(Attribute(op, "select"));
			const std::vector<std::string> unset = NameList(Attribute(op, "unset"));
			for (Node *t : targets) {
				if (t == doc) continue;
				for (const Attr &pa : op.attrs) {
					const std::string n = Lower(pa.name);
					if (n == "select" || n == "optional" || n == "unset") continue;
					bool found = false;
					for (Attr &a : t->attrs) {
						if (IEq(a.name, pa.name)) {
							a.value = pa.value;
							a.quote = pa.quote;
							found = true;
						}
					}
					if (!found) t->attrs.push_back(pa);
					if (values) values->push_back({"", t->key + "/@" + n, shown + "/@" + pa.name, pa.value, false});
				}
				for (const std::string &name : unset) {
					t->attrs.erase(std::remove_if(t->attrs.begin(), t->attrs.end(), [&](const Attr &a) { return IEq(a.name, name); }), t->attrs.end());
					if (values) values->push_back({"", t->key + "/@" + Lower(name), shown + "/@" + name, "", true});
				}
			}
			applied++;
		}
		else if (kind == "remove") {
			for (Node *t : targets) {
				Node *p = t->parent;
				if (!p) continue;
				size_t at = IndexIn(t);
				t->removed = true;
				state->graveyard.push_back(std::move(p->kids[at]));
				p->kids.erase(p->kids.begin() + static_cast<std::ptrdiff_t>(at));
				if (at > 0 && p->kids[at - 1]->kind == Node::kText && BlankText(p->kids[at - 1]->text) &&
					(at >= p->kids.size() || (p->kids[at]->kind == Node::kText))) {
					p->kids.erase(p->kids.begin() + static_cast<std::ptrdiff_t>(at - 1));
				}
			}
			applied++;
		}
		else {
			const std::vector<const Node *> payload = Payload(op);
			if (payload.empty()) {
				fail(Tr(kind == "add" ? "it brings nothing to add" : "it brings nothing to set"));
				continue;
			}
			const std::string position = Lower(Attribute(op, "position"));
			if (kind == "add" && !position.empty() && position != "end" && position != "start" && position != "before" && position != "after") {
				fail(Tr("position is end, start, before or after"));
				continue;
			}
			for (Node *t : targets) {
				if (kind == "replace") {
					Node *p = t->parent;
					if (!p) continue;
					const size_t at = IndexIn(t);
					const std::string ind = IndentOf(t);
					state->graveyard.push_back(std::move(p->kids[at]));
					p->kids.erase(p->kids.begin() + static_cast<std::ptrdiff_t>(at));
					bool first = true;
					size_t pos = at;
					for (const Node *src : payload) {
						if (!first) {
							auto ws = std::make_unique<Node>();
							ws->kind = Node::kText;
							ws->text = ind;
							ws->parent = p;
							p->kids.insert(p->kids.begin() + static_cast<std::ptrdiff_t>(pos++), std::move(ws));
						}
						p->kids.insert(p->kids.begin() + static_cast<std::ptrdiff_t>(pos++), Clone(*src, p, "+" + std::to_string(++state->added)));
						first = false;
					}
				}
				else if (position == "before" || position == "after") {
					Node *p = t->parent;
					if (!p) continue;
					const size_t at = IndexIn(t);
					InsertNodes(p, position == "before" ? at : at + 1, payload, IndentOf(t), state->added, position == "before");
				}
				else if (position == "start") {
					const std::string ind = t->kids.empty() ? "\n" : IndentOf(t->kids.front().get());
					InsertNodes(t, 0, payload, ind == "\n" ? "\n\t" : ind, state->added);
				}
				else {
					size_t at = t->kids.size();
					while (at > 0 && t->kids[at - 1]->kind == Node::kText && BlankText(t->kids[at - 1]->text)) at--;
					std::string ind = "\n\t";
					for (const auto &k : t->kids) {
						if (k->kind == Node::kElement) {
							ind = IndentOf(k.get());
							break;
						}
					}
					InsertNodes(t, at, payload, ind, state->added);
					if (at == t->kids.size() - payload.size() * 2) {
						auto ws = std::make_unique<Node>();
						ws->kind = Node::kText;
						ws->text = "\n";
						ws->parent = t;
						t->kids.push_back(std::move(ws));
					}
				}
			}
			applied++;
		}
	}
	if (!applied) return false;
	Normalize(*doc);
	return true;
}

bool PatchXmlText(std::string &data, const std::string &patch, std::vector<std::string> &errors)
{
	XmlPatchRun run(data);
	if (!run.Apply(patch, errors)) return false;
	data = run.Text();
	return true;
}

namespace {

struct IniLines {
	std::vector<std::string> lines;
	std::string eol = "\r\n";
	bool endsWithEol = true;
};

IniLines SplitLines(const std::string &data)
{
	IniLines r;
	r.eol = data.find("\r\n") != std::string::npos || data.find('\n') == std::string::npos ? "\r\n" : "\n";
	size_t pos = 0;
	while (pos < data.size()) {
		size_t nl = data.find('\n', pos);
		std::string line = data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		r.lines.push_back(line);
		if (nl == std::string::npos) {
			r.endsWithEol = false;
			break;
		}
		pos = nl + 1;
	}
	return r;
}

std::string JoinLines(const IniLines &r)
{
	std::string out;
	for (size_t i = 0; i < r.lines.size(); i++) {
		out += r.lines[i];
		if (i + 1 < r.lines.size() || r.endsWithEol) out += r.eol;
	}
	return out;
}

bool IsSectionLine(const std::string &line, std::string *name = nullptr)
{
	const std::string t = Trim(line);
	if (t.size() < 2 || t[0] != '[' || t.back() != ']') return false;
	if (name) *name = Trim(t.substr(1, t.size() - 2));
	return true;
}

bool IsComment(const std::string &t)
{
	return t.empty() || t[0] == ';' || t[0] == '#';
}

bool FindSection(const IniLines &r, const std::string &name, size_t &from, size_t &to)
{
	for (size_t i = 0; i < r.lines.size(); i++) {
		std::string n;
		if (!IsSectionLine(r.lines[i], &n) || !IEq(n, name)) continue;
		from = i + 1;
		to = from;
		while (to < r.lines.size() && !IsSectionLine(r.lines[to])) to++;
		return true;
	}
	return false;
}

std::string KeyOf(const std::string &line)
{
	const std::string t = Trim(line);
	if (IsComment(t)) return "";
	const size_t eq = t.find('=');
	return Trim(eq == std::string::npos ? t : t.substr(0, eq));
}

std::string IniValueKey(const std::string &section, const std::string &key)
{
	return "[" + Lower(section) + "]" + Lower(key);
}

size_t AppendPoint(const IniLines &r, size_t from, size_t to)
{
	size_t at = to;
	while (at > from && Trim(r.lines[at - 1]).empty()) at--;
	return at;
}

}

bool PatchIniText(std::string &data, const std::string &patch, std::vector<std::string> &errors, std::set<std::string> *removed)
{
	IniLines ini = SplitLines(data);
	if (data.compare(0, 3, "\xEF\xBB\xBF") == 0 && !ini.lines.empty()) ini.lines[0].erase(0, 3);
	const bool bom = data.compare(0, 3, "\xEF\xBB\xBF") == 0;
	const IniLines pl = SplitLines(patch);
	std::string section;
	bool haveSection = false;
	int applied = 0;
	for (size_t n = 0; n < pl.lines.size(); n++) {
		std::string raw = pl.lines[n];
		if (n == 0 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);
		const std::string t = Trim(raw);
		if (IsComment(t)) continue;
		const std::string where = TrFmt("line %s: ", {std::to_string(n + 1)});
		if (IsSectionLine(t, &section)) {
			haveSection = true;
			continue;
		}
		if (!haveSection) {
			errors.push_back(where + TrFmt("missing the [Section] before \"%s\"", {t}));
			continue;
		}
		size_t from = 0, to = 0;
		const bool exists = FindSection(ini, section, from, to);
		const char op = t[0] == '-' || t[0] == '+' ? t[0] : '=';
		if (op == '-') {
			const std::string key = Trim(t.substr(1));
			// A bare "-" would match every blank and comment line of the section.
			if (key.empty()) {
				errors.push_back(where + TrFmt("there is no \"%s\" in [%s]", {key, section}));
				continue;
			}
			bool gone = false;
			if (exists) {
				for (size_t i = from; i < to;) {
					if (IEq(KeyOf(ini.lines[i]), key) || IEq(Trim(ini.lines[i]), key)) {
						ini.lines.erase(ini.lines.begin() + static_cast<std::ptrdiff_t>(i));
						to--;
						gone = true;
					}
					else {
						i++;
					}
				}
			}
			if (gone) {
				if (removed) removed->insert(IniValueKey(section, key));
				applied++;
			}
			else if (!removed || !removed->count(IniValueKey(section, key))) {
				errors.push_back(where + TrFmt("there is no \"%s\" in [%s]", {key, section}));
			}
		}
		else if (op == '+') {
			const std::string line = Trim(t.substr(1));
			if (!exists) {
				ini.lines.push_back("");
				ini.lines.push_back("[" + section + "]");
				ini.lines.push_back(line);
				applied++;
				continue;
			}
			bool present = false;
			for (size_t i = from; i < to; i++) present = present || IEq(Trim(ini.lines[i]), line);
			if (!present) {
				ini.lines.insert(ini.lines.begin() + static_cast<std::ptrdiff_t>(AppendPoint(ini, from, to)), line);
				applied++;
			}
		}
		else {
			const size_t eq = t.find('=');
			if (eq == std::string::npos) {
				errors.push_back(where + TrFmt("\"%s\" is not Key = value, -Key or +Line", {t}));
				continue;
			}
			const std::string key = Trim(t.substr(0, eq)), value = Trim(t.substr(eq + 1));
			const std::string line = key + " = " + value;
			if (!exists) {
				ini.lines.push_back("");
				ini.lines.push_back("[" + section + "]");
				ini.lines.push_back(line);
				applied++;
				continue;
			}
			bool set = false;
			for (size_t i = from; i < to; i++) {
				if (IEq(KeyOf(ini.lines[i]), key) && ini.lines[i].find('=') != std::string::npos) {
					ini.lines[i] = line;
					set = true;
					break;
				}
			}
			if (!set) ini.lines.insert(ini.lines.begin() + static_cast<std::ptrdiff_t>(AppendPoint(ini, from, to)), line);
			applied++;
		}
	}
	if (!applied) return false;
	if (!ini.endsWithEol && !ini.lines.empty()) ini.endsWithEol = true;
	data = (bom ? std::string("\xEF\xBB\xBF") : std::string()) + JoinLines(ini);
	return true;
}

namespace {

std::vector<std::string> VsSplitLines(const std::string &text)
{
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos) end = text.size();
		std::string line = text.substr(start, end - start);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(line);
		if (end == text.size()) break;
		start = end + 1;
	}
	return lines;
}

std::string VsTrim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
	return s.substr(a, b - a);
}

}

bool PatchScriptText(std::string &data, const std::string &patch, std::vector<std::string> &errors)
{
	struct Op {
		std::string kind, anchor;
		std::vector<std::string> body;
		int line;
	};
	std::vector<Op> ops;
	const std::vector<std::string> pl = VsSplitLines(patch);
	for (size_t i = 0; i < pl.size(); i++) {
		const std::string &l = pl[i];
		if (l.empty() || l[0] != '@') {
			if (!ops.empty() && !ops.back().kind.empty()) ops.back().body.push_back(l);
			continue;
		}
		size_t sp = 0;
		while (sp < l.size() && l[sp] != ' ' && l[sp] != '\t') sp++;
		const std::string word = LegacyDirective(l.substr(1, sp - 1));
		const std::string rest = VsTrim(l.substr(sp));
		if (word == "end") {
			if (!ops.empty()) ops.push_back({"", "", {}, static_cast<int>(i) + 1});
			continue;
		}
		if (word != "top" && word != "bottom" && word != "before" && word != "after" && word != "replace") {
			errors.push_back(TrFmt("line %s: the directive @%s does not exist (@top, @bottom, @before, @after, @replace, @end)", {std::to_string(i + 1), word}));
			ops.push_back({"", "", {}, static_cast<int>(i) + 1});
			continue;
		}
		if ((word == "before" || word == "after" || word == "replace") && rest.empty()) {
			errors.push_back(TrFmt("line %s: @%s needs the text of the line to look for", {std::to_string(i + 1), word}));
			ops.push_back({"", "", {}, static_cast<int>(i) + 1});
			continue;
		}
		ops.push_back({word, rest, {}, static_cast<int>(i) + 1});
	}
	const bool crlf = data.find("\r\n") != std::string::npos;
	const bool endsWithEol = !data.empty() && data.back() == '\n';
	std::vector<std::string> lines = VsSplitLines(data);
	if (endsWithEol && !lines.empty() && lines.back().empty()) lines.pop_back();
	int applied = 0;
	for (Op &op : ops) {
		if (op.kind.empty()) continue;
		while (!op.body.empty() && VsTrim(op.body.back()).empty()) op.body.pop_back();
		if (op.body.empty() && op.kind != "replace") {
			errors.push_back(TrFmt("line %s: @%s without code below", {std::to_string(op.line), op.kind}));
			continue;
		}
		if (op.kind == "bottom") {
			while (!lines.empty() && VsTrim(lines.back()).empty()) lines.pop_back();
			lines.insert(lines.end(), op.body.begin(), op.body.end());
			applied++;
			continue;
		}
		if (op.kind == "top") {
			const size_t at = !lines.empty() && VsTrim(lines[0]).compare(0, 2, "//") == 0 ? 1 : 0;
			lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), op.body.begin(), op.body.end());
			applied++;
			continue;
		}
		size_t found = lines.size();
		for (size_t i = 0; i < lines.size(); i++) {
			if (lines[i].find(op.anchor) != std::string::npos) {
				found = i;
				break;
			}
		}
		if (found == lines.size()) {
			errors.push_back(TrFmt("line %s: no line of the script contains \"%s\"", {std::to_string(op.line), op.anchor}));
			continue;
		}
		if (op.kind == "before") lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(found), op.body.begin(), op.body.end());
		else if (op.kind == "after") lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(found + 1), op.body.begin(), op.body.end());
		else {
			lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(found));
			lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(found), op.body.begin(), op.body.end());
		}
		applied++;
	}
	if (!applied) return false;
	std::string out;
	for (size_t i = 0; i < lines.size(); i++) {
		out += lines[i];
		if (i + 1 < lines.size() || endsWithEol) out += crlf ? "\r\n" : "\n";
	}
	data = out;
	return true;
}

namespace {

void XmlValues(const std::string &file, const std::string &patch, std::vector<PatchedValue> &out)
{
	std::string err;
	NodePtr pdoc = Parser(patch).Parse(err);
	if (!pdoc) return;
	const Node *root = nullptr;
	for (const auto &k : pdoc->kids) {
		if (k->kind == Node::kElement) root = k.get();
	}
	if (!root) return;
	for (const auto &op : root->kids) {
		if (op->kind != Node::kElement || Lower(op->name) != "set") continue;
		const std::string select = Attribute(*op, "select");
		std::vector<Step> steps;
		std::string selError;
		if (!ParseSelector(select, steps, selError)) continue;
		const std::string node = CanonicalSelector(steps), shown = Trim(select);
		for (const Attr &a : op->attrs) {
			const std::string name = Lower(a.name);
			if (name == "select" || name == "optional" || name == "unset") continue;
			out.push_back({file, node + "/@" + name, shown + "/@" + a.name, a.value, false});
		}
		for (const std::string &name : NameList(Attribute(*op, "unset"))) out.push_back({file, node + "/@" + Lower(name), shown + "/@" + name, "", true});
	}
}

void IniValues(const std::string &file, const std::string &patch, std::vector<PatchedValue> &out)
{
	const IniLines pl = SplitLines(patch);
	std::string section;
	bool haveSection = false;
	for (size_t n = 0; n < pl.lines.size(); n++) {
		std::string raw = pl.lines[n];
		if (n == 0 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);
		const std::string t = Trim(raw);
		if (IsComment(t)) continue;
		if (IsSectionLine(t, &section)) {
			haveSection = true;
			continue;
		}
		if (!haveSection || t[0] == '+') continue;
		const bool removed = t[0] == '-';
		const size_t eq = t.find('=');
		if (!removed && eq == std::string::npos) continue;
		const std::string key = Trim(removed ? t.substr(1) : t.substr(0, eq));
		out.push_back({file, IniValueKey(section, key), "[" + section + "] " + key, removed ? "" : Trim(t.substr(eq + 1)), removed});
	}
}

}

std::vector<PatchedValue> PatchValues(const std::string &file, const std::string &patch)
{
	std::vector<PatchedValue> out;
	const std::string name = Lower(file);
	const auto endsWith = [&](const std::string &ext) { return name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0; };
	if (endsWith(".xml")) XmlValues(name, patch, out);
	else if (endsWith(".ini")) IniValues(name, patch, out);
	return out;
}

std::vector<std::vector<PatchedValue>> PatchXmlResolve(const std::string &file, const std::string &base, const std::vector<std::string> &before,
	const std::vector<std::string> &patches)
{
	XmlPatchRun run(base);
	if (!run.Ok()) return {};
	std::vector<std::string> errors;
	for (const std::string &b : before) run.Apply(b, errors);
	std::vector<std::vector<PatchedValue>> out(patches.size());
	const std::string name = Lower(file);
	for (size_t i = 0; i < patches.size(); i++) {
		run.Apply(patches[i], errors, &out[i]);
		for (PatchedValue &v : out[i]) v.file = name;
	}
	return out;
}
