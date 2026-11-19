// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <string>
#include <vector>

#include "ui/modui_internal.h"

using namespace modui_impl;

namespace modui_impl {

namespace {

// The code of the profile on screen, made again only when the profile changes: making it reads every installed pack.
struct CodeCache {
	std::string key, code;
};
CodeCache g_code;

const std::string &ProfileCode(const ModProfile &p)
{
	std::string key = p.name;
	for (const std::string &m : p.mods) key += "\x01" + m;
	for (const ModChoice &c : p.choices) key += "\x02" + c.use + ">" + c.over;
	for (const ModFileChoice &f : p.files) key += "\x03" + f.path + ">" + f.use;
	if (key != g_code.key) {
		g_code.key = key;
		g_code.code = ModsProfileCode(p);
	}
	return g_code.code;
}

// What the pasted code holds, read again only when its text changes: reading it looks for each pack among the installed ones.
struct ImportPreview {
	std::string text;
	bool ok = false;
	std::string error;
	ModsSharedProfile profile;
};
ImportPreview g_preview;

const ImportPreview &PreviewOf(const std::string &text)
{
	if (text != g_preview.text) {
		g_preview = ImportPreview();
		g_preview.text = text;
		g_preview.ok = ModsParseProfileCode(text, g_preview.profile, g_preview.error);
	}
	return g_preview;
}

void PaintCheck(Painter &p, int x, int cy, COLORREF c)
{
	p.Line(x, cy, x + 4, cy + 4, c, 2);
	p.Line(x + 4, cy + 4, x + 12, cy - 4, c, 2);
}

void PaintCross(Painter &p, int x, int cy, COLORREF c)
{
	p.Line(x + 1, cy - 5, x + 11, cy + 5, c, 2);
	p.Line(x + 11, cy - 5, x + 1, cy + 5, c, 2);
}

void PaintDownArrow(Painter &p, int x, int cy, COLORREF c)
{
	p.Fill(x + 5, cy - 7, 2, 8, c);
	p.ArrowDown(x, cy, 12, 6, c);
}

std::string NamesOf(const std::vector<const ModsSharedMod *> &mods)
{
	std::string out;
	for (size_t i = 0; i < mods.size() && i < 4; i++) out += (i ? ", " : "") + (mods[i]->name.empty() ? mods[i]->id : mods[i]->name);
	if (mods.size() > 4) out += TrFmt(" and %s more", {std::to_string(mods.size() - 4)});
	return out;
}

}

// Sharing the profile on screen as a code, and making a new profile from someone else's.
void PaintShareCard(Painter &p, const State &st, const PanelLayout &l)
{
	const int mx = st.mouseX, my = st.mouseY, pad = 24;
	const int x = l.detailX, y0 = l.cardTop, w = l.detailW, h = l.cardBottom - l.cardTop;
	PaintGlassCard(p, x, y0, w, h);
	const int dx = x + pad, dw = w - 2 * pad, bottom = y0 + h - pad;
	int y = y0 + pad - 4;
	p.Font(2);
	p.Text(dx, y, dw - 50, 40, "Share the profile", pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	p.Button(dx + dw - 40, y + 2, 40, 36, "\xE2\x9C\x95", kShareClose, 0, mx, my, true, false, 0);
	y += 42;
	p.Font(3);
	y += p.TextWrapped(dx, y, dw, 44, "The same packs, in the same order and with your file choices, in one code.", pal::textDim) + 18;

	PaintSectionLabel(p, dx, y, dw, 22, TrFmt("Your profile \"%s\"", {ModsProfileLabel(st.profile.name)}));
	y += 30;
	if (st.profile.mods.empty()) {
		p.Font(0);
		y += p.TextWrapped(dx, y, dw, 50, "This profile has no packs to share.", pal::textDim) + 16;
	}
	else {
		const std::string &code = ProfileCode(st.profile);
		p.Fill(dx, y, dw, 48, pal::bg);
		p.Frame(dx, y, dw, 48, pal::lineStrong);
		p.Font(3);
		const bool was = p.raw;
		p.raw = true;
		p.Text(dx + 14, y, dw - 28, 48, code, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = was;
		y += 56;
		p.Button(dx, y, dw, 40, "Copy code", kCopyCode, 0, mx, my, !code.empty());
		y += 48;
		size_t onGb = 0;
		for (const std::string &f : st.profile.mods) {
			const ModInfo *m = FindModIn(st.mods, f);
			if (m && m->gbId) onGb++;
		}
		p.Font(3);
		std::string note = TrFmt("%s packs · %s file choices", {std::to_string(st.profile.mods.size()), std::to_string(st.profile.files.size())});
		if (onGb < st.profile.mods.size()) note += "  ·  " + TrFmt("%s are not on GameBanana: whoever gets the code will have to ask you for them", {std::to_string(st.profile.mods.size() - onGb)});
		y += p.TextWrapped(dx, y, dw, 44, note, pal::caption) + 22;
	}

	PaintSectionLabel(p, dx, y, dw, 22, "Import a profile");
	y += 30;
	const int pasteW = 120;
	PaintField(p, st, dx, y, dw - pasteW - 10, kBarH, st.importText, "Paste a profile code here", st.focus == kImportField, false, kFocusImport, mx, my);
	p.Button(dx + dw - pasteW, y, pasteW, kBarH, "Paste", kPasteCode, 0, mx, my, true, false, 0);
	y += kBarH + 12;

	const int btnH = 44, btnY = bottom - btnH;
	if (!st.importProfile.empty()) {
		p.Font(0);
		p.TextWrapped(dx, y, dw, 80,
			TrFmt("Downloading the packs of \"%s\": %s left. They join the profile as they arrive.",
				{ModsProfileLabel(st.importProfile), std::to_string(st.importWant.size() + st.importLoading.size())}),
			pal::text);
		p.Button(dx, btnY, dw, btnH, "Stop waiting for them", kImportClear, 0, mx, my, true, false, 0);
		return;
	}
	if (st.importText.empty()) return;
	const ImportPreview &pv = PreviewOf(st.importText);
	if (!pv.ok) {
		p.Font(0);
		p.TextWrapped(dx, y, dw, 60, pv.error, pal::bad);
		p.Button(dx, btnY, dw, btnH, "Cancel", kImportClear, 0, mx, my, true, false, 0);
		return;
	}
	std::vector<const ModsSharedMod *> have, download, missing;
	for (const ModsSharedMod &m : pv.profile.mods) {
		if (m.installed) have.push_back(&m);
		else if (m.gbId) download.push_back(&m);
		else missing.push_back(&m);
	}
	const int boxPad = 14, iw = dw - 2 * boxPad;
	const int boxY = y, boxBottom = btnY - 12;
	p.Fill(dx, boxY, dw, boxBottom - boxY, pal::raised);
	p.Frame(dx, boxY, dw, boxBottom - boxY, pal::hairline);
	int by = boxY + boxPad;
	p.Font(1);
	p.Text(dx + boxPad, by, iw - 120, 24, pv.profile.name.empty() ? Tr("Shared profile") : pv.profile.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	p.Text(dx + boxPad, by, iw, 24, TrFmt("%s packs", {std::to_string(pv.profile.mods.size())}), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	by += 34;
	const auto line = [&](int kind, const std::string &text, COLORREF c, const std::vector<const ModsSharedMod *> &mods) {
		if (by + 26 > boxBottom - 40) return;
		const int cy = by + 13;
		if (kind == 0) PaintCheck(p, dx + boxPad, cy, c);
		else if (kind == 1) PaintDownArrow(p, dx + boxPad, cy, c);
		else PaintCross(p, dx + boxPad, cy, c);
		p.Font(0);
		p.Text(dx + boxPad + 24, by, iw - 24, 26, text, c, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		by += 28;
		if (kind != 0 && !mods.empty()) {
			p.Font(3);
			by += p.TextWrapped(dx + boxPad + 24, by, iw - 24, 40, NamesOf(mods), pal::textDim) + 6;
		}
	};
	if (!have.empty()) line(0, TrFmt("%s are already installed", {std::to_string(have.size())}), pal::ok, have);
	if (!download.empty()) line(1, TrFmt("%s download from GameBanana", {std::to_string(download.size())}), pal::text, download);
	if (!missing.empty()) {
		line(2, TrFmt("%s are not on GameBanana", {std::to_string(missing.size())}), pal::bad, missing);
		if (by + 22 < boxBottom - 40) {
			p.Font(3);
			p.Text(dx + boxPad + 24, by, iw - 24, 20, "Ask whoever shared the code for them.", pal::caption);
			by += 24;
		}
	}
	p.Fill(dx + boxPad, boxBottom - 40, iw, 1, pal::hairline);
	p.Font(3);
	p.Text(dx + boxPad, boxBottom - 38, iw, 36, "It is made as a new profile; yours does not change.", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	const int cancelW = 150;
	p.Button(dx, btnY, cancelW, btnH, "Cancel", kImportClear, 0, mx, my, true, false, 0);
	p.Button(dx + cancelW + 10, btnY, dw - cancelW - 10, btnH, download.empty() ? "Import" : "Import and download", kImportCode, 0, mx, my, !st.busy, true);
}

}
