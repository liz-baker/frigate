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
    // MENU_MISSION_SELECT: where the single player choice goes.
    MissionSelect,
    // MENU_MP_OPTIONS: where the multiplayer choice goes.
    MultiplayerOptions,
    // MENU_CHEAT: where the cheat choice goes.
    Cheat,
    // MENU_NO_CONTROLLERS.
    NoControllers,
    // MENU_DIFFICULTY and MENU_007_OPTIONS.
    Difficulty,
    Options007,
    Briefing,
    // MENU_RUN_STAGE: the stage the front-end hands the game loop to.
    RunStage,
    DisplayCast,
};

} // namespace Frigate::Game
