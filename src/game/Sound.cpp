#include "game/Sound.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <mutex>

namespace Frigate::Game::Sound {

namespace {

using AudioDriver::AudioEngine;

std::mutex gLock;
AudioEngine* gEngine = nullptr;
ListenerSource gListeners;
const void* gListenersOwner = nullptr;
uint16_t gMTrack2Vol = 0x7FFF;
// The effects volume while no engine is installed, so the watch's slider still
// moves and reads back.
uint16_t gSilentSfxVolume = 0x7FFF;

// The music players, when an engine with music is installed.
AudioDriver::MusicPlayer* Music() {
    return gEngine != nullptr && gEngine->HasMusic() ? &gEngine->Music() : nullptr;
}

} // namespace

void Install(AudioEngine* engine) {
    std::lock_guard<std::mutex> lock(gLock);
    gEngine = engine;
}

void Uninstall() {
    Install(nullptr);
}

bool Installed() {
    std::lock_guard<std::mutex> lock(gLock);
    return gEngine != nullptr;
}

void RenderFrame(int16_t* out) {
    std::lock_guard<std::mutex> lock(gLock);
    if (gEngine == nullptr) {
        std::fill(out, out + AudioDriver::kFrameSamples * 2, int16_t{ 0 });
        return;
    }
    if (AudioDriver::MusicPlayer* music = Music()) {
        for (int i = 0; i < kFadeTicksPerAudioFrame; i++) {
            music->FadeTick();
        }
    }
    gEngine->RenderFrame(out);
}

SoundState* PlaySfx(int16_t sound, SoundState** pending) {
    std::lock_guard<std::mutex> lock(gLock);
    return gEngine != nullptr ? gEngine->PlaySfx(sound, pending) : nullptr;
}

void DetachHandle(SoundState** handle) {
    std::lock_guard<std::mutex> lock(gLock);
    if (gEngine != nullptr) {
        gEngine->Sfx().DetachHandle(handle);
    }
}

uint8_t GetPlayingState(const SoundState* state) {
    std::lock_guard<std::mutex> lock(gLock);
    return gEngine != nullptr ? gEngine->Sfx().GetPlayingState(state) : 0;
}

void Deactivate(SoundState* state) {
    std::lock_guard<std::mutex> lock(gLock);
    if (gEngine != nullptr) {
        gEngine->Sfx().Deactivate(state);
    }
}

void DeactivateAllSfxByFlag(uint8_t flag) {
    std::lock_guard<std::mutex> lock(gLock);
    if (gEngine != nullptr) {
        gEngine->Sfx().DeactivateAllByFlag(flag);
    }
}

void DeactivateAllSfxByFlag1() {
    DeactivateAllSfxByFlag(0x01);
}

void DeactivateAllSfxByFlag11() {
    DeactivateAllSfxByFlag(0x11);
}

void DeactivateAllSfxByFlag3() {
    DeactivateAllSfxByFlag(0x03);
}

void CreatePostEvent(SoundState* state, int16_t eventType, int32_t arg) {
    std::lock_guard<std::mutex> lock(gLock);
    if (gEngine != nullptr) {
        gEngine->Sfx().CreatePostEvent(state, eventType, arg);
    }
}

void SetVolume(SoundState* state, int32_t volume) {
    CreatePostEvent(state, AudioDriver::kSndpVol, volume);
}

uint16_t GetSfxSlotFirstNaturalVolume() {
    std::lock_guard<std::mutex> lock(gLock);
    return gEngine != nullptr ? gEngine->Sfx().GetSfxSlotFirstNaturalVolume() : gSilentSfxVolume;
}

void ApplyVolumeAllSfxSlot(uint16_t volume) {
    std::lock_guard<std::mutex> lock(gLock);
    gSilentSfxVolume = volume;
    if (gEngine != nullptr) {
        gEngine->Sfx().ApplyVolumeAllSfxSlot(volume);
    }
}

void SetScalerApplyVolumeAllSfxSlot(float scale) {
    std::lock_guard<std::mutex> lock(gLock);
    if (gEngine != nullptr) {
        gEngine->Sfx().SetScalerApplyVolumeAllSfxSlot(scale);
    }
}

void MusicTrackPlay(int slot, int track) {
    std::lock_guard<std::mutex> lock(gLock);
    if (AudioDriver::MusicPlayer* music = Music()) {
        music->TrackPlay(slot, track);
    }
}

void MusicTrackStop(int slot) {
    std::lock_guard<std::mutex> lock(gLock);
    if (AudioDriver::MusicPlayer* music = Music()) {
        music->TrackStop(slot);
    }
}

uint16_t MusicTrackGetVolume(int slot) {
    std::lock_guard<std::mutex> lock(gLock);
    if (AudioDriver::MusicPlayer* music = Music()) {
        return music->TrackGetVolume(slot);
    }
    return AudioDriver::kMusicVolumeMax;
}

void MusicTrackApplySeqpVol(int slot, uint16_t volume) {
    std::lock_guard<std::mutex> lock(gLock);
    if (AudioDriver::MusicPlayer* music = Music()) {
        music->TrackApplySeqpVol(slot, volume);
    }
}

void MusicTrackSaveCurrentVolumeAsTrackDefault(int slot) {
    std::lock_guard<std::mutex> lock(gLock);
    if (AudioDriver::MusicPlayer* music = Music()) {
        music->TrackSaveCurrentVolumeAsTrackDefault(slot);
    }
}

void MusicTrackFadeOut(int slot, float seconds) {
    std::lock_guard<std::mutex> lock(gLock);
    if (AudioDriver::MusicPlayer* music = Music()) {
        music->TrackFadeOut(slot, seconds);
    }
}

void MusicTrackFadeIn(int slot, float seconds, uint16_t volume) {
    std::lock_guard<std::mutex> lock(gLock);
    if (AudioDriver::MusicPlayer* music = Music()) {
        music->TrackFadeIn(slot, seconds, volume);
    }
}

int32_t MusicTrackFade(int slot) {
    std::lock_guard<std::mutex> lock(gLock);
    if (AudioDriver::MusicPlayer* music = Music()) {
        return music->FadeState(slot);
    }
    return AudioDriver::kMusicFadeUnset;
}

void SetMusicTrackFade(int slot, int32_t fade) {
    std::lock_guard<std::mutex> lock(gLock);
    if (AudioDriver::MusicPlayer* music = Music()) {
        music->SetFadeState(slot, fade);
    }
}

uint16_t GetMTrack2Vol() {
    std::lock_guard<std::mutex> lock(gLock);
    return gMTrack2Vol;
}

void SetMTrack2Vol(uint16_t volume) {
    {
        std::lock_guard<std::mutex> lock(gLock);
        gMTrack2Vol = volume;
    }
    MusicTrackApplySeqpVol(1, volume);
}

void SetListenerSource(const void* owner, ListenerSource source) {
    std::lock_guard<std::mutex> lock(gLock);
    gListeners = std::move(source);
    gListenersOwner = owner;
}

void ClearListenerSource(const void* owner) {
    std::lock_guard<std::mutex> lock(gLock);
    if (gListenersOwner == owner) {
        gListeners = nullptr;
        gListenersOwner = nullptr;
    }
}

int32_t VolumeForDistance(float distance, float min, float max) {
    if (distance <= 200.0f) {
        return SHRT_MAX;
    }
    if (max <= distance) {
        return 0;
    }
    if (min <= distance) {
        return static_cast<int32_t>(((max - distance) * 10000.0f) / (max - min));
    }
    return SHRT_MAX - static_cast<int32_t>((std::sqrt(distance - 200.0f) * 22767.0f) / std::sqrt(min - 200.0f));
}

int32_t VolumeAtPosition(const std::array<float, 3>& pos, float low, float high) {
    ListenerSource source;
    {
        std::lock_guard<std::mutex> lock(gLock);
        source = gListeners;
    }
    float shortest = high;
    const std::vector<std::array<float, 3>> listeners = source ? source() : std::vector<std::array<float, 3>>();
    for (const std::array<float, 3>& listener : listeners) {
        const float dx = listener[0] - pos[0];
        const float dy = listener[1] - pos[1];
        const float dz = listener[2] - pos[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance < shortest) {
            shortest = distance;
        }
    }
    return VolumeForDistance(shortest, low, high);
}

int32_t VolumeForDistanceDefault(float distance) {
    return VolumeForDistance(distance, 5000.0f, 6000.0f);
}

int32_t VolumeAtPositionDefault(const std::array<float, 3>& pos) {
    return VolumeAtPosition(pos, 5000.0f, 6000.0f);
}

void PostVolumeAtPosition(SoundState* state, const std::array<float, 3>& pos, float low, float high) {
    SetVolume(state, VolumeAtPosition(pos, low, high));
}

void PostVolumeAtPositionDefault(SoundState* state, const std::array<float, 3>& pos) {
    PostVolumeAtPosition(state, pos, 5000.0f, 6000.0f);
}

} // namespace Frigate::Game::Sound
