/*
	Copyright (C) 2023 Benoit Hauquier and the "Aleph One" developers.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	This license is contained in the file "COPYING",
	which is included with this source code; it is available online at
	http://www.gnu.org/licenses/gpl.html
*/

#include "OpenALManager.h"
#include "Logging.h"
#include <AL/efx.h>

// EFX is optional: audio continues normally when a device has no effect slots.
static LPALGENEFFECTS sprintathon_alGenEffects = nullptr;
static LPALDELETEEFFECTS sprintathon_alDeleteEffects = nullptr;
static LPALEFFECTI sprintathon_alEffecti = nullptr;
static LPALEFFECTF sprintathon_alEffectf = nullptr;
static LPALGENAUXILIARYEFFECTSLOTS sprintathon_alGenAuxiliaryEffectSlots = nullptr;
static LPALDELETEAUXILIARYEFFECTSLOTS sprintathon_alDeleteAuxiliaryEffectSlots = nullptr;
static LPALAUXILIARYEFFECTSLOTI sprintathon_alAuxiliaryEffectSloti = nullptr;
static LPALAUXILIARYEFFECTSLOTF sprintathon_alAuxiliaryEffectSlotf = nullptr;

LPALCLOOPBACKOPENDEVICESOFT OpenALManager::alcLoopbackOpenDeviceSOFT;
LPALCISRENDERFORMATSUPPORTEDSOFT OpenALManager::alcIsRenderFormatSupportedSOFT;
LPALCRENDERSAMPLESSOFT OpenALManager::alcRenderSamplesSOFT;
LPALGETSTRINGISOFT OpenALManager::alGetStringiSOFT;
LPALGENFILTERS OpenALManager::alGenFilters;
LPALDELETEFILTERS OpenALManager::alDeleteFilters;
LPALFILTERF OpenALManager::alFilterf;
LPALFILTERI OpenALManager::alFilteri;

OpenALManager* OpenALManager::instance = nullptr;

bool OpenALManager::Init(const AudioParameters& parameters) {

	if (instance) { //Don't bother recreating all the OpenAL context if nothing changed for it
		if (parameters.hrtf != instance->audio_parameters.hrtf || parameters.rate != instance->audio_parameters.rate
			|| parameters.channel_type != instance->audio_parameters.channel_type || parameters.sample_frame_size != instance->audio_parameters.sample_frame_size) {

			Shutdown();

		} else {
			instance->UpdateParameters(parameters);
			return true;
		}
	} else {
		if (alcIsExtensionPresent(NULL, "ALC_SOFT_loopback")) {
#define LOAD_PROC(T, x)  ((x) = (T)alGetProcAddress(#x))
			LOAD_PROC(LPALCLOOPBACKOPENDEVICESOFT, alcLoopbackOpenDeviceSOFT);
			LOAD_PROC(LPALCISRENDERFORMATSUPPORTEDSOFT, alcIsRenderFormatSupportedSOFT);
			LOAD_PROC(LPALCRENDERSAMPLESSOFT, alcRenderSamplesSOFT);
			LOAD_PROC(LPALGETSTRINGISOFT, alGetStringiSOFT);
			LOAD_PROC(LPALGENFILTERS, alGenFilters);
			LOAD_PROC(LPALDELETEFILTERS, alDeleteFilters);
			LOAD_PROC(LPALFILTERI, alFilteri);
			LOAD_PROC(LPALFILTERF, alFilterf);
#undef LOAD_PROC
		} else {
			logError("ALC_SOFT_loopback extension is not supported"); //Should never be the case as long as >= OpenAL 1.14
			return false;
		}
	}

	instance = new OpenALManager(parameters);
	return instance->OpenDevice() && instance->LoadOptionalExtensions() && instance->GenerateSources() && instance->GenerateEffects();
}

bool OpenALManager::LoadOptionalExtensions() {

	for (const auto& extension : mapping_extensions_names)
		extension_support[extension.first] = alIsExtensionPresent(extension.second.c_str());

	return true;
}

void OpenALManager::ProcessAudioQueue() {

	std::shared_ptr<AudioPlayer> audioPlayer;
	while (audio_players_shared.pop(audioPlayer)) {
		audio_players_queue.push_back(audioPlayer);
	}

	UpdateListener();
	UpdateCavernEcho();
	for (int i = 0; i < audio_players_queue.size(); i++) {

		auto audio = audio_players_queue.front();
		const bool mustStillPlay = !audio->stop_signal && audio->AssignSource() && audio->Update() && audio->Play();

		audio_players_queue.pop_front();

		if (!mustStillPlay) {
			RetrieveSource(audio);
			continue;
		}

		audio_players_queue.push_back(audio); //We have just processed a part of the data for you, now wait your next turn
	}
}

//we update our listener's position for 3D sounds
void OpenALManager::UpdateListener() {

	if (!audio_parameters.sounds_3d) return;

	const auto& listener = listener_location.Get();

	const auto yaw = listener.yaw * angleConvert;
	const auto pitch = listener.pitch * angleConvert;

	ALfloat u = std::cos(degreToRadian * yaw) * std::cos(degreToRadian * pitch);
	ALfloat	v = std::sin(degreToRadian * yaw) * std::cos(degreToRadian * pitch);
	ALfloat	w = std::sin(degreToRadian * pitch);

	//OpenAL uses the same coordinate system as OpenGL, so we have to swap Z <-> Y
	ALfloat vectorDirection[] = { u, w, v, 0, 1, 0 };

	ALfloat position[] = { (float)(listener.point.x) / WORLD_ONE,
						   (float)(listener.point.z) / WORLD_ONE,
						   (float)(listener.point.y) / WORLD_ONE };

	ALfloat velocity[] = { (float)listener.velocity.i / WORLD_ONE,
						   (float)listener.velocity.k / WORLD_ONE,
						   (float)listener.velocity.j / WORLD_ONE };

	alListenerfv(AL_ORIENTATION, vectorDirection);
	alListenerfv(AL_POSITION, position);
	alListenerfv(AL_VELOCITY, velocity);
}

void OpenALManager::SetMasterVolume(float volume) {
	volume = std::min(1.f, std::max(volume, 0.f));
	if (master_volume == volume) return;
	master_volume = volume;
	ResyncPlayers();
}

void OpenALManager::SetMusicVolume(float volume) {
	volume = std::min(10.f, std::max(volume, 0.f));
	if (music_volume == volume) return;
	music_volume = volume;
	ResyncPlayers(true);
}

void OpenALManager::ResyncPlayers(bool music_players_only) {
	SDL_LockAudio();

	for (auto& player : audio_players_queue) 
	{
		if (!music_players_only || std::dynamic_pointer_cast<MusicPlayer>(player) != nullptr)
		{
			player->is_sync_with_al_parameters = false;
		}
	}

	SDL_UnlockAudio();
}

void OpenALManager::Start() {
	SDL_PauseAudio(is_using_recording_device); //Start playing only if not recording playback
	process_audio_active = SDL_GetAudioStatus() != SDL_AUDIO_STOPPED;
}

void OpenALManager::Pause(bool paused) {
	if (!process_audio_active || paused_audio == paused) return;

	paused_audio = paused;
	SDL_PauseAudio(paused_audio);
	elapsed_pause_time = machine_tick_count() - elapsed_pause_time;
}

void OpenALManager::Stop() {
	SDL_PauseAudio(true);
	StopAllPlayers();
	process_audio_active = false;
}

void OpenALManager::ToggleDeviceMode(bool recording_device) {
	is_using_recording_device = recording_device;
	SDL_PauseAudio(is_using_recording_device);
}

std::shared_ptr<SoundPlayer> OpenALManager::PlaySound(const Sound& sound, const SoundParameters& parameters) {
	if (!process_audio_active) return std::shared_ptr<SoundPlayer>();
	auto soundPlayer = std::make_shared<SoundPlayer>(sound, parameters);
	audio_players_shared.push(soundPlayer);
	return soundPlayer;
}

std::shared_ptr<MusicPlayer> OpenALManager::PlayMusic(std::vector<MusicPlayer::Sequence>& sequences, uint32_t starting_sequence_index, uint32_t starting_segment_index, const MusicParameters& parameters) {
	if (!process_audio_active) return std::shared_ptr<MusicPlayer>();
	auto musicPlayer = std::make_shared<MusicPlayer>(sequences, starting_sequence_index, starting_segment_index, parameters);
	audio_players_shared.push(musicPlayer);
	return musicPlayer;
}

//Used for video playback
std::shared_ptr<StreamPlayer> OpenALManager::PlayStream(CallBackStreamPlayer callback, uint32_t rate, bool stereo, AudioFormat audioFormat, void* userdata) {
	if (!process_audio_active) return std::shared_ptr<StreamPlayer>();
	auto streamPlayer = std::make_shared<StreamPlayer>(callback, rate, stereo, audioFormat, userdata);
	audio_players_shared.push(streamPlayer);
	return streamPlayer;
}

//It's not a good idea generating dynamically a new source for each player
//It's slow so it's better having a pool, also we already know the max amount
//of supported simultaneous playing sources for the device
std::unique_ptr<AudioPlayer::AudioSource> OpenALManager::PickAvailableSource(const AudioPlayer& audioPlayer) {
	if (sources_pool.empty()) {
		const auto& victimPlayer = *std::min_element(audio_players_queue.begin(), audio_players_queue.end(),
			[](const std::shared_ptr<AudioPlayer>& a, const std::shared_ptr<AudioPlayer>& b)
			{  return a->audio_source && a->GetPriority() < b->GetPriority(); });

		return victimPlayer->GetPriority() < audioPlayer.GetPriority() ? victimPlayer->RetrieveSource() : nullptr;
	}

	auto source = std::move(sources_pool.front());
	sources_pool.pop();
	return source;
}

void OpenALManager::StopAllPlayers() {
	SDL_LockAudio();

	for (auto& audioPlayer : audio_players_queue) {
		RetrieveSource(audioPlayer);
	}

	audio_players_queue.clear();

	std::shared_ptr<AudioPlayer> audioPlayer;
	while (audio_players_shared.pop(audioPlayer)) {
		audioPlayer->is_active = false;
	}

	SDL_UnlockAudio();
}

void OpenALManager::RetrieveSource(const std::shared_ptr<AudioPlayer>& player) {
	auto audioSource = player->RetrieveSource();
	if (audioSource) sources_pool.push(std::move(audioSource));
	player->is_active = false;
}

//this is used with the recording device and this allows OpenAL to
//not output the audio once it has mixed it but instead, makes 
//the mixed data available with alcRenderSamplesSOFT
void OpenALManager::GetPlayBackAudio(uint8* data, int length) {
	ProcessAudioQueue();
	alcRenderSamplesSOFT(p_ALCDevice, data, length);
}

OpenALManager::HrtfSupport OpenALManager::GetHrtfSupport() const {
	ALCint hrtfStatus;
	alcGetIntegerv(p_ALCDevice, ALC_HRTF_STATUS_SOFT, 1, &hrtfStatus);

	switch (hrtfStatus) {
		case ALC_HRTF_DENIED_SOFT:
		case ALC_HRTF_UNSUPPORTED_FORMAT_SOFT:
			return HrtfSupport::Unsupported;
		case ALC_HRTF_REQUIRED_SOFT:
			return HrtfSupport::Required;
		default:
			return HrtfSupport::Supported;
	}
}

bool OpenALManager::IsHrtfEnabled() const {
	ALCint hrtfStatus;
	alcGetIntegerv(p_ALCDevice, ALC_HRTF_SOFT, 1, &hrtfStatus);
	return hrtfStatus;
}

bool OpenALManager::OpenDevice() {
	if (p_ALCDevice) return true;

	p_ALCDevice = alcLoopbackOpenDeviceSOFT(nullptr);
	if (!p_ALCDevice) {
		logError("Could not open audio loopback device");
		return false;
	}

	if (!openal_rendering_format) {
		openal_rendering_format = GetBestOpenALSupportedFormat();
	}

	if (openal_rendering_format) {
		const bool has_efx = alcIsExtensionPresent(p_ALCDevice, "ALC_EXT_EFX") == ALC_TRUE;
		ALCint attrs[] = {
			ALC_FORMAT_TYPE_SOFT,     openal_rendering_format,
			ALC_FORMAT_CHANNELS_SOFT, mapping_sdl_openal_channel.at(audio_parameters.channel_type),
			ALC_FREQUENCY,            static_cast<ALCint>(audio_parameters.rate),
			ALC_HRTF_SOFT,            audio_parameters.hrtf,
			has_efx ? ALC_MAX_AUXILIARY_SENDS : 0, has_efx ? 1 : 0,
			0,
		};

		p_ALCContext = alcCreateContext(p_ALCDevice, attrs);
		if (!p_ALCContext) {
			logError("Could not create audio context from loopback device");
			return false;
		}

		if (!alcMakeContextCurrent(p_ALCContext)) {
			logError("Could not make audio context from loopback device current");
			return false;
		}

		return true;
	}

	return false;
}

bool OpenALManager::CloseDevice() {
	if (!alcMakeContextCurrent(nullptr)) {
		logError("Could not remove current audio context");
		return false;
	}

	if (p_ALCContext) {
		alcDestroyContext(p_ALCContext);
		p_ALCContext = nullptr;
	}

	if (p_ALCDevice) {
		if (!alcCloseDevice(p_ALCDevice)) {
			logError("Could not close audio device");
			return false;
		}

		p_ALCDevice = nullptr;
	}

	return true;
}

bool OpenALManager::GenerateEffects() {
	alGenFilters(1, &low_pass_filter);
	alFilteri(low_pass_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
	alFilterf(low_pass_filter, AL_LOWPASS_GAIN, 1.f);
	alFilterf(low_pass_filter, AL_LOWPASS_GAINHF, 1.f);
	if (alGetError() != AL_NO_ERROR) return false;
	GenerateCavernEcho();
	return true;
}

void OpenALManager::GenerateCavernEcho() {
	if (alcIsExtensionPresent(p_ALCDevice, "ALC_EXT_EFX") != ALC_TRUE) return;
	sprintathon_alGenEffects = reinterpret_cast<LPALGENEFFECTS>(alGetProcAddress("alGenEffects"));
	sprintathon_alDeleteEffects = reinterpret_cast<LPALDELETEEFFECTS>(alGetProcAddress("alDeleteEffects"));
	sprintathon_alEffecti = reinterpret_cast<LPALEFFECTI>(alGetProcAddress("alEffecti"));
	sprintathon_alEffectf = reinterpret_cast<LPALEFFECTF>(alGetProcAddress("alEffectf"));
	sprintathon_alGenAuxiliaryEffectSlots = reinterpret_cast<LPALGENAUXILIARYEFFECTSLOTS>(alGetProcAddress("alGenAuxiliaryEffectSlots"));
	sprintathon_alDeleteAuxiliaryEffectSlots = reinterpret_cast<LPALDELETEAUXILIARYEFFECTSLOTS>(alGetProcAddress("alDeleteAuxiliaryEffectSlots"));
	sprintathon_alAuxiliaryEffectSloti = reinterpret_cast<LPALAUXILIARYEFFECTSLOTI>(alGetProcAddress("alAuxiliaryEffectSloti"));
	sprintathon_alAuxiliaryEffectSlotf = reinterpret_cast<LPALAUXILIARYEFFECTSLOTF>(alGetProcAddress("alAuxiliaryEffectSlotf"));
	if (!sprintathon_alGenEffects || !sprintathon_alDeleteEffects ||
	    !sprintathon_alEffecti || !sprintathon_alEffectf ||
	    !sprintathon_alGenAuxiliaryEffectSlots ||
	    !sprintathon_alDeleteAuxiliaryEffectSlots ||
	    !sprintathon_alAuxiliaryEffectSloti || !sprintathon_alAuxiliaryEffectSlotf) return;
	sprintathon_alGenEffects(1, &cavern_echo_effect);
	if (alGetError() != AL_NO_ERROR) { cavern_echo_effect = 0; return; }
	sprintathon_alEffecti(cavern_echo_effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
	sprintathon_alEffectf(cavern_echo_effect, AL_REVERB_DECAY_TIME, 2.1f);
	sprintathon_alEffectf(cavern_echo_effect, AL_REVERB_GAIN, 0.25f);
	sprintathon_alEffectf(cavern_echo_effect, AL_REVERB_REFLECTIONS_GAIN, 0.18f);
	sprintathon_alEffectf(cavern_echo_effect, AL_REVERB_LATE_REVERB_GAIN, 1.25f);
	if (alGetError() != AL_NO_ERROR) { DeleteCavernEcho(); return; }
	sprintathon_alGenAuxiliaryEffectSlots(1, &cavern_echo_slot);
	if (alGetError() != AL_NO_ERROR) { cavern_echo_slot = 0; DeleteCavernEcho(); return; }
	sprintathon_alAuxiliaryEffectSloti(cavern_echo_slot, AL_EFFECTSLOT_EFFECT, cavern_echo_effect);
	sprintathon_alAuxiliaryEffectSlotf(cavern_echo_slot, AL_EFFECTSLOT_GAIN, 0.f);
	if (alGetError() != AL_NO_ERROR) DeleteCavernEcho();
}

void OpenALManager::UpdateCavernEcho() {
	if (!cavern_echo_slot) return;
	const float target = cavern_echo_target.load() * 0.65f;
	if (std::abs(target - cavern_echo_current) < 0.002f) return;
	cavern_echo_current += (target - cavern_echo_current) * 0.04f;
	sprintathon_alAuxiliaryEffectSlotf(cavern_echo_slot, AL_EFFECTSLOT_GAIN, cavern_echo_current);
	alGetError(); // optional effect must not interrupt sound playback
}

void OpenALManager::AttachCavernEcho(ALuint source, bool in_world) const {
	if (cavern_echo_slot) {
		alSource3i(source, AL_AUXILIARY_SEND_FILTER,
			in_world ? cavern_echo_slot : AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
		alGetError(); // a device may reject sends for some source formats
	}
}

void OpenALManager::DeleteCavernEcho() {
	if (cavern_echo_slot) {
		sprintathon_alDeleteAuxiliaryEffectSlots(1, &cavern_echo_slot);
		cavern_echo_slot = 0;
	}
	if (cavern_echo_effect) {
		sprintathon_alDeleteEffects(1, &cavern_echo_effect);
		cavern_echo_effect = 0;
	}
}

ALuint OpenALManager::GetLowPassFilter(float highFrequencyGain) const {
	alFilterf(low_pass_filter, AL_LOWPASS_GAINHF, highFrequencyGain);
	return low_pass_filter;
}

bool OpenALManager::GenerateSources() {

	/* how many simultaneous sources are supported on this device ? */
	int monoSources, stereoSources;
	alcGetIntegerv(p_ALCDevice, ALC_MONO_SOURCES, 1, &monoSources);
	alcGetIntegerv(p_ALCDevice, ALC_STEREO_SOURCES, 1, &stereoSources);
	int nbSources = monoSources + stereoSources;

	std::vector<ALuint> sources_id(nbSources);
	alGenSources(nbSources, sources_id.data());
	for (auto source_id : sources_id) {

		alSourcei(source_id, AL_BUFFER, 0);
		alSourceRewind(source_id);

		if (alGetError() != AL_NO_ERROR) {
			logError("Could not set source parameters: [source id: %d] [number of sources: %d]", source_id, nbSources);
			return false;
		}

		AudioPlayer::AudioSource audioSource;
		audioSource.source_id = source_id;
		ALuint buffers_id[num_buffers];
		alGenBuffers(num_buffers, buffers_id);
		if (alGetError() != AL_NO_ERROR) {
			logError("Could not create source buffers: [source id: %d] [number of sources: %d]", source_id, nbSources);
			return false;
		}

		for (int i = 0; i < num_buffers; i++) {
			audioSource.buffers.insert({ buffers_id[i], false });
		}

		sources_pool.push(std::make_unique<AudioPlayer::AudioSource>(audioSource));
	}

	return !sources_id.empty();
}

OpenALManager::OpenALManager(const AudioParameters& parameters) {
	UpdateParameters(parameters);
	alListener3i(AL_POSITION, 0, 0, 0);

	auto openalFormat = GetBestOpenALSupportedFormat();
	assert(openalFormat && "Audio format not found or not supported");
	SDL_AudioSpec desired = {};
	desired.freq = parameters.rate;
	desired.format = mapping_openal_sdl_format.at(openalFormat);
	desired.channels = static_cast<int>(parameters.channel_type);
	desired.samples = parameters.sample_frame_size;
	desired.callback = MixerCallback;
	desired.userdata = reinterpret_cast<void*>(this);

	if (SDL_OpenAudio(&desired, &sdl_audio_specs_obtained) < 0) {
		CleanEverything();
	} else {
		audio_parameters.rate = sdl_audio_specs_obtained.freq;
		audio_parameters.channel_type = static_cast<ChannelType>(sdl_audio_specs_obtained.channels);
		openal_rendering_format = mapping_sdl_openal_format.at(sdl_audio_specs_obtained.format);
		// Allocate outside the audio callback. Keep one delay tap per output channel.
		const size_t delay_frames = static_cast<size_t>(std::max(1, sdl_audio_specs_obtained.freq * 28 / 100));
		cavern_echo_delay.resize(delay_frames * sdl_audio_specs_obtained.channels, 0.f);
		cavern_echo_damping.resize(sdl_audio_specs_obtained.channels, 0.f);
	}
}

void OpenALManager::MixerCallback(void* usr, uint8* stream, int len) {
	auto manager = (OpenALManager*)usr;
	int frameSize = manager->sdl_audio_specs_obtained.channels * SDL_AUDIO_BITSIZE(manager->sdl_audio_specs_obtained.format) / 8;
	manager->GetPlayBackAudio(stream, len / frameSize);
	manager->MixCavernEcho(stream, len / frameSize);
}

// A delayed reflection mixed after OpenAL's loopback render also works on
// devices without EFX. Its wet level follows the enclosed-room estimate.
void OpenALManager::MixCavernEcho(uint8* stream, int frames) {
	if (cavern_echo_delay.empty() || frames <= 0) return;
	const float target = cavern_echo_target.load() * 0.28f;
	if (target == 0.f && cavern_echo_wet < 0.001f) {
		if (!cavern_echo_cleared) {
			std::fill(cavern_echo_delay.begin(), cavern_echo_delay.end(), 0.f);
			std::fill(cavern_echo_damping.begin(), cavern_echo_damping.end(), 0.f);
			cavern_echo_cleared = true;
		}
		return;
	}
	const SDL_AudioFormat format = sdl_audio_specs_obtained.format;
	if (format != AUDIO_S16SYS && format != AUDIO_F32SYS &&
	    format != AUDIO_S32SYS && format != AUDIO_U8) return;
	const size_t channels = sdl_audio_specs_obtained.channels;
	const size_t delay_frames = cavern_echo_delay.size() / channels;
	const size_t early_a = std::min(delay_frames - 1,
		static_cast<size_t>(sdl_audio_specs_obtained.freq * 75 / 1000));
	const size_t early_b = std::min(delay_frames - 1,
		static_cast<size_t>(sdl_audio_specs_obtained.freq * 145 / 1000));
	cavern_echo_cleared = false;
	for (int frame = 0; frame < frames; ++frame) {
		cavern_echo_wet += (target - cavern_echo_wet) * 0.0005f;
		for (size_t channel = 0; channel < channels; ++channel) {
			const size_t sample = static_cast<size_t>(frame) * channels + channel;
			const size_t tap = cavern_echo_frame * channels + channel;
			const size_t tap_a = ((cavern_echo_frame + delay_frames - early_a) % delay_frames) * channels + channel;
			const size_t tap_b = ((cavern_echo_frame + delay_frames - early_b) % delay_frames) * channels + channel;
			float input = 0.f;
			if (format == AUDIO_S16SYS)
				input = reinterpret_cast<int16_t*>(stream)[sample] / 32768.f;
			else if (format == AUDIO_F32SYS)
				input = reinterpret_cast<float*>(stream)[sample];
			else if (format == AUDIO_S32SYS)
				input = static_cast<float>(reinterpret_cast<int32_t*>(stream)[sample] / 2147483648.0);
			else
				input = (stream[sample] - 128) / 128.f;
			const float delayed = cavern_echo_delay[tap];
			const float other_b = channels == 2 ?
				cavern_echo_delay[tap_b - channel + (channel ^ 1)] : 0.f;
			const float reflections = cavern_echo_delay[tap_a] * 0.60f +
				cavern_echo_delay[tap_b] * 0.38f + other_b * 0.12f + delayed * 0.20f;
			float& damping = cavern_echo_damping[channel];
			damping += (reflections - damping) * 0.22f;
			cavern_echo_delay[tap] = std::max(-1.f, std::min(1.f, input + delayed * 0.20f));
			const float output = std::max(-1.f, std::min(1.f, input + damping * cavern_echo_wet));
			if (format == AUDIO_S16SYS)
				reinterpret_cast<int16_t*>(stream)[sample] = static_cast<int16_t>(output * 32767.f);
			else if (format == AUDIO_F32SYS)
				reinterpret_cast<float*>(stream)[sample] = output;
			else if (format == AUDIO_S32SYS)
				reinterpret_cast<int32_t*>(stream)[sample] = static_cast<int32_t>(output * 2147483647.0);
			else
				stream[sample] = static_cast<uint8>(std::max(0.f, std::min(255.f, output * 127.f + 128.f)));
		}
		cavern_echo_frame = (cavern_echo_frame + 1) % delay_frames;
	}
}

void OpenALManager::CleanEverything() {
	Stop();

	while (!sources_pool.empty()) {
		const auto& audioSource = sources_pool.front();
		alDeleteSources(1, &audioSource->source_id);

		for (auto const& buffer : audioSource->buffers) {
			alDeleteBuffers(1, &buffer.first);
		}

		sources_pool.pop();
	}

	DeleteCavernEcho();
	alDeleteFilters(1, &low_pass_filter);
	bool closedDevice = CloseDevice();
	assert(closedDevice && "Could not close audio device");
}

int OpenALManager::GetBestOpenALSupportedFormat() {
	auto device = p_ALCDevice ? p_ALCDevice : alcLoopbackOpenDeviceSOFT(nullptr);
	if (!device) {
		logError("Could not open audio loopback device to find best rendering format");
		return 0;
	}

	ALCint format = 0;
	for (int i = 0; i < format_type.size(); i++) {

		ALCint attrs[] = {
			ALC_FORMAT_TYPE_SOFT,     format_type[i],
			ALC_FORMAT_CHANNELS_SOFT, mapping_sdl_openal_channel.at(audio_parameters.channel_type),
			ALC_FREQUENCY,            static_cast<ALCint>(audio_parameters.rate)
		};

		if (alcIsRenderFormatSupportedSOFT(device, attrs[5], attrs[3], attrs[1]) == AL_TRUE) {
			format = format_type[i];
			break;
		}
	}

	if (!p_ALCDevice) {
		if (!alcCloseDevice(device)) {
			logError("Could not close audio loopback device to find best rendering format");
			return 0;
		}
	}

	return format;
}

void OpenALManager::UpdateParameters(const AudioParameters& parameters) {
	audio_parameters = parameters;
	SetMasterVolume(parameters.master_volume);
	SetMusicVolume(parameters.music_volume);
}

void OpenALManager::Shutdown() {
	delete instance;
	instance = nullptr;
}

OpenALManager::~OpenALManager() {
	CleanEverything();
	SDL_CloseAudio();
}
