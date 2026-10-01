#include "SoundTest.h"

#include <chrono>
#include <string_view>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "AudioEngine.h"
#include "AudioSink.h"
#include "PcmResampler.h"
#include "SoundNames.h"
#include "resource/type/AudioBankResource.h"
#include "ship/audio/Audio.h"
#include "ship/audio/AudioPlayer.h"
#include "ship/core/Context.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"

namespace Frigate::Audio {

namespace {

constexpr const char* kSfxBankPath = "audio/sfx";
constexpr const char* kSfxNamesPath = "audio/sfx_names";

// Frames of silence after which a finished sound's reverb tail is cut off.
constexpr int kSilentFramesToStop = 8;
// Longest a tail is rendered for, in engine frames.
constexpr int kMaxTailFrames = 90;

bool IsSilent(const std::vector<int16_t>& frame) {
    for (int16_t sample : frame) {
        if (sample != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

int RunSoundTest(const std::shared_ptr<Ship::Context>& context, const std::string& sound, double maxSeconds) {
    auto resourceManager = context->GetChildren().GetFirst<Ship::ResourceManager>();
    auto audio = context->GetChildren().GetFirst<Ship::Audio>();
    auto player = audio ? audio->GetAudioPlayer() : nullptr;
    if (!player || !player->IsInitialized()) {
        spdlog::error("play_sound: no audio output is available");
        return 1;
    }
    if (player->GetNumOutputChannels() != 2) {
        spdlog::error("play_sound: the audio output has {} channels; only stereo is supported",
                      player->GetNumOutputChannels());
        return 1;
    }

    auto bank = resourceManager->LoadResource<AudioBankResource>(kSfxBankPath);
    if (bank == nullptr) {
        spdlog::error("play_sound: no '{}' in the loaded archives (pack the audio archive with `just pack_archives`)",
                      kSfxBankPath);
        return 1;
    }

    std::string namesText;
    if (auto blob = resourceManager->LoadResource<Ship::Blob>(kSfxNamesPath)) {
        namesText.assign(reinterpret_cast<const char*>(blob->Data.data()), blob->Data.size());
    }
    const SoundNames names(namesText);

    const auto index = names.Find(sound);
    if (!index || *index <= 0 ||
        *index > static_cast<int>(bank->Data.instruments.empty() ? 0 : bank->Data.instruments[0].sounds.size())) {
        spdlog::error("play_sound: no sound effect '{}'", sound);
        for (const std::string& match : names.Suggest(sound)) {
            spdlog::error("  did you mean {}?", match);
        }
        return 1;
    }

    AudioDriver::AudioEngine engine(bank->Data);
    AudioDriver::SoundState* state = engine.PlaySfx(static_cast<int16_t>(*index));
    if (state == nullptr) {
        spdlog::error("play_sound: sound {} could not be started", *index);
        return 1;
    }

    const std::string& name = names.Name(*index);
    spdlog::info("play_sound: playing {} ({}), {} Hz output", name.empty() ? "sound" : name, *index,
                 player->GetSampleRate());

    PcmResampler resampler(AudioDriver::kOutputRate, player->GetSampleRate());
    std::vector<int16_t> frame(AudioDriver::kFrameSamples * 2);
    std::vector<int16_t> pending;
    const int desired = player->GetDesiredBuffered();
    const auto start = std::chrono::steady_clock::now();
    const int maxFrames = static_cast<int>(maxSeconds * AudioDriver::kOutputRate / AudioDriver::kFrameSamples);

    int frames = 0;
    int silentFrames = 0;
    int tailFrames = 0;
    bool released = false;
    bool finished = false;
    while (!finished) {
        if (!pending.empty()) {
            if (PushPcm(audio, pending.data(), pending.size())) {
                pending.clear();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            continue;
        }
        if (player->Buffered() >= desired) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        if (!released && frames >= maxFrames) {
            // A sound that holds on its own is let go.
            engine.Sfx().Deactivate(state);
            released = true;
        }

        engine.RenderFrame(frame.data());
        frames++;
        resampler.Process(frame.data(), AudioDriver::kFrameSamples, &pending);

        if (engine.Sfx().ActiveStates() == 0) {
            tailFrames++;
            silentFrames = IsSilent(frame) ? silentFrames + 1 : 0;
            finished = silentFrames >= kSilentFramesToStop || tailFrames >= kMaxTailFrames;
        }
    }

    // Let what is queued play out.
    while (!pending.empty() && !PushPcm(audio, pending.data(), pending.size())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    while (player->Buffered() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const double seconds = static_cast<double>(frames) * AudioDriver::kFrameSamples / AudioDriver::kOutputRate;
    spdlog::info("play_sound: done after {:.2f} s of audio ({:.2f} s elapsed)", seconds,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return 0;
}

} // namespace Frigate::Audio
