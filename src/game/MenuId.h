#pragma once

namespace Frigate::Game {

// A small, fixed subset of the GoldenEye decomp's `MENU` enum (front.h) --
// only the states frigate's menu state machine actually drives so far.
enum class MenuId {
    // Matches the decomp's MENU_INVALID sentinel.
    Invalid,

    // The ~4-frame black transition every menu-to-menu switch passes
    // through (MENU_SWITCH_SCREENS/interface_menu17_switchscreens) -- not a
    // menu with content of its own.
    SwitchScreens,

    LegalScreen,
    NintendoLogo,
};

} // namespace Frigate::Game
