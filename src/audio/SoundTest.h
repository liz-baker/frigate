#pragma once

#include <memory>
#include <string>

namespace Ship {
class Context;
}

// Plays one sound effect through the real audio backend and returns, so the
// audio path can be checked without any game code (`just play_sound`).
namespace Frigate::Audio {

// Plays the sound named by `sound` (see SoundNames::Find) from the `audio/sfx`
// bank in the loaded archives, to the end of its envelope plus the reverb
// tail. A sound that holds until released (a loop) is released after
// `maxSeconds`. Returns a process exit code.
int RunSoundTest(const std::shared_ptr<Ship::Context>& context, const std::string& sound, double maxSeconds = 6.0);

} // namespace Frigate::Audio
