// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>

#include "addr/soundcache.h"
#include "addr/state.h"
#include "core/crashlog.h"
#include "game/soundreload.h"

namespace {
typedef void *(__cdecl *GetPtrPool_t)();
typedef void(__fastcall *Free_t)(void *pool, void *edx, void *block);
typedef void(__fastcall *DeleteEntity_t)(void *entity, void *edx, int flags);
typedef void(__fastcall *LoadEntities_t)(void *manager);

const unsigned kCountOffset = 0x1b0, kTableOffset = 0x1b4;
const unsigned kPoolFreeSlot = 3, kDeletingDestructorSlot = 0;
}

bool ReloadSoundEntities(unsigned *before, unsigned *after, bool *active)
{
	bool ok = false;
	*before = *after = 0;
	*active = false;
	CRITICAL_SECTION *dmLock = reinterpret_cast<CRITICAL_SECTION *>(addr::kDirectMusicLock);
	CRITICAL_SECTION *soundLock = reinterpret_cast<CRITICAL_SECTION *>(addr::kSoundManagerLock);
	volatile bool locked = false;
	g_crashLogQuiet++;
	__try {
		char *manager = *reinterpret_cast<char **>(addr::kSoundManager);
		if (!manager) {
			ok = true;
		}
		else {
			void **table = *reinterpret_cast<void ***>(manager + kTableOffset);
			*before = *reinterpret_cast<unsigned *>(manager + kCountOffset);
			if (!table) {
				// No sound in this session: the manager has no table, and PlaySound returns at once.
				ok = true;
			}
			else {
				*active = true;
				// The order every sound function takes them in (the notification thread takes the first, then the second).
				EnterCriticalSection(dmLock);
				EnterCriticalSection(soundLock);
				locked = true;
				// What ~CSoundManager does with the table: each entity is deleted through its own destructor, then the table goes back to the pool.
				for (unsigned i = 0; i < *before; i++) {
					void *entity = table[i];
					if (entity) reinterpret_cast<DeleteEntity_t>((*reinterpret_cast<void ***>(entity))[kDeletingDestructorSlot])(entity, nullptr, 1);
				}
				void *pool = reinterpret_cast<GetPtrPool_t>(addr::kGetPtrPool)();
				reinterpret_cast<Free_t>((*reinterpret_cast<void ***>(pool))[kPoolFreeSlot])(pool, nullptr, table);
				*reinterpret_cast<void **>(manager + kTableOffset) = nullptr;
				*reinterpret_cast<unsigned *>(manager + kCountOffset) = 0;
				reinterpret_cast<LoadEntities_t>(addr::kLoadSoundEntities)(manager);
				*after = *reinterpret_cast<unsigned *>(manager + kCountOffset);
				// The table is made again (and the same size, plus or minus what the ruleset adds): an empty one would silence the game.
				ok = *reinterpret_cast<void **>(manager + kTableOffset) != nullptr && *after > 0;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
	}
	if (locked) {
		LeaveCriticalSection(soundLock);
		LeaveCriticalSection(dmLock);
	}
	g_crashLogQuiet--;
	return ok;
}
