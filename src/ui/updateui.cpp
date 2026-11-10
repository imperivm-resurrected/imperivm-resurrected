// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "ui/modui.h"
#include "mods/updater.h"
#include "ui/updateui.h"
#include "ui/uikit.h"
#include "ui/widgets.h"
#include "i18n/tr.h"

using namespace ui;

namespace {

enum Action { kNone = 0, kClose, kInstall, kRestart };

std::mutex g_lock;
bool g_open = false;
bool g_prompted = false;
bool g_promptedReady = false;
DWORD g_cornerTick = 0;
RECT g_icon = {};
RECT g_panel = {};
int g_mx = -1, g_my = -1;
bool g_swallowUp = false, g_escArmed = false;
std::vector<Hit> g_hits;
int g_tabAction = -1;

const int kNotesMaxH = 260;
const int kNotesBarW = 8;
const int kNotesWheelStep = 48;

struct NotesScroll {
	std::string version;
	float scroll = 0, maxScroll = 0, scale = 1;
	RECT view = {};
	ScrollDrag bar;
} g_notes;

const int kNotesBar = 0;

void ScrollNotesTo(float value) { g_notes.scroll = std::max(0.0f, std::min(g_notes.maxScroll, value)); }

void ReplaceAll(std::string &text, const std::string &from, const std::string &to)
{
	for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
}

enum NoteStyle { kNoteText, kNoteBold, kNoteCode, kNoteHeading };
enum NoteBlockKind { kBlockParagraph, kBlockBullet, kBlockHeading };

struct NotePiece {
	std::wstring text;
	int style;
};

struct NoteBlock {
	int kind;
	std::vector<std::vector<NotePiece>> words;
};

struct NoteWord {
	int x, y, style;
	std::wstring text;
};

struct NotesLayout {
	std::vector<NoteWord> words;
	std::vector<int> bullets;
	int lineH = 0, height = 0;
};

const int kNotesIndent = 16;

void AddInline(NoteBlock &block, const std::string &line, bool heading)
{
	const std::wstring ws = W(line);
	bool bold = false, code = false, newWord = true;
	std::wstring cur;
	auto style = [&]() { return heading ? kNoteHeading : code ? kNoteCode : bold ? kNoteBold : kNoteText; };
	auto flush = [&]() {
		if (cur.empty()) return;
		if (newWord || block.words.empty()) block.words.push_back({});
		block.words.back().push_back({cur, style()});
		cur.clear();
		newWord = false;
	};
	for (size_t i = 0; i < ws.size(); i++) {
		if (!code && ws.compare(i, 2, L"**") == 0) {
			flush();
			bold = !bold;
			i++;
		}
		else if (ws[i] == L'`') {
			flush();
			code = !code;
		}
		else if (ws[i] == L' ') {
			flush();
			newWord = true;
		}
		else cur += ws[i];
	}
	flush();
}

std::vector<NoteBlock> ParseNotes(const std::string &text)
{
	std::vector<NoteBlock> blocks;
	bool open = false;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos) end = text.size();
		std::string line = text.substr(start, end - start);
		start = end + 1;
		const size_t lead = line.find_first_not_of(' ');
		if (lead == std::string::npos) {
			open = false;
			continue;
		}
		line = line.substr(lead);
		if (line[0] == '#') {
			blocks.push_back({kBlockHeading, {}});
			AddInline(blocks.back(), line.substr(std::min(line.size(), line.find_first_not_of('#'))), true);
			open = false;
		}
		else if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
			blocks.push_back({kBlockBullet, {}});
			AddInline(blocks.back(), line.substr(2), false);
			open = true;
		}
		else {
			if (!open) blocks.push_back({kBlockParagraph, {}});
			AddInline(blocks.back(), line, false);
			open = true;
		}
	}
	return blocks;
}

int NoteFont(int style) { return style == kNoteText || style == kNoteCode ? 3 : 4; }

COLORREF NoteColor(int style) { return style == kNoteText ? kDim : style == kNoteCode ? RGB(232, 192, 104) : kText; }

NotesLayout LayoutNotes(Painter &p, const std::vector<NoteBlock> &blocks, int width)
{
	NotesLayout out;
	TEXTMETRICW tm = {};
	p.Font(3);
	GetTextMetricsW(p.dc, &tm);
	out.lineH = static_cast<int>(tm.tmHeight / p.s + 0.99f) + 2;
	auto widthOf = [&](const std::wstring &s, int style) {
		p.Font(NoteFont(style));
		SIZE sz = {};
		GetTextExtentPoint32W(p.dc, s.c_str(), static_cast<int>(s.size()), &sz);
		return static_cast<int>(sz.cx / p.s + 0.99f);
	};
	const int space = widthOf(L" ", kNoteText);
	int y = 0;
	for (size_t b = 0; b < blocks.size(); b++) {
		const NoteBlock &block = blocks[b];
		if (b > 0) y += block.kind == kBlockHeading ? 10 : block.kind == kBlockBullet && blocks[b - 1].kind == kBlockBullet ? 2 : 6;
		const int indent = block.kind == kBlockBullet ? kNotesIndent : 0;
		if (block.kind == kBlockBullet) out.bullets.push_back(y);
		int x = indent;
		for (const std::vector<NotePiece> &word : block.words) {
			int w = 0;
			for (const NotePiece &piece : word) w += widthOf(piece.text, piece.style);
			if (x > indent && x + w > width) {
				y += out.lineH;
				x = indent;
			}
			for (const NotePiece &piece : word) {
				out.words.push_back({x, y, piece.style, piece.text});
				x += widthOf(piece.text, piece.style);
			}
			x += space;
		}
		y += out.lineH;
	}
	out.height = y;
	return out;
}

std::string NotesForPlayer(const std::string &raw)
{
	std::string text = raw;
	const size_t comment = text.find("<!--");
	if (comment != std::string::npos) {
		const size_t close = text.find("-->", comment);
		if (close != std::string::npos) text.erase(comment, close + 3 - comment);
	}
	size_t heading = text.find(Tr("## English"));
	if (heading == std::string::npos) heading = text.find("## English");
	if (heading != std::string::npos) {
		const size_t body = text.find('\n', heading);
		const size_t next = body == std::string::npos ? std::string::npos : text.find("\n## ", body);
		text = body == std::string::npos ? std::string() : text.substr(body + 1, next == std::string::npos ? std::string::npos : next - body - 1);
	}
	ReplaceAll(text, "\r", "");
	const size_t first = text.find_first_not_of(" \n"), last = text.find_last_not_of(" \n");
	return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

void TabStep(bool back)
{
	const FocusItem next = FocusStep(FocusOrder(g_hits, [](int a) { return a == kNone; }), {g_tabAction, 0}, back);
	if (next.action >= 0) g_tabAction = next.action;
}
Canvas g_panelCanvas;

const int kPanelW = 560;

struct IconMasks {
	int size = 0;
	std::vector<uint8_t> circle, arrow;
};

const IconMasks &Masks(int size)
{
	static IconMasks m;
	if (m.size == size) return m;
	m.size = size;
	m.circle.assign(static_cast<size_t>(size) * size, 0);
	m.arrow.assign(static_cast<size_t>(size) * size, 0);
	for (int py = 0; py < size; py++) {
		for (int px = 0; px < size; px++) {
			int c = 0, a = 0;
			for (int sy = 0; sy < 4; sy++) {
				for (int sx = 0; sx < 4; sx++) {
					const float x = (px + (sx + 0.5f) / 4) / size, y = (py + (sy + 0.5f) / 4) / size;
					const float dx = x - 0.5f, dy = y - 0.5f;
					if (dx * dx + dy * dy <= 0.25f) c++;
					const bool stem = x > 0.43f && x < 0.57f && y > 0.2f && y < 0.56f;
					const bool head = y >= 0.46f && y < 0.7f && std::fabs(x - 0.5f) < (0.7f - y) * 1.05f;
					const bool base = y > 0.75f && y < 0.82f && x > 0.29f && x < 0.71f;
					if (stem || head || base) a++;
				}
			}
			m.circle[static_cast<size_t>(py) * size + px] = static_cast<uint8_t>(c * 255 / 16);
			m.arrow[static_cast<size_t>(py) * size + px] = static_cast<uint8_t>(a * 255 / 16);
		}
	}
	return m;
}

void Paint(uint32_t *buf, int w, int h, int left, int top, const std::vector<uint8_t> &mask, int size, uint32_t color, int alphaMax)
{
	for (int py = 0; py < size; py++) {
		for (int px = 0; px < size; px++) {
			const unsigned a = mask[static_cast<size_t>(py) * size + px] * alphaMax / 255;
			if (!a) continue;
			const int x = left + px, y = top + py;
			if (x < 0 || x >= w || y < 0 || y >= h) continue;
			uint32_t &dst = buf[static_cast<size_t>(y) * w + x];
			const uint32_t rb = ((color & 0xff00ff) * a + (dst & 0xff00ff) * (255 - a)) >> 8;
			const uint32_t g = ((color & 0x00ff00) * a + (dst & 0x00ff00) * (255 - a)) >> 8;
			dst = 0xff000000u | (rb & 0xff00ff) | (g & 0x00ff00);
		}
	}
}

std::string HoverText(const UpdaterInfo &i)
{
	switch (i.state) {
	case UpdaterInfo::kAvailable: return TrFmt("Update to %s", {i.newVersion});
	case UpdaterInfo::kDownloading: return TrFmt("Downloading %s...", {i.newVersion}) + " " + std::to_string(i.percent) + " %";
	case UpdaterInfo::kReady: return TrFmt("Version %s installed: restart", {i.newVersion});
	case UpdaterInfo::kFailed: return "Couldn't update: retry";
	default: return "";
	}
}

}

void UpdateUiDrawCorner(uint32_t *buf, int w, int h, const RECT &heart)
{
	const UpdaterInfo info = UpdaterGetInfo();
	std::lock_guard<std::mutex> lock(g_lock);
	g_cornerTick = GetTickCount();
	if (info.state == UpdaterInfo::kNone) {
		g_icon = {};
		return;
	}
	const int size = heart.right - heart.left;
	const int gap = std::max(8, size / 4);
	g_icon = {heart.left, heart.top - gap - size, heart.right, heart.top - gap};
	const bool hover = !g_open && g_mx >= g_icon.left && g_mx < g_icon.right && g_my >= g_icon.top && g_my < g_icon.bottom;
	const IconMasks &m = Masks(size);
	const int shadow = std::max(1, size / 24);
	Paint(buf, w, h, g_icon.left + shadow, g_icon.top + shadow, m.circle, size, 0x000000u, 130);
	Paint(buf, w, h, g_icon.left, g_icon.top, m.circle, size, hover ? 0xFFE9A0u : 0xE8C068u, 240);
	Paint(buf, w, h, g_icon.left, g_icon.top, m.arrow, size, 0x2A1F12u, 255);
	if (info.state == UpdaterInfo::kAvailable || info.state == UpdaterInfo::kReady) {
		const int d = std::max(6, size / 4), cx = g_icon.right - d / 2, cy = g_icon.top + d / 2;
		for (int y = -d / 2; y <= d / 2; y++) {
			for (int x = -d / 2; x <= d / 2; x++) {
				if (x * x + y * y > (d / 2) * (d / 2)) continue;
				const int px = cx + x, py = cy + y;
				if (px >= 0 && px < w && py >= 0 && py < h) buf[static_cast<size_t>(py) * w + px] = 0xffE74C3Cu;
			}
		}
	}
	if (hover) ModUiDrawHoverCard(buf, w, h, HoverText(info), RGB(232, 192, 104), g_icon.right + size / 3, (g_icon.top + g_icon.bottom) / 2);
}

void UpdateUiDrawPanel(uint32_t *buf, int w, int h)
{
	const UpdaterInfo info = UpdaterGetInfo();
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_cornerTick = GetTickCount();
		if (!g_open && info.state == UpdaterInfo::kAvailable && !g_prompted) {
			g_open = true;
			g_prompted = true;
			LogI("update", "new version window (%s)", info.newVersion.c_str());
		}
		if (!g_open && info.state == UpdaterInfo::kReady && !g_promptedReady) {
			g_open = true;
			g_promptedReady = true;
		}
		if (info.state == UpdaterInfo::kNone) g_open = false;
		if (!g_open) return;
	}
	std::string title, line, notes;
	struct Btn {
		const char *label;
		int action;
		bool enabled, strong;
	};
	std::vector<Btn> buttons;
	switch (info.state) {
	case UpdaterInfo::kAvailable:
		title = "New version available";
		line = TrFmt("You have Imperivm Resurrected %s. Version %s is now available.", {info.current, info.newVersion});
		notes = info.notes;
		buttons = {{"Not now", kClose, true, false}, {"Refresh", kInstall, true, true}};
		break;
	case UpdaterInfo::kDownloading:
		title = "Updating";
		line = TrFmt("Downloading %s... You can close this window: it keeps going in the background.", {info.newVersion});
		buttons = {{"Hide", kClose, true, false}};
		break;
	case UpdaterInfo::kReady:
		title = TrFmt("Version %s installed", {info.newVersion});
		line = "Restart the game to use it. If you'd rather keep playing, it will be used the next time you open the game.";
		buttons = {{"Later", kClose, true, false}, {"Restart now", kRestart, true, true}};
		break;
	case UpdaterInfo::kFailed:
		title = "Couldn't update";
		line = info.error.empty() ? "Something went wrong while installing the new version." : TrFmt("Reason: %s.", {info.error});
		buttons = {{"Close", kClose, true, false}, {"Retry", kInstall, true, true}};
		break;
	default: return;
	}
	notes = NotesForPlayer(notes);
	const int x = 28, w2 = kPanelW - 56;
	const float sw = std::max(0.6f, std::min(1.6f, (w - 24) / static_cast<float>(kPanelW)));
	EnsureFonts(sw);
	int lineH, notesH = 0, viewH = 0, contentH = 0;
	NotesLayout layout;
	{
		HDC measure = CreateCompatibleDC(nullptr);
		Painter pm{measure, sw, 0, 0, nullptr};
		std::copy(std::begin(g_fonts), std::end(g_fonts), std::begin(pm.fonts));
		pm.Font(3);
		lineH = pm.Measure(w2, line) + 6;
		if (!notes.empty()) {
			const std::vector<NoteBlock> blocks = ParseNotes(notes);
			layout = LayoutNotes(pm, blocks, w2);
			if (layout.height > kNotesMaxH) layout = LayoutNotes(pm, blocks, w2 - kNotesBarW - 10);
			contentH = layout.height;
			viewH = std::min(contentH, kNotesMaxH);
			notesH = 30 + viewH + 8;
		}
		DeleteDC(measure);
	}
	float scroll = 0;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_notes.version != info.newVersion) {
			g_notes = NotesScroll();
			g_notes.version = info.newVersion;
		}
		g_notes.maxScroll = static_cast<float>(std::max(0, contentH - viewH));
		ScrollNotesTo(g_notes.scroll);
		scroll = g_notes.scroll;
	}
	const int barH = info.state == UpdaterInfo::kDownloading ? 34 : 0;
	const int H = 24 + 40 + lineH + notesH + barH + 80;
	const float s = std::max(0.6f, std::min(sw, (h - 24) / static_cast<float>(H)));
	PanelRender r(g_panelCanvas, buf, w, h);
	if (!r.Begin(kPanelW, H, 24, true, s)) return;
	r.Focus(g_tabAction, 0);
	Painter &p = r.p;
	const int ox = r.ox, oy = r.oy, pw = r.pw, ph = r.ph;
	int mx, my;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		mx = g_mx;
		my = g_my;
	}
	PaintWindowFrame(p, kPanelW, H);
	int y = 20;
	PaintTitle(p, x, y, w2, title);
	y += 40;
	p.Font(3);
	p.TextWrapped(x, y, w2, lineH, line, info.state == UpdaterInfo::kFailed ? kBad : kText);
	y += lineH;
	if (!notes.empty()) {
		p.Font(1);
		p.Text(x, y + 4, w2, 22, "What's new", kText);
		p.Fill(x, y + 28, w2, 1, pal::lineDim);
		p.Font(3);
		const int top = y + 34, offset = static_cast<int>(scroll);
		p.Clip(x, top, w2, viewH);
		for (int by : layout.bullets) {
			const int dot = std::max(4, layout.lineH / 4);
			p.Ellipse(x + 4, top - offset + by + (layout.lineH - dot) / 2, dot, dot, RGB(214, 170, 84));
		}
		for (const NoteWord &word : layout.words) {
			if (word.y + layout.lineH < offset || word.y > offset + viewH) continue;
			p.Font(NoteFont(word.style));
			p.LineW(x + word.x, top - offset + word.y, w2, layout.lineH, word.text, NoteColor(word.style));
		}
		p.Unclip();
		if (contentH > viewH) {
			ScrollDrag bar;
			{
				std::lock_guard<std::mutex> lock(g_lock);
				bar = g_notes.bar;
			}
			ScrollbarLook look;
			look.width = kNotesBarW;
			look.minThumb = 28;
			look.track = RGB(30, 24, 16);
			look.thumb = RGB(150, 118, 58);
			PaintScrollbar(p, kNotesBar, x + w2 - kNotesBarW, top, viewH, {offset, contentH - viewH, viewH}, bar, mx, my, look);
		}
		RECT view = p.R(x, top, w2, viewH);
		OffsetRect(&view, ox, oy);
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_notes.view = view;
			g_notes.scale = p.s;
		}
		y += notesH;
	}
	if (barH) {
		PaintProgress(p, x, y + 6, w2, 12, info.percent / 100.0f);
		p.Font(3);
		p.Text(x, y + 18, w2, 16, std::to_string(info.percent) + " %", kDim, DT_RIGHT | DT_TOP | DT_SINGLELINE);
		y += barH;
	}
	int bx = kPanelW - 28;
	for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
		const int bw = it->strong ? 170 : 130;
		bx -= bw;
		p.Button(bx, H - 60, bw, 38, it->label, it->action, 0, mx, my, it->enabled, it->strong);
		bx -= 12;
	}
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_hits = std::move(r.hits);
	g_panel = {ox, oy, ox + pw, oy + ph};
}

bool UpdateUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	std::unique_lock<std::mutex> lock(g_lock);
	if (msg == WM_MOUSEMOVE) {
		g_mx = static_cast<short>(LOWORD(lp));
		g_my = static_cast<short>(HIWORD(lp));
		ScrollTo to;
		if (g_open && ScrollbarMouse(g_notes.bar, g_hits, msg, wp, {g_mx, g_my}, to)) {
			ScrollNotesTo(static_cast<float>(to.pos));
			return true;
		}
	}
	if (GetTickCount() - g_cornerTick >= 300) return false;
	if (!g_open) {
		if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
			const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
			if (g_icon.right > g_icon.left && PtInRect(&g_icon, pt)) {
				g_open = true;
				g_tabAction = -1;
				g_swallowUp = true;
				return true;
			}
		}
		if (msg == WM_LBUTTONUP && g_swallowUp) {
			g_swallowUp = false;
			return true;
		}
		return false;
	}
	auto perform = [&](int action) {
		if (action == kClose) {
			g_open = false;
		}
		else if (action == kInstall) {
			lock.unlock();
			UpdaterStartInstall();
		}
		else if (action == kRestart) {
			g_open = false;
			lock.unlock();
			UpdaterRestartNow();
		}
	};
	switch (msg) {
	case WM_MOUSEMOVE:
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		g_tabAction = -1;
		ScrollTo to;
		if (ScrollbarMouse(g_notes.bar, g_hits, msg, wp, pt, to)) {
			if (to.id == kNotesBar) ScrollNotesTo(static_cast<float>(to.pos));
			g_swallowUp = true;
			return true;
		}
		int action = kNone;
		for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it) {
			if (PtInRect(&it->r, pt)) {
				action = it->action;
				break;
			}
		}
		if (action == kNone && !PtInRect(&g_panel, pt)) action = kClose;
		g_swallowUp = true;
		perform(action);
		return true;
	}
	case WM_KEYDOWN:
		if (g_notes.maxScroll > 0 && (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT || wp == VK_HOME || wp == VK_END)) {
			const float page = static_cast<float>(kNotesMaxH - kNotesWheelStep);
			if (wp == VK_UP) ScrollNotesTo(g_notes.scroll - kNotesWheelStep);
			else if (wp == VK_DOWN) ScrollNotesTo(g_notes.scroll + kNotesWheelStep);
			else if (wp == VK_PRIOR) ScrollNotesTo(g_notes.scroll - page);
			else if (wp == VK_NEXT) ScrollNotesTo(g_notes.scroll + page);
			else ScrollNotesTo(wp == VK_HOME ? 0.0f : g_notes.maxScroll);
			return true;
		}
		if (wp == VK_ESCAPE) g_escArmed = true;
		else if (wp == VK_TAB) TabStep((GetKeyState(VK_SHIFT) & 0x8000) != 0);
		else if (g_tabAction >= 0 && !(lp & (1 << 30)) && (wp == VK_RETURN || wp == VK_SPACE)) perform(g_tabAction);
		return true;
	case WM_KEYUP:
		if (wp == VK_ESCAPE && g_escArmed) {
			g_escArmed = false;
			g_open = false;
		}
		return true;
	case WM_LBUTTONUP:
		g_swallowUp = false;
		g_notes.bar = ScrollDrag();
		return true;
	case WM_MOUSEWHEEL:
		if (g_notes.maxScroll > 0) {
			const POINT pt = {g_mx, g_my};
			if (PtInRect(&g_panel, pt)) ScrollNotesTo(g_notes.scroll - GET_WHEEL_DELTA_WPARAM(wp) * kNotesWheelStep / static_cast<float>(WHEEL_DELTA));
		}
		return true;
	case WM_CHAR:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
		return true;
	default:
		return false;
	}
}
