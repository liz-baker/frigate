#include "SoundTest.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "AudioEngine.h"
#include "AudioSink.h"
#include "PcmResampler.h"
#include "SoundNames.h"
#include "resource/type/AudioBankResource.h"
#include "resource/type/MusicSequencesResource.h"
#include "ship/audio/Audio.h"
#include "ship/audio/AudioPlayer.h"
#include "ship/core/Context.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"

namespace Frigate::Audio {

namespace {

constexpr const char* kSfxBankPath = "audio/sfx";
constexpr const char* kSfxNamesPath = "audio/sfx_names";
constexpr const char* kInstrumentBankPath = "audio/instruments";
constexpr const char* kMusicSequencesPath = "audio/music";
constexpr const char* kMusicNamesPath = "audio/music_names";
constexpr std::string_view kMusicPathPrefix = "audio/music/";

// Frames of silence after which a finished sound's reverb tail is cut off.
constexpr int kSilentFramesToStop = 8;
// Longest a tail is rendered for, in engine frames.
constexpr int kMaxTailFrames = 90;

// The slot a music track is played on.
constexpr int kMusicSlot = 0;

std::atomic<bool> gInterrupted{ false };

void OnInterrupt(int) {
    gInterrupted = true;
}

bool IsSilent(const std::vector<int16_t>& frame) {
    for (int16_t sample : frame) {
        if (sample != 0) {
            return false;
        }
    }
    return true;
}

std::string LoadNames(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const char* path) {
    if (auto blob = resourceManager->LoadResource<Ship::Blob>(path)) {
        return std::string(reinterpret_cast<const char*>(blob->Data.data()), blob->Data.size());
    }
    return std::string();
}

// Feeds rendered frames to the audio output at the rate it plays them.
class Output {
  public:
    Output(const std::shared_ptr<Ship::Audio>& audio, const std::shared_ptr<Ship::AudioPlayer>& player)
        : mAudio(audio), mPlayer(player), mResampler(AudioDriver::kOutputRate, player->GetSampleRate()),
          mDesired(player->GetDesiredBuffered()) {
    }

    // Whether the output wants another frame now; sleeps briefly when not.
    bool WantsFrame() {
        if (!mPending.empty()) {
            if (PushPcm(mAudio, mPending.data(), mPending.size())) {
                mPending.clear();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return false;
        }
        if (mPlayer->Buffered() >= mDesired) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return false;
        }
        return true;
    }

    void Submit(const int16_t* frame) {
        mResampler.Process(frame, AudioDriver::kFrameSamples, &mPending);
    }

    // Lets what is queued play out.
    void Drain() {
        while (!mPending.empty() && !PushPcm(mAudio, mPending.data(), mPending.size())) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        while (mPlayer->Buffered() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

  private:
    std::shared_ptr<Ship::Audio> mAudio;
    std::shared_ptr<Ship::AudioPlayer> mPlayer;
    PcmResampler mResampler;
    std::vector<int16_t> mPending;
    int mDesired;
};

struct Report {
    int frames = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    void Log(const char* what) const {
        const double seconds = static_cast<double>(frames) * AudioDriver::kFrameSamples / AudioDriver::kOutputRate;
        spdlog::info("{}: done after {:.2f} s of audio ({:.2f} s elapsed)", what, seconds,
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    }
};

int PlaySfx(AudioDriver::AudioEngine& engine, Output& output, int index, const std::string& name, double maxSeconds) {
    AudioDriver::SoundState* state = engine.PlaySfx(static_cast<int16_t>(index));
    if (state == nullptr) {
        spdlog::error("play_sound: sound {} could not be started", index);
        return 1;
    }
    spdlog::info("play_sound: playing {} ({})", name.empty() ? "sound" : name, index);

    std::vector<int16_t> frame(AudioDriver::kFrameSamples * 2);
    const int maxFrames = static_cast<int>(maxSeconds * AudioDriver::kOutputRate / AudioDriver::kFrameSamples);

    Report report;
    int silentFrames = 0;
    int tailFrames = 0;
    bool released = false;
    bool finished = false;
    while (!finished) {
        if (!output.WantsFrame()) {
            continue;
        }

        if (!released && report.frames >= maxFrames) {
            // A sound that holds on its own is let go.
            engine.Sfx().Deactivate(state);
            released = true;
        }

        engine.RenderFrame(frame.data());
        report.frames++;
        output.Submit(frame.data());

        if (engine.Sfx().ActiveStates() == 0) {
            tailFrames++;
            silentFrames = IsSilent(frame) ? silentFrames + 1 : 0;
            finished = silentFrames >= kSilentFramesToStop || tailFrames >= kMaxTailFrames;
        }
    }

    output.Drain();
    report.Log("play_sound");
    return 0;
}

// Plays a music track on one slot, over and over, until interrupted (or until
// `seconds` have passed, when that is above zero).
int PlayMusic(AudioDriver::AudioEngine& engine, Output& output, int track, const std::string& name, double seconds) {
    AudioDriver::MusicPlayer& music = engine.Music();
    if (!music.TrackPlay(kMusicSlot, track)) {
        spdlog::error("play_sound: music track {} could not be started", track);
        return 1;
    }
    spdlog::info("play_sound: playing music {} ({}){}", name.empty() ? "track" : name, track,
                 seconds > 0 ? "" : ", Ctrl-C to stop");

    std::vector<int16_t> frame(AudioDriver::kFrameSamples * 2);
    const int maxFrames = static_cast<int>(seconds * AudioDriver::kOutputRate / AudioDriver::kFrameSamples);

    gInterrupted = false;
    const auto previousHandler = std::signal(SIGINT, OnInterrupt);

    Report report;
    int silentFrames = 0;
    int tailFrames = 0;
    bool stopping = false;
    bool finished = false;
    while (!finished) {
        if (!output.WantsFrame()) {
            continue;
        }

        if (!stopping && (gInterrupted || (seconds > 0 && report.frames >= maxFrames))) {
            music.TrackStop(kMusicSlot);
            stopping = true;
        }

        engine.RenderFrame(frame.data());
        report.frames++;
        output.Submit(frame.data());

        if (music.SeqState(kMusicSlot) == AudioDriver::kSeqStopped) {
            tailFrames++;
            silentFrames = IsSilent(frame) ? silentFrames + 1 : 0;
            if (stopping) {
                finished = silentFrames >= kSilentFramesToStop || tailFrames >= kMaxTailFrames;
            } else if (silentFrames >= kSilentFramesToStop) {
                // A track with no loop of its own ended: play it again.
                spdlog::info("play_sound: the track ended, starting it again");
                music.TrackPlay(kMusicSlot, track);
                silentFrames = 0;
                tailFrames = 0;
            }
        } else {
            silentFrames = 0;
            tailFrames = 0;
        }
    }

    std::signal(SIGINT, previousHandler);
    output.Drain();
    report.Log("play_sound");
    return 0;
}

} // namespace

int RunSoundTest(const std::shared_ptr<Ship::Context>& context, const std::string& sound, double maxSeconds,
                 double musicSeconds) {
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

    const SoundNames sfxNames(LoadNames(resourceManager, kSfxNamesPath));
    const SoundNames musicNames(LoadNames(resourceManager, kMusicNamesPath), NameKind::kMusic);

    // A sound effect by default; a name that is not one, or a path under
    // `audio/music/`, is a music track. A bare number is always a sound effect.
    std::optional<int> sfxIndex;
    std::optional<int> trackIndex;
    if (sound.rfind(kMusicPathPrefix, 0) == 0) {
        trackIndex = musicNames.Find(sound);
    } else {
        sfxIndex = sfxNames.Find(sound);
        const int soundCount =
            static_cast<int>(bank->Data.instruments.empty() ? 0 : bank->Data.instruments[0].sounds.size());
        if (sfxIndex && (*sfxIndex <= 0 || *sfxIndex > soundCount)) {
            sfxIndex.reset();
        }
        if (!sfxIndex && !std::isdigit(static_cast<unsigned char>(sound.empty() ? 'x' : sound[0]))) {
            trackIndex = musicNames.Find(sound);
        }
    }
    if (trackIndex && (*trackIndex < 0 || *trackIndex >= AudioDriver::kNumMusicTracks)) {
        trackIndex.reset();
    }
    if (!sfxIndex && !trackIndex) {
        spdlog::error("play_sound: no sound effect or music track '{}'", sound);
        for (const std::string& match : sfxNames.Suggest(sound)) {
            spdlog::error("  did you mean {}?", match);
        }
        for (const std::string& match : musicNames.Suggest(sound)) {
            spdlog::error("  did you mean {}?", match);
        }
        return 1;
    }

    std::shared_ptr<AudioBankResource> instruments;
    std::shared_ptr<MusicSequencesResource> sequences;
    AudioDriver::MusicAssets assets;
    if (trackIndex) {
        instruments = resourceManager->LoadResource<AudioBankResource>(kInstrumentBankPath);
        sequences = resourceManager->LoadResource<MusicSequencesResource>(kMusicSequencesPath);
        if (instruments == nullptr || sequences == nullptr) {
            spdlog::error("play_sound: no '{}' or '{}' in the loaded archives (pack the audio archive with `just "
                          "pack_archives`)",
                          kInstrumentBankPath, kMusicSequencesPath);
            return 1;
        }
        assets.instruments = &instruments->Data;
        assets.sequences = &sequences->Data;
    }

    AudioDriver::AudioEngine engine(bank->Data, AudioDriver::EngineConfig(), trackIndex ? &assets : nullptr);
    Output output(audio, player);
    spdlog::info("play_sound: {} Hz output", player->GetSampleRate());

    if (trackIndex) {
        return PlayMusic(engine, output, *trackIndex, musicNames.Name(*trackIndex), musicSeconds);
    }
    return PlaySfx(engine, output, *sfxIndex, sfxNames.Name(*sfxIndex), maxSeconds);
}

} // namespace Frigate::Audio
