// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include "hooks/audio.h"
#include "core/config.h"
#include "core/log.h"

#include <atomic>

namespace {

std::atomic<bool> g_background{false}, g_cover{false};

void ApplyMute()
{
	const bool mute = g_background || g_cover;
	HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	IMMDeviceEnumerator *enumerator = nullptr;
	IMMDevice *device = nullptr;
	IAudioSessionManager *manager = nullptr;
	ISimpleAudioVolume *volume = nullptr;

	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
			reinterpret_cast<void **>(&enumerator))) &&
		SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
		SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr,
			reinterpret_cast<void **>(&manager))) &&
		SUCCEEDED(manager->GetSimpleAudioVolume(nullptr, FALSE, &volume))) {
		volume->SetMute(mute ? TRUE : FALSE, nullptr);
	}
	else {
		LogW("audio", "sound: could not access the process audio session");
	}
	if (volume) volume->Release();
	if (manager) manager->Release();
	if (device) device->Release();
	if (enumerator) enumerator->Release();
	if (SUCCEEDED(init)) {
		CoUninitialize();
	}
}

}

void AudioSetBackgroundMute(bool mute)
{
	g_background = mute;
	ApplyMute();
}

void AudioSetCoverMute(bool mute)
{
	if (g_cover.exchange(mute) != mute) ApplyMute();
}
