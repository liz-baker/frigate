#include "game/SoundOutput.h"

#include <vector>

#include <spdlog/spdlog.h>

#include "AudioEngine.h"
#include "AudioOutput.h"
#include "game/Sound.h"
#include "resource/type/AudioBankResource.h"
#include "resource/type/MusicSequencesResource.h"
#include "ship/audio/Audio.h"
#include "ship/audio/AudioPlayer.h"
#include "ship/core/Context.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game {

namespace {

constexpr const char* kSfxBankPath = "audio/sfx";
constexpr const char* kInstrumentBankPath = "audio/instruments";
constexpr const char* kMusicSequencesPath = "audio/music";

} // namespace

std::unique_ptr<SoundOutput> SoundOutput::Start(const std::shared_ptr<Ship::Context>& context) {
    auto resourceManager = context->GetChildren().GetFirst<Ship::ResourceManager>();
    auto audio = context->GetChildren().GetFirst<Ship::Audio>();
    auto player = audio ? audio->GetAudioPlayer() : nullptr;
    if (!player || !player->IsInitialized()) {
        spdlog::warn("sound: no audio output is available; running silent");
        return nullptr;
    }
    if (player->GetNumOutputChannels() != 2) {
        spdlog::warn("sound: the audio output has {} channels; only stereo is supported, running silent",
                     player->GetNumOutputChannels());
        return nullptr;
    }

    std::unique_ptr<SoundOutput> output(new SoundOutput());
    output->mSfxBank = resourceManager->LoadResource<AudioBankResource>(kSfxBankPath);
    output->mInstruments = resourceManager->LoadResource<AudioBankResource>(kInstrumentBankPath);
    output->mSequences = resourceManager->LoadResource<MusicSequencesResource>(kMusicSequencesPath);
    if (output->mSfxBank == nullptr) {
        spdlog::warn("sound: no '{}' in the loaded archives (pack them with `just pack_archives`); running silent",
                     kSfxBankPath);
        return nullptr;
    }

    AudioDriver::MusicAssets music;
    if (output->mInstruments != nullptr && output->mSequences != nullptr) {
        music.instruments = &output->mInstruments->Data;
        music.sequences = &output->mSequences->Data;
    } else {
        spdlog::warn("sound: no '{}' or '{}' in the loaded archives; the music is off", kInstrumentBankPath,
                     kMusicSequencesPath);
    }
    output->mEngine = std::make_unique<AudioDriver::AudioEngine>(output->mSfxBank->Data, AudioDriver::EngineConfig(),
                                                                 music.instruments != nullptr ? &music : nullptr);
    Sound::Install(output->mEngine.get());

    SoundOutput* self = output.get();
    self->mThread = std::thread([self, audio, player] {
        Audio::AudioOutput sink(audio, player, AudioDriver::kOutputRate);
        std::vector<int16_t> frame(AudioDriver::kFrameSamples * 2);
        while (!self->mStop) {
            if (!sink.WantsFrame()) {
                continue;
            }
            Sound::RenderFrame(frame.data());
            sink.Submit(frame.data(), AudioDriver::kFrameSamples);
        }
    });
    return output;
}

SoundOutput::~SoundOutput() {
    mStop = true;
    if (mThread.joinable()) {
        mThread.join();
    }
    Sound::Uninstall();
}

} // namespace Frigate::Game
