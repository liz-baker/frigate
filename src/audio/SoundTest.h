#pragma once

#include <memory>
#include <string>

namespace Ship {
class Context;
}

// Plays one sound effect or music track through the real audio backend, so the
// audio path can be checked without any game code (`just play_sound`).
namespace Frigate::Audio {

// Plays the sound named by `sound` (see SoundNames::Find) from the `audio/sfx`
// bank in the loaded archives, to the end of its envelope plus the reverb
// tail. A sound that holds until released (a loop) is released after
// `maxSeconds`.
//
// A name that is not a sound effect but is a music track (`M_INTRO`, or
// anything under `audio/music/`) plays that track on the first music slot
// instead, restarting it whenever it ends, until interrupted with Ctrl-C or,
// when `musicSeconds` is above zero, for that many seconds. Returns a process
// exit code.
int RunSoundTest(const std::shared_ptr<Ship::Context>& context, const std::string& sound, double maxSeconds = 6.0,
                 double musicSeconds = 0.0);

} // namespace Frigate::Audio
