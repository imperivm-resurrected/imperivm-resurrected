// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>

#include "core/buildid.h"

namespace {

void BuildsCompareByTheirHash()
{
	const std::string a(64, 'a'), b(64, 'b');
	CHECK(CompareBuilds(a, a) == BuildMatch::kSame);
	CHECK(CompareBuilds(a, b) == BuildMatch::kDifferent);
	// A build from before the check announces nothing; a malformed one counts the same.
	CHECK(CompareBuilds(a, "") == BuildMatch::kMissing);
	CHECK(CompareBuilds(a, a.substr(1)) == BuildMatch::kMissing);
	CHECK(CompareBuilds(a, std::string(64, 'A')) == BuildMatch::kMissing);
	CHECK(CompareBuilds(a, std::string(63, 'a') + "g") == BuildMatch::kMissing);
	CHECK(BuildShaValid("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
	CHECK_EQ(BuildShortId("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"), std::string("0123456789ab"));
}

void TheHostJudgesEachMember()
{
	const std::string mine(64, 'a'), other(64, 'b');
	CHECK(JudgeMemberBuild(mine, mine, 0, 15000) == BuildVerdict::kAdmit);
	CHECK(JudgeMemberBuild(mine, other, 0, 15000) == BuildVerdict::kRefuse);
	// The announcement arrives a moment after the member enters; a build from before the check never sends one.
	CHECK(JudgeMemberBuild(mine, "", 3000, 15000) == BuildVerdict::kWait);
	CHECK(JudgeMemberBuild(mine, "", 15000, 15000) == BuildVerdict::kRefuse);
	// A host that could not read its own DLL turns nobody away.
	CHECK(JudgeMemberBuild("", other, 60000, 15000) == BuildVerdict::kAdmit);
}

void AMemberWhoComesBackWaitsAfresh()
{
	ArrivalClock clock;
	CHECK_EQ(clock.Waited(7, 1000), 0u);
	CHECK_EQ(clock.Waited(7, 21000), 20000u);
	CHECK_EQ(clock.Waited(8, 21000), 0u);
	// 7 leaves; when it enters again its announcement is new, and so is its wait.
	clock.KeepOnly({8});
	CHECK_EQ(clock.Waited(7, 40000), 0u);
	CHECK_EQ(clock.Waited(8, 40000), 19000u);
	// The host hears that 8 entered again: its wait starts afresh.
	clock.Forget(8);
	CHECK_EQ(clock.Waited(8, 50000), 0u);
	// The tick count wraps after 49 days.
	ArrivalClock wrap;
	wrap.Waited(1, 0xFFFFF000u);
	CHECK_EQ(wrap.Waited(1, 0x1000u), 0x2000u);
}

void IdListsAddAndTakeOut()
{
	CHECK(!IdListHas("", 7));
	CHECK_EQ(IdListWith("", 7), std::string(";7;"));
	CHECK_EQ(IdListWith(";7;", 7), std::string(";7;"));
	CHECK_EQ(IdListWith(";7;", 76561198000000001ULL), std::string(";7;76561198000000001;"));
	CHECK(IdListHas(";7;8;", 8));
	CHECK(!IdListHas(";17;", 7));
	CHECK_EQ(IdListWithout(";7;8;", 7), std::string(";8;"));
	CHECK_EQ(IdListWithout(";7;8;", 8), std::string(";7;"));
	CHECK_EQ(IdListWithout(";7;", 7), std::string());
	CHECK_EQ(IdListWithout(";17;", 7), std::string(";17;"));
	CHECK_EQ(IdListWith(";7;", 0), std::string(";7;"));
}

void TheRunningBuildHasAHash()
{
	// The test runs from an .exe, so the module is the test itself: its hash is read all the same.
	CHECK(BuildShaValid(BuildSha256()));
	CHECK(&BuildSha256() == &BuildSha256());
}

}

int main()
{
	BuildsCompareByTheirHash();
	TheHostJudgesEachMember();
	AMemberWhoComesBackWaitsAfresh();
	IdListsAddAndTakeOut();
	TheRunningBuildHasAHash();
	return test::Summary("buildid");
}
