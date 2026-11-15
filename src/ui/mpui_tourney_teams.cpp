// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "addr/state.h"
#include "core/tournament.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_tourney.h"
#include "net/steamnet.h"
#include "ui/modui.h"
#include "ui/mpui_tourney_internal.h"
#include "ui/textedit.h"

namespace mpui_tourney_impl {
namespace {

std::string Folded(const std::string &s)
{
	std::string out = s;
	for (char &c : out) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return out;
}

// The cup an invitation is for, by its name, or "a cup" when the league sends none.
std::string CupName(const std::string &name)
{
	return name.empty() ? Tr("a cup") : name;
}

}

std::string MembersText(const std::vector<TourneyMember> &members)
{
	std::vector<std::string> names;
	for (const TourneyMember &m : members) {
		if (!m.name.empty()) names.push_back(m.name);
	}
	if (names.empty()) return std::string();
	if (names.size() == 1) return names[0];
	std::string head;
	for (size_t i = 0; i + 1 < names.size(); i++) head += (i ? ", " : "") + names[i];
	return TrFmt("%s and %s", {head, names.back()});
}

std::string TeamPlayersLine(const std::string &name, const std::vector<TourneyMember> &members)
{
	if (members.size() < 2 || TourneyNameIsMembers(name, members)) return std::string();
	return MembersText(members);
}

int SideDiscsWidth(int size, size_t count)
{
	return count <= 1 ? size : size + static_cast<int>(count - 1) * size * 2 / 3;
}

int PaintSideDiscs(Painter &p, int x, int y, int size, const std::vector<TourneyMember> &members, COLORREF ring)
{
	if (members.empty()) {
		PaintPlayerDisc(p, x, y, size, 0, "?", ring);
		return size;
	}
	// Each disc after the first over the one before it, with a rim of the background between them.
	for (size_t i = 0; i < members.size(); i++) {
		const int dx = x + static_cast<int>(i) * size * 2 / 3;
		if (i) p.Shape(dx - 2, y - 2, size + 4, size + 4, InUnitEllipse, pal::panel);
		PaintPlayerDisc(p, dx, y, size, members[i].steamId, members[i].name, ring);
	}
	return SideDiscsWidth(size, members.size());
}

std::string SideCountText(const TourneySummary &t, int n)
{
	if (t.Teams()) return n == 1 ? Tr("1 team") : TrFmt("%s teams", {std::to_string(n)});
	return n == 1 ? Tr("1 player") : TrFmt("%s players", {std::to_string(n)});
}

std::string SignedUpText(const TourneySummary &t, int entries, int places)
{
	return TrFmt(t.Teams() ? "%s of %s teams signed up" : "%s of %s signed up", {std::to_string(entries), std::to_string(places)});
}

bool MemberInRoom(const State &st, int64_t match, const TourneyMember &m, bool sideIn)
{
	// From inside the room, Steam knows who is in it before the league does.
	if (st.room.id && !st.room.info.spectate && TourneyMatchOfTag(st.room.info.leagueTag) == match) {
		for (const OnlineMember &x : st.room.members) {
			if (x.id == m.steamId) return true;
		}
		return false;
	}
	if (m.inRoom >= 0) return m.inRoom != 0;
	return sideIn;
}

std::vector<SteamFriendInfo> TeamFriends(const std::string &filter)
{
	std::vector<SteamFriendInfo> out;
	const std::string want = Folded(TourneyTrim(filter));
	for (const SteamFriendInfo &f : SteamFriendsInfo()) {
		if (f.id && (want.empty() || Folded(f.name).find(want) != std::string::npos)) out.push_back(f);
	}
	std::stable_sort(out.begin(), out.end(), [](const SteamFriendInfo &a, const SteamFriendInfo &b) {
		if (a.inGame != b.inGame) return a.inGame;
		return Folded(a.name) < Folded(b.name);
	});
	return out;
}

void OpenTeamDialog(Ui &u, int64_t tournament)
{
	u.teamCup = tournament;
	u.teamPartner = 0;
	u.teamPartnerName.clear();
	u.teamName.clear();
	u.teamSearch.clear();
	u.teamError.clear();
	u.teamScroll = 0;
	FocusField(kTeamSearchField);
}

void PaintTeamDialog(Painter &p, const State &st, const Ui &u, PaintOut &out)
{
	const int mx = st.mouseX, my = st.mouseY;
	const int w = std::min(720, st.viewW - 60), h = std::min(660, st.viewH - 60);
	p.raw = true;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, w, h, Tr("Sign up my team"), pal::redText);
	const int bx = d.BodyX(), bw = d.BodyW();
	p.Font(3);
	const std::string intro = Tr("Choose your partner among your Steam friends. They have to have the game linked to the league, and they accept from the game: "
								 "until then your team takes no place.");
	const int introH = std::min(66, p.Measure(bw, intro));
	p.TextWrapped(bx, d.BodyTop(), bw, introH + 2, intro, pal::textDim);
	int y = d.BodyTop() + introH + 16;

	// The friends, with a field that narrows them down.
	const std::vector<SteamFriendInfo> friends = TeamFriends(u.teamSearch);
	PaintCaps(p, bx, y, bw / 2, 20, Tr("Partner"), pal::caption);
	if (u.teamPartner) {
		p.Font(3);
		p.Text(bx + bw / 2, y, bw / 2, 20, u.teamPartnerName, pal::ok, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	y += 26;
	PaintEditField(p, bx, y, bw, 40, u.teamSearch, Tr("Look for a friend by name"), st.focus == kTeamSearchField, false, st.edit, kFieldTeamSearch);
	y += 48;
	// From the bottom: the buttons, the team's name, and what the league answered.
	const int fieldY = d.FooterTop() - 18 - 44, labelY = fieldY - 28;
	int listBottom = labelY - 14;
	if (!u.teamError.empty()) {
		p.Font(3);
		const int eh = std::min(44, p.Measure(bw, u.teamError));
		listBottom -= eh + 10;
		p.TextWrapped(bx, labelY - 12 - eh, bw, eh + 2, u.teamError, pal::bad);
	}
	const int listY = y, listH = std::max(60, listBottom - listY), rowH = 52;
	p.Fill(bx, listY, bw, listH, pal::bg);
	p.Frame(bx, listY, bw, listH, pal::lineStrong);
	const int viewRows = std::max(1, (listH - 4) / rowH), count = static_cast<int>(friends.size());
	const int maxScroll = std::max(0, count - viewRows), scroll = std::max(0, std::min(u.teamScroll, maxScroll));
	p.Clip(bx + 1, listY + 1, bw - 2, listH - 2);
	for (int r = 0; r < viewRows && scroll + r < count; r++) {
		const SteamFriendInfo &f = friends[static_cast<size_t>(scroll + r)];
		const int ry = listY + 2 + r * rowH;
		const bool on = f.id == u.teamPartner, hot = !on && p.Hover(bx + 2, ry, bw - 16, rowH, mx, my);
		if (on) p.Fill(bx + 2, ry, bw - 4, rowH, pal::selected);
		else if (hot) p.Fill(bx + 2, ry, bw - 4, rowH, pal::raised);
		if (on) p.Fill(bx + 2, ry, 3, rowH, pal::red);
		PaintPlayerDisc(p, bx + 14, ry + 8, 36, f.id, f.name);
		const int tx = bx + 14 + 36 + 14, stateW = f.inGame ? std::min(200, bw / 3) : 0, tw = bx + bw - 24 - stateW - tx;
		UseFace(p, on ? BrandFace::UiStrong : BrandFace::Body, 17);
		p.Text(tx, ry, tw, rowH, f.name, on ? pal::text : pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (f.inGame) {
			const int sx = bx + bw - 24 - stateW;
			p.Shape(sx, ry + rowH / 2 - 4, 8, 8, InUnitEllipse, pal::ok);
			p.Font(3);
			p.Text(sx + 14, ry, stateW - 14, rowH, Tr("In the game now"), pal::ok, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		if (!on) p.AddHit(bx + 2, ry, bw - 16, rowH, kTourney, ArgOf(kSubTeamPick, scroll + r));
	}
	if (friends.empty()) {
		p.Font(0);
		const std::string empty = !st.ready ? Tr("Steam is not available. Open Steam and try again.")
			: !TourneyTrim(u.teamSearch).empty() ? TrFmt("No friend's name has \"%s\".", {TourneyTrim(u.teamSearch)})
												 : Tr("Your Steam friends show here. Add your partner as a friend on Steam first.");
		p.TextWrapped(bx + 16, listY + 16, bw - 32, listH - 32, empty, pal::textDim);
	}
	p.Unclip();
	out.teamArea.valid = true;
	out.teamArea.r = p.R(bx, listY, bw, listH);
	OffsetRect(&out.teamArea.r, p.ox, p.oy);
	out.teamArea.max = maxScroll;
	out.teamRows = friends;
	if (maxScroll > 0) PaintScrollbar(p, kBarFriends, bx + bw - 10, listY + 2, listH - 4, {scroll, maxScroll, viewRows}, st.dropBar, mx, my);

	// The team's name, optional: the league names the team after both players without one.
	const int n = TourneyTextLength(TourneyTrim(u.teamName));
	PaintCaps(p, bx, labelY, bw - 80, 20, Tr("Team name (optional)"), pal::caption);
	p.Font(3);
	p.Text(bx + bw - 80, labelY, 80, 20, std::to_string(n) + "/" + std::to_string(kTourneyTeamNameMax), n > kTourneyTeamNameMax ? pal::bad : pal::caption,
		DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	// A team without a name is named after its players, as the cup's views show it.
	const std::string placeholder = u.teamPartner ? TrFmt("%s and %s", {st.player, u.teamPartnerName}) : Tr("Without one, your two names");
	PaintEditField(p, bx, fieldY, bw, 44, u.teamName, placeholder, st.focus == kTeamNameField, false, st.edit, kFieldTeamName);
	const bool busy = LeagueTourneyBusy(TourneyOp::SignUp, u.teamCup);
	DialogButtons(p, d,
		{{Tr(busy ? "Sending..." : "Send the invitation"), kTourney, ArgOf(kSubTeamSend, 0), true, !busy && u.teamPartner != 0 && LeagueLinked()},
			{Tr("Cancel"), kTourney, ArgOf(kSubTeamCancel, 0)}},
		mx, my);
	p.raw = false;
}

std::string InviteText(const TourneyInvite &i)
{
	const std::string from = i.from.name.empty() ? Tr("A friend") : i.from.name;
	if (!i.teamName.empty()) return TrFmt("%s invites you to their team %s for %s.", {from, "\xC2\xAB" + i.teamName + "\xC2\xBB", CupName(i.name)});
	return TrFmt("%s invites you to their team for %s.", {from, CupName(i.name)});
}

void AskInvite(const TourneyNotice &n, bool ask)
{
	TourneyInvite i;
	i.tournamentId = n.tournamentId;
	i.entryId = n.entry;
	i.name = n.name;
	i.teamName = n.teamName;
	i.from = n.from;
	const std::string line = InviteText(i);
	// In a room or a game, single player too, a dialog would be in the way.
	const bool playing = *reinterpret_cast<void *const *>(addr::kGame) != nullptr;
	if (!ask || g_s.room.id || g_s.inOnlineGame || playing) {
		ModUiToast(line + " " + Tr("Answer it in Multiplayer, Tournaments."));
		return;
	}
	const int64_t tournament = n.tournamentId, entry = n.entry;
	// The dialog has room for three lines.
	const std::string text = line + " " + Tr("You can also answer later in Multiplayer, Tournaments.");
	ModUiChoose(Tr("Invitation to a team"), text, "Accept", [tournament, entry] { LeagueTourneyAnswerInvite(tournament, entry, true); }, "Decline",
		[tournament, entry] { LeagueTourneyAnswerInvite(tournament, entry, false); });
}

std::string PendingText(const std::string &partner)
{
	return partner.empty() ? Tr("Waiting for your partner") : Tr("Waiting for your partner") + " (" + partner + ")";
}

}
