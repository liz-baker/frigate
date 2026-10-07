#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

// The multiplayer character select's state and logic (init_menu0f_mpcharsel
// and interface_menu0F_mpcharsel) apart from the drawing and the wallet: each
// player's cursor over the roster, the scroll of their strip of portraits, and
// the choice they lock in, which goes to FrontState::multiplayer.character.
class MpCharSelectModel {
  public:
    // Where one portrait ends and the next begins in a strip, how far a strip
    // scrolls each frame, and the size at which a locked-in portrait is full
    // (size_mp_select_image_player's 0xb).
    static constexpr int32_t kPortraitStride = 0x54;
    static constexpr int32_t kScrollStep = 0xC;
    static constexpr int32_t kFullSize = 0xB;

    // init_menu0f_mpcharsel (without the portrait loads and the wallet): the
    // roster follows the unlocks, a player whose character is no longer
    // offered is moved, and every strip starts on the player's character with
    // nothing locked in.
    void Init(FrontState& front);

    // One frame of interface_menu0F_mpcharsel after the view setup, with each
    // controller's reading in `inputs` (the first `kMpMaxPlayers` ports).
    // Moves `front.cursor` with the first controller's stick, raises sounds
    // into Sfx(), and returns whether every player has locked in, which sends
    // the game back to the options.
    bool Update(const std::array<Input::Sample, kMpMaxPlayers>& inputs, FrontState& front);

    // joyGetStickXInRange(player, -2, 1): the stick's x position as one of
    // -2 to 1. A centred stick reads -1; 1 is 60 and over to the right and
    // -2 is -21 and under to the left.
    static int StickXInRange(int8_t stickX);

    // mp_char_cur_select_player: the character under the player's cursor.
    int Cursor(int player) const {
        return cursor_[static_cast<size_t>(player)];
    }
    // mp_char_prev_select_player: the portrait the strip is centred on, which
    // trails Cursor while it scrolls.
    int Previous(int player) const {
        return previous_[static_cast<size_t>(player)];
    }
    // mp_char_select_scroll_offset: how far, 0 to kPortraitStride - 1, the
    // strip is past Previous.
    int32_t ScrollOffset(int player) const {
        return scroll_[static_cast<size_t>(player)];
    }
    // size_mp_select_image_player: 0 while choosing, growing to kFullSize
    // once the choice is locked in.
    int32_t ImageSize(int player) const {
        return imageSize_[static_cast<size_t>(player)];
    }
    // The roster size the strips scroll through (num_chars_selectable_mp).
    int NumCharacters() const {
        return numCharacters_;
    }
    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    std::array<int, kMpMaxPlayers> cursor_{};
    std::array<int, kMpMaxPlayers> previous_{};
    std::array<int32_t, kMpMaxPlayers> scroll_{};
    std::array<int32_t, kMpMaxPlayers> imageSize_{};
    int numCharacters_ = 0;
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
