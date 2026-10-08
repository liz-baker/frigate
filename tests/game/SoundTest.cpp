#include "AudioBank.h"
#include "AudioEngine.h"
#include "BankBuilder.h"
#include "CompactSeq.h"
#include "MusicSequences.h"
#include "game/AiSoundChannels.h"
#include "game/MissionMusic.h"
#include "game/Sound.h"
#include "level/LevelInfo.h"

#include <array>
#include <climits>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

// Checks the game's sound calls (Sound.h), the AI's sound channels and the
// stage's music state machine against a synthetic bank and music table: the
// state they leave in the players, not the PCM, which the audio tests measure.

using namespace Frigate::Audio;
using namespace Frigate::AudioDriver;
using namespace Frigate::AudioTest;
using namespace Frigate::Game;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// A track that plays one looped note forever.
std::vector<uint8_t> LoopingTrack() {
    std::vector<uint8_t> t;
    auto varLen = [&t](uint32_t value) { t.push_back(uint8_t(value)); };
    varLen(0);
    t.insert(t.end(), { 0xFF, 0x2E, 0, 0 });
    const uint32_t loopStart = 0x44 + uint32_t(t.size());
    varLen(0);
    t.insert(t.end(), { 0x90, 60, 100 });
    varLen(48);
    varLen(48);
    t.insert(t.end(), { 0xFF, 0x2D, 0xFF, 0xFF });
    const uint32_t back = 0x44 + uint32_t(t.size()) + 4 - loopStart;
    t.insert(t.end(), { uint8_t(back >> 24), uint8_t(back >> 16), uint8_t(back >> 8), uint8_t(back) });
    varLen(0);
    t.insert(t.end(), { 0xFF, 0x2F });

    std::vector<uint8_t> out;
    auto u32 = [&out](uint32_t v) {
        out.insert(out.end(), { uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) });
    };
    for (int i = 0; i < 16; i++) {
        u32(i == 0 ? 0x44 : 0);
    }
    u32(96);
    out.insert(out.end(), t.begin(), t.end());
    return out;
}

// An engine with one looped sound effect (sound 1) and music tracks 0 to 63.
struct Rig {
    AudioBank sfx;
    AudioBank instruments;
    MusicSequenceTable sequences;
    std::unique_ptr<AudioEngine> engine;
    std::vector<int16_t> frame = std::vector<int16_t>(kFrameSamples * 2);

    Rig() {
        SoundSpec spec;
        spec.keyMin = 0;
        spec.keyMax = 127;
        spec.velocityMin = 0;
        spec.velocityMax = 127;
        spec.looped = true;
        const std::vector<uint8_t> bank = BuildToneBank({ spec });
        Check(ParseAudioBankPayload(bank.data(), bank.size(), &sfx), "the tone bank parses");
        instruments = sfx;

        const std::vector<uint8_t> track = LoopingTrack();
        sequences.romBase = 0x419790;
        for (size_t i = 0; i < 64; i++) {
            MusicSequenceEntry entry;
            entry.address = 0x20000 + uint32_t(i) * 0x100;
            entry.uncompressedLength = uint16_t(track.size());
            entry.length = uint16_t(track.size());
            entry.dataOffset = uint32_t(sequences.data.size());
            sequences.entries.push_back(entry);
            sequences.data.insert(sequences.data.end(), track.begin(), track.end());
        }
        MusicAssets assets;
        assets.instruments = &instruments;
        assets.sequences = &sequences;
        engine = std::make_unique<AudioEngine>(sfx, EngineConfig(), &assets);
        Frigate::Game::Sound::Install(engine.get());
    }
    ~Rig() {
        Frigate::Game::Sound::Uninstall();
    }

    void Render(int frames = 1) {
        for (int i = 0; i < frames; i++) {
            Frigate::Game::Sound::RenderFrame(frame.data());
        }
    }
};

void TestNothingInstalled() {
    Check(!Frigate::Game::Sound::Installed(), "nothing is installed at first");
    SoundState* handle = nullptr;
    Check(Frigate::Game::Sound::PlaySfx(kSfxRocketLaunch, &handle) == nullptr && handle == nullptr, "no sound plays");
    Check(Frigate::Game::Sound::GetPlayingState(nullptr) == 0, "a null state is not playing");
    Frigate::Game::Sound::MusicTrackPlay(0, kMusicIntro);
    Frigate::Game::Sound::MusicTrackFadeOut(0, 1.0f);
    Frigate::Game::Sound::Deactivate(nullptr);
    Check(Frigate::Game::Sound::MusicTrackFade(0) == kMusicFadeUnset, "no music fades");

    std::vector<int16_t> frame(kFrameSamples * 2, 1);
    Frigate::Game::Sound::RenderFrame(frame.data());
    bool silent = true;
    for (int16_t sample : frame) {
        silent = silent && sample == 0;
    }
    Check(silent, "a frame is silent");

    // The options' volumes still read back what was set.
    Frigate::Game::Sound::ApplyVolumeAllSfxSlot(0x2000);
    Check(Frigate::Game::Sound::GetSfxSlotFirstNaturalVolume() == 0x2000, "the effects volume reads back");
    Frigate::Game::Sound::SetMTrack2Vol(0x1234);
    Check(Frigate::Game::Sound::GetMTrack2Vol() == 0x1234, "the music volume reads back");
    Frigate::Game::Sound::SetMTrack2Vol(0x7FFF);
    Frigate::Game::Sound::ApplyVolumeAllSfxSlot(0x7FFF);
}

void TestSfxHandleAndDeactivate() {
    Rig rig;
    Check(Frigate::Game::Sound::Installed(), "the engine is installed");

    SoundState* handle = nullptr;
    SoundState* state = Frigate::Game::Sound::PlaySfx(1, &handle);
    Check(state != nullptr && handle == state, "the handle names the new sound");
    Check(Frigate::Game::Sound::GetPlayingState(handle) != 0, "it is playing");
    rig.Render(2);
    Check(Frigate::Game::Sound::GetPlayingState(handle) == kSoundStatePlaying, "it plays");

    Frigate::Game::Sound::Deactivate(handle);
    rig.Render(8);
    Check(handle == nullptr, "the handle clears when the sound ends");

    // A handle that goes away first is not written to afterwards.
    auto stale = std::make_unique<SoundState*>(nullptr);
    Frigate::Game::Sound::PlaySfx(1, stale.get());
    rig.Render(2);
    Frigate::Game::Sound::DetachHandle(stale.get());
    Frigate::Game::Sound::DeactivateAllSfxByFlag1();
    rig.Render(8);
    Check(rig.engine->Sfx().ActiveStates() == 0, "deactivating by flag ends the sound");
    Check(*stale != nullptr, "a detached handle is left alone");
}

void TestSlotVolumes() {
    Rig rig;
    Check(Frigate::Game::Sound::GetSfxSlotFirstNaturalVolume() == 0x7FFF, "the effects volume starts at the maximum");
    Frigate::Game::Sound::ApplyVolumeAllSfxSlot(0x4000);
    Check(Frigate::Game::Sound::GetSfxSlotFirstNaturalVolume() == 0x4000, "the natural volume is the one set");
    Check(rig.engine->Sfx().GetSfxSlotNaturalVolume(6) == 0x4000, "every slot takes it");
    Frigate::Game::Sound::SetScalerApplyVolumeAllSfxSlot(0.5f);
    Check(Frigate::Game::Sound::GetSfxSlotFirstNaturalVolume() == 0x4000, "scaling leaves the natural volume");

    Frigate::Game::Sound::SetMTrack2Vol(0x3000);
    Check(Frigate::Game::Sound::MusicTrackGetVolume(1) == 0x3000, "the music volume applies to slot 1");
}

void TestFadeRunsAtSixtyHertz() {
    Rig rig;
    Frigate::Game::Sound::MusicTrackPlay(0, kMusicBunker1);
    rig.Render(2);
    Check(Frigate::Game::Sound::MusicTrackFade(0) == kMusicFadeUnset, "no fade at first");

    // One second is sixty ticks: thirty audio frames.
    Frigate::Game::Sound::MusicTrackFadeOut(0, 1.0f);
    Check(Frigate::Game::Sound::MusicTrackFade(0) == kMusicFadeOut, "the fade starts");
    rig.Render(29);
    Check(Frigate::Game::Sound::MusicTrackFade(0) == kMusicFadeOut && Frigate::Game::Sound::MusicTrackGetVolume(0) != 0,
          "it is still fading");
    rig.Render(1);
    Check(Frigate::Game::Sound::MusicTrackFade(0) == kMusicFadeUnset &&
              Frigate::Game::Sound::MusicTrackGetVolume(0) == 0,
          "it ends after sixty ticks, silent");

    Frigate::Game::Sound::SetMusicTrackFade(0, kMusicFadeIn);
    Check(Frigate::Game::Sound::MusicTrackFade(0) == kMusicFadeIn, "the fade state can be written");
}

void TestDistanceVolume() {
    Check(Frigate::Game::Sound::VolumeForDistance(100.0f, 5000.0f, 6000.0f) == 32767, "full volume up close");
    Check(Frigate::Game::Sound::VolumeForDistance(200.0f, 5000.0f, 6000.0f) == 32767, "full volume at 200 units");
    Check(Frigate::Game::Sound::VolumeForDistance(250.0f, 5000.0f, 6000.0f) == 30444, "the square root fall-off");
    Check(Frigate::Game::Sound::VolumeForDistance(1000.0f, 5000.0f, 6000.0f) == 23473, "the fall-off further out");
    Check(Frigate::Game::Sound::VolumeForDistance(5500.0f, 5000.0f, 6000.0f) == 5000,
          "the linear fall-off past the minimum");
    Check(Frigate::Game::Sound::VolumeForDistance(6000.0f, 5000.0f, 6000.0f) == 0, "silent at the maximum");
    Check(Frigate::Game::Sound::VolumeForDistanceDefault(9000.0f) == 0, "silent beyond it");

    const std::array<float, 3> sound = { 0.0f, 0.0f, 0.0f };
    Check(Frigate::Game::Sound::VolumeAtPositionDefault(sound) == 0, "no listener hears nothing");
    Frigate::Game::Sound::SetListenerSource(&gFailures, [] {
        return std::vector<std::array<float, 3>>{ { 5500.0f, 0.0f, 0.0f }, { 0.0f, 100.0f, 0.0f } };
    });
    Check(Frigate::Game::Sound::VolumeAtPositionDefault(sound) == 32767, "the nearest listener is the one measured");
    Frigate::Game::Sound::SetListenerSource(&gFailures, [] {
        return std::vector<std::array<float, 3>>{ { 5500.0f, 0.0f, 0.0f } };
    });
    Check(Frigate::Game::Sound::VolumeAtPosition(sound, 5000.0f, 6000.0f) == 5000, "a position's volume");
    Frigate::Game::Sound::ClearListenerSource(&gFailures);
}

// With two, three and four players the volume is the one for the nearest, whichever
// player that is.
void TestNearestOfEveryPlayer() {
    for (int players = 2; players <= 4; players++) {
        for (int nearest = 0; nearest < players; nearest++) {
            Frigate::Game::Sound::SetListenerSource(&gFailures, [players, nearest] {
                std::vector<std::array<float, 3>> listeners;
                for (int player = 0; player < players; player++) {
                    listeners.push_back({ player == nearest ? 5500.0f : 5900.0f, 0.0f, 0.0f });
                }
                return listeners;
            });
            const std::array<float, 3> sound = { 0.0f, 0.0f, 0.0f };
            Check(Frigate::Game::Sound::VolumeAtPositionDefault(sound) == 5000,
                  "the nearest of the players is the one measured");
        }
    }
    Frigate::Game::Sound::ClearListenerSource(&gFailures);
}

// The call log records what the game asked for with or without an engine, and
// a null state is not a call.
void TestCallLog() {
    using Kind = Frigate::Game::Sound::Call::Kind;
    std::vector<Frigate::Game::Sound::Call> log;
    Frigate::Game::Sound::SetCallLog(&log);
    Frigate::Game::Sound::PlaySfx(kSfxRocketLaunch);
    Frigate::Game::Sound::Deactivate(nullptr);
    Frigate::Game::Sound::DeactivateAllSfxByFlag1();
    Frigate::Game::Sound::MusicTrackPlay(1, kMusicIntro);
    Frigate::Game::Sound::MusicTrackFadeOut(0, 1.0f);
    Frigate::Game::Sound::MusicTrackFadeIn(2, 1.0f, 0x7FFF);
    Frigate::Game::Sound::MusicTrackStop(2);
    Frigate::Game::Sound::SetCallLog(nullptr);
    Frigate::Game::Sound::PlaySfx(kSfxRocketLaunch);
    Check(log.size() == 6, "six calls are logged and none after the log is removed");
    if (log.size() == 6) {
        Check(log[0].kind == Kind::PlaySfx && log[0].a == kSfxRocketLaunch, "the sound is logged");
        Check(log[1].kind == Kind::DeactivateAllByFlag && log[1].a == 1, "the flag is logged");
        Check(log[2].kind == Kind::MusicPlay && log[2].a == 1 && log[2].b == kMusicIntro,
              "the slot and track are logged");
        Check(log[3].kind == Kind::MusicFadeOut && log[3].a == 0, "a fade out is logged");
        Check(log[4].kind == Kind::MusicFadeIn && log[4].a == 2, "a fade in is logged");
        Check(log[5].kind == Kind::MusicStop && log[5].a == 2, "a stop is logged");
    }
}

void TestMissionMusic() {
    Rig rig;
    int draws = 0;
    MissionMusic music([&draws] { return uint32_t(draws++); });
    MusicPlayer& player = rig.engine->Music();

    // A stage with a theme: it plays on slot 0 and draws no random number.
    music.StartStage(static_cast<int>(Frigate::Level::LevelId::Bunker1));
    Check(music.State() == MissionState::Theme, "a stage with no background starts on the theme");
    Check(player.CurrentTrack(0) == kMusicBunker1 && draws == 0, "its own theme plays");
    Check(player.CurrentTrack(1) == 0 && player.CurrentTrack(2) == 0, "nothing else plays");

    // The AI asks for the tense track, then lets it go.
    music.PlaySlot(0, 1, 2);
    music.TickSlots(1);
    Check(music.State() == MissionState::Tense, "a running slot raises the tense track");
    Check(player.CurrentTrack(1) == kMusicBunker1X, "the stage's tense track plays on slot 1");
    Check(player.FadeState(0) == kMusicFadeOut, "the theme fades out");
    music.TickSlots(200);
    Check(music.State() == MissionState::Theme, "it comes back when the slot runs out");
    Check(player.FadeState(0) == kMusicFadeIn, "the theme fades in");
    Check(player.FadeState(1) == kMusicFadeOut, "the tense track fades out");

    // The watch menu takes over and gives back what it found.
    music.EnterWatch();
    Check(music.State() == MissionState::Watch && player.CurrentTrack(1) == kMusicWatch, "the watch's music plays");
    music.LeaveWatch();
    Check(music.State() == MissionState::Theme, "the mission's state is restored");

    // A change the table does not list carries on into the next state's.
    music.SetState(MissionState::Theme);
    Check(player.FadeState(0) == kMusicFadeIn, "an unlisted change falls through to the next state's handler");

    music.StopSlot(-1);
    music.Stop();
    Check(music.State() == MissionState::Stopped, "stopped");
    Check(player.CurrentTrack(0) == 0 && player.CurrentTrack(1) == 0 && player.CurrentTrack(2) == 0,
          "every slot stops");

    // A stage with a background sound uses the third slot.
    music.StartStage(static_cast<int>(Frigate::Level::LevelId::Surface2));
    Check(music.State() == MissionState::ThemeBackground, "a background stage starts on its two tracks");
    Check(player.CurrentTrack(0) == kMusicSurface2 && player.CurrentTrack(2) == kMusicWind,
          "the theme and the wind play");
    music.Stop();

    // A stage with no theme draws a random one.
    const int before = draws;
    music.StartStage(static_cast<int>(Frigate::Level::LevelId::Complex));
    Check(draws == before + 1 && player.CurrentTrack(0) != 0, "an unlisted stage draws a random theme");
    music.Stop();

    Check(MissionMusic::TenseTrackFor(static_cast<int>(Frigate::Level::LevelId::Citadel)) < 0,
          "a stage with no tense track says so");
}

void TestAiSoundChannels() {
    Rig rig;
    int clockTimer = 1;
    bool locked = false;
    AiSoundChannels::Host host;
    host.clockTimer = [&clockTimer] { return clockTimer; };
    host.controlsLocked = [&locked] { return locked; };
    host.propPosition = [](int prop) -> std::optional<AiSoundChannels::Position> {
        if (prop != 7) {
            return std::nullopt;
        }
        return AiSoundChannels::Position{ 5500.0f, 0.0f, 0.0f };
    };
    AiSoundChannels channels(host);
    Frigate::Game::Sound::SetListenerSource(&gFailures, [] {
        return std::vector<std::array<float, 3>>{ { 0.0f, 0.0f, 0.0f } };
    });

    Check(channels.Volume(3) == 0, "an idle channel is silent");
    channels.Play(3, 1);
    rig.Render(2);
    Check(channels.Volume(3) == SHRT_MAX, "a played sound starts at full volume");
    Check(rig.engine->Sfx().ActiveStates() == 1, "it plays");

    // A second sound on a busy channel plays without being tracked by it.
    channels.Play(3, 1);
    Check(rig.engine->Sfx().ActiveStates() == 2, "a second sound plays");

    // No fade: the volume is set at once.
    channels.SetVolume(3, 1000, 0);
    Check(channels.Volume(3) == 1000, "a volume set with no fade applies at once");

    // A fade of ten ticks, five a frame.
    clockTimer = 5;
    channels.SetVolume(3, 0, 10);
    Check(channels.Volume(3) == 1000, "a fade has not started changing the volume yet");
    channels.Tick();
    Check(channels.Volume(3) == 500, "half way after five ticks");
    channels.Tick();
    Check(channels.Volume(3) == 0, "at the target after ten");

    // Heard from a prop 5500 units away, at full volume 5000 over 6000.
    clockTimer = 1;
    channels.EmitFromProp(3, 7, 0);
    Check(channels.Volume(3) == 5000, "a prop's sound is as loud as its distance says");
    channels.EmitFromPad(3, { 0.0f, 100.0f, 0.0f }, 0);
    Check(channels.Volume(3) == SHRT_MAX, "a pad's sound beside the listener is full volume");

    // Locked controls silence it.
    locked = true;
    channels.Tick();
    Check(channels.Volume(3) == 0, "locked controls mute the channel");
    locked = false;

    // Stopping and cleaning up end the sounds.
    channels.Stop(3);
    channels.Cleanup();
    rig.Render(8);
    Check(channels.Volume(3) == 0, "a stopped channel reads zero");
    channels.Tick();
    Check(channels.Volume(3) == 0, "and stays so");
    Frigate::Game::Sound::DeactivateAllSfxByFlag1();
    rig.Render(8);
    Check(rig.engine->Sfx().ActiveStates() == 0, "every sound ends");
    Frigate::Game::Sound::ClearListenerSource(&gFailures);

    channels.Reset();
    channels.Play(9, 1);
    Check(rig.engine->Sfx().ActiveStates() == 1, "a channel past the table still plays, untracked");
}

} // namespace

int main() {
    TestNothingInstalled();
    TestSfxHandleAndDeactivate();
    TestSlotVolumes();
    TestFadeRunsAtSixtyHertz();
    TestDistanceVolume();
    TestNearestOfEveryPlayer();
    TestCallLog();
    TestMissionMusic();
    TestAiSoundChannels();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
