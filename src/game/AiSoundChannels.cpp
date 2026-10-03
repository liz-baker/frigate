#include "game/AiSoundChannels.h"

#include <climits>

namespace Frigate::Game {

AiSoundChannels::~AiSoundChannels() {
    // The channels' sounds may outlive them.
    for (Channel& channel : mChannels) {
        Sound::DetachHandle(&channel.state);
    }
}

void AiSoundChannels::Reset() {
    for (Channel& channel : mChannels) {
        Sound::DetachHandle(&channel.state);
        channel.state = nullptr;
    }
}

void AiSoundChannels::Update(Channel& channel) {
    if (channel.state == nullptr || Sound::GetPlayingState(channel.state) == 0) {
        channel.volume2 = 0;
        return;
    }

    if (channel.position) {
        channel.volume = Sound::VolumeAtPositionDefault(*channel.position);
    } else if (channel.prop >= 0) {
        if (const std::optional<Position> position = mHost.propPosition(channel.prop)) {
            channel.volume = Sound::VolumeAtPositionDefault(*position);
        }
    }

    int volume = channel.volume;
    if (channel.fadeTicks >= 0) {
        const int clockTimer = mHost.clockTimer();
        if (clockTimer < channel.fadeTicks) {
            volume = (((channel.volume - channel.volume2) * clockTimer) / channel.fadeTicks) + channel.volume2;
        }
        channel.fadeTicks -= clockTimer;
    }
    if (mHost.controlsLocked()) {
        volume = 0;
    }
    if (volume != channel.volume2) {
        Sound::SetVolume(channel.state, volume);
        channel.volume2 = volume;
    }
}

void AiSoundChannels::Tick() {
    for (Channel& channel : mChannels) {
        Update(channel);
    }
}

void AiSoundChannels::Play(int channel, int16_t sound) {
    Sound::SoundState** handle = nullptr;
    if (Valid(channel)) {
        Channel& slot = mChannels[static_cast<size_t>(channel)];
        if (slot.state == nullptr || Sound::GetPlayingState(slot.state) == 0) {
            slot.volume = SHRT_MAX;
            slot.volume2 = SHRT_MAX;
            slot.fadeTicks = -1;
            slot.position.reset();
            slot.prop = -1;
            handle = &slot.state;
        }
    }
    Sound::PlaySfx(sound, handle);
}

void AiSoundChannels::Stop(int channel) {
    if (Valid(channel)) {
        Sound::Deactivate(mChannels[static_cast<size_t>(channel)].state);
    }
}

void AiSoundChannels::Cleanup() {
    for (Channel& channel : mChannels) {
        if (channel.state != nullptr && Sound::GetPlayingState(channel.state) != 0) {
            Sound::Deactivate(channel.state);
        }
    }
}

void AiSoundChannels::SetVolume(int channel, int volume, int fadeTicks) {
    if (!Valid(channel)) {
        return;
    }
    Channel& slot = mChannels[static_cast<size_t>(channel)];
    slot.fadeTicks = fadeTicks;
    slot.volume = volume;
    slot.position.reset();
    slot.prop = -1;
    if (fadeTicks == 0) {
        Update(slot);
    }
}

void AiSoundChannels::FadeVolume(int channel, float distance, int fadeTicks) {
    if (!Valid(channel)) {
        return;
    }
    Channel& slot = mChannels[static_cast<size_t>(channel)];
    slot.fadeTicks = fadeTicks;
    slot.volume = Sound::VolumeForDistanceDefault(distance);
    slot.position.reset();
    slot.prop = -1;
    if (fadeTicks == 0) {
        Update(slot);
    }
}

void AiSoundChannels::EmitFromProp(int channel, int prop, int fadeTicks) {
    if (!Valid(channel)) {
        return;
    }
    Channel& slot = mChannels[static_cast<size_t>(channel)];
    slot.fadeTicks = fadeTicks;
    slot.position.reset();
    slot.prop = prop;
    if (fadeTicks == 0) {
        Update(slot);
    }
}

void AiSoundChannels::EmitFromPad(int channel, const Position& pad, int fadeTicks) {
    if (!Valid(channel)) {
        return;
    }
    Channel& slot = mChannels[static_cast<size_t>(channel)];
    slot.fadeTicks = fadeTicks;
    slot.position = pad;
    slot.prop = -1;
    if (fadeTicks == 0) {
        Update(slot);
    }
}

int AiSoundChannels::Volume(int channel) const {
    return Valid(channel) ? mChannels[static_cast<size_t>(channel)].volume2 : 0;
}

} // namespace Frigate::Game
