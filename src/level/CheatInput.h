#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Frigate::Level {

// The buffer of button presses the cheat codes are read from (cheatInputBuffer,
// cheatInputBufferIndex and cheatInputCount on the player), in a stage or in
// the menus.
class CheatInput {
  public:
    // disableOnscreenCheatText.
    void Clear() {
        index_ = 0;
        count_ = 0;
    }

    // cheat_buttons_mp_related: `held` and `pressed` are joyGetButtons and
    // joyGetButtonsPressedThisFrame. Returns the cheats applicable under
    // `applicableMask` (CheatApplicableMask) whose codes the buffer now ends
    // with, in cheatTryActivateFromInputBuffer's order, each of which
    // cheatTryActivateFromInputBuffer would switch.
    std::vector<int32_t> Feed(uint16_t held, uint16_t pressed, uint32_t applicableMask);

    // CHEAT_INPUT_BUFFER_SIZE.
    static constexpr int kBufferSize = 20;

  private:
    std::array<uint16_t, kBufferSize> buffer_{};
    int index_ = 0;
    int count_ = 0;
};

// The ten buttons (a trigger mask and the button pressed with it) of the code
// that switches the cheat on, in a stage or in the menus, or null for a cheat
// without one.
const uint16_t* CheatCodeFor(int32_t cheat);

} // namespace Frigate::Level
