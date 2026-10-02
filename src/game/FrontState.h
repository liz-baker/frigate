#pragma once

#include <cstddef>
#include <optional>
#include <vector>

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
    // select_ramrom_to_play's pick: the ramrom table entry the run-stage menu
    // plays.
    std::optional<size_t> ramromIndex;
    // Play these table entries in turn instead of the random pick (a testing
    // aid; empty plays the game's own selection).
    std::vector<size_t> demoOrder;
    size_t demoOrderNext = 0;
};

} // namespace Frigate::Game
