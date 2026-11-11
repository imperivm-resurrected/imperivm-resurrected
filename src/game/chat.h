// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "game/chatlog.h"

// The game's in-match chat, without its dialog: the mod opens its own bar on the same key, sends the game's own chat command
// (so players without the mod read it as always) and shows the chat lines and the game's own messages (game saved…) in its
// chat instead of the game's message feed.
void InstallChat(bool knownExe);
// The hooks are in place: when false, the game's own dialog and feed work as always.
bool ChatFloating();

struct ChatPlayer {
	int index = -1;
	std::string name;
	uint32_t rgb = 0;
	bool enemy = false;
	bool human = false;
};

struct ChatMatch {
	// A match on screen that the player can write in: not a replay and not the editor. The open zoom map counts.
	bool playing = false;
	// A match or a replay on screen, where received lines are shown.
	bool onScreen = false;
	// The local player is still in the match: not defeated and has not surrendered.
	bool alive = false;
	bool severalHumans = false;
	int local = -1;
	unsigned enemies = 0;
	// Every other player, human or computer: a line to a computer player is harmless and Tab always has somewhere to go.
	std::vector<ChatPlayer> others;
	bool hasAllies = false;
	uint32_t clockMs = 0;
	bool haveView = false;
	int viewLeft = 0, viewTop = 0, viewRight = 0, viewBottom = 0;
	bool haveBar = false;
	int barLeft = 0, barTop = 0, barRight = 0, barBottom = 0;
};
ChatMatch ChatReadMatch();

// Called by the game when it would open its chat dialog; true when the mod opened its bar instead.
typedef bool (*ChatOpenFn)(bool withLocation);
void ChatSetOpenHandler(ChatOpenFn fn);

// Text and target as typed; withLocation adds the centre of the camera, as the dialog's "Send location". False if nothing was sent.
bool ChatSend(const std::string &text, ChatTarget target, bool withLocation);
// An empty chat line with a location: the point under the mouse (map or open zoom map), else the centre of the camera. The
// sender also gets the type's notification, which the game itself only gives to the others. how says what asked for it, for
// the log; type is a ChatPingType.
bool ChatPing(int mouseX, int mouseY, bool haveMouse, const char *how, int type, ChatTarget target);
// Whether (x, y) is on the map itself: inside the big map's picture when it is open, else on the map view and not on a bar or
// window of the game. area is the part of the screen around it that is map, to keep a menu inside; why says what is there.
bool ChatPointOnMap(int x, int y, int *area, const char *&why);
// A line in the chat for the local player only.
void ChatAddLocal(const std::string &text);
void ChatAddLocalEntry(ChatEntry e);
// In a match with the floating chat, a notice of the mod (a player dropped, rejoined…) goes to the chat as one of the game's
// messages, translated already. False out of a match: the caller shows it as before.
bool ChatAddGameNotice(const std::string &text, bool error);

uint32_t ChatLogSerial();
std::vector<ChatEntry> ChatLogLines();
void ChatGameTick();
