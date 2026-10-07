#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/FixedMtx.h"
#include "level/Hud.h"
#include "level/Inventory.h"
#include "level/Matrix.h"
#include "level/Objectives.h"
#include "level/PlayerMove.h"
#include "level/PlayerWeapons.h"
#include "level/WeaponTable.h"
#include "libultraship/libultra/gbi.h"
#include "model/JointDraw.h"
#include "model/ModelInstance.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

class ModelFactory;

// WATCH_INDEX_*: the watch's pages, in the order of the tabs under its face.
namespace WatchPage {
constexpr int32_t MissionStatus = 0;
constexpr int32_t Inventory = 1;
constexpr int32_t ControlOptions = 2;
constexpr int32_t GameOptions = 3;
constexpr int32_t MissionBriefing = 4;
constexpr int32_t Count = 5;
} // namespace WatchPage

// WATCH_BRIEF_INDEX_*: the briefing page's five sections.
namespace BriefIndex {
constexpr int32_t Background = 0;
constexpr int32_t M = 1;
constexpr int32_t Q = 2;
constexpr int32_t Moneypenny = 3;
constexpr int32_t Objectives = 4;
} // namespace BriefIndex

// WATCH_BRIEFING_PAGE: the stage's briefing text records.
namespace BriefingText {
constexpr int32_t Title = 0;
constexpr int32_t Overview = 1;
constexpr int32_t M = 2;
constexpr int32_t Q = 3;
constexpr int32_t Moneypenny = 4;
} // namespace BriefingText

// What the watch menu reads of the rest of the game and the few things it
// does to it.
class WatchMenuHost {
  public:
    virtual ~WatchMenuHost() = default;
    // langGet: the string `id` names.
    virtual std::string_view Text(int32_t id) const = 0;
    // currentPlayerGetHealth and currentPlayerGetArmor.
    virtual float Health() const = 0;
    virtual float Armour() const = 0;
    // lvlGetSelectedDifficulty.
    virtual int32_t Difficulty() const = 0;
    // objectiveGetCount, get_difficulty_for_objective, get_text_for_objective,
    // get_status_of_objective and objectiveIsAllComplete.
    virtual int32_t ObjectiveCount() const = 0;
    virtual int32_t ObjectiveDifficulty(int32_t objective) const = 0;
    virtual std::string_view ObjectiveText(int32_t objective) const = 0;
    virtual ObjectiveStatus ObjectiveStatusOf(int32_t objective) const = 0;
    virtual bool AllObjectivesComplete() const = 0;
    // get_ptr_text_for_watch_breifing_page: a BriefingText value's text.
    virtual std::string_view BriefingPage(int32_t page) const = 0;
    // Whether an inventory entry that is a kept prop is listed by the watch
    // (bondinvCountTotalItemsInInv's tests of the prop's object).
    virtual bool PropListed(const InvItem& item) const = 0;
    // set_missionstate(MISSION_STATE_0), bossRunTitleStage and
    // mission_failed_or_aborted: the mission is aborted and the game returns
    // to the title.
    virtual void AbortMission() = 0;
    // sub_GAME_7F0C1310 and sub_GAME_7F0C1340: the mission's music gives way
    // to the watch's while the menu is open, and returns when it closes.
    virtual void EnterWatchMusic() = 0;
    virtual void LeaveWatchMusic() = 0;
};

// The watch's side of the menu: its zoom, which the page changes start and the
// face is scaled by.
class WatchMenuOwner {
  public:
    virtual ~WatchMenuOwner() = default;
    // trigger_watch_zoom.
    virtual void TriggerZoom(float fovY, float time) = 0;
    // check_watch_page_transistion_running.
    virtual bool ZoomRunning() const = 0;
    // How far a page change has zoomed, squared and clamped to 1
    // (draw_background_health_and_armor's scale while it runs).
    virtual float PageZoomScale() const = 0;
    // bondviewWatchAnimationRelated: how far the face has unfolded while the
    // watch zooms in or out.
    virtual float UnfoldScale() const = 0;
    // watch_animation_state.
    virtual int32_t State() const = 0;
};

// What the watch menu draws with each frame.
struct WatchMenuFrame {
    HudFrame hud;
    // speedgraphframes.
    int32_t speedFrames = 1;
    // The environment colour of the tile the player stands on.
    uint32_t tileColour = 0xFFFFFF00;
    // sub_GAME_7F078474: the look-at the reflection-mapped items are drawn with.
    const LookAt* lookAt = nullptr;
};

// The solo watch menu (options.c): the pages behind the watch's face, their
// navigation, and the face's static and gauges. The watch itself, its arm and
// its zoom are the Watch class's.
class WatchMenu {
  public:
    // The menu reaches the rest of the game through these.
    struct Links {
        WatchMenuHost* host = nullptr;
        WatchMenuOwner* owner = nullptr;
        Hud* hud = nullptr;
        PlayerWeapons* weapons = nullptr;
        PlayerOptions* options = nullptr;
        const Inventory* inventory = nullptr;
        ModelFactory* models = nullptr;
        // randomGetNext.
        std::function<uint32_t()> random;
    };

    void Attach(const Links& links) {
        links_ = links;
    }

    // What deleteCurrentSelectedFolder does when the watch closes: stores the
    // options in the folder the game was started from. A stage started from no
    // folder (an attract demo) has none, and the options are left alone.
    void SetSettingsSaver(std::function<void()> saver) {
        settingsSaver_ = std::move(saver);
    }

    // init_watch_at_start_of_stage's menu fields.
    void Reset();

    // The controller the menu reads this frame (the joy functions).
    void SetInput(const MoveInput& input) {
        input_ = input;
    }

    // trigger_solo_watch_menu's setup, run as the watch starts to open: the
    // gauges, the page tabs and the static.
    void Begin();
    // sub_GAME_7F0A69A8: the menu goes back to its first page.
    void ResetPages();
    // sub_GAME_7F0A6A80, once a tick while the watch is open.
    void Tick(int32_t speedFrames);
    // The player was hit while the watch is up (record_damage_kills).
    void RedrawGauges();
    // The watch is closing (deleteCurrentSelectedFolder and sub_GAME_7F0C1340).
    void Exit();

    // draw_watch_current_page. `matrix` is the position of the watch's face;
    // `open` is whether the menu is open rather than the arm moving.
    Gfx* Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const Mtx::FixedMtx* matrix,
                bool open, const WatchMenuFrame& frame);
    // Starts a frame: the draws of the frame before last can go.
    void BeginFrame();

    // outside_watch_menu: the face shows the model's own screen.
    bool OutsideMenu() const {
        return outsideMenu_;
    }
    // sub_GAME_7F0BD8F0: whether the level draws.
    bool LevelDrawn() const {
        return levelDrawn_;
    }
    // open_close_solo_watch_menu: a Start press has asked to close the menu.
    bool CloseRequested() const {
        return closeRequested_;
    }
    void ClearCloseRequest() {
        closeRequested_ = false;
    }
    // init_player_BONDdata: outside_watch_menu is set and open_close_solo_watch_menu
    // clear.
    void BeginLife() {
        outsideMenu_ = true;
        closeRequested_ = false;
    }

    // The values the pages show.
    int32_t Page() const {
        return page_;
    }
    bool ItemSelected() const {
        return itemSelected_;
    }
    int32_t Green() const {
        return green_;
    }
    int32_t CurrentItem() const {
        return curItemIndex_;
    }

  private:
    // The menu's scale of the list rows of the control style page.
    static constexpr int32_t kStyleLineHeight = 10;
    // D_80040B10 and D_80040B0C: the chances a page change and a tick start the
    // static, as the high half of a random draw.
    static constexpr uint32_t kPageStaticChance = 0xF800;
    static constexpr uint32_t kStaticChance = 0xFFA0;
    // D_80040B1C: the degrees a tick turns the inventory page's item by.
    static constexpr float kItemSpin = 2.5f;

    // The pages and what they draw.
    Gfx* DrawFace(Gfx* dl, const Mtx::FixedMtx* matrix, bool unfold, const WatchMenuFrame& frame);
    Gfx* DrawStatusPage(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const Mtx::FixedMtx* matrix,
                        const WatchMenuFrame& frame);
    Gfx* DrawInventoryPage(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const Mtx::FixedMtx* matrix,
                           const WatchMenuFrame& frame);
    Gfx* DrawControlPage(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const Mtx::FixedMtx* matrix,
                         const WatchMenuFrame& frame);
    Gfx* DrawGamePage(Gfx* dl, const Mtx::FixedMtx* matrix, const WatchMenuFrame& frame);
    Gfx* DrawBriefingPage(Gfx* dl, const Mtx::FixedMtx* matrix, const WatchMenuFrame& frame);

    // The text and shapes the pages share.
    Gfx* DrawAbortConfirm(Gfx* dl, const WatchMenuFrame& frame);
    Gfx* DrawMissionStatusText(Gfx* dl, const WatchMenuFrame& frame);
    Gfx* DrawVersionText(Gfx* dl, const WatchMenuFrame& frame);
    Gfx* DrawHandItemAndAmmo(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const WatchMenuFrame& frame);
    Gfx* DrawLabel(Gfx* dl, const WatchMenuFrame& frame, int32_t x, int32_t y, std::string_view text, uint32_t colour,
                   bool outlined, uint32_t outlineColour, bool centre, bool drawBackground, uint32_t backgroundColour,
                   bool rightAlign);
    Gfx* DrawVolumeSlider(Gfx* dl, bool music, int32_t verticalOffset);
    Gfx* DrawToggleOptions(Gfx* dl, const WatchMenuFrame& frame);
    Gfx* DrawToggleValues(Gfx* dl, const WatchMenuFrame& frame, int32_t y, int32_t option, int32_t state);
    Gfx* DrawControlStyleText(Gfx* dl, const WatchMenuFrame& frame);
    Gfx* DrawControllerLabels(Gfx* dl, const WatchMenuFrame& frame);
    Gfx* DrawDualControllerLabels(Gfx* dl, const WatchMenuFrame& frame);
    Gfx* DrawController(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const WatchMenuFrame& frame);
    // watchRenderController.
    Gfx* DrawControllerModel(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const Mtxf& base, int32_t pad,
                             int32_t envColour);
    // set_enviro_fog_for_items_in_solo_watch_menu.
    Gfx* DrawItemModel(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, int32_t item, const Mtxf& matrix,
                       int32_t envColour, uint32_t fogColour, const LookAt* lookAt);
    // sub_GAME_7F0A6EE8: the render state the item models are drawn in.
    Gfx* SetItemRenderState(Gfx* dl, const WatchMenuFrame& frame);
    // set_page_rectangle_colors.
    void ColourTabs();
    // setup_watch_rectangles.
    static void MakeRectangle(Vtx_t* vertices, int32_t startX, int32_t startZ, int32_t width, int32_t height,
                              int32_t horizontalOffset, int32_t verticalOffset);

    // The navigation the pages run.
    void NavigateStatus();
    void NavigateInventoryPage();
    void NavigateControl();
    void NavigateGame();
    void NavigateBriefingPage();
    void NavigateControlStyleEntry();
    void NavigateControlInputsEntry();
    void NavigateGameOptionsList();
    void NavigateMusicVolume();
    void NavigateEffectsVolume();
    void NavigateBriefing(int32_t up, int32_t down, bool returnAfterUp);
    void NavigateInventoryList();
    void NavigateControlStyleList();
    // sub_GAME_7F0A5210: the change of page.
    void ChangePageEffects();
    // sub_GAME_7F0A51D8: the static.
    void StartStatic();
    // watch_play_beep_sound.
    void BeepToggleSelection();
    // sub_GAME_7F0A8378.
    void EquipSelectedItem();
    // The options saved into the selected folder (deleteCurrentSelectedFolder).
    void SaveSettings();
    // watch_adjust_volume_slider.
    void AdjustVolume(uint16_t* volume);
    // The controller wheel's easing (sub_GAME_7F0A95C4 and sub_GAME_7F0A9684).
    static float Ease(float value, float target, float divisor);
    void SpinController(int32_t pad);
    // watchWrapAroundPI.
    static float WrapAroundPi(float angle);

    // The controller, as the joy functions read it.
    bool Pressed(uint16_t mask) const {
        return (input_.pressed & mask) != 0;
    }
    bool Held(uint16_t mask) const {
        return (input_.buttons & mask) != 0;
    }
    int32_t StickX() const {
        return input_.stickX;
    }
    int32_t StickY() const {
        return input_.stickY;
    }
    bool StickLeft() const;
    bool StickRight() const;
    bool StickUp() const;
    bool StickDown() const;
    bool NavLeft() const;
    bool NavRight() const;
    bool NavUp() const;
    bool NavDown() const;
    bool FineUp() const;
    bool FineDown() const;

    // The inventory as the watch lists it (bondinv.c).
    Inventory::ListedFn Listed() const;
    int32_t ItemCount() const;
    const InvItem* ItemAt(int32_t index) const;
    int32_t ItemWeapon(int32_t index) const;
    // The short name (kind 0), first title (1) or second title (2) of the item.
    std::string_view ItemName(int32_t index, int32_t kind) const;
    void DetermineEquippedItem();

    // The options the game page shows, by PLAYER_OPTION_*.
    uint32_t OptionValue(int32_t option) const;
    void SetOptionValue(int32_t option, uint32_t value);
    void SelectOptionValue(int32_t option, uint32_t value);
    void ToggleOption(int32_t option);
    int32_t ControlType() const;
    void SetControlType(int32_t type);
    // cur_player_set_control_type's list fields, once the options are known.
    void SyncControlType();

    // The string of slot `slot` of the options bank.
    std::string_view OptionText(int32_t slot) const;
    // A control style's row of ten strings (game_control_styles), its own name
    // first, and one of them.
    static const std::array<int32_t, 10>& StyleRow(int32_t style);
    std::string_view StyleText(int32_t style, int32_t column) const;

    Links links_;
    std::function<void()> settingsSaver_;
    MoveInput input_;

    // watch_screen_index, controller_options_index, game_options_index and
    // mission_brief_index.
    int32_t page_ = WatchPage::MissionStatus;
    int32_t controllerIndex_ = 0;
    int32_t gameOptionsIndex_ = 0;
    int32_t briefIndex_ = BriefIndex::Objectives;
    // D_800409A4: the abort is confirmed rather than cancelled.
    bool abortConfirmed_ = false;
    // watch_item_is_actively_selected.
    bool itemSelected_ = false;
    // The inventory page: watch_inventory_text_y and its target,
    // g_curWatchItemIndex, watch_inventory_cursor_pos,
    // watch_inventory_text_is_settled, D_800409C4 (the ticks the chosen item
    // shows highlighted) and equipcuritem.
    int32_t inventoryTextY_ = 0;
    int32_t inventoryTextTargetY_ = 0;
    int32_t curItemIndex_ = 0;
    float inventoryCursor_ = 0.0f;
    bool inventorySettled_ = false;
    int32_t equipFlash_ = 0;
    int32_t equippedItem_ = 0;
    // The control style list: cur_player_control_type_2 (the scroll),
    // cur_player_control_type_0 (the style, which the options hold),
    // cur_player_control_type_1 (where the list scrolls to),
    // neg_vspacing_for_control_type_entry (where it is),
    // has_set_control_type_data (it has arrived) and D_800409D8 (the styles listed).
    float styleScroll_ = 0.0f;
    int32_t styleIndex_ = 0;
    int32_t styleTarget_ = 0;
    int32_t styleOffset_ = 0;
    bool styleSettled_ = true;
    int32_t styleCount_ = 8;
    bool styleSynced_ = false;

    // controlstick_lr_enabled, watch_stick_y_nav_ready and
    // watch_stick_y_prev_active.
    bool stickLrEnabled_ = false;
    bool stickYReady_ = false;
    bool stickYPrevActive_ = false;

    // D_80040AF4 to D_80040B00: the colours that cycle, with their counters.
    uint32_t incompleteColour_ = 0xFF00A0;
    int32_t incompleteCounter_ = 10;
    uint32_t objectiveGreen_ = 0xFF;
    int32_t objectiveCounter_ = 10;
    // g_WatchBackgroundGreen, g_WatchStaticScanlineAlpha and the scanline's
    // position.
    int32_t green_ = 0xE0;
    int32_t scanlineAlpha_ = 0;
    int32_t scanlineY_ = 0;
    // D_80040B14: the angle the inventory page's item turns by.
    float itemAngle_ = 0.0f;
    // The controller models' pitch, spin angle, spin speed and idle counter,
    // for each controller.
    struct Spin {
        float pitch = 0.0f;
        float angle = 0.0f;
        float speed = 0.0f;
        int32_t counter = 0;
    };
    std::array<Spin, 2> spin_{};
    bool outsideMenu_ = true;
    bool levelDrawn_ = true;
    bool closeRequested_ = false;

    // buffer_for_watch_greenbackdrop_vertices and _static_vertices: the page
    // tabs' rectangles (four vertices each) and the static's bar.
    std::array<Vtx_t, WatchPage::Count * 4> tabVertices_{};
    std::array<Vtx_t, 4> staticVertices_{};

    // What a draw leaves for the interpreter to read once the frame is built.
    struct FrameData {
        std::deque<std::array<Vtx_t, 32>> vertices;
        std::deque<Mtx::FixedMtx> matrices;
        std::deque<Model::JointDraw> draws;
        std::deque<std::vector<Mtx::FixedMtx>> modelMatrices;
    };
    std::array<FrameData, 2> frames_;
    size_t generation_ = 0;
    FrameData& Frame() {
        return frames_[generation_];
    }
    // Appends vertices (up to 32) to the frame's and returns where they are.
    Vtx_t* NewVertices();
    Mtx::FixedMtx* NewMatrix(const Mtx::FixedMtx& value);

    // The models the pages draw, by item.
    std::map<int32_t, std::shared_ptr<Model::ModelInstance>> models_;
    // g_WeaponEnvmapLight: the light on the reflection-mapped items; drawn from
    // by address.
    Lights1 envmapLight_ = gdSPDefLights1(0x96, 0x96, 0x96, 0xff, 0xff, 0xff, -78, 77, 46);
};

} // namespace Frigate::Level
