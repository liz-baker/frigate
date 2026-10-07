#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/FixedMtx.h"
#include "level/BgView.h"
#include "level/Lang.h"
#include "level/PlayerWeapons.h"
#include "level/StageSetup.h"
#include "level/WeaponTable.h"
#include "libultraship/libultra/gbi.h"
#include "resource/type/Font.h"

namespace Frigate::Game::Menu {
class BloodAnimation;
} // namespace Frigate::Game::Menu

namespace Frigate::Level {

// GUNSIGHTREASON_*: the reasons the crosshair is hidden.
namespace SightReason {
constexpr int32_t Option = 0x01;
constexpr int32_t NotAiming = 0x02;
constexpr int32_t NoControl = 0x04;
constexpr int32_t Damage = 0x10;
} // namespace SightReason

// GUNAMMOREASON_*: the reasons the ammo counter is hidden.
namespace AmmoReason {
constexpr int32_t Option = 0x01;
constexpr int32_t NoControl = 0x02;
constexpr int32_t Damage = 0x08;
} // namespace AmmoReason

// PLAYERFLAG_*: the reasons the message strips are hidden.
namespace HudFlag {
constexpr int32_t LockControls = 0x1;
constexpr int32_t NoControl = 0x2;
constexpr int32_t NoTimer = 0x4;
} // namespace HudFlag

// The two font banks the HUD text draws with.
enum class HudFont { Gothic, Zurich };

// What the HUD reads off the frame each time it draws (maybe_mp_interface's
// inputs).
struct HudFrame {
    // viGetX, viGetY and the view rectangle.
    ViewMetrics vi;
    // get_screen_ratio() is 16:9.
    bool ratio16x9 = false;
    // How much wider than 4:3 the display is, for the draws over the whole
    // view that 2D coordinates can't reach the edges of.
    float widen = 1.0f;
    // g_ClockTimer.
    int32_t clockTimer = 1;
    // The current player's watch_animation_state.
    int32_t watchState = 0;
    // The mission countdown (clock_time) in ticks and whether it is drawn
    // (clock_drawn_flag is clear).
    float countdownTime = 0.0f;
    bool countdownShown = false;
    // The intro camera is showing (cameramode == CAMERAMODE_INTRO).
    bool introCamera = false;
    // The stage's visibility scale, which sizes the gauge bars' view.
    float visibilityScale = 1.0f;
    // The perspective projection the frame draws with, which the gauge bars
    // put back after their own.
    const Mtx::FixedMtx* projection = nullptr;
    const PlayerWeapons* weapons = nullptr;
    // currentPlayerDrawFade: the colour the view is washed with, and how much.
    int colourRed = 0;
    int colourGreen = 0;
    int colourBlue = 0;
    float colourFrac = 0.0f;
    // The credits table and the text it names while the credits roll (Cuba's
    // credits_state == 1); null otherwise.
    const std::vector<CreditsEntry>* credits = nullptr;
    const Lang* lang = nullptr;
    // The blood frame a dying player's view is overlaid with
    // (gameplayBloodOverlayDL); null when there is none.
    const Frigate::Game::Menu::BloodAnimation* blood = nullptr;
};

// Where the HUD's fonts and image sizes come from.
class HudSource {
  public:
    virtual ~HudSource() = default;
    // A font bank (`font/fontBankGothic`, `font/fontZurichBold`); null when the
    // source has none.
    virtual std::shared_ptr<Font> LoadFont(const std::string& path) {
        (void)path;
        return nullptr;
    }
    // The text of `hud/images`.
    virtual std::optional<std::string> LoadHudImages() {
        return std::nullopt;
    }
};

// The player's health and armour as the gauge bars show them.
struct HealthDisplay {
    // apparenthealth and apparentarmour.
    float health = 1.0f;
    float armour = 0.0f;
    // healthshowtime: how long the gauges have shown, -1 while they don't.
    int32_t showTime = -1;
    // healthdisplaytime, which only the multiplayer watch sets.
    int32_t displayTime = 0;
};

// An image of the global image table the HUD draws, by its offset there
// (`hud/images`, docs/formats/goldeneye-hud-images.md).
struct HudImage {
    int32_t offset = 0;
    int32_t width = 0;
    int32_t height = 0;
};

// The gauge bars' vertices: 23 pairs of a segment's inner and outer edge.
constexpr size_t kGaugeVertexCount = 46;
using GaugeVertices = std::array<Vtx_t, kGaugeVertexCount>;

// hudMakeDamageSegments: fills `vertices` with the bar for `value` (0 to 1).
// `isArmour` is 1 for the armour bar, which curves to the right, and -1 for
// the health bar, which curves to the left.
void MakeDamageSegments(GaugeVertices* vertices, int32_t isArmour, float value);

// The current player's HUD: the crosshair, ammo counter, health and armour
// gauge bars and the message strips (maybe_mp_interface's part after the
// guns), and the state they and the hooks that hide them share.
class Hud {
  public:
    static constexpr int32_t kBottomMessageSlots = 5;
    static constexpr int32_t kTopMessageSlots = 2;

    // The stage-load and life-start reset of the fields this class owns
    // (init_player_BONDdata, bondviewResetIntroCameraMessageDialogs and
    // bondviewResetUpperTextDisplay).
    void Reset();
    // mp_respawn_handler's share: the ammo counter and the strips are shown,
    // and the crosshair is hidden until the player aims (gunammooff,
    // gunsightmode, hudmsgsSetOn(-1) and bondviewClearUpperTextDisplayFlag(-1)).
    void BeginLife() {
        ammoOff_ = 0;
        sightMode_ = SightReason::NotAiming;
        messagesOff_ = 0;
        upperTextFlag_ = 0;
    }
    // bondviewResetIntroCameraMessageDialogs: the bottom strip is cleared and
    // switched back on, with its font.
    void ResetIntroCameraMessages();

    // The fonts the text draws with; without them only the crosshair and the
    // gauge bars draw.
    void SetFonts(std::shared_ptr<Font> gothic, std::shared_ptr<Font> zurich) {
        gothic_ = std::move(gothic);
        zurich_ = std::move(zurich);
    }

    // The sizes of the images the HUD draws; without them there are no ammo
    // icons or crosshair. False (and none set) if the text is malformed.
    bool SetImages(std::string_view text);

    // gunSetSightVisible and gunSetGunAmmoVisible.
    void SetSightVisible(int32_t reason, bool visible);
    void SetAmmoVisible(int32_t reason, bool visible);
    // hudmsgsSetOn and hudmsgsSetOff.
    void MessagesOn(int32_t flags) {
        messagesOff_ &= ~flags;
    }
    void MessagesOff(int32_t flags) {
        messagesOff_ |= flags;
    }
    // bondviewSetUpperTextDisplayFlag and bondviewClearUpperTextDisplayFlag.
    void SetUpperTextFlag(int32_t flags) {
        upperTextFlag_ |= flags;
    }
    void ClearUpperTextFlag(int32_t flags) {
        upperTextFlag_ &= ~flags;
    }
    // setFontTables: the font the bottom strip draws with.
    void SetMessageFont(HudFont font) {
        messageFont_ = font;
    }

    // hudmsgBottomShow and hudmsgTopShow: queue a message on the strip. A
    // strip holds as many as its slots; the rest are dropped, as in the game.
    void BottomShow(std::string_view text);
    void TopShow(std::string_view text);

    // maybe_mp_interface's HUD part, once the guns and the watch are drawn.
    Gfx* Render(Gfx* dl, const HudFrame& frame);
    // The same function while a cinematic camera shows: just the strips, the
    // countdown, the fade and the credits.
    Gfx* RenderIntroCamera(Gfx* dl, const HudFrame& frame);
    // credits_state == 2: the credits have scrolled past their last line.
    bool CreditsFinished() const {
        return creditsFinished_;
    }
    // camera_80036438 and credits_state's reset at the stage load.
    void ResetCredits() {
        creditsFrame_ = 0;
        creditsFinished_ = false;
    }

    // The gauge bars the open watch shows (trigger_solo_watch_menu and
    // record_damage_kills): the vertices are made from the values given, and
    // the bars' triangles (watch_body_armor_bar_gdl and watch_health_bar_gdl)
    // are appended to a display list.
    void MakeWatchGauges(float armour, float health);
    Gfx* EmitWatchGauges(Gfx* dl) const;
    // gunDrawWatchAmmoDisplay: the ammo counter of the right hand on the
    // watch's status page.
    Gfx* RenderWatchAmmo(Gfx* dl, const HudFrame& frame);

    // ptrFontBankGothic; null without the font.
    const Font* GothicFont() const {
        return gothic_.get();
    }

    // currentPlayerDrawFade.
    static Gfx* RenderColourScreen(Gfx* dl, const HudFrame& frame);
    // gameplayBloodOverlayDL.
    static Gfx* RenderBloodOverlay(Gfx* dl, const HudFrame& frame);

    HealthDisplay& Health() {
        return health_;
    }
    const HealthDisplay& Health() const {
        return health_;
    }
    int32_t SightMode() const {
        return sightMode_;
    }
    int32_t AmmoOff() const {
        return ammoOff_;
    }
    // The text of the strip's current message, or empty for none.
    std::string_view BottomMessage() const;
    std::string_view TopMessage() const;
    // The messages waiting or showing (display_statusbar and
    // display_upper_text_window).
    int32_t BottomMessageCount() const {
        return bottomCount_;
    }
    int32_t TopMessageCount() const {
        return topCount_;
    }

  private:
    // bondviewIntroCameraTextTick and bondviewUpperTextWindowTimerTick.
    void TickBottomMessage(int32_t clockTimer);
    void TickTopMessage(int32_t clockTimer);
    // hudmsgBottomRender and sub_GAME_7F08AAE8.
    Gfx* RenderBottomMessage(Gfx* dl, const HudFrame& frame);
    Gfx* RenderTopMessage(Gfx* dl, const HudFrame& frame);
    // bondviewRenderCredits.
    Gfx* RenderCredits(Gfx* dl, const HudFrame& frame);
    // bondviewRenderGaugeBars.
    Gfx* RenderGaugeBars(Gfx* dl, const HudFrame& frame);
    // gunDrawSight.
    Gfx* RenderSight(Gfx* dl, const HudFrame& frame);
    // generate_ammo_total_microcode.
    Gfx* RenderAmmo(Gfx* dl, const HudFrame& frame);
    // countdownTimerRender: the mission clock, as minutes, seconds and
    // hundredths of a second.
    Gfx* RenderCountdown(Gfx* dl, const HudFrame& frame);
    // gunDrawHudString and gunDrawHudInteger.
    Gfx* DrawString(Gfx* dl, const HudFrame& frame, std::string_view text, int32_t x, int32_t halign, int32_t y,
                    int32_t valign);
    // The ammo icon (set_rgba_redirect_generate_microcode).
    Gfx* DrawAmmoIcon(Gfx* dl, int32_t image, float x, float y, float belowIcon, bool flipped, float yOffset);

    const Font* MessageFontBank() const;
    const HudImage* Image(int32_t offset) const;

    std::shared_ptr<Font> gothic_;
    std::shared_ptr<Font> zurich_;
    HudFont messageFont_ = HudFont::Gothic;
    // camera_80036438: how many frames the credits have rolled.
    int32_t creditsFrame_ = 0;
    bool creditsFinished_ = false;

    int32_t sightMode_ = 0;
    int32_t ammoOff_ = 0;
    int32_t messagesOff_ = 0;
    int32_t upperTextFlag_ = 0;

    // bondmesscnt, display_statusbar, status_bar_text_buffer_index and
    // stringbuffer_lowerleft.
    int32_t bottomTimer_ = -1;
    int32_t bottomCount_ = 0;
    int32_t bottomIndex_ = 0;
    std::array<std::string, kBottomMessageSlots> bottomText_;
    // upper_text_window_timer, display_upper_text_window,
    // upper_text_buffer_index and the top strip's buffers.
    int32_t topTimer_ = -1;
    int32_t topCount_ = 0;
    int32_t topIndex_ = 0;
    std::array<std::string, kTopMessageSlots> topText_;

    std::vector<HudImage> images_;

    HealthDisplay health_;
    // The gauge bars' vertices and matrices, which the display list reads
    // after the frame's commands are built.
    GaugeVertices armourVertices_{};
    GaugeVertices healthVertices_{};
    Mtx::FixedMtx gaugeProjection_{};
    Mtx::FixedMtx gaugeView_{};
};

} // namespace Frigate::Level
