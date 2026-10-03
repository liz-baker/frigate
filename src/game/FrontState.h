#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "game/MenuId.h"

namespace Frigate::Game {

namespace Save {
class SaveManager;
}

// The front-end globals (front.c) that more than one intro screen reads or
// writes, at their cold-boot values.
struct FrontState {
    bool isFirstTimeOnMainMenu = true;
    // Set when a button press skipped an intro screen; the GoldenEye logo
    // then goes to the file select instead of the cast sequence.
    bool prevKeypresses = false;
    bool geLogoBool = false;
    bool fullActorIntro = false;
    // maybe_is_in_menu: the front-end music (the folders' track) still has to
    // start. The intro screens and a stage's return to the title set it; the
    // first screen past them clears it.
    bool maybeIsInMenu = true;
    // intro_character_index: the cast screen's current entry.
    int introCharacterIndex = 0;
    // select_ramrom_to_play's pick: the ramrom table entry the run-stage menu
    // plays.
    std::optional<size_t> ramromIndex;
    // Play these table entries in turn instead of the random pick (a testing
    // aid; empty plays the game's own selection).
    std::vector<size_t> demoOrder;
    size_t demoOrderNext = 0;
    // The `saves` array and its file2.c routines; the file, mode and mission
    // screens read and update the player's folders through it.
    std::shared_ptr<Save::SaveManager> save;
};

// The menu the front-end returns to when the title stage loads
// (init_menus_or_reset). The frame the demo stage is requested, the run-stage
// interface still sees the playback flag set (the stage that clears it has not
// loaded yet) and asks for the legal screen, so that is where it returns
// unless a skipped screen (prev_keypresses) sends it to the file select.
inline MenuId MenuAfterTitleStageLoad(const FrontState& front) {
    return front.prevKeypresses ? MenuId::FileSelect : MenuId::LegalScreen;
}

} // namespace Frigate::Game
