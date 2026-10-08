#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "AudioEngine.h"
#include "SfxIds.h"

// The game's sound and music calls (snd.c and music.c as the rest of the game
// sees them), over the AudioEngine that Install gives it. Each function is the
// decomp function of the name in its comment; the game code calls these where
// the original calls those.
//
// Every call takes one lock, shared with RenderFrame, so the game's thread and
// the one that renders audio may call freely. With no engine installed (no
// audio archive, no output device) the calls do nothing and sounds return
// null, as the original's disabled-sound flag (g_sndBootswitchSound) does.
namespace Frigate::Game::Sound {

using AudioDriver::MusicFadeState;
using AudioDriver::MusicTrack;
using AudioDriver::SfxId;
using AudioDriver::SoundState;

// The game's retrace rate, which musicFadeTick runs at. RenderFrame's 736
// samples at 22048 Hz last two of them.
constexpr int kFadeTicksPerAudioFrame = 2;

// Lifecycle.
//
// Install makes `engine` (which must outlive the installation) the one the
// calls below use; Uninstall detaches it. The engine's own thread-safety rule
// is met by the lock the calls and RenderFrame share.
void Install(AudioDriver::AudioEngine* engine);
void Uninstall();
bool Installed();

// What the game asked the sound layer to start or stop, in order, for tests
// that check a call with no ears: the log is written whether or not an engine
// is installed.
struct Call {
    enum class Kind {
        PlaySfx,
        Deactivate,
        DeactivateAllByFlag,
        MusicPlay,
        MusicStop,
        MusicFadeOut,
        MusicFadeIn,
    };
    Kind kind;
    // The sound, flag or music slot.
    int a = 0;
    // The music track.
    int b = 0;
};
// Appends each call to `log` until it is set to null.
void SetCallLog(std::vector<Call>* log);

// Produces the next AudioEngine frame (AudioEngine::kFrameSamples stereo
// frames): musicFadeTick's retraces of that frame, then the synth. Silence
// when nothing is installed.
void RenderFrame(int16_t* out);

// snd.c

// sndPlaySfx on the game's sound-effect bank. `pending` is the game's handle
// for the sound (a SoundState* it keeps in a record): set to the new state,
// and reset to null when the sound ends. The handle must outlive the sound.
SoundState* PlaySfx(int16_t sound, SoundState** pending = nullptr);
// A handle that is about to go away while its sound plays on stops being
// written to when the sound ends.
void DetachHandle(SoundState** handle);
// sndGetPlayingState: 0 when the state is null or its sound has ended.
uint8_t GetPlayingState(const SoundState* state);
// sndDeactivate.
void Deactivate(SoundState* state);
// sndDeactivateAllSfxByFlag and its three fixed forms.
void DeactivateAllSfxByFlag(uint8_t flag);
void DeactivateAllSfxByFlag1();
void DeactivateAllSfxByFlag11();
void DeactivateAllSfxByFlag3();
// sndCreatePostEvent.
void CreatePostEvent(SoundState* state, int16_t eventType, int32_t arg);
// sndCreatePostEvent(state, AL_SNDP_VOL_EVT, volume): the volume the game
// sets for a positional or channel sound, 0 to 32767.
void SetVolume(SoundState* state, int32_t volume);
// sndGetSfxSlotFirstNaturalVolume, sndApplyVolumeAllSfxSlot,
// sndSetScalerApplyVolumeAllSfxSlot.
uint16_t GetSfxSlotFirstNaturalVolume();
void ApplyVolumeAllSfxSlot(uint16_t volume);
void SetScalerApplyVolumeAllSfxSlot(float scale);

// music.c, with the slot (0 to 2) in place of the 1/2/3 of the function names.

// musicTrackNPlay.
void MusicTrackPlay(int slot, int track);
// musicTrackNStop.
void MusicTrackStop(int slot);
// musicTrackNGetVolume.
uint16_t MusicTrackGetVolume(int slot);
// musicTrackNApplySeqpVol.
void MusicTrackApplySeqpVol(int slot, uint16_t volume);
// musicTrackNSaveCurrentVolumeAsTrackDefault.
void MusicTrackSaveCurrentVolumeAsTrackDefault(int slot);
// musicTrackNFadeOut and musicTrackNFadeIn.
void MusicTrackFadeOut(int slot, float seconds);
void MusicTrackFadeIn(int slot, float seconds, uint16_t volume);
// g_musicXTrackNFade, which the game reads and writes directly.
int32_t MusicTrackFade(int slot);
void SetMusicTrackFade(int slot, int32_t fade);

// options.c's music volume (mTrack2Vol): the volume the watch's slider sets
// and the stage's music plays at, on slot 1 through set_mTrack2Vol.
uint16_t GetMTrack2Vol();
void SetMTrack2Vol(uint16_t volume);

// Where the sounds are heard from: the players' positions (a prop's `pos`),
// asked for each time a positional volume is worked out, as the original reads
// the players' props then. Positional volumes are measured to the nearest.
//
// `owner` names who set the source, so that ClearListenerSource from someone
// else (a stage being torn down after another took over) leaves it alone.
using ListenerSource = std::function<std::vector<std::array<float, 3>>()>;
void SetListenerSource(const void* owner, ListenerSource source);
void ClearListenerSource(const void* owner);

// propobj.c's distance-to-volume helpers, 0 to 32767. A sound is full volume
// within 200 units, falls with the square root to `min`, then linearly to
// silence at `max`.
// sub_GAME_7F0537B8.
int32_t VolumeForDistance(float distance, float min, float max);
// sub_GAME_7F053894: the volume at `pos` for the nearest listener.
int32_t VolumeAtPosition(const std::array<float, 3>& pos, float low, float high);
// sub_GAME_7F0539B8 and sub_GAME_7F0539E4: the same over 5000 to 6000 units.
int32_t VolumeForDistanceDefault(float distance);
int32_t VolumeAtPositionDefault(const std::array<float, 3>& pos);
// chrobjSndCreatePostEvent and chrobjSndCreatePostEventDefault: sets a
// playing sound's volume for its position.
void PostVolumeAtPosition(SoundState* state, const std::array<float, 3>& pos, float low, float high);
void PostVolumeAtPositionDefault(SoundState* state, const std::array<float, 3>& pos);

} // namespace Frigate::Game::Sound
