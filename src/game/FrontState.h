#pragma once

namespace Frigate::Game {

// The front-end globals (front.c) that more than one intro screen reads or
// writes, at their cold-boot values.
struct FrontState {
    bool isFirstTimeOnMainMenu = true;
    // Set when a button press skipped an intro screen; the GoldenEye logo
    // then goes to the file select instead of the cast sequence.
    bool prevKeypresses = false;
    bool geLogoBool = false;
    bool fullActorIntro = false;
    // intro_character_index: the cast screen's current entry.
    int introCharacterIndex = 0;
};

} // namespace Frigate::Game
