#pragma once

#include <cstdint>

namespace Frigate::Game {

// viGetX()/viGetY() during the front-end. Cold boot leaves spectrum_related_flag
// set, so menu_init()'s first SwitchScreens pass switches to the 440x330 mode
// (screen_size = SCREEN_SIZE_440x330) that every menu screen, including the
// legal page's text coordinates, is authored in. The 320x240 alternative is a
// later options-menu setting.
constexpr int32_t kMenuScreenWidth = 440;
constexpr int32_t kMenuScreenHeight = 330;

} // namespace Frigate::Game
