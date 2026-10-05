#pragma once

#include <atomic>

namespace Frigate::Game {

// The control style the menu's Controls tab forces on live play, as a
// Level::ControlConfig value; -1 follows the option stored in the folder's
// save. Set from the menu, read by the stage's input step.
inline std::atomic<int>& ControlStyleOverride() {
    static std::atomic<int> style{ -1 };
    return style;
}

} // namespace Frigate::Game
