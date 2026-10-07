#pragma once

#include <cstddef>

namespace Frigate::Game::Menu::TitleString {

// TITLE_STR_xxx: the slots of LTITLE the folder screens share.
constexpr size_t kStart = 4;
constexpr size_t kNext = 5;
constexpr size_t kPrevious = 6;
// TITLE_STR_19_AGENT to TITLE_STR_22_007: the difficulties' names.
constexpr size_t kFirstDifficultyName = 19;
constexpr size_t kJamesBond = 32;
constexpr size_t kMission = 33;
constexpr size_t kPart = 34;

// The multiplayer options screen's labels (TITLE_STR_76_MPOPTIONS to
// TITLE_STR_84_AIM and TITLE_STR_286_CONTROLSTYLE).
constexpr size_t kMpOptions = 76;
constexpr size_t kPlayers = 77;
constexpr size_t kScenario = 78;
constexpr size_t kLevel = 79;
constexpr size_t kGameLength = 80;
constexpr size_t kWeapons = 81;
constexpr size_t kCharacter = 82;
constexpr size_t kHealth = 83;
constexpr size_t kAim = 84;
constexpr size_t kControlStyle = 286;

// Runs of values in MpGameLength, MpScenario and aim setting order:
// TITLE_STR_45_UNLIMITED to 52_LASTALIVE, 53_NORMAL to 60_2V1 and
// 72_SOFFAOFF to 75_SONAON.
constexpr size_t kFirstGameLength = 45;
constexpr size_t kFirstScenario = 53;
constexpr size_t kFirstAimSetting = 72;

// multi_stage_setups' folder_text_preset for each MpStage, in order.
constexpr size_t kMpStageFolderText[] = { 154, 156, 158, 162, 160, 269, 271, 164, 166, 168, 170, 172 };

} // namespace Frigate::Game::Menu::TitleString
