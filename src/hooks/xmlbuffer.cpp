// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstring>

#include "addr/engine.h"
#include "core/log.h"
#include "hooks/xmlbuffer.h"

namespace {
// The loop of CXmlParser::ParseStream, as it is in the game:
//   67a226  test eax,eax        ; bytes read by the last block
//   67a228  je   67a274         ; none: return 0 (the buffer is not freed)      <- 74 4a
//   ...
//   67a269  mov ecx,[esi]       ; the allocator the buffer came from
//   67a26b  mov edx,[esi+0x24]  ; the buffer
//   67a26e  mov eax,[ecx]
//   67a270  push edx
//   67a271  call [eax+0xc]      ; free it
//   67a274  xor eax,eax / pop ... / ret 8
// The jump is made to go to the free instead (74 3f). Both exits then return 0 with the buffer freed, as the one that already frees it does.
const unsigned char kJumpBefore[2] = {0x74, 0x4a};
const unsigned char kFreeCode[11] = {0x8B, 0x0E, 0x8B, 0x56, 0x24, 0x8B, 0x01, 0x52, 0xFF, 0x50, 0x0C};
}

void InstallXmlBufferFix(bool knownExe)
{
	if (!knownExe) return;
	unsigned char *jump = reinterpret_cast<unsigned char *>(addr::kXmlParseNoBytesJump);
	const unsigned char *freeCode = reinterpret_cast<const unsigned char *>(addr::kXmlParseFreeBuffer);
	if (memcmp(jump, kJumpBefore, sizeof(kJumpBefore)) != 0 || memcmp(freeCode, kFreeCode, sizeof(kFreeCode)) != 0) {
		LogW("game", "xml buffer fix: the code of the XML reader is not the one expected; its buffers stay leaked");
		return;
	}
	// The displacement is counted from the end of the jump.
	const unsigned char displacement = static_cast<unsigned char>(addr::kXmlParseFreeBuffer - (addr::kXmlParseNoBytesJump + 2));
	DWORD old = 0;
	if (!VirtualProtect(jump, 2, PAGE_EXECUTE_READWRITE, &old)) {
		LogW("game", "xml buffer fix: could not make the code writable; its buffers stay leaked");
		return;
	}
	jump[1] = displacement;
	VirtualProtect(jump, 2, old, &old);
	FlushInstructionCache(GetCurrentProcess(), jump, 2);
	LogI("game", "xml buffer fix: the XML reader frees its read buffer when a file ends");
}
