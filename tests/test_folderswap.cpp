// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <windows.h>

#include <string>

#include "mods/folderswap.h"

static void MakeFolder(const std::string &dir, const std::string &marker)
{
	CreateDirectoryA(dir.c_str(), nullptr);
	HANDLE h = CreateFileA((dir + "\\" + marker).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static bool Has(const std::string &dir, const std::string &marker)
{
	return GetFileAttributesA((dir + "\\" + marker).c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool Exists(const std::string &path)
{
	return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static void NewFolder(const std::string &root)
{
	const std::string incoming = root + "\\new-a", target = root + "\\a";
	MakeFolder(incoming, "new.txt");
	unsigned long error = 1;
	CHECK(SwapInFolder(incoming, target, root + "\\backup-a", error) == FolderSwap::kMoved);
	CHECK(Has(target, "new.txt"));
	CHECK(!Exists(incoming));
	CHECK(!Exists(root + "\\backup-a"));
	CHECK_EQ(error, 0ul);
}

static void ReplacesAndKeepsBackup(const std::string &root)
{
	const std::string incoming = root + "\\new-b", target = root + "\\b", backup = root + "\\backup-b";
	MakeFolder(target, "old.txt");
	MakeFolder(incoming, "new.txt");
	unsigned long error = 0;
	CHECK(SwapInFolder(incoming, target, backup, error) == FolderSwap::kMoved);
	CHECK(Has(target, "new.txt"));
	CHECK(!Has(target, "old.txt"));
	CHECK(Has(backup, "old.txt"));
}

static void PutsTheOldFolderBack(const std::string &root)
{
	const std::string target = root + "\\c", backup = root + "\\backup-c";
	MakeFolder(target, "old.txt");
	unsigned long error = 0;
	CHECK(SwapInFolder(root + "\\missing", target, backup, error) == FolderSwap::kFailed);
	CHECK(Has(target, "old.txt"));
	CHECK(!Exists(backup));
	CHECK(error != 0);
}

static void TargetThatCannotMove(const std::string &root)
{
	const std::string incoming = root + "\\new-d", target = root + "\\d";
	MakeFolder(target, "old.txt");
	MakeFolder(incoming, "new.txt");
	unsigned long error = 0;
	CHECK(SwapInFolder(incoming, target, root + "\\no-such-folder\\backup-d", error) == FolderSwap::kTargetBusy);
	CHECK(Has(target, "old.txt"));
	CHECK(Has(incoming, "new.txt"));
	CHECK(error != 0);
}

int main()
{
	const std::string root = test::TempDir("folderswap");
	NewFolder(root);
	ReplacesAndKeepsBackup(root);
	PutsTheOldFolderBack(root);
	TargetThatCannotMove(root);
	return test::Summary("folderswap");
}
