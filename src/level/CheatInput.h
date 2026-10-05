#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Frigate::Level {

// The buffer of button presses the in-stage cheat codes are read from
// (cheatInputBuffer, cheatInputBufferIndex and cheatInputCount on the player).
class CheatInput {
  public:
    // disableOnscreenCheatText.
    void Clear() {
        index_ = 0;
        count_ = 0;
    }

    // cheat_buttons_mp_related for a single-player stage: `held` and `pressed`
    // are joyGetButtons and joyGetButtonsPressedThisFrame. Returns the cheats
    // whose codes the buffer now ends with, in cheatTryActivateFromInputBuffer's
    // order, each of which cheatTryActivateFromInputBuffer would switch.
    std::vector<int32_t> Feed(uint16_t held, uint16_t pressed);

    // CHEAT_INPUT_BUFFER_SIZE.
    static constexpr int kBufferSize = 20;

  private:
    std::array<uint16_t, kBufferSize> buffer_{};
    int index_ = 0;
    int count_ = 0;
};

} // namespace Frigate::Level
