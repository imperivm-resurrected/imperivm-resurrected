// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/volumes.h"

#include <cstring>

const VolumeName *VolumeOf(const std::string &rel)
{
	for (unsigned i = 0; i < kVolumeCount; i++) {
		const size_t n = strlen(kVolumeNames[i].name);
		if (rel.compare(0, n, kVolumeNames[i].name) == 0) return &kVolumeNames[i];
	}
	return nullptr;
}

bool VolumeCovers(const std::string &rel)
{
	if (VolumeOf(rel)) return true;
	for (unsigned i = 0; i < kVolumeCount; i++) {
		if (rel.compare(0, strlen(kVolumeNames[i].real), kVolumeNames[i].real) == 0) return true;
	}
	return false;
}

std::string VolumeRealPath(const std::string &rel)
{
	const VolumeName *v = VolumeOf(rel);
	return v ? std::string(v->real) + rel.substr(strlen(v->name)) : rel;
}
