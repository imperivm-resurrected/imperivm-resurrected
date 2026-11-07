// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include "hooks/audio.h"
#include "addr/media.h"
#include "addr/state.h"
#include "core/config.h"
#include "core/log.h"
#include "hooks/hookcount.h"
#include "hooks/soundlevels.h"

#include <atomic>

namespace {

std::atomic<bool> g_background{false}, g_cover{false};

// Game sound types (CSoundManager +0xc of a channel): 2 and 8 ambient, 9 the spoken lines of the conversations.
const unsigned short kTypeAmbient = 2, kTypeAmbient2 = 8, kTypeConversation = 9;
const int kOptConversations = 7, kOptEffectsVolume = 10;

typedef void(__cdecl *ApplySoundOptions_t)(int *options);
typedef void(__fastcall *EnableType_t)(void *manager, void *edx, unsigned short type, int on);
typedef void(__fastcall *SetTypeVolume_t)(void *manager, void *edx, unsigned short type, unsigned short volume);
typedef void(__fastcall *UpdateChannelVolumes_t)(void *manager);

ApplySoundOptions_t g_origApplySound = nullptr;
int g_effectsVolume = 50;
bool g_conversationsOn = true, g_optionsSeen = false;

void ApplyLevels()
{
	void *manager = *reinterpret_cast<void **>(addr::kSoundManager);
	if (!manager || !g_optionsSeen) return;
	const Config &c = GetConfig();
	const auto volume = reinterpret_cast<SetTypeVolume_t>(addr::kSoundSetTypeVolume);
	const unsigned short nature = static_cast<unsigned short>(SoundNatureVolume(c.natureVolume, g_effectsVolume));
	volume(manager, nullptr, kTypeAmbient, nature);
	volume(manager, nullptr, kTypeAmbient2, nature);
	// The game never applies its Conversations option nor a volume to them: they always played at full volume.
	reinterpret_cast<EnableType_t>(addr::kSoundEnableType)(manager, nullptr, kTypeConversation, g_conversationsOn ? 1 : 0);
	volume(manager, nullptr, kTypeConversation, static_cast<unsigned short>(SoundTypeVolume(c.conversationVolume, 100)));
	reinterpret_cast<UpdateChannelVolumes_t>(addr::kUpdateChannelVolumes)(manager);
}

void __cdecl HookApplySoundOptions(int *options)
{
	g_origApplySound(options);
	if (!options) return;
	g_effectsVolume = options[kOptEffectsVolume] & 0xffff;
	g_conversationsOn = options[kOptConversations] != 0;
	g_optionsSeen = true;
	ApplyLevels();
}

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

void SoundLevelsApply()
{
	ApplyLevels();
}

void InstallSoundLevels(bool knownExe)
{
	if (!knownExe) return;
	void *target = reinterpret_cast<void *>(addr::kApplySoundOptions);
	if (HookCreate(target, reinterpret_cast<void *>(&HookApplySoundOptions), reinterpret_cast<void **>(&g_origApplySound)) == MH_OK &&
		HookEnable(target) == MH_OK) {
		LogD("audio", "nature sounds and conversations have their own volume");
	}
	else {
		LogE("audio", "could not hook ApplySoundOptions: nature sounds and conversations keep the game's volumes");
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
