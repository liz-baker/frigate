#pragma once

namespace Frigate::Game {

// A small, fixed subset of the GoldenEye decomp's `MENU` enum (front.h) --
// only the states frigate's menu state machine drives or targets so far. A
// state with no registered handlers just draws the black clear.
enum class MenuId {
    // Matches the decomp's MENU_INVALID sentinel.
    Invalid,

    // The ~4-frame black transition every menu-to-menu switch passes
    // through (MENU_SWITCH_SCREENS/interface_menu17_switchscreens) -- not a
    // menu with content of its own.
    SwitchScreens,

    LegalScreen,
    NintendoLogo,
    RarewareLogo,
    EyeIntro,
    GoldenEyeLogo,
    FileSelect,
    // MENU_MODE_SELECT: where the file select continues once a folder is chosen.
    ModeSelect,
    // MENU_RUN_STAGE: the stage the front-end hands the game loop to.
    RunStage,
    DisplayCast,
};

} // namespace Frigate::Game
