#include "level/WatchMenu.h"

#include <algorithm>
#include <cmath>

#include "level/Lang.h"
#include "level/Matrix.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;

// WATCHZOOM1 to WATCHZOOM3 (US): the field of view each page settles at,
// reached over 15 ticks.
constexpr float kZoom1 = 4.6f;
constexpr float kZoom2 = 5.9f;
constexpr float kZoom3 = 3.95f;
constexpr float kPageZoomTime = 15.0f;

// WATCH_SCREEN_SELECT_RECTANGLE_HSTEP and the tab geometry
// trigger_solo_watch_menu builds the page tabs with.
constexpr int32_t kTabStep = 125;
constexpr int32_t kTabWidth = 100;
constexpr int32_t kTabHeight = 20;
constexpr int32_t kTabLeft = -299;
constexpr int32_t kTabTop = 310;
// The static's bar: the width of the face, as tall as a tab.
constexpr int32_t kStaticWidth = 920;
constexpr int32_t kStaticLeft = -460;

// The line height of the inventory page's list.
constexpr int32_t kListLineHeight = 12;

} // namespace

void WatchMenu::Reset() {
    page_ = WatchPage::MissionStatus;
    controllerIndex_ = 0;
    gameOptionsIndex_ = 0;
    briefIndex_ = BriefIndex::Objectives;
    abortConfirmed_ = false;
    itemSelected_ = false;
    inventoryTextY_ = 0;
    inventoryTextTargetY_ = 0;
    curItemIndex_ = 0;
    inventoryCursor_ = 0.0f;
    inventorySettled_ = false;
    equipFlash_ = 0;
    styleScroll_ = 0.0f;
    styleIndex_ = 0;
    styleTarget_ = 0;
    styleOffset_ = 0;
    styleSettled_ = true;
    styleCount_ = 8;
    styleSynced_ = false;
    stickLrEnabled_ = false;
    stickYReady_ = false;
    stickYPrevActive_ = false;
    incompleteColour_ = 0xFF00A0;
    incompleteCounter_ = 10;
    objectiveGreen_ = 0xFF;
    objectiveCounter_ = 10;
    green_ = 0xE0;
    scanlineAlpha_ = 0;
    scanlineY_ = 0;
    itemAngle_ = 0.0f;
    spin_ = {};
    outsideMenu_ = true;
    closeRequested_ = false;
}

void WatchMenu::SyncControlType() {
    // cur_player_set_control_type, which the stage's load of the saved
    // settings runs after the reset.
    if (!styleSynced_ && links_.options != nullptr) {
        const int32_t type = static_cast<int32_t>(links_.options->controlType);
        styleIndex_ = type;
        styleTarget_ = type;
        styleScroll_ = static_cast<float>(type);
        styleOffset_ = -(kStyleLineHeight * type);
        styleSettled_ = true;
        styleSynced_ = true;
    }
}

void WatchMenu::Begin() {
    SyncControlType();
    links_.hud->MakeWatchGauges(links_.host->Armour(), links_.host->Health());
    ResetPages();

    // The page tabs, one rectangle for each page, and the static's bar.
    for (int32_t page = 0; page < WatchPage::Count; page++) {
        MakeRectangle(tabVertices_.data() + (page * 4), page * kTabStep, 0, kTabWidth, kTabHeight, kTabLeft, kTabTop);
    }
    MakeRectangle(staticVertices_.data(), 0, 0, kStaticWidth, kTabHeight, kStaticLeft, 0);
}

void WatchMenu::RedrawGauges() {
    links_.hud->MakeWatchGauges(links_.host->Armour(), links_.host->Health());
}

void WatchMenu::MakeRectangle(Vtx_t* vertices, int32_t startX, int32_t startZ, int32_t width, int32_t height,
                              int32_t horizontalOffset, int32_t verticalOffset) {
    // setup_watch_rectangles: the corners in the order (x0, z0), (x0, z1),
    // (x1, z0), (x1, z1), the colour of an unselected tab.
    int32_t x = startX + horizontalOffset;
    for (int32_t i = 0; i < 2; i++, x += width) {
        int32_t z = startZ + verticalOffset;
        for (int32_t j = 0; j < 2; j++, z += height) {
            Vtx_t& vertex = *vertices++;
            vertex.ob[0] = static_cast<int16_t>(x);
            vertex.ob[1] = 0;
            vertex.ob[2] = static_cast<int16_t>(z);
            vertex.flag = 0;
            vertex.tc[0] = 0;
            vertex.tc[1] = 0;
            vertex.cn[0] = 0x20;
            vertex.cn[1] = 0x70;
            vertex.cn[2] = 0x20;
            vertex.cn[3] = 0xF0;
        }
    }
}

void WatchMenu::ResetPages() {
    // sub_GAME_7F0A69A8.
    styleCount_ = input_.controllers < 2 ? 4 : 8;
    itemSelected_ = false;
    page_ = WatchPage::MissionStatus;
    briefIndex_ = BriefIndex::Objectives;
    DetermineEquippedItem();
}

void WatchMenu::SaveSettings() {
    // deleteCurrentSelectedFolder: fileClearSavefileForFolder saves the
    // options into the selected save folder.
    if (settingsSaver_) {
        settingsSaver_();
    }
}

void WatchMenu::Exit() {
    SaveSettings();
    // TODO(https://github.com/liz-baker/frigate/issues/241): sub_GAME_7F0C1340,
    // which returns the music to the mission's.
}

bool WatchMenu::StickLeft() const {
    return StickX() < -0x2D;
}

bool WatchMenu::StickRight() const {
    return !(StickX() < 0x2E);
}

bool WatchMenu::StickUp() const {
    return !(StickY() < 0x2E);
}

bool WatchMenu::StickDown() const {
    return StickY() < -0x2D;
}

bool WatchMenu::NavLeft() const {
    return StickLeft() && stickLrEnabled_;
}

bool WatchMenu::NavRight() const {
    return StickRight() && stickLrEnabled_;
}

bool WatchMenu::NavUp() const {
    return StickUp() && stickYReady_;
}

bool WatchMenu::NavDown() const {
    return StickDown() && stickYReady_;
}

bool WatchMenu::FineUp() const {
    return !(StickY() < 0x10);
}

bool WatchMenu::FineDown() const {
    return StickY() < -0xF;
}

void WatchMenu::StartStatic() {
    // sub_GAME_7F0A51D8.
    green_ = 0x80;
    // TODO(https://github.com/liz-baker/frigate/issues/41): WATCH_STATIC_SFX.
}

void WatchMenu::ChangePageEffects() {
    // sub_GAME_7F0A5210.
    stickLrEnabled_ = false;
    // TODO(https://github.com/liz-baker/frigate/issues/41): CAMERA_BEEP1_SFX.
    if ((kPageStaticChance << 16) < links_.random()) {
        StartStatic();
    }
}

void WatchMenu::BeepToggleSelection() {
    // watch_play_beep_sound.
    if (itemSelected_) {
        itemSelected_ = false;
    } else {
        itemSelected_ = true;
        // TODO(https://github.com/liz-baker/frigate/issues/41): CAMERA_BEEP1_SFX.
    }
}

void WatchMenu::Tick(int32_t speedFrames) {
    // sub_GAME_7F0A6A80.
    if (Pressed(Buttons::Start)) {
        closeRequested_ = true;
    }

    if (!stickLrEnabled_) {
        if (StickX() >= -0xA && StickX() < 0xB) {
            stickLrEnabled_ = true;
        } else if (StickX() < 0xB && input_.prevStickX >= 0xB) {
            stickLrEnabled_ = true;
        } else if (StickX() >= -0xA && input_.prevStickX < -0xA) {
            stickLrEnabled_ = true;
        }
    }

    if (!stickYReady_) {
        if (StickY() >= -0xA && StickY() < 0xB) {
            stickYReady_ = true;
        } else if (StickY() < 0xB && input_.prevStickY >= 0xB) {
            stickYReady_ = true;
        } else if (StickY() >= -0xA && input_.prevStickY < -0xA) {
            stickYReady_ = true;
        }
    }

    // The colour of "incomplete" and of the objectives cycles.
    const int32_t incompleteCounter = incompleteCounter_;
    if (incompleteCounter < 0) {
        incompleteColour_ -= 0x100000;
    }
    incompleteCounter_ = incompleteCounter - 1;
    if (incompleteColour_ < 0x5F00A1u) {
        incompleteColour_ = 0xFF00A0u;
        incompleteCounter_ = 0xF;
    }

    const int32_t objectiveCounter = objectiveCounter_;
    if (objectiveCounter < 0) {
        objectiveGreen_ -= 0x10;
    }
    objectiveCounter_ = objectiveCounter - 1;
    if (objectiveGreen_ < 0x60u) {
        objectiveGreen_ = 0xFFu;
        objectiveCounter_ = 0xF;
    }

    itemAngle_ += (kItemSpin * static_cast<float>(speedFrames) * kTau) / 360.0f;
    itemAngle_ = WrapAroundPi(itemAngle_);

    if ((kStaticChance << 16) < links_.random()) {
        StartStatic();
    }

    if (green_ < 0xE0) {
        const uint32_t draw = links_.random();
        green_ += static_cast<int32_t>(draw >> 0x1E);
    }
    if (green_ > 0xE0) {
        green_ = 0xE0;
    }

    scanlineAlpha_ = (-green_ * 4) + 0x380;
    scanlineY_ -= 4;
    if (scanlineY_ >= 0x157) {
        scanlineY_ = -0x156;
    }
    if (scanlineY_ < -0x156) {
        scanlineY_ = 0x156;
    }

    switch (page_) {
        case WatchPage::MissionStatus:
            NavigateStatus();
            break;
        case WatchPage::ControlOptions:
            if (controllerIndex_ == 0) {
                NavigateControlStyleEntry();
            } else if (controllerIndex_ == 1) {
                NavigateControlInputsEntry();
            }
            NavigateControl();
            break;
        case WatchPage::GameOptions:
            switch (gameOptionsIndex_) {
                case 0:
                    NavigateMusicVolume();
                    break;
                case 1:
                    NavigateEffectsVolume();
                    break;
                default:
                    if (gameOptionsIndex_ >= 2 && gameOptionsIndex_ <= 9) {
                        NavigateGameOptionsList();
                    }
                    break;
            }
            NavigateGame();
            break;
        case WatchPage::MissionBriefing:
            NavigateBriefingPage();
            break;
        case WatchPage::Inventory:
            NavigateInventoryPage();
            break;
        default:
            break;
    }
}

float WatchMenu::WrapAroundPi(float angle) {
    // watchWrapAroundPI.
    constexpr float kPi = 3.1415927f;
    if (angle > kPi) {
        return angle - kTau;
    }
    if (angle < -kPi) {
        return angle + kTau;
    }
    return angle;
}

// watch_screen0_navigation.
void WatchMenu::NavigateStatus() {
    if (!itemSelected_) {
        bool toBriefing = false;
        bool toInventory = false;

        // get_debug_gunwatchpos_flag is never set.
        if (Pressed(Buttons::L | Buttons::LeftC)) {
            toBriefing = true;
        }
        if (Pressed(Buttons::R | Buttons::RightC)) {
            toInventory = true;
        }
        if (Pressed(Buttons::LeftPad) || NavLeft()) {
            toBriefing = true;
        }
        if (Pressed(Buttons::RightPad) || NavRight()) {
            toInventory = true;
        }

        if (toBriefing) {
            page_ = WatchPage::MissionBriefing;
            ChangePageEffects();
            links_.owner->TriggerZoom(kZoom1, kPageZoomTime);
        }
        if (toInventory) {
            page_ = WatchPage::Inventory;
            ChangePageEffects();
            links_.owner->TriggerZoom(kZoom1, kPageZoomTime);
            return;
        }
    } else if (abortConfirmed_ && Pressed(Buttons::Z | Buttons::A)) {
        abortConfirmed_ = false;
        links_.host->AbortMission();
        SaveSettings();
    }
}

// watch_screen1_navigation.
void WatchMenu::NavigateInventoryPage() {
    if (!itemSelected_) {
        bool toStatus = false;
        bool toControl = false;

        if (Pressed(Buttons::L | Buttons::LeftC)) {
            toStatus = true;
        }
        if (Pressed(Buttons::R | Buttons::RightC)) {
            toControl = true;
        }
        if (Pressed(Buttons::LeftPad) || NavLeft()) {
            toStatus = true;
        }
        if (Pressed(Buttons::RightPad) || NavRight()) {
            toControl = true;
        }

        if (toStatus) {
            page_ = WatchPage::MissionStatus;
            abortConfirmed_ = false;
            ChangePageEffects();
            links_.owner->TriggerZoom(kZoom2, kPageZoomTime);
        }
        if (toControl) {
            page_ = WatchPage::ControlOptions;
            stickLrEnabled_ = false;
            ChangePageEffects();
            links_.owner->TriggerZoom(kZoom3, kPageZoomTime);
        }
    }
}

// watch_screen2_navigation.
void WatchMenu::NavigateControl() {
    if (Pressed(Buttons::LeftC | Buttons::L | Buttons::LeftPad) || NavLeft()) {
        if (!Held(Buttons::Z) && !itemSelected_) {
            page_ = WatchPage::Inventory;
            ChangePageEffects();
            links_.owner->TriggerZoom(kZoom1, kPageZoomTime);
            return;
        }
    }
    if (Pressed(Buttons::RightC | Buttons::R | Buttons::RightPad) || NavRight()) {
        if (!Held(Buttons::Z) && !itemSelected_) {
            page_ = WatchPage::GameOptions;
            gameOptionsIndex_ = 0;
            stickLrEnabled_ = false;
        }
    }
}

// watch_screen3_navigation.
void WatchMenu::NavigateGame() {
    if (Pressed(Buttons::LeftC | Buttons::L | Buttons::LeftPad) || NavLeft()) {
        if (!Held(Buttons::Z) && !itemSelected_) {
            page_ = WatchPage::ControlOptions;
            controllerIndex_ = 0;
            stickLrEnabled_ = false;
            return;
        }
    }
    if (Pressed(Buttons::RightC | Buttons::R | Buttons::RightPad) || NavRight()) {
        if (!Held(Buttons::Z) && !itemSelected_) {
            page_ = WatchPage::MissionBriefing;
            ChangePageEffects();
            links_.owner->TriggerZoom(kZoom1, kPageZoomTime);
        }
    }
}

// watch_screen4_navigation.
void WatchMenu::NavigateBriefingPage() {
    if (Pressed(Buttons::LeftC | Buttons::L | Buttons::LeftPad) || NavLeft()) {
        if (!itemSelected_) {
            page_ = WatchPage::GameOptions;
            gameOptionsIndex_ = 0;
            ChangePageEffects();
            links_.owner->TriggerZoom(kZoom3, kPageZoomTime);
            return;
        }
    }
    if (Pressed(Buttons::RightC | Buttons::R | Buttons::RightPad) || NavRight()) {
        if (!itemSelected_) {
            page_ = WatchPage::MissionStatus;
            abortConfirmed_ = false;
            ChangePageEffects();
            links_.owner->TriggerZoom(kZoom2, kPageZoomTime);
        }
    }
}

// controller_options_controlstyle_navigation.
void WatchMenu::NavigateControlStyleEntry() {
    if (Pressed(Buttons::UpC | Buttons::UpPad) || NavUp()) {
        if (!itemSelected_) {
            controllerIndex_ = 1;
            stickYReady_ = false;
            return;
        }
    }
    if (Pressed(Buttons::DownC | Buttons::DownPad) || NavDown()) {
        if (!itemSelected_) {
            controllerIndex_ = 1;
            stickYReady_ = false;
        }
    }
}

// controller_options_inputs_navigation.
void WatchMenu::NavigateControlInputsEntry() {
    if (Pressed(Buttons::UpC | Buttons::UpPad) || NavUp()) {
        if (!itemSelected_) {
            controllerIndex_ = 0;
            stickYReady_ = false;
            return;
        }
    }
    if (Pressed(Buttons::DownC | Buttons::DownPad) || NavDown()) {
        if (!itemSelected_) {
            controllerIndex_ = 0;
            stickYReady_ = false;
        }
    }
}

// sub_GAME_7F0A5998.
void WatchMenu::NavigateGameOptionsList() {
    if (Pressed(Buttons::UpC | Buttons::UpPad) || NavUp()) {
        gameOptionsIndex_--;
        stickYReady_ = false;
        itemSelected_ = false;
    } else if (Pressed(Buttons::DownC | Buttons::DownPad) || NavDown()) {
        gameOptionsIndex_++;
        stickYReady_ = false;
        itemSelected_ = false;
    }

    if (gameOptionsIndex_ >= 10) {
        gameOptionsIndex_ = 0;
        return;
    }
    if (gameOptionsIndex_ < 0) {
        gameOptionsIndex_ = 9;
    }
}

// game_options_music_volume_navigation.
void WatchMenu::NavigateMusicVolume() {
    if (Pressed(Buttons::UpC | Buttons::UpPad) || NavUp()) {
        gameOptionsIndex_ = 9;
        stickYReady_ = false;
        itemSelected_ = false;
        return;
    }
    if (Pressed(Buttons::DownC | Buttons::DownPad) || NavDown()) {
        gameOptionsIndex_ = 1;
        stickYReady_ = false;
        itemSelected_ = false;
    }
}

// game_options_fx_volume_navigation.
void WatchMenu::NavigateEffectsVolume() {
    if (Pressed(Buttons::UpC | Buttons::UpPad) || NavUp()) {
        gameOptionsIndex_ = 0;
        stickYReady_ = false;
        itemSelected_ = false;
        return;
    }
    if (Pressed(Buttons::DownC | Buttons::DownPad) || NavDown()) {
        gameOptionsIndex_ = 2;
        stickYReady_ = false;
        itemSelected_ = false;
    }
}

// mission_brief_*_navigation: up and down move through the five sections in
// a ring. Only the background's checks down as well after going up.
void WatchMenu::NavigateBriefing(int32_t up, int32_t down, bool returnAfterUp) {
    if (Pressed(Buttons::UpC | Buttons::UpPad) || NavUp()) {
        briefIndex_ = up;
        stickYReady_ = false;
        itemSelected_ = false;
        if (returnAfterUp) {
            return;
        }
    }
    if (Pressed(Buttons::DownC | Buttons::DownPad) || NavDown()) {
        briefIndex_ = down;
        stickYReady_ = false;
        itemSelected_ = false;
    }
}

// game_options_inventory_navigation.
void WatchMenu::NavigateInventoryList() {
    const int32_t count = ItemCount();

    // get_debug_gunwatchpos_flag is never set.
    if (Pressed(Buttons::UpPad | Buttons::UpC) || StickY() >= 0x47) {
        if (static_cast<int32_t>(inventoryCursor_) > 0 && !itemSelected_) {
            inventoryCursor_ -= 1.0f;
        }
    } else if (Pressed(Buttons::DownPad | Buttons::DownC) || StickY() < -0x46) {
        if (static_cast<int32_t>(inventoryCursor_) < count - 1 && !itemSelected_) {
            inventoryCursor_ += 1.0f;
        }
    }

    if (Held(Buttons::UpPad | Buttons::UpC)) {
        if (static_cast<int32_t>(inventoryCursor_) > 0 && !itemSelected_) {
            inventoryCursor_ -= 0.1f;
        }
    } else if (Held(Buttons::DownPad | Buttons::DownC)) {
        if (static_cast<int32_t>(inventoryCursor_) < count - 1 && !itemSelected_) {
            inventoryCursor_ += 0.1f;
        }
    }

    if (StickY() >= 0x1F && StickY() < 0x46 && curItemIndex_ > 0 && !itemSelected_) {
        inventoryCursor_ -= static_cast<float>(StickY()) / 300.0f;
    } else if (StickY() < -0x1E && StickY() >= -0x45 && static_cast<int32_t>(inventoryCursor_) < count - 1 &&
               !itemSelected_) {
        inventoryCursor_ -= static_cast<float>(StickY()) / 300.0f;
    }

    if (FineUp() && !stickYPrevActive_ && curItemIndex_ > 0 && !itemSelected_) {
        inventoryCursor_ -= 1.0f;
    } else if (FineDown() && !stickYPrevActive_ && curItemIndex_ < count - 1 && !itemSelected_) {
        inventoryCursor_ += 1.0f;
    }

    stickYPrevActive_ = FineUp() || FineDown();

    if (static_cast<float>(count) - 0.5f < inventoryCursor_) {
        inventoryCursor_ = static_cast<float>(count) - 0.5f;
    }
    if (inventoryCursor_ < -0.5f) {
        inventoryCursor_ = -0.5f;
    }

    // The current item is the integer part of the cursor.
    curItemIndex_ = static_cast<int32_t>(inventoryCursor_);

    inventoryTextTargetY_ = (2 * kListLineHeight) + (-curItemIndex_ * kListLineHeight);
    if (inventoryTextTargetY_ < inventoryTextY_) {
        inventoryTextY_ = (inventoryTextY_ - ((inventoryTextY_ - inventoryTextTargetY_) / 3)) - 1;
        inventorySettled_ = false;
    } else if (inventoryTextY_ < inventoryTextTargetY_) {
        inventoryTextY_ = (inventoryTextY_ + ((inventoryTextTargetY_ - inventoryTextY_) / 3)) + 1;
        inventorySettled_ = false;
    } else {
        inventorySettled_ = true;
    }

    if (static_cast<float>(curItemIndex_) + 0.55f < inventoryCursor_ && input_.buttons == 0) {
        inventoryCursor_ -= 0.1f;
    } else if (inventoryCursor_ <= static_cast<float>(curItemIndex_) + 0.45f && input_.buttons == 0) {
        inventoryCursor_ += 0.1f;
    }
}

// sub_GAME_7F0A611C, as the control style list runs it.
void WatchMenu::NavigateControlStyleList() {
    // The list's rows are 10 pixels apart.
    constexpr int32_t kRow = kStyleLineHeight;
    const int32_t count = styleCount_;

    // get_debug_gunwatchpos_flag is never set.
    if (Pressed(Buttons::UpPad | Buttons::UpC) || StickY() >= 0x47) {
        if (static_cast<int32_t>(styleScroll_) > 0) {
            styleScroll_ -= 1.0f;
        }
    } else if (Pressed(Buttons::DownPad | Buttons::DownC) || StickY() < -0x46) {
        if (static_cast<int32_t>(styleScroll_) < count - 1) {
            styleScroll_ += 1.0f;
        }
    }

    if (Held(Buttons::UpPad | Buttons::UpC)) {
        if (static_cast<int32_t>(styleScroll_) > 0) {
            styleScroll_ -= 0.1f;
        }
    } else if (Held(Buttons::DownPad | Buttons::DownC)) {
        if (static_cast<int32_t>(styleScroll_) < count - 1) {
            styleScroll_ += 0.1f;
        }
    }

    if (StickY() >= 0x1F && StickY() < 0x46 && styleIndex_ > 0) {
        styleScroll_ -= static_cast<float>(StickY()) / 300.0f;
    } else if (StickY() < -0x1E && StickY() >= -0x45 && static_cast<int32_t>(styleScroll_) < count - 1) {
        styleScroll_ -= static_cast<float>(StickY()) / 300.0f;
    }

    if (FineUp() && !stickYPrevActive_ && styleIndex_ > 0) {
        styleScroll_ -= 1.0f;
    } else if (FineDown() && !stickYPrevActive_ && styleIndex_ < count - 1) {
        styleScroll_ += 1.0f;
    }

    stickYPrevActive_ = FineUp() || FineDown();

    if (static_cast<float>(count) - 0.5f < styleScroll_) {
        styleScroll_ = static_cast<float>(count) - 0.5f;
    }
    if (styleScroll_ < -0.5f) {
        styleScroll_ = -0.5f;
    }

    // The style is the integer part of the scroll; the options hold it.
    styleIndex_ = static_cast<int32_t>(styleScroll_);
    SetControlType(styleIndex_);
    styleTarget_ = (0 * kRow) + (-styleIndex_ * kRow);

    if (styleTarget_ < styleOffset_) {
        styleOffset_ = (styleOffset_ - ((styleOffset_ - styleTarget_) / 3)) - 1;
        styleSettled_ = false;
    } else if (styleOffset_ < styleTarget_) {
        styleOffset_ = (((styleTarget_ - styleOffset_) / 3) + styleOffset_) + 1;
        styleSettled_ = false;
    } else {
        styleSettled_ = true;
    }

    if (static_cast<float>(styleIndex_) + 0.55f < styleScroll_) {
        if (input_.buttons == 0) {
            styleScroll_ -= 0.1f;
            return;
        }
    }
    if (styleScroll_ <= static_cast<float>(styleIndex_) + 0.45f) {
        if (input_.buttons == 0) {
            styleScroll_ += 0.1f;
        }
    }
}

int32_t WatchMenu::ControlType() const {
    return static_cast<int32_t>(links_.options->controlType);
}

void WatchMenu::SetControlType(int32_t type) {
    links_.options->controlType = static_cast<ControlConfig>(type);
}

uint32_t WatchMenu::OptionValue(int32_t option) const {
    const PlayerOptions& options = *links_.options;
    switch (option) {
        case 0:
            return options.lookInverted ? 1 : 0;
        case 1:
            return options.autoAim ? 1 : 0;
        case 2:
            return options.aimToggle ? 1 : 0;
        case 3:
            return options.sightOnScreen ? 1 : 0;
        case 4:
            return options.lookAhead ? 1 : 0;
        case 5:
            return options.ammoOnScreen ? 1 : 0;
        case 6:
            return static_cast<uint32_t>(options.screen);
        default:
            return options.ratio16x9 ? 1 : 0;
    }
}

void WatchMenu::SetOptionValue(int32_t option, uint32_t value) {
    PlayerOptions& options = *links_.options;
    switch (option) {
        case 0:
            options.lookInverted = value != 0;
            break;
        case 1:
            options.autoAim = value != 0;
            break;
        case 2:
            options.aimToggle = value != 0;
            break;
        case 3:
            options.sightOnScreen = value != 0;
            break;
        case 4:
            options.lookAhead = value != 0;
            break;
        case 5:
            options.ammoOnScreen = value != 0;
            break;
        case 6:
            options.screen = static_cast<ScreenSize>(value);
            break;
        default:
            options.ratio16x9 = value != 0;
            break;
    }
}

void WatchMenu::SelectOptionValue(int32_t option, uint32_t value) {
    // game_option_select_value.
    SetOptionValue(option, value);
    stickLrEnabled_ = false;
    // TODO(https://github.com/liz-baker/frigate/issues/41): OPTION_CHOOSE_SFX.
}

void WatchMenu::ToggleOption(int32_t option) {
    // game_option_toggle_input.
    const uint32_t value = OptionValue(option);
    if ((Pressed(Buttons::LeftC | Buttons::L | Buttons::LeftPad) || NavLeft()) && itemSelected_) {
        if (value == 1) {
            SelectOptionValue(option, 0);
        } else if (value == 2) {
            SelectOptionValue(option, 1);
        }
    } else if ((Pressed(Buttons::RightC | Buttons::R | Buttons::RightPad) || NavRight()) && itemSelected_) {
        // Only the screen has a third value.
        if (value == 0) {
            SelectOptionValue(option, 1);
        } else if (value == 1 && option == 6) {
            SelectOptionValue(option, 2);
        }
    }
}

int32_t WatchMenu::ItemCount() const {
    // bondinvCountTotalItemsInInv.
    // TODO(https://github.com/liz-baker/frigate/issues/136): the cheat that
    // makes every gun available (equipallguns).
    int32_t count = 0;
    for (const InvItem& item : links_.inventory->Items()) {
        if (item.IsProp()) {
            count += links_.host->PropListed(item) ? 1 : 0;
        } else if (!item.IsDual()) {
            count++;
        }
    }
    return count;
}

const InvItem* WatchMenu::ItemAt(int32_t index) const {
    // bondinvGetItemByIndex.
    for (const InvItem& item : links_.inventory->Items()) {
        const bool listed = item.IsProp() ? links_.host->PropListed(item) : !item.IsDual();
        if (!listed) {
            continue;
        }
        if (index == 0) {
            return &item;
        }
        index--;
    }
    return nullptr;
}

int32_t WatchMenu::ItemWeapon(int32_t index) const {
    // bondinvGetTextbyInvIndex.
    const InvItem* item = ItemAt(index);
    if (item == nullptr) {
        return 0;
    }
    if (item->IsProp()) {
        const TextOverride* override = links_.inventory->TextByObject(item->object);
        return override != nullptr ? override->weapon : 0;
    }
    return item->right;
}

std::string_view WatchMenu::ItemName(int32_t index, int32_t kind) const {
    // bondinvGetNameByIndex (kind 0) and bondinvGetFirstTitlebyIndex and
    // bondinvGetSecondTitlebyIndex (kinds 1 and 2): a text override's string
    // wins, then the item's own.
    const InvItem* item = ItemAt(index);
    int32_t weapon = 0;
    const auto overrideText = [kind](const TextOverride& override) -> uint32_t {
        return kind == 0 ? override.shortText : (kind == 1 ? override.titleText1 : override.titleText2);
    };
    if (item != nullptr) {
        if (item->IsProp()) {
            if (const TextOverride* override = links_.inventory->TextByObject(item->object)) {
                if (overrideText(*override) != 0) {
                    return links_.host->Text(static_cast<int32_t>(overrideText(*override)));
                }
                weapon = override->weapon;
            }
        } else {
            weapon = item->right;
            if (const TextOverride* override = links_.inventory->TextByWeapon(weapon)) {
                if (overrideText(*override) != 0) {
                    return links_.host->Text(static_cast<int32_t>(overrideText(*override)));
                }
            }
        }
    }
    const WatchItem& watch = links_.weapons->Table().Watch(weapon);
    const uint32_t id = kind == 0 ? watch.shortName : (kind == 1 ? watch.upperText : watch.lowerText);
    return links_.host->Text(static_cast<int32_t>(id));
}

void WatchMenu::DetermineEquippedItem() {
    // bondinvDetermineEquippedItem.
    const int32_t current = links_.weapons->WeaponId(HandRight);
    equippedItem_ = kItemUnarmed;
    const int32_t count = ItemCount();
    for (int32_t i = 0; i < count; i++) {
        if (ItemWeapon(i) == current) {
            equippedItem_ = i;
            return;
        }
    }
}

void WatchMenu::EquipSelectedItem() {
    // sub_GAME_7F0A8378: Z or A equips the listed item, and so does the Start
    // that closes the watch, unless it is the one held.
    if (!Pressed(Buttons::Z | Buttons::A)) {
        if (!Pressed(Buttons::Start)) {
            return;
        }
        if (links_.weapons->WeaponId(HandRight) == ItemWeapon(curItemIndex_)) {
            return;
        }
    }

    links_.weapons->UnEquipWeapon(HandRight, ItemWeapon(curItemIndex_));
    links_.weapons->UnEquipWeapon(HandLeft, 0);
    equippedItem_ = curItemIndex_;
    equipFlash_ = 10;
    // TODO(https://github.com/liz-baker/frigate/issues/41): CAMERA_BEEP1_SFX.
}

void WatchMenu::BeginFrame() {
    generation_ ^= 1;
    frames_[generation_].vertices.clear();
    frames_[generation_].matrices.clear();
    frames_[generation_].draws.clear();
}

} // namespace Frigate::Level
