#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

#include "game/Sound.h"

// The sounds a stage's AI plays on its eight channels (chrai.c's sfx_related
// and the functions around it): each channel holds the sound playing on it, the
// volume it is heard at, and what it is heard from.
namespace Frigate::Game {

class AiSoundChannels {
  public:
    // SFX_RELATED_LEN.
    static constexpr int kCount = 8;
    using Position = std::array<float, 3>;

    // What the channels read of the stage.
    struct Host {
        // g_ClockTimer.
        std::function<int()> clockTimer;
        // lvlGetControlsLockedFlag.
        std::function<bool()> controlsLocked;
        // The world position of a prop (obj->runtime_pos), if it has one.
        std::function<std::optional<Position>(int prop)> propPosition;
    };

    explicit AiSoundChannels(Host host) : mHost(std::move(host)) {
    }
    ~AiSoundChannels();
    AiSoundChannels(const AiSoundChannels&) = delete;
    AiSoundChannels& operator=(const AiSoundChannels&) = delete;

    // init_sound_effects_registers: no channel holds a sound.
    void Reset();
    // audioPlayFromProp: plays `sound` on `channel`, unless a sound already
    // plays there, in which case it plays without being tracked by it.
    void Play(int channel, int16_t sound);
    // sub_GAME_7F0349BC: stops the channel's sound.
    void Stop(int channel);
    // cleanupObjectSounds: a stage ends; every channel's sound is stopped.
    void Cleanup();
    // AI_SfxSetChannelVolume and AI_SfxFadeChannelVolume: the volume the
    // channel's sound heads to over `fadeTicks`, 0 or more.
    void SetVolume(int channel, int volume, int fadeTicks);
    // The fade variant takes the volume as a distance (sub_GAME_7F0539B8).
    void FadeVolume(int channel, float distance, int fadeTicks);
    // AI_SfxEmitFromObject and AI_SfxEmitFromPad: the sound on the channel is
    // heard from a prop, or from a place.
    void EmitFromProp(int channel, int prop, int fadeTicks);
    void EmitFromPad(int channel, const Position& pad, int fadeTicks);
    // The volume the channel's sound is at (Volume2; 0 when nothing plays), which
    // AI_IFSfxChannelVolumeLessThan reads.
    int Volume(int channel) const;
    // loop_set_sound_effect_all_slots, once a frame.
    void Tick();

  private:
    struct Channel {
        // The sound playing, handed to PlaySfx as its handle.
        Sound::SoundState* state = nullptr;
        // Volume2: where the volume is now. `volume` is where it is going.
        int volume2 = 0;
        // sfxID: ticks left of the fade, or negative for no fade.
        int fadeTicks = 0;
        int volume = 0;
        // What it is heard from: a place, or a prop.
        std::optional<Position> position;
        int prop = -1;
    };

    // audioPlayFromProp2.
    void Update(Channel& channel);
    static bool Valid(int channel) {
        return channel >= 0 && channel < kCount;
    }
    Host mHost;
    std::array<Channel, kCount> mChannels;
};

} // namespace Frigate::Game
