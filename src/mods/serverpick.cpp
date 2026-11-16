// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/serverpick.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/json.h"
#include "mods/modtext.h"

namespace {
const char kPrefix[] = "filecache";
const char kSuffix[] = ".gamebanana.com";

uint64_t Fnv1a(const std::string &s)
{
	uint64_t h = 14695981039346656037ull;
	for (unsigned char c : s) {
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

using modtext::Lower;

}

int GbFileServerNumber(const std::string &host)
{
	const std::string h = Lower(host);
	const size_t pre = sizeof(kPrefix) - 1, suf = sizeof(kSuffix) - 1;
	if (h.size() <= pre + suf || h.compare(0, pre, kPrefix) != 0 || h.compare(h.size() - suf, suf, kSuffix) != 0) return -1;
	const std::string digits = h.substr(pre, h.size() - pre - suf);
	if (digits.empty() || digits.size() > 3) return -1;
	int n = 0;
	for (char c : digits) {
		if (!isdigit(static_cast<unsigned char>(c))) return -1;
		n = n * 10 + (c - '0');
	}
	return n;
}

bool IsGbFileServer(const std::string &host) { return GbFileServerNumber(host) >= 0; }

std::string GbFileServerHost(int number) { return kPrefix + std::to_string(number) + kSuffix; }

std::vector<FileServer> ParseFileservers(const Json &response)
{
	std::vector<FileServer> out;
	const Json *records = response.Get("_aRecords");
	if (!records) return out;
	for (const Json &r : records->arr) {
		if (r.type != Json::Object || r.GetString("_sState") != "up") continue;
		FileServer s;
		s.host = Lower(r.GetString("_sDomain")) + kSuffix;
		if (!IsGbFileServer(s.host)) continue;
		const Json *stats = r.Get("_aStats");
		const Json *hour = stats ? stats->Get("_a1hr") : nullptr;
		s.weight = hour ? std::max(1.0, hour->GetNumber("_fRate")) : 1.0;
		out.push_back(s);
	}
	return out;
}

std::vector<std::string> RendezvousOrder(const std::string &path, const std::vector<FileServer> &servers)
{
	std::vector<std::pair<double, std::string>> scored;
	for (const FileServer &s : servers) {
		const std::string host = Lower(s.host);
		if (std::find_if(scored.begin(), scored.end(), [&](const auto &p) { return p.second == host; }) != scored.end()) continue;
		const double unit = (static_cast<double>(Fnv1a(path + "|" + host) >> 11) + 0.5) / 9007199254740992.0;
		scored.push_back({-std::max(1.0, s.weight) / std::log(unit), host});
	}
	std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
	std::vector<std::string> out;
	for (const auto &p : scored) out.push_back(p.second);
	return out;
}

std::vector<double> RememberRate(std::vector<double> recent, double bestThisDownload)
{
	if (bestThisDownload <= 0) return recent;
	recent.push_back(bestThisDownload);
	if (recent.size() > kRecentDownloads) recent.erase(recent.begin(), recent.end() - kRecentDownloads);
	return recent;
}

double ConnectionRate(const std::vector<double> &recent)
{
	double best = 0;
	for (double r : recent) best = std::max(best, r);
	return best;
}

ServerPicker::ServerPicker(const std::string &assigned, double connectionRate)
	: m_current(Lower(assigned)), m_best(Lower(assigned)), m_connection(connectionRate)
{
}

void ServerPicker::SetCandidates(const std::vector<std::string> &ordered)
{
	m_candidates.clear();
	for (const std::string &s : ordered) {
		const std::string host = Lower(s);
		if (host != m_current && host != m_best) m_candidates.push_back(host);
	}
	m_next = 0;
	m_known = true;
}

bool ServerPicker::ShouldTry(double rate) const { return m_connection <= 0 || rate < m_connection * kDropped; }

bool ServerPicker::WantsCandidates(double rate) const
{
	if (m_known) return false;
	if (m_state == kMeasuring) return ShouldTry(rate);
	return m_state == kSettled && rate < m_bestRate * kDropped;
}

bool ServerPicker::NextCandidate(std::string &out)
{
	while (m_next < m_candidates.size()) {
		out = m_candidates[m_next++];
		if (out != m_best) return true;
	}
	return false;
}

ServerPicker::Step ServerPicker::Try()
{
	std::string next;
	if (m_tries < kMaxTries && NextCandidate(next)) {
		m_current = next;
		m_tries++;
		m_state = kTesting;
		return kTry;
	}
	m_current = m_best;
	m_state = kSettled;
	return kStay;
}

bool ServerPicker::RateWorthRemembering() const
{
	if (m_fastest <= 0) return false;
	return m_measured.size() >= 2 || (m_connection > 0 && m_fastest >= m_connection * kDropped);
}

ServerPicker::Step ServerPicker::OnWindow(double rate)
{
	m_fastest = std::max(m_fastest, rate);
	if (std::find(m_measured.begin(), m_measured.end(), m_current) == m_measured.end()) m_measured.push_back(m_current);
	switch (m_state) {
	case kMeasuring:
		m_best = m_current;
		m_bestRate = rate;
		if (ShouldTry(rate)) return Try();
		m_state = kSettled;
		return kStay;
	case kTesting:
		if (rate >= m_bestRate * kClearlyFaster) {
			m_best = m_current;
			m_bestRate = rate;
			if (ShouldTry(rate) && Try() == kTry) return kTry;
			m_current = m_best;
			m_state = kSettled;
			return kKeepNew;
		}
		m_current = m_best;
		m_state = kSettled;
		if (m_connection > 0 && ShouldTry(m_bestRate) && Try() == kTry) return kTry;
		return kBack;
	case kSettled:
	default:
		if (rate > m_bestRate) {
			m_bestRate = rate;
			return kStay;
		}
		if (rate < m_bestRate * kDropped) {
			m_bestRate = rate;
			return Try();
		}
		return kStay;
	}
}

void ServerPicker::NoteRate(const std::string &server, double rate)
{
	if (rate <= 0) return;
	m_fastest = std::max(m_fastest, rate);
	const std::string host = Lower(server);
	if (std::find(m_measured.begin(), m_measured.end(), host) == m_measured.end()) m_measured.push_back(host);
}

ServerPicker::Step ServerPicker::OnFailure()
{
	if (m_state == kTesting) return Try() == kTry ? kTry : kBack;
	m_bestRate = 0;
	return Try();
}
