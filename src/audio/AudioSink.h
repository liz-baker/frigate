#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Ship {
class Audio;
}

// Feeds finished PCM audio (produced by AudioUcode, GE's decoded/mixed
// output) into libultraship's audio pipeline -- the native AX backend
// on Wii U, or whichever `AudioPlayer` backend is active elsewhere
// (`Audio::GetCurrentAudioBackend()`). This is the "final PCM-out sink"
// role frigate issue #3 describes, mirroring the graphics side's
// Interpreter -> GfxRenderingAPI hand-off.
//
// libultraship's `Context`/`Audio` have no global singleton accessor
// (dependencies are constructor-injected -- see Audio.h's class comment);
// callers pass in the `Audio` component their own Context owns, rather
// than this file reaching for one itself.
namespace Frigate::Audio {

// Frames buffered past which PushPcm backs off instead of pushing more,
// so a producer that's running ahead of the audio thread doesn't overrun
// AudioPlayer's own buffering (SDL drops silently past its own limit; the
// Wii U AX backend has no such guard and will overwrite unread samples).
// Matches AudioPlayer::AudioSettings' default DesiredBuffered; pass a
// smaller value to PushPcm for a backend configured with tighter buffering.
constexpr int32_t kDefaultMaxBufferedFrames = 2480;

// Pushes interleaved S16 PCM to `audio`'s active AudioPlayer. `samples`
// points to `sampleCount` interleaved int16_t values (i.e. frames *
// channel count, channel order/count per AudioPlayer::GetNumOutputChannels()).
//
// Returns false, pushing nothing, when: `audio` is null, its player isn't
// initialized, sampleCount isn't a whole number of frames for the
// player's current channel count, or the player already has at least
// maxBufferedFrames queued (backpressure -- call again once more room is
// available rather than blocking).
bool PushPcm(const std::shared_ptr<Ship::Audio>& audio, const int16_t* samples, size_t sampleCount,
             int32_t maxBufferedFrames = kDefaultMaxBufferedFrames);

} // namespace Frigate::Audio
