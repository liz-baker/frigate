#include "level/CheatInput.h"

#include "game/Input.h"
#include "game/menu/CheatIds.h"

namespace Frigate::Level {

namespace {

namespace Cheat = Game::Menu::Cheat;
namespace B = Game::Input::Button;

// requiredInputCount of every cheat with a button code.
constexpr int kCodeLength = 10;

// The codes of g_CheatInfo that apply to a single-player stage
// (CHEAT_MASK_SPGAME). The rest are for the menus and for multiplayer games.
constexpr uint16_t kInvincibilityCode[kCodeLength] = {
    B::kL | B::kPadDown,
    B::kR | B::kCRight,
    B::kR | B::kCUp,
    B::kL | B::kPadRight,
    B::kL | B::kCDown,
    B::kR | B::kCUp,
    B::kL | B::kPadRight,
    B::kR | B::kPadDown,
    B::kL | B::kPadLeft,
    B::kL | B::kR | B::kCRight,
};
constexpr uint16_t kAllGunsCode[kCodeLength] = {
    B::kL | B::kR | B::kPadDown,
    B::kL | B::kCLeft,
    B::kL | B::kCRight,
    B::kL | B::kR | B::kCLeft,
    B::kL | B::kPadDown,
    B::kL | B::kCDown,
    B::kR | B::kCLeft,
    B::kL | B::kR | B::kCRight,
    B::kR | B::kPadUp,
    B::kL | B::kCLeft,
};
constexpr uint16_t kMaxAmmoCode[kCodeLength] = {
    B::kL | B::kR | B::kCRight,
    B::kR | B::kPadUp,
    B::kR | B::kPadDown,
    B::kR | B::kPadDown,
    B::kL | B::kR | B::kCRight,
    B::kL | B::kR | B::kPadLeft,
    B::kR | B::kPadDown,
    B::kR | B::kPadUp,
    B::kL | B::kR | B::kCRight,
    B::kR | B::kPadLeft,
};
constexpr uint16_t kDebugUnk5Code[kCodeLength] = {
    B::kL | B::kPadUp,
    B::kR | B::kCRight,
    B::kR | B::kCDown,
    B::kL | B::kR | B::kCLeft,
    B::kL | B::kR | B::kPadLeft,
    B::kL | B::kR | B::kPadRight,
    B::kR | B::kPadDown,
    B::kR | B::kCUp,
    B::kR | B::kCRight,
    B::kL | B::kR | B::kPadDown,
};
constexpr uint16_t kLineModeCode[kCodeLength] = {
    B::kR | B::kCDown,
    B::kL | B::kR | B::kPadDown,
    B::kL | B::kPadRight,
    B::kR | B::kCUp,
    B::kL | B::kR | B::kCRight,
    B::kR | B::kPadUp,
    B::kL | B::kPadDown,
    B::kL | B::kPadRight,
    B::kR | B::kCLeft,
    B::kR | B::kCUp,
};
constexpr uint16_t kInvisibilityCode[kCodeLength] = {
    B::kR | B::kCLeft,
    B::kL | B::kR | B::kCUp,
    B::kL | B::kR | B::kPadLeft,
    B::kL | B::kR | B::kPadUp,
    B::kR | B::kPadUp,
    B::kL | B::kCLeft,
    B::kR | B::kCUp,
    B::kL | B::kCDown,
    B::kL | B::kR | B::kPadLeft,
    B::kR | B::kPadRight,
};

struct CheatCode {
    int32_t cheat;
    const uint16_t* buttons;
};

constexpr CheatCode kCodes[] = {
    { Cheat::kInvincibility, kInvincibilityCode },
    { Cheat::kAllGuns, kAllGunsCode },
    { Cheat::kMaxAmmo, kMaxAmmoCode },
    { Cheat::kDebugUnk5, kDebugUnk5Code },
    { Cheat::kLineMode, kLineModeCode },
    { Cheat::kInvisibility, kInvisibilityCode },
};

// cheatButtonCountBitsSet.
int CountBitsSet(uint16_t bits) {
    int count = 0;
    for (; bits != 0; bits >>= 1) {
        if ((bits & 1) != 0) {
            count++;
        }
    }
    return count;
}

} // namespace

std::vector<int32_t> CheatInput::Feed(uint16_t held, uint16_t pressed) {
    const uint16_t triggers = held & (B::kL | B::kR);
    held &= ~triggers;
    pressed &= ~triggers;

    if (pressed != 0) {
        // Only one button besides the triggers may be down, and it must be the
        // one that just went down.
        if (CountBitsSet(pressed) == 1 && CountBitsSet(held) == 1) {
            buffer_[static_cast<size_t>(index_)] = pressed | triggers;
            index_ = (index_ + 1) % kBufferSize;
            if (count_ < kBufferSize) {
                count_++;
            }
        } else {
            count_ = 0;
        }
    }

    std::vector<int32_t> matched;
    for (const CheatCode& code : kCodes) {
        if (count_ < kCodeLength) {
            continue;
        }
        int at = ((index_ - kCodeLength) + kBufferSize) % kBufferSize;
        int step = 0;
        for (; step < kCodeLength; step++) {
            if (buffer_[static_cast<size_t>(at)] != code.buttons[step]) {
                break;
            }
            at = (at + 1) % kBufferSize;
        }
        if (step == kCodeLength) {
            count_ = 0;
            matched.push_back(code.cheat);
        }
    }
    return matched;
}

} // namespace Frigate::Level
