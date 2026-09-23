#pragma once

namespace Frigate::Game {

// A small, fixed subset of the GoldenEye decomp's `MENU` enum
// (external/007/src/game/front.h) -- only the states frigate's menu state
// machine actually drives so far. Extended as later menu/scene work needs
// more.
enum class MenuId {
    // No menu selected yet -- matches the decomp's MENU_INVALID sentinel
    // (current_menu's value before the very first frontChangeMenu()).
    Invalid,

    // The ~4-frame black transition every menu-to-menu switch passes
    // through (front.c's MENU_SWITCH_SCREENS/interface_menu17_switchscreens)
    // -- not a menu with content of its own.
    SwitchScreens,

    LegalScreen,
    NintendoLogo,
};

} // namespace Frigate::Game
