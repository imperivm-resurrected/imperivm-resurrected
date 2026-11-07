// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/roomcountdown.h"

#include <cstddef>

namespace {

bool Reached(uint32_t now, uint32_t when)
{
	return static_cast<int32_t>(now - when) >= 0;
}

int FirstDifference(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b)
{
	const size_t n = a.size() < b.size() ? a.size() : b.size();
	for (size_t i = 0; i < n; i++) {
		if (a[i] != b[i]) return static_cast<int>(i);
	}
	return a.size() == b.size() ? -1 : static_cast<int>(n);
}

CountdownStep Step(CountdownEvent event, CountdownReason reason, int part = -1)
{
	CountdownStep s;
	s.event = event;
	s.reason = reason;
	s.part = part;
	return s;
}

}

CountdownStep RoomCountdown::HostStep(const RoomCountdownInput &in, uint32_t now, bool autoStart)
{
	int changed = -1;
	if (haveParts_) {
		changed = FirstDifference(parts_, in.parts);
		if (changed >= 0) {
			changedAt_ = now;
			changedSeen_ = true;
		}
	}
	if (!haveParts_ || changed >= 0) {
		parts_ = in.parts;
		haveParts_ = true;
	}
	if (!in.allReady) held_ = false;
	if (counting_) {
		CountdownReason why = CountdownReason::None;
		if (!in.allReady) why = CountdownReason::NotReady;
		else if (in.blocked) why = CountdownReason::Blocked;
		else if (changed >= 0) why = CountdownReason::Changed;
		else if (!autoStart) why = CountdownReason::Host;
		if (why != CountdownReason::None) {
			counting_ = false;
			seq_++;
			return Step(CountdownEvent::Cancel, why, why == CountdownReason::Changed ? changed : -1);
		}
		if (!Reached(now, deadline_)) return {};
		counting_ = false;
		held_ = true;
		return Step(CountdownEvent::Fire, CountdownReason::None);
	}
	if (held_ || !autoStart || !in.allReady || in.blocked) return {};
	if (changedSeen_ && !Reached(now, changedAt_ + kRoomCountdownSettleMs)) return {};
	counting_ = true;
	deadline_ = now + kRoomCountdownMs;
	seq_++;
	return Step(CountdownEvent::Start, CountdownReason::None);
}

CountdownStep RoomCountdown::HostCancel()
{
	held_ = true;
	if (!counting_) return {};
	counting_ = false;
	seq_++;
	return Step(CountdownEvent::Cancel, CountdownReason::Host);
}

bool RoomCountdown::RemoteStart(uint32_t seq, int ms, uint32_t now)
{
	if (static_cast<int32_t>(seq - seq_) <= 0) return false;
	seq_ = seq;
	counting_ = true;
	deadline_ = now + static_cast<uint32_t>(ms > 0 ? ms : 0);
	return true;
}

bool RoomCountdown::RemoteCancel(uint32_t seq)
{
	if (static_cast<int32_t>(seq - seq_) < 0) return false;
	seq_ = seq;
	const bool was = counting_;
	counting_ = false;
	return was;
}

CountdownStep RoomCountdown::GuestStep(bool localReady, uint32_t now)
{
	if (!counting_) return {};
	if (!localReady) {
		counting_ = false;
		return Step(CountdownEvent::Cancel, CountdownReason::NotReady);
	}
	if (Reached(now, deadline_ + kRoomCountdownGraceMs)) {
		counting_ = false;
		return Step(CountdownEvent::Cancel, CountdownReason::Remote);
	}
	return {};
}

void RoomCountdown::Reset()
{
	*this = RoomCountdown();
}

int RoomCountdown::LeftMs(uint32_t now) const
{
	if (!counting_) return -1;
	const int32_t left = static_cast<int32_t>(deadline_ - now);
	return left > 0 ? left : 0;
}
