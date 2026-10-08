// Headless multiplayer checks against the real stages: `frigate_multiplayer_harness
// <archive dir> [case or group ...]`. Needs the packed archives (`just
// pack_archives`) and skips (exit 77) without them. Cases are listed in kCases;
// see MultiplayerHarness.h for what a case can drive and read.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "MultiplayerHarness.h"
#include "game/ChrRecords.h"
#include "game/MultiplayerQuickStart.h"
#include "game/TextRender.h"
#include "game/menu/CheatIds.h"
#include "game/save/SaveData.h"
#include "../HudDisplayList.h"
#include "level/CheatInput.h"
#include "level/CheatMasks.h"
#include "level/Cheats.h"
#include "level/Fog.h"
#include "level/Hud.h"
#include "level/MpWeaponSets.h"
#include "level/PlayerMove.h"
#include "level/ScreenLayout.h"
#include "level/Stage.h"
#include "level/WeaponTable.h"
#include "model/Random.h"
#include "platform/Boot.h"
#include "platform/Rumble.h"
#include "ship/core/Context.h"
#include "ship/utils/StrHash64.h"
#include "ship/resource/ResourceManager.h"

using namespace Frigate;
using namespace Frigate::Test;
using Game::MultiplayerSettings;

namespace {

constexpr int kSkipped = 77;
// Ticks the players need to land and settle before a case moves them.
constexpr int kSettleTicks = 30;
constexpr int kWalkTicks = 60;
// A walking player carries on for a while after the stick is released.
constexpr int kCoastTicks = 150;
constexpr float kMoved = 30.0f;
constexpr float kStill = 1.0f;

struct Case {
    const char* name;
    // The group a command line can select; "core" cases run at every player count.
    const char* group;
    std::function<void(MultiplayerSettings&)> configure;
    std::function<void(Report&, MultiplayerSession&)> run;
    // The case reads gAudio: the engine and the call log are installed before the stage loads.
    bool audio = false;
};

std::shared_ptr<Ship::ResourceManager> gResources;

// The audio of the case being run, for the cases that set Case::audio.
std::unique_ptr<AudioRig> gAudio;

float LookChange(const Position& a, const Position& b) {
    return Distance(a, b);
}

void CheckSpawn(Report& report, MultiplayerSession& game) {
    report.Check(game.GetStage().PlayerCount() == game.Players(), "the stage has %d players, not %d",
                 game.GetStage().PlayerCount(), game.Players());
    report.Check(game.Ticks() == 0, "a new stage is at tick 0, not %d", game.Ticks());
    for (int a = 0; a < game.Players(); a++) {
        report.Check(game.HealthOf(a) > 0.99f, "player %d starts at full health, not %.2f", a + 1, game.HealthOf(a));
        for (int b = a + 1; b < game.Players(); b++) {
            report.Check(Distance(game.PositionOf(a), game.PositionOf(b)) > 100.0f,
                         "players %d and %d start in different places", a + 1, b + 1);
        }
    }
    report.Check(game.CameraMode() == Level::CameraMode::MultiPlayer, "a multiplayer stage opens on its own camera");
    game.Run(10);
    report.Check(game.Ticks() == 10, "ten ticks advance the clock to 10, not %d", game.Ticks());
}

void CheckIdle(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    std::vector<Position> before;
    for (int player = 0; player < game.Players(); player++) {
        before.push_back(game.PositionOf(player));
    }
    game.Run(kWalkTicks);
    for (int player = 0; player < game.Players(); player++) {
        report.Check(Distance(before[static_cast<size_t>(player)], game.PositionOf(player)) < kStill,
                     "player %d stands still with an idle pad", player + 1);
    }
}

// Only `mover`'s pad is used; nobody else may move or turn.
void CheckOnlyMoverMoves(Report& report, MultiplayerSession& game, bool turn) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    for (int mover = 0; mover < game.Players(); mover++) {
        game.Run(kCoastTicks);
        std::vector<Position> position;
        std::vector<Position> look;
        for (int player = 0; player < game.Players(); player++) {
            position.push_back(game.PositionOf(player));
            look.push_back(game.LookOf(player));
        }
        Scripts scripts;
        if (turn) {
            scripts[static_cast<size_t>(mover)].Turn(0, 30);
        } else {
            scripts[static_cast<size_t>(mover)].Walk(0, kWalkTicks);
        }
        game.Run(kWalkTicks, scripts);
        for (int player = 0; player < game.Players(); player++) {
            const size_t index = static_cast<size_t>(player);
            const float walked = Distance(position[index], game.PositionOf(player));
            const float turned = LookChange(look[index], game.LookOf(player));
            if (player == mover && turn) {
                report.Check(turned > 0.05f, "player %d turns when their stick turns (look moved %.3f)", player + 1,
                             turned);
            } else if (player == mover) {
                report.Check(walked > kMoved, "player %d walks when their stick pushes forward (%.1f units)",
                             player + 1, walked);
            } else {
                report.Check(walked < kStill && turned < 0.001f,
                             "player %d does not react to player %d's pad (moved %.2f, look %.4f)", player + 1,
                             mover + 1, walked, turned);
            }
        }
    }
}

void CheckWalkAlone(Report& report, MultiplayerSession& game) {
    CheckOnlyMoverMoves(report, game, /*turn=*/false);
}

void CheckTurnAlone(Report& report, MultiplayerSession& game) {
    CheckOnlyMoverMoves(report, game, /*turn=*/true);
}

// Everyone's pad at once: the even players push forward and the odd ones back,
// so each player's displacement must lie along (or against) their own facing.
void CheckAllAtOnce(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    std::vector<Position> before;
    for (int player = 0; player < game.Players(); player++) {
        before.push_back(game.PositionOf(player));
    }
    Scripts scripts;
    for (int player = 0; player < game.Players(); player++) {
        scripts[static_cast<size_t>(player)].Walk(0, 40, player % 2 == 0 ? 70 : -70);
    }
    game.Run(40, scripts);
    for (int player = 0; player < game.Players(); player++) {
        const Position now = game.PositionOf(player);
        const Position& from = before[static_cast<size_t>(player)];
        const Position look = game.LookOf(player);
        const float along = (now[0] - from[0]) * look[0] + (now[2] - from[2]) * look[2];
        report.Check(FloorDistance(now, from) > 10.0f, "player %d moved with the others (%.1f units)", player + 1,
                     FloorDistance(now, from));
        report.Check(player % 2 == 0 ? along > 0.0f : along < 0.0f,
                     "player %d went %s their facing, as their own stick said (%.1f along it)", player + 1,
                     player % 2 == 0 ? "along" : "against", along);
    }
}

void ConfigureStyles(MultiplayerSettings& settings) {
    settings.controlStyle = { Level::ControlConfig::Honey, Level::ControlConfig::Solitare, Level::ControlConfig::Kissy,
                              Level::ControlConfig::Goodnight };
}

void CheckStyles(Report& report, MultiplayerSession& game) {
    for (int player = 0; player < game.Players(); player++) {
        report.Check(game.GetStage().PlayerAt(player).options.controlType ==
                         game.Settings().controlStyle[static_cast<size_t>(player)],
                     "player %d plays with the control style they chose", player + 1);
    }
}

// Gives every player the pistols, ammunition and a settled right hand.
void ArmEveryone(MultiplayerSession& game) {
    for (int player = 0; player < game.Players(); player++) {
        Level::PlayerState& state = game.GetStage().PlayerAt(player);
        for (const int32_t item : { Level::kItemPp7, Level::kItemDd44 }) {
            state.intro.inventory.AddItem(item);
            state.weapons.AddAmmoToWeapon(item, 100);
        }
        state.weapons.EquipWeapon(Level::HandRight, Level::kItemPp7);
    }
    game.Run(90);
}

// A weapon change takes 12 ticks to lower and 12 to raise with two or more
// players, not the single player's 16 and 23.
void CheckSwitchTimes(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    ArmEveryone(game);
    for (int player = 0; player < game.Players(); player++) {
        game.GetStage().PlayerAt(player).weapons.EquipWeapon(Level::HandRight, Level::kItemDd44);
    }
    std::vector<int> swapped(static_cast<size_t>(game.Players()), -1);
    std::vector<int> settled(static_cast<size_t>(game.Players()), -1);
    for (int tick = 1; tick <= 60; tick++) {
        game.Run(1);
        for (int player = 0; player < game.Players(); player++) {
            const Level::PlayerWeapons& weapons = game.GetStage().PlayerAt(player).weapons;
            const size_t index = static_cast<size_t>(player);
            if (swapped[index] < 0 && weapons.WeaponId(Level::HandRight) == Level::kItemDd44) {
                swapped[index] = tick;
            }
            if (swapped[index] >= 0 && settled[index] < 0 &&
                weapons.Hands(Level::HandRight).actionState == Level::GunState::Idle) {
                settled[index] = tick;
            }
        }
    }
    for (int player = 0; player < game.Players(); player++) {
        const size_t index = static_cast<size_t>(player);
        report.Check(swapped[index] >= 12 && swapped[index] <= 14,
                     "player %d lowers the old weapon in 12 ticks (the new one in hand at tick %d)", player + 1,
                     swapped[index]);
        report.Check(settled[index] >= 0 && settled[index] <= swapped[index] + 17,
                     "player %d raises the new weapon in 12 ticks (idle at tick %d, in hand at %d)", player + 1,
                     settled[index], swapped[index]);
    }
}

// With every gun available (the all guns cheat) cycling through them never
// gives a player a gun in each hand: only the single player dual wields.
void CheckNoDualWield(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    for (int player = 0; player < game.Players(); player++) {
        game.GetStage().PlayerAt(player).intro.inventory.SetAllGuns(true);
    }
    int duals = 0;
    int changes = 0;
    for (int step = 0; step < 24; step++) {
        for (int player = 0; player < game.Players(); player++) {
            game.GetStage().PlayerAt(player).weapons.AdvanceThroughInventory();
        }
        game.Run(40);
        for (int player = 0; player < game.Players(); player++) {
            const Level::PlayerWeapons& weapons = game.GetStage().PlayerAt(player).weapons;
            const int32_t right = weapons.WeaponId(Level::HandRight);
            const int32_t left = weapons.WeaponId(Level::HandLeft);
            changes += right != Level::kItemUnarmed ? 1 : 0;
            duals += (left != Level::kItemUnarmed && left == right) ? 1 : 0;
        }
    }
    report.Check(changes > 0, "the players cycled to some weapon");
    report.Check(duals == 0, "no player held a gun in both hands (%d times)", duals);
}

// Firing a pistol ejects no cartridge casing in a multiplayer game.
void CheckNoCasings(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    ArmEveryone(game);
    int fired = 0;
    int casings = 0;
    for (int tick = 0; tick < 60; tick++) {
        Scripts scripts;
        if (tick < 40) {
            for (int player = 0; player < game.Players(); player++) {
                scripts[static_cast<size_t>(player)].Press(0, 1, Level::Buttons::Z);
            }
        }
        game.Run(1, scripts);
        for (int player = 0; player < game.Players(); player++) {
            fired += game.GetStage().PlayerAt(player).weapons.Firing(Level::HandRight) ? 1 : 0;
        }
        for (const Level::Casing& casing : game.GetStage().EjectedCasings().Slots()) {
            casings += casing.InUse() ? 1 : 0;
        }
    }
    report.Check(fired > 0, "the players fired");
    report.Check(casings == 0, "no casing was ejected (%d slot-ticks used)", casings);
}

// An explosion leaves no scorch on the floor in a multiplayer game.
void CheckNoScorches(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    Level::Stage& stage = game.GetStage();
    const Level::Prop& at = stage.PlacedObjects().Props()[stage.PlayerAt(0).move.PropId()];
    constexpr int kGrenade = 2;
    stage.GetExplosions().Create(-1, at.position, at.stan, kGrenade, 1, 0, at.rooms, false, RandomGetNext);
    const int prop = stage.PlacedObjects().Props().Active().front();
    game.Run(2);
    report.Check(stage.GetExplosions().ExplosionOf(prop) != nullptr, "the explosion is running");
    game.Run(60);
    const Level::Explosions::Explosion* explosion = stage.GetExplosions().ExplosionOf(prop);
    report.Check(explosion != nullptr && explosion->age > 40, "the explosion ran past the tick a scorch is made on");
    int scorches = 0;
    for (const Level::BulletImpacts::Scorch& scorch : stage.Impacts().Scorches()) {
        scorches += scorch.room >= 0 ? 1 : 0;
    }
    report.Check(scorches == 0, "no scorch was left (%d)", scorches);
}

// The fog and sky the stage loaded are the record for its player count.
void CheckFogRecord(Report& report, MultiplayerSession& game) {
    const int32_t stage = static_cast<int32_t>(game.GetStage().CurrentStage());
    const int players = game.Players();
    const Level::EnvironmentRecord* expected = nullptr;
    for (const int32_t id : { stage + players * 100, players * 100 }) {
        for (const Level::EnvironmentRecord& record : Level::EnvironmentTable()) {
            if (record.id == id) {
                expected = &record;
                break;
            }
        }
        if (expected != nullptr) {
            break;
        }
    }
    const Level::Fog& fog = game.GetStage().GetFog();
    report.Check(fog.Enabled() == (expected != nullptr), "the fog is %s for %d players", expected ? "on" : "off",
                 players);
    if (expected != nullptr && fog.Enabled()) {
        report.Check(fog.ZFar() == expected->farFog && fog.ZNear() == expected->blendMultiplier,
                     "the z range is the %d player record's (%.0f to %.0f, not %.0f to %.0f)", players,
                     expected->blendMultiplier, expected->farFog, fog.ZNear(), fog.ZFar());
    }
}

// The portals of a stage's windows stay as the stage set them: one player's
// distance from the glass does not close them for the rest.
void CheckWindowPortals(Report& report, MultiplayerSession& game) {
    const Level::Stage& stage = game.GetStage();
    const auto closed = [&stage](int portal) {
        return (stage.Bg().Portals()[static_cast<size_t>(portal)].controlBytes1 & Level::kPortalDisabled) != 0;
    };
    std::vector<std::pair<int, bool>> before;
    for (const Level::PlacedObject& object : stage.PlacedObjects().All()) {
        if (object.type == static_cast<uint8_t>(Level::PropDefType::TintedGlass) && object.portal >= 0) {
            before.emplace_back(object.portal, closed(object.portal));
        }
    }
    game.Run(60);
    for (const auto& [portal, wasClosed] : before) {
        report.Check(closed(portal) == wasClosed, "the portal of a window is %s, as the stage left it",
                     wasClosed ? "closed" : "open");
    }
}

// The same scripts from the same seed give the same game, which the random
// stream and the per-player order both have to honour.
void CheckDeterministic(Report& report, MultiplayerSession& first);

void CheckRender(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    report.Check(game.Render() > 0, "a frame of the stage draws something");
}

// The rectangles of the views of a split screen, as the decomp's viewport
// functions give them: the top and bottom halves for two players, quadrants for
// three and four.
struct ViewRect {
    int32_t left;
    int32_t top;
    int32_t width;
    int32_t height;
};

ViewRect ExpectedView(int players, int player) {
    if (players == 2) {
        return player == 0 ? ViewRect{ 0, 10, 320, 109 } : ViewRect{ 0, 121, 320, 109 };
    }
    const std::array<ViewRect, 4> quadrants = { ViewRect{ 0, 10, 159, 109 }, ViewRect{ 161, 10, 159, 109 },
                                                ViewRect{ 0, 121, 159, 109 }, ViewRect{ 161, 121, 159, 109 } };
    return quadrants[static_cast<size_t>(player)];
}

// Each player's view is their own rectangle of the screen, with the aspect of
// its shape, and the rectangles tile the screen without touching.
void CheckViewports(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    game.Render();
    const Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    for (int player = 0; player < players; player++) {
        const ViewRect want = ExpectedView(players, player);
        const Level::ViewMetrics& vi = stage.PlayerAt(player).camera.Metrics();
        report.Check(vi.viewLeft == want.left && vi.viewTop == want.top && vi.viewWidth == want.width &&
                         vi.viewHeight == want.height,
                     "player %d's view is %d,%d %dx%d, not %d,%d %dx%d", player + 1, vi.viewLeft, vi.viewTop,
                     vi.viewWidth, vi.viewHeight, want.left, want.top, want.width, want.height);
        report.Check(vi.x == 320 && vi.y == 240, "player %d's screen is 320 by 240, not %d by %d", player + 1, vi.x,
                     vi.y);
        const float aspect = static_cast<float>(want.width) / static_cast<float>(want.height);
        report.Check(std::fabs(stage.PlayerAt(player).camera.Aspect() - aspect) < 1e-3f,
                     "player %d's projection aspect is %.3f, the shape of its view's %.3f", player + 1,
                     stage.PlayerAt(player).camera.Aspect(), aspect);
        for (int other = 0; other < player; other++) {
            const ViewRect o = ExpectedView(players, other);
            report.Check(want.left + want.width <= o.left || o.left + o.width <= want.left ||
                             want.top + want.height <= o.top || o.top + o.height <= want.top,
                         "the views of players %d and %d do not overlap", other + 1, player + 1);
        }
    }
}

// What a frame's display list holds of each player's pass.
struct PassCommands {
    ViewRect viewport{};
    const void* projection = nullptr;
    int depthClears = 0;
};

std::vector<PassCommands> ReadPasses(const std::vector<Gfx>& list) {
    using Fast::F3DEX_G_MOVEMEM;
    using Fast::F3DEX_G_MTX;
    using Fast::F3DEX_G_MV_VIEWPORT;
    constexpr uint32_t kProjectionLoad = Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD;
    std::vector<PassCommands> passes;
    int clears = 0;
    bool awaitingProjection = false;
    for (const Gfx& command : list) {
        const uint32_t op = command.words.w0 >> 24;
        if (op == static_cast<uint8_t>(G_SETZIMG)) {
            clears++;
        } else if (op == static_cast<uint8_t>(F3DEX_G_MOVEMEM) &&
                   ((command.words.w0 >> 16) & 0xFF) == static_cast<uint32_t>(F3DEX_G_MV_VIEWPORT)) {
            const auto* vp = reinterpret_cast<const Vp_t*>(command.words.w1);
            PassCommands pass;
            pass.viewport.width = vp->vscale[0] / 2;
            pass.viewport.height = vp->vscale[1] / 2;
            pass.viewport.left = (vp->vtrans[0] - vp->vscale[0]) / 4;
            pass.viewport.top = (vp->vtrans[1] - vp->vscale[1]) / 4;
            pass.depthClears = clears;
            clears = 0;
            passes.push_back(pass);
            awaitingProjection = true;
        } else if (op == static_cast<uint8_t>(F3DEX_G_MTX) && awaitingProjection &&
                   ((command.words.w0 >> 16) & 0xFF) == kProjectionLoad) {
            passes.back().projection = reinterpret_cast<const void*>(command.words.w1);
            awaitingProjection = false;
        }
    }
    return passes;
}

// A frame draws one pass per player, in the order the shuffle left, and each
// pass has its own player's view, projection and z-buffer clear.
void CheckPasses(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    Scripts scripts;
    for (int player = 0; player < game.Players(); player++) {
        scripts[static_cast<size_t>(player)].Walk(0, 40, 30 + 10 * player).Turn(10, 30, player % 2 ? -60 : 60);
    }
    game.Run(60, scripts);
    const std::vector<Gfx> list = game.RenderFrame();
    const std::vector<PassCommands> passes = ReadPasses(list);
    Level::Stage& stage = game.GetStage();
    report.Check(passes.size() == static_cast<size_t>(game.Players()), "the frame has %d passes, not %zu",
                 game.Players(), passes.size());
    for (size_t pass = 0; pass < passes.size() && pass < static_cast<size_t>(game.Players()); pass++) {
        const int player = stage.NthPlayerShuffled(static_cast<int>(pass));
        const ViewRect want = ExpectedView(game.Players(), player);
        const ViewRect got = passes[pass].viewport;
        report.Check(got.left == want.left && got.top == want.top && got.width == want.width &&
                         got.height == want.height,
                     "pass %zu is player %d's view (%d,%d %dx%d), not %d,%d %dx%d", pass + 1, player + 1, want.left,
                     want.top, want.width, want.height, got.left, got.top, got.width, got.height);
        report.Check(passes[pass].projection == &stage.PlayerAt(player).camera.Projection(),
                     "pass %zu projects with player %d's own camera", pass + 1, player + 1);
        report.Check(passes[pass].depthClears == 1, "pass %zu clears one z-buffer half, not %d", pass + 1,
                     passes[pass].depthClears);
    }

    // The camera of each view is its own player's, and no two are alike.
    for (int player = 0; player < game.Players(); player++) {
        const Level::PlayerState& state = stage.PlayerAt(player);
        const Level::Vec3 eye = state.camera.WorldPosition();
        const Level::Vec3 body = state.move.Position();
        report.Check(eye == body, "player %d's camera is at (%.0f, %.0f, %.0f), not their own (%.0f, %.0f, %.0f)",
                     player + 1, eye[0], eye[1], eye[2], body[0], body[1], body[2]);
        for (int other = 0; other < player; other++) {
            const Level::Vec3 theirs = stage.PlayerAt(other).camera.WorldPosition();
            report.Check(!(eye == theirs), "players %d and %d do not share a camera", other + 1, player + 1);
        }
    }
    report.Check(stage.CurrentPlayer() == 0, "the current player is player 1 again after the frame");
}

Scripts DeterminismScripts(int players) {
    Scripts scripts;
    for (int player = 0; player < players; player++) {
        scripts[static_cast<size_t>(player)].Walk(0, 50, 40 + 10 * player).Turn(20, 40, player % 2 ? -50 : 50);
    }
    return scripts;
}

std::vector<float> Snapshot(MultiplayerSession& game) {
    std::vector<float> values;
    for (int player = 0; player < game.Players(); player++) {
        for (const float value : game.PositionOf(player)) {
            values.push_back(value);
        }
        for (const float value : game.LookOf(player)) {
            values.push_back(value);
        }
    }
    return values;
}

// Prints where every player is each ten ticks while player 1 walks, for
// working out what a case should expect (`... debug`).
void TracePlayers(Report&, MultiplayerSession& game) {
    Scripts scripts;
    scripts[0].Walk(0, 10);
    for (int step = 0; step < 24; step++) {
        game.Run(10, step < 6 ? Scripts{} : scripts);
        std::printf("tick %3d", game.Ticks());
        for (int player = 0; player < game.Players(); player++) {
            const Position at = game.PositionOf(player);
            std::printf("  p%d (%.0f %.0f %.0f)", player + 1, at[0], at[1], at[2]);
        }
        std::printf("  mode %d\n", static_cast<int>(game.CameraMode()));
    }
}

// Prints the weapon and ammo records of the stage in setup order, as stocked
// (`... setup-dump`).
void DumpSetup(Report&, MultiplayerSession& game) {
    const Level::Stage& stage = game.GetStage();
    const auto& defs = stage.Setup()->PropDefs();
    std::printf("-- %d players, weapon set %d\n", game.Players(), game.Settings().weaponSet);
    for (size_t i = 0; i < defs.size(); i++) {
        const Level::PropDef& def = defs[i];
        if (!def.Is(Level::PropDefType::Collectable) && !def.Is(Level::PropDefType::Ammo)) {
            continue;
        }
        const bool loaded = Level::StageSetup::LoadsObject(def, Game::Save::kDifficultyMulti, game.Players());
        std::printf("%4zu %-11s item %3d model %3d flags2 %08x loaded %d made %d", i,
                    def.Is(Level::PropDefType::Ammo) ? "ammo" : "weapon", def.weaponNum, def.model, def.flags2, loaded,
                    stage.PlacedObjects().ForDef(i) != nullptr);
        if (def.Is(Level::PropDefType::Ammo)) {
            for (size_t slot = 0; slot < def.ammoSlots.size(); slot++) {
                if (def.ammoSlots[slot].quantity != 0) {
                    std::printf("  ammo%zu=%d", slot + 1, def.ammoSlots[slot].quantity);
                }
            }
        }
        std::printf("\n");
    }
}

// -- Spawn, death, respawn and the kill bookkeeping ---------------------------------------

// A hit that kills a player at full health (the handicap scales it).
constexpr float kLethal = 10.0f;
// Ticks the death screen and the respawn press take to play out.
constexpr int kDeathLimit = 900;
// The hit's display ends within this many ticks.
constexpr int kHitDisplayTicks = 90;

Level::PlayerState& PlayerOf(MultiplayerSession& game, int player) {
    return game.GetStage().PlayerAt(player);
}

const Level::PlayerStats& StatsOf(MultiplayerSession& game, int player) {
    return game.GetStage().Stats(player);
}

bool IsDead(MultiplayerSession& game, int player) {
    return PlayerOf(game, player).health.IsDead();
}

// The weapons lying free within 150 units of the player: what drop_inventory
// leaves where the player stood.
int WeaponsAround(MultiplayerSession& game, int player) {
    const Level::PropList& props = game.GetStage().PlacedObjects().Props();
    const Position at = game.PositionOf(player);
    int count = 0;
    for (const int id : props.Active()) {
        const Level::Prop& prop = props[id];
        if (prop.type == Level::PropType::Weapon && prop.parent < 0 &&
            Distance(at, { prop.position[0], at[1], prop.position[2] }) < 150.0f) {
            count++;
        }
    }
    return count;
}

// Ticks until the death animation, the blood and the fade to black are over, so
// the pad can respawn the player. False if they never are.
bool WaitForDeathScreen(MultiplayerSession& game, int player) {
    for (int waited = 0; waited < kDeathLimit; waited++) {
        Level::PlayerState& state = PlayerOf(game, player);
        if (state.health.DeathAnimFinished() && state.colour.IsComplete()) {
            return true;
        }
        game.RunDrawingTheDead(1);
    }
    return false;
}

// Presses A on `player`'s pad until the player lives again. False if it never does.
bool PressToRespawn(MultiplayerSession& game, int player) {
    for (int attempt = 0; attempt < 10; attempt++) {
        Scripts scripts;
        scripts[static_cast<size_t>(player)].Press(0, 3, Level::Buttons::A);
        game.RunDrawingTheDead(3, scripts);
        if (!IsDead(game, player)) {
            return true;
        }
    }
    return false;
}

// The players are in play: the opening is over and everyone has settled.
bool StartPlaying(Report& report, MultiplayerSession& game) {
    const bool playing = game.WaitForControl(kSettleTicks) >= 0;
    report.Check(playing, "the opening ends and the players take control");
    return playing;
}

// Waits out the display of the last hit, so the next one lands.
void LetHitsSettle(MultiplayerSession& game) {
    game.RunDrawingTheDead(kHitDisplayTicks);
}

void Kill(MultiplayerSession& game, int victim, int attacker) {
    game.GetStage().DamagePlayer(victim, attacker, kLethal, 0.0f, 1.0f);
}

// Each player in turn is killed by the next one: the kill, the death and the
// credit land on the right players and nobody else's counts move.
void CheckKill(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    for (int victim = 0; victim < players; victim++) {
        const int attacker = (victim + 1) % players;
        std::vector<int> kills, deaths, killsThisLife, suicides;
        std::vector<std::vector<int>> credited;
        for (int player = 0; player < players; player++) {
            kills.push_back(StatsOf(game, player).killCount);
            deaths.push_back(PlayerOf(game, player).deathCount);
            killsThisLife.push_back(PlayerOf(game, player).killsThisLife);
            suicides.push_back(PlayerOf(game, player).suicides);
            credited.emplace_back(StatsOf(game, player).killCounts.begin(), StatsOf(game, player).killCounts.end());
        }

        Kill(game, victim, attacker);

        for (int player = 0; player < players; player++) {
            const size_t at = static_cast<size_t>(player);
            report.Check(IsDead(game, player) == (player == victim), "only player %d dies when player %d kills them",
                         victim + 1, attacker + 1);
            report.Check(StatsOf(game, player).killCount == kills[at] + (player == attacker ? 1 : 0),
                         "player %d's kill count after player %d's kill of player %d", player + 1, attacker + 1,
                         victim + 1);
            report.Check(PlayerOf(game, player).killsThisLife == killsThisLife[at] + (player == attacker ? 1 : 0),
                         "player %d's kills this life", player + 1);
            report.Check(PlayerOf(game, player).deathCount == deaths[at] + (player == victim ? 1 : 0),
                         "player %d's death count", player + 1);
            report.Check(PlayerOf(game, player).suicides == suicides[at], "player %d has no suicide", player + 1);
            for (int other = 0; other < players; other++) {
                const int expected =
                    credited[at][static_cast<size_t>(other)] + (player == attacker && other == victim ? 1 : 0);
                report.Check(StatsOf(game, player).killCounts[static_cast<size_t>(other)] == expected,
                             "player %d's kills of player %d are %d", player + 1, other + 1, expected);
            }
        }
        report.Check(PlayerOf(game, attacker).lastKillTime[0] >= 0, "the kill is recorded in the attacker's times");
        report.Check(StatsOf(game, victim).shortestInning < INT32_MAX, "the victim's shortest life is recorded");

        // A dead player takes no further hit.
        Kill(game, victim, attacker);
        report.Check(StatsOf(game, attacker).killCount == kills[static_cast<size_t>(attacker)] + 1,
                     "a second hit on a dead player is not a second kill");

        report.Check(WaitForDeathScreen(game, victim), "player %d's death screen ends", victim + 1);
        report.Check(PressToRespawn(game, victim), "player %d respawns on a button", victim + 1);
        LetHitsSettle(game);
    }
}

// A player who kills themselves is counted as a suicide and not as a kill or a
// death, and credits themselves.
void CheckSuicide(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    for (int player = 0; player < game.Players(); player++) {
        const int deaths = PlayerOf(game, player).deathCount;
        const int kills = StatsOf(game, player).killCount;
        const int suicides = PlayerOf(game, player).suicides;
        const int credited = StatsOf(game, player).killCounts[static_cast<size_t>(player)];
        PlayerOf(game, player).intro.inventory.AddItem(Level::kItemPp7);
        const int around = WeaponsAround(game, player);

        Kill(game, player, player);

        report.Check(IsDead(game, player), "player %d dies by their own hand", player + 1);
        report.Check(PlayerOf(game, player).suicides == suicides + 1, "player %d's suicide is counted", player + 1);
        report.Check(StatsOf(game, player).killCount == kills, "a suicide is not a kill");
        report.Check(PlayerOf(game, player).deathCount == deaths, "a suicide is not counted as a death");
        report.Check(StatsOf(game, player).killCounts[static_cast<size_t>(player)] == credited + 1,
                     "the suicide is credited to the player");
        report.Check(WeaponsAround(game, player) == around, "a suicide drops nothing");
        for (int other = 0; other < game.Players(); other++) {
            if (other != player) {
                report.Check(!IsDead(game, other), "player %d is unharmed by player %d's suicide", other + 1,
                             player + 1);
            }
        }
        report.Check(WaitForDeathScreen(game, player) && PressToRespawn(game, player),
                     "player %d respawns after a suicide", player + 1);
        LetHitsSettle(game);
    }
}

// A player killed by another drops what they carry; the kill by no player (and
// the player's own) does the same for a death.
void CheckDropInventory(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    for (int victim = 0; victim < players; victim++) {
        const int attacker = (victim + 1) % players;
        Level::Inventory& inventory = PlayerOf(game, victim).intro.inventory;
        int carried = 0;
        for (const int32_t item : { Level::kItemPp7, Level::kItemShotgun, Level::kItemRocketLauncher }) {
            inventory.AddItem(item);
            carried++;
        }
        const int before = WeaponsAround(game, victim);
        const int others = WeaponsAround(game, attacker);

        Kill(game, victim, attacker);

        report.Check(WeaponsAround(game, victim) >= before + carried,
                     "player %d drops the %d weapons they carry (%d lie where they fell, %d before)", victim + 1,
                     carried, WeaponsAround(game, victim), before);
        report.Check(WeaponsAround(game, attacker) == others, "the killer's surroundings are unchanged");

        report.Check(WaitForDeathScreen(game, victim) && PressToRespawn(game, victim), "player %d respawns",
                     victim + 1);
        LetHitsSettle(game);
    }
}

// Whether any start pad is 1000 or more units from every player other than `player`.
bool ClearPadExists(MultiplayerSession& game, int player) {
    Level::Stage& stage = game.GetStage();
    for (const size_t index : PlayerOf(game, player).intro.startPads) {
        const Level::Vec3& pad = stage.Setup()->Pads()[index].position;
        bool clear = true;
        for (int other = 0; other < game.Players(); other++) {
            if (other == player) {
                continue;
            }
            const Position at = game.PositionOf(other);
            clear = clear && FloorDistance({ pad[0], pad[1], pad[2] }, at) >= 1000.0f;
        }
        if (clear) {
            return true;
        }
    }
    return false;
}

bool OnStartPad(MultiplayerSession& game, int player, const Position& at) {
    Level::Stage& stage = game.GetStage();
    for (const size_t index : PlayerOf(game, player).intro.startPads) {
        const Level::Vec3& pad = stage.Setup()->Pads()[index].position;
        if (FloorDistance({ pad[0], pad[1], pad[2] }, at) < 1.0f) {
            return true;
        }
    }
    return false;
}

// The life that follows a death: full health, a start pad away from the other
// players, the intro's inventory and ammunition, the stats of the life reset.
void CheckRespawn(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    for (int victim = 0; victim < players; victim++) {
        const int attacker = (victim + 1) % players;
        Level::PlayerState& state = PlayerOf(game, victim);
        // Whatever the player collected is lost with the life.
        state.intro.inventory.AddItem(Level::kItemShotgun);
        state.weapons.AddAmmoToWeapon(Level::kItemShotgun, 50);
        state.killsThisLife = 5;

        std::vector<Position> others;
        std::vector<float> healths;
        for (int player = 0; player < players; player++) {
            others.push_back(game.PositionOf(player));
            healths.push_back(game.HealthOf(player));
        }
        const int mission = game.GetStage().MissionTimer();

        Kill(game, victim, attacker);
        report.Check(WaitForDeathScreen(game, victim), "player %d's death screen ends", victim + 1);
        game.RunDrawingTheDead(30);
        report.Check(IsDead(game, victim), "player %d stays dead until a button is pressed", victim + 1);
        const bool clearPad = ClearPadExists(game, victim);
        report.Check(PressToRespawn(game, victim), "player %d respawns on a button", victim + 1);

        report.Check(!IsDead(game, victim) && game.HealthOf(victim) > 0.99f, "player %d is alive at full health",
                     victim + 1);
        report.Check(OnStartPad(game, victim, game.PositionOf(victim)), "player %d starts the new life on a start pad",
                     victim + 1);
        if (clearPad) {
            for (int other = 0; other < players; other++) {
                if (other != victim) {
                    report.Check(FloorDistance(game.PositionOf(victim), game.PositionOf(other)) >= 1000.0f,
                                 "player %d respawns 1000 units from player %d", victim + 1, other + 1);
                }
            }
        }
        report.Check(state.killsThisLife == 0, "the new life has no kills");
        report.Check(state.lifeStartTime > mission, "the new life starts at the time of the respawn");
        report.Check(!state.intro.inventory.HasItem(Level::kItemShotgun), "what the player collected is gone");
        report.Check(state.weapons.AmmoCountForWeapon(Level::kItemShotgun) == 0, "and its ammunition");
        for (const Level::IntroItem& item : state.intro.items) {
            report.Check(state.intro.inventory.HasItem(item.itemRight), "the intro's item %d is back", item.itemRight);
        }
        for (const Level::IntroAmmo& ammo : state.intro.ammo) {
            report.Check(state.weapons.AmmoHeld(ammo.ammoType) >= ammo.amount, "the intro's ammunition %d is back",
                         ammo.ammoType);
        }
        for (int player = 0; player < players; player++) {
            if (player != victim) {
                report.Check(Distance(game.PositionOf(player), others[static_cast<size_t>(player)]) < kStill &&
                                 !IsDead(game, player),
                             "player %d is not touched by player %d's respawn", player + 1, victim + 1);
            }
        }
        // The new life walks.
        const Position from = game.PositionOf(victim);
        Scripts scripts;
        scripts[static_cast<size_t>(victim)].Walk(0, kWalkTicks);
        game.RunDrawingTheDead(kWalkTicks, scripts);
        report.Check(Distance(from, game.PositionOf(victim)) > kMoved, "player %d walks in the new life", victim + 1);
        LetHitsSettle(game);
    }
}

void ConfigureYoltScenario(MultiplayerSettings& settings) {
    settings.scenario = Game::MpScenario::YouOnlyLiveTwice;
}

// A player killed twice by the same opponent is out in You Only Live Twice.
void CheckYolt(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int victim = 0;
    const int attacker = 1;
    Kill(game, victim, attacker);
    report.Check(WaitForDeathScreen(game, victim) && PressToRespawn(game, victim),
                 "the first death respawns in You Only Live Twice");
    LetHitsSettle(game);
    Kill(game, victim, attacker);
    report.Check(WaitForDeathScreen(game, victim), "the second death screen ends");
    Scripts scripts;
    scripts[static_cast<size_t>(victim)].Press(0, 60, Level::Buttons::A);
    game.RunDrawingTheDead(60, scripts);
    report.Check(IsDead(game, victim), "the second death is final");
}

void ConfigureGoldenGunScenario(MultiplayerSettings& settings) {
    settings.scenario = Game::MpScenario::TheManWithTheGoldenGun;
}

// Killing the holder of the golden gun is counted for the killer.
void CheckGoldenGunKill(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int holder = 0;
    const int killer = 1;
    PlayerOf(game, holder).intro.inventory.AddItem(Level::kItemGoldenGun);
    const int around = WeaponsAround(game, holder);
    Kill(game, holder, killer);
    report.Check(StatsOf(game, killer).killedGoldenGunOwner == 1,
                 "the killer is credited with the golden gun's holder");
    report.Check(StatsOf(game, holder).killedGoldenGunOwner == 0, "and nobody else");
    report.Check(WeaponsAround(game, holder) > around, "the golden gun drops");
    report.Check(WaitForDeathScreen(game, holder) && PressToRespawn(game, holder), "the holder respawns");
    LetHitsSettle(game);

    // The holder of nothing special does not count.
    Kill(game, holder, killer);
    report.Check(StatsOf(game, killer).killedGoldenGunOwner == 1, "a kill of a player without it is not counted");
}

// The streak, timing and back-damage statistics.
void CheckKillStats(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    const int attacker = 0;

    // Every other player killed in the same tick is a streak of that many.
    for (int victim = 1; victim < players; victim++) {
        Kill(game, victim, attacker);
    }
    const Level::PlayerStats& stats = StatsOf(game, attacker);
    report.Check(stats.killCount == players - 1, "player 1 killed %d players", players - 1);
    report.Check(stats.mostKilledOneTime == (players == 2 ? 1 : players - 1), "the kills in a streak are counted (%d)",
                 stats.mostKilledOneTime);
    report.Check(PlayerOf(game, attacker).killsThisLife == players - 1, "and in the life");
    if (players > 2) {
        report.Check(stats.minTimeBetweenKills == 0 && stats.maxTimeBetweenKills == 0,
                     "kills in one tick are no time apart (%d, %d)", stats.minTimeBetweenKills,
                     stats.maxTimeBetweenKills);
    }
    for (int victim = 1; victim < players; victim++) {
        report.Check(WaitForDeathScreen(game, victim) && PressToRespawn(game, victim), "player %d respawns",
                     victim + 1);
    }
    LetHitsSettle(game);

    // A hit from behind counts for the attacker, from the front it does not.
    const int victim = 1;
    const float theta = PlayerOf(game, victim).move.Theta();
    const float behind = (360.0f - theta) * 3.1415927f / 180.0f;
    const int backside = StatsOf(game, attacker).damageToBackside;
    game.GetStage().DamagePlayer(victim, attacker, 0.05f, std::sin(behind), std::cos(behind));
    report.Check(StatsOf(game, attacker).damageToBackside == backside + 1,
                 "a hit from behind is counted for the attacker");
    LetHitsSettle(game);
    game.GetStage().DamagePlayer(victim, attacker, 0.05f, -std::sin(behind), -std::cos(behind));
    report.Check(StatsOf(game, attacker).damageToBackside == backside + 1, "a hit from the front is not");
    for (int player = 0; player < players; player++) {
        if (player != attacker) {
            report.Check(StatsOf(game, player).damageToBackside == 0, "player %d did not hit anyone", player + 1);
        }
    }
}

// Distance covered is each player's own.
void CheckDistance(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    for (int mover = 0; mover < game.Players(); mover++) {
        game.Run(kCoastTicks);
        std::vector<float> before;
        for (int player = 0; player < game.Players(); player++) {
            before.push_back(StatsOf(game, player).distanceTraveled);
        }
        Scripts scripts;
        scripts[static_cast<size_t>(mover)].Walk(0, kWalkTicks);
        game.Run(kWalkTicks, scripts);
        for (int player = 0; player < game.Players(); player++) {
            const float travelled = StatsOf(game, player).distanceTraveled - before[static_cast<size_t>(player)];
            if (player == mover) {
                report.Check(travelled > kMoved, "player %d's distance grows as they walk (%.1f)", player + 1,
                             travelled);
            } else {
                report.Check(travelled < kStill, "player %d's distance does not grow when player %d walks (%.2f)",
                             player + 1, mover + 1, travelled);
            }
        }
    }
}

// An explosion kills the player at its centre, credited to whoever made it.
void CheckExplosionCredit(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    // EXPLOSION_DEF_STANDARD.
    constexpr int kStandardExplosion = 13;
    const int players = game.Players();
    for (int victim = 0; victim < players; victim++) {
        const int attacker = (victim + 1) % players;
        const int kills = StatsOf(game, attacker).killCount;
        const Level::Prop& prop = game.GetStage().PlacedObjects().Props()[PlayerOf(game, victim).move.PropId()];
        game.GetStage().GetExplosions().Create(-1, prop.position, prop.stan, kStandardExplosion, 0, attacker,
                                               prop.rooms, false, RandomGetNext);
        game.RunDrawingTheDead(30);
        report.Check(IsDead(game, victim), "player %d dies in player %d's explosion", victim + 1, attacker + 1);
        report.Check(StatsOf(game, attacker).killCount == kills + 1 &&
                         StatsOf(game, attacker).killCounts[static_cast<size_t>(victim)] >= 1,
                     "the explosion's maker is credited with the kill");
        report.Check(WaitForDeathScreen(game, victim) && PressToRespawn(game, victim), "player %d respawns",
                     victim + 1);
        LetHitsSettle(game);
    }
}

// A player's start is on a start pad and, where the stage has room, away from
// the others; the choice is the same for the same seed.
void CheckSpawnPads(Report& report, MultiplayerSession& game) {
    Level::Stage& stage = game.GetStage();
    for (int player = 0; player < game.Players(); player++) {
        report.Check(!PlayerOf(game, player).intro.startPads.empty(), "the stage has start pads for player %d",
                     player + 1);
        report.Check(OnStartPad(game, player, game.PositionOf(player)), "player %d starts on a start pad", player + 1);
        report.Check(stage.PlayerAt(player).spawn.eyeHeight > 0.0f, "player %d's eye height is set", player + 1);
    }
    for (int a = 0; a < game.Players(); a++) {
        for (int b = a + 1; b < game.Players(); b++) {
            report.Check(FloorDistance(game.PositionOf(a), game.PositionOf(b)) >= 1000.0f ||
                             stage.PlayerAt(a).intro.startPads.size() < static_cast<size_t>(game.Players()) * 2,
                         "players %d and %d start 1000 units apart on a stage with room", a + 1, b + 1);
        }
    }
}

// Distinct roster rows, with a woman among them and a short and a tall character.
void ConfigureCharacters(MultiplayerSettings& settings) {
    settings.character = { 10, 1, 9, 40 };
}

// Each player is a character in the stage with the head, body and height they
// chose.
void CheckCharacters(Report& report, MultiplayerSession& game) {
    Level::Stage& stage = game.GetStage();
    const Game::ChrRecords* records = stage.PlacedChrs().Records();
    report.Check(records != nullptr, "the stage has the characters' records");
    if (records == nullptr) {
        return;
    }
    std::set<int> slots;
    for (int player = 0; player < game.Players(); player++) {
        const Level::PlayerState& state = stage.PlayerAt(player);
        report.Check(state.bondChr >= 0, "player %d has a character", player + 1);
        if (state.bondChr < 0) {
            continue;
        }
        slots.insert(state.bondChr);
        const Level::Chr& chr = stage.PlacedChrs().Slots()[static_cast<size_t>(state.bondChr)];
        const Level::Prop& prop = stage.PlacedObjects().Props()[state.move.PropId()];
        report.Check(prop.type == Level::PropType::Viewer && prop.object == state.bondChr,
                     "player %d's prop is a viewer whose character is theirs", player + 1);
        report.Check(chr.bodynum == records->Slot(game.Settings().CharacterBody(player)),
                     "player %d has the body of their character", player + 1);
        report.Check(chr.headnum == records->Slot(game.Settings().CharacterHead(player)),
                     "player %d has the head of their character", player + 1);
        report.Check(stage.Stats(player).perspectiveHeight == game.Settings().CharacterHeight(player),
                     "player %d stands as tall as their character", player + 1);
        report.Check(chr.model != nullptr, "player %d's character has a model", player + 1);
    }
    report.Check(static_cast<int>(slots.size()) == game.Players(), "every player has a character of their own");
}

// The character holds the weapon in the player's hand, and swaps it when the
// hand does.
void CheckHeldWeapons(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    Level::Stage& stage = game.GetStage();
    const auto held = [&](int player) -> int {
        const Level::PlayerState& state = stage.PlayerAt(player);
        if (state.bondChr < 0) {
            return -1;
        }
        const int prop = stage.PlacedChrs().Slots()[static_cast<size_t>(state.bondChr)].weaponsHeld[Level::HandRight];
        const Level::PlacedObject* object = prop >= 0 ? stage.PlacedObjects().ForProp(prop) : nullptr;
        return object != nullptr ? object->weaponNum : -1;
    };
    for (int player = 0; player < game.Players(); player++) {
        const int item = stage.PlayerAt(player).weapons.WeaponId(Level::HandRight);
        if (item == Level::kItemUnarmed || item == Level::kItemFist) {
            report.Check(held(player) < 0, "player %d's character holds nothing for bare hands", player + 1);
            continue;
        }
        report.Check(held(player) == item, "player %d's character holds the %d in their hand, not %d", player + 1, item,
                     held(player));
    }
    for (int player = 0; player < game.Players(); player++) {
        stage.PlayerAt(player).weapons.EquipWeapon(Level::HandRight, Level::kItemShotgun);
    }
    game.Run(120);
    for (int player = 0; player < game.Players(); player++) {
        report.Check(stage.PlayerAt(player).weapons.WeaponId(Level::HandRight) == Level::kItemShotgun,
                     "player %d changed to the shotgun", player + 1);
        report.Check(held(player) == Level::kItemShotgun, "player %d's character holds the shotgun, not %d", player + 1,
                     held(player));
    }
}

// Stands `player` `distance` units from `other`, on `other`'s tile, and returns
// whether a spot was found.
bool StandBeside(MultiplayerSession& game, int player, int other, float distance) {
    Level::Stage& stage = game.GetStage();
    const Level::Stan& stan = stage.Bg().StanFile();
    const Level::PlayerMove& target = stage.PlayerAt(other).move;
    const Position at = target.CollisionPosition();
    for (const float angle : { 0.0f, 1.5708f, 3.1416f, 4.7124f }) {
        const float x = at[0] + (std::sin(angle) * distance);
        const float z = at[2] + (std::cos(angle) * distance);
        if (stan.PointWithinTileBounds(target.Tile(), x, z)) {
            stage.PlayerAt(player).move.AdoptBodyPosition({ x, at[1], z }, target.Tile(), stan);
            return true;
        }
    }
    return false;
}

// Turns `player` with the stick until they face `other`, drawing a frame each
// tick (the shots read the props the last frame left on screen).
void FaceAndRender(MultiplayerSession& game, int player, int other, int ticks, uint16_t buttons = 0) {
    for (int tick = 0; tick < ticks; tick++) {
        const Position from = game.PositionOf(player);
        const Position to = game.PositionOf(other);
        const Position look = game.LookOf(player);
        const float dx = to[0] - from[0];
        const float dz = to[2] - from[2];
        const float length = std::sqrt((dx * dx) + (dz * dz));
        const float scale = length > 0.0f ? length : 1.0f;
        const float cross = ((look[0] * dz) - (look[2] * dx)) / scale;
        const float dot = ((look[0] * dx) + (look[2] * dz)) / scale;
        // The signed angle to the target, which saturates the stick when it is behind.
        const float angle = std::atan2(cross, dot);
        const int stick = static_cast<int>(std::max(-70.0f, std::min(70.0f, angle * 200.0f)));
        Scripts scripts;
        scripts[static_cast<size_t>(player)].Hold(0, 1, stick, 0, buttons);
        game.Run(1, scripts);
        game.Render();
    }
}

// The bodies of the other players are on screen in a player's view, and their
// own is not.
void CheckBodiesDrawn(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    Level::Stage& stage = game.GetStage();
    for (int other = 1; other < game.Players(); other++) {
        report.Check(StandBeside(game, other, 0, 90.0f + (30.0f * static_cast<float>(other))),
                     "player %d finds a place beside player 1", other + 1);
    }
    game.Run(2);
    game.Render();
    FaceAndRender(game, 0, 1, 40);
    // The on-screen flags are those of the pass drawn last, so draw player 1's
    // view alone to read theirs.
    game.RenderPlayer(0);
    const auto onScreen = [&](int player) {
        const Level::Prop& prop = stage.PlacedObjects().Props()[stage.PlayerAt(player).move.PropId()];
        return (prop.flags & Level::PropFlag::OnScreen) != 0;
    };
    report.Check(!onScreen(0), "player 1 does not see their own body");
    report.Check(onScreen(1), "player 1 sees the body of player 2");
    for (int other = 2; other < game.Players(); other++) {
        report.Check(stage.PlayerAt(other).bondChr >= 0, "player %d has a body to draw", other + 1);
    }
}

// A player's shots hit the body of another, who takes the damage.
void CheckShootPlayer(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    Level::PlayerState& shooter = game.GetStage().PlayerAt(0);
    shooter.intro.inventory.AddItem(Level::kItemPp7);
    shooter.weapons.GiveAmmo(shooter.weapons.Table().AmmoType(Level::kItemPp7), 200);
    shooter.weapons.EquipWeapon(Level::HandRight, Level::kItemPp7);
    shooter.weapons.EquipWeapon(Level::HandLeft, Level::kItemUnarmed);
    game.Run(80);
    for (int victim = 1; victim < game.Players(); victim++) {
        if (!StandBeside(game, 0, victim, 120.0f)) {
            report.Check(false, "player 1 finds a place beside player %d", victim + 1);
            continue;
        }
        game.Run(2);
        game.Render();
        FaceAndRender(game, 0, victim, 60);
        const float before = game.HealthOf(victim);
        for (int burst = 0; burst < 8; burst++) {
            FaceAndRender(game, 0, victim, 1, Level::Buttons::Z);
            FaceAndRender(game, 0, victim, 8);
        }
        report.Check(game.HealthOf(victim) < before, "player 1's shots hurt player %d (%.2f to %.2f)", victim + 1,
                     before, game.HealthOf(victim));
    }
}

// Handicaps spread over the table's rows: the victims take 1.3, 0.455 and 1.0
// times a hit's damage.
void ConfigurePlayerDamage(MultiplayerSettings& settings) {
    settings.handicap = { 5, 4, 8, 5 };
}

bool NearlyEqual(float a, float b) {
    return std::fabs(a - b) < 0.0005f;
}

// The damage of a DD44 (destruction 1.0) hit on a player: record_damage_kills
// takes handles_shot_actors' eighth of the weapon's damage times the part hit
// (4 for the head, 2 for the chest, 1 for a limb), times the victim's
// handicap, off the victim's health: 0.5, 0.25 or 0.125 of a full bar at the
// normal handicap, so two head hits or four to the chest kill.
void CheckPlayerDamage(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    constexpr float kChestHit = 0.25f;

    // Hits recorded directly: the victim's handicap multiplies them.
    for (int victim = 0; victim < game.Players(); victim++) {
        const int attacker = (victim + 1) % game.Players();
        const float handicap = game.Settings().Handicap(victim);
        const float before = game.HealthOf(victim);
        stage.DamagePlayer(victim, attacker, kChestHit, 0.0f, 1.0f);
        report.Check(NearlyEqual(before - game.HealthOf(victim), kChestHit * handicap),
                     "a chest hit takes %.3f of player %d's health at handicap %.3f (took %.4f)", kChestHit * handicap,
                     victim + 1, handicap, before - game.HealthOf(victim));
        LetHitsSettle(game);
    }

    // Hits from a DD44, which land on the head, chest or limbs.
    Level::PlayerState& shooter = stage.PlayerAt(0);
    shooter.intro.inventory.AddItem(Level::kItemDd44);
    shooter.weapons.GiveAmmo(shooter.weapons.Table().AmmoType(Level::kItemDd44), 200);
    shooter.weapons.EquipWeapon(Level::HandRight, Level::kItemDd44);
    shooter.weapons.EquipWeapon(Level::HandLeft, Level::kItemUnarmed);
    game.Run(80);
    for (int victim = 1; victim < game.Players(); victim++) {
        if (!StandBeside(game, 0, victim, 120.0f)) {
            report.Check(false, "player 1 finds a place beside player %d", victim + 1);
            continue;
        }
        game.Run(2);
        game.Render();
        FaceAndRender(game, 0, victim, 60);
        const float handicap = game.Settings().Handicap(victim);
        float last = game.HealthOf(victim);
        int hits = 0;
        bool wholeEighths = true;
        for (int burst = 0; burst < 24 && game.HealthOf(victim) > 0.0f; burst++) {
            FaceAndRender(game, 0, victim, 1, Level::Buttons::Z);
            FaceAndRender(game, 0, victim, 12);
            const float drop = last - game.HealthOf(victim);
            if (drop > 0.0f) {
                const float eighths = drop / (0.125f * handicap);
                wholeEighths = wholeEighths && NearlyEqual(eighths, std::round(eighths)) && eighths > 0.5f;
                hits++;
                last = game.HealthOf(victim);
            }
        }
        report.Check(hits > 0, "player 1's DD44 hurts player %d", victim + 1);
        report.Check(wholeEighths, "every DD44 hit takes a whole number of eighths times player %d's handicap %.3f",
                     victim + 1, handicap);
        if (NearlyEqual(handicap, 1.0f)) {
            report.Check(hits >= 2 && hits <= 8, "player %d, at the normal handicap, takes %d DD44 hits to kill",
                         victim + 1, hits);
        }
    }
}

// Women, men and the body's own character: the players are Oddjob, Natalya,
// Jaws and a woman (mp_chr_setup rows 10, 1, 9 and 40).
void ConfigureHurtSounds(MultiplayerSettings& settings) {
    settings.character = { 10, 1, 9, 40 };
}

// A shot that hits a player's body plays the hurt sound of the character the
// player chose (get_player_mp_char_gender), and the player shot also hears
// Bond's own.
void CheckHurtSounds(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::PlayerState& shooter = game.GetStage().PlayerAt(0);
    shooter.intro.inventory.AddItem(Level::kItemPp7);
    shooter.weapons.GiveAmmo(shooter.weapons.Table().AmmoType(Level::kItemPp7), 200);
    shooter.weapons.EquipWeapon(Level::HandRight, Level::kItemPp7);
    shooter.weapons.EquipWeapon(Level::HandLeft, Level::kItemUnarmed);
    game.Run(80);
    const auto count = [&](int first, int last) {
        int total = 0;
        for (int sound = first; sound <= last; sound++) {
            total += gAudio->Count(AudioRig::Call::Kind::PlaySfx, sound);
        }
        return total;
    };
    for (int victim = 1; victim < game.Players(); victim++) {
        if (!StandBeside(game, 0, victim, 120.0f)) {
            report.Check(false, "player 1 finds a place beside player %d", victim + 1);
            continue;
        }
        game.Run(2);
        game.Render();
        FaceAndRender(game, 0, victim, 60);
        gAudio->Clear();
        for (int burst = 0; burst < 8; burst++) {
            FaceAndRender(game, 0, victim, 1, Level::Buttons::Z);
            FaceAndRender(game, 0, victim, 8);
        }
        const bool female = game.Settings().CharacterIsFemale(victim);
        const int men = count(AudioDriver::kSfxGetHitMale0, AudioDriver::kSfxGetHitMale24);
        const int women = count(AudioDriver::kSfxGetHitGirl1, AudioDriver::kSfxGetHitGirl3);
        report.Check(gAudio->Count(AudioRig::Call::Kind::PlaySfx, AudioDriver::kSfxBondGetHit1) > 0,
                     "player %d, shot, hears the hit sound of their own", victim + 1);
        if (female) {
            report.Check(women > 0 && men == 0,
                         "player %d, a woman, is shot to the women's hurt sounds (%d women, %d men)", victim + 1, women,
                         men);
        } else {
            report.Check(men > 0 && women == 0, "player %d, a man, is shot to the men's hurt sounds (%d men, %d women)",
                         victim + 1, men, women);
        }
        LetHitsSettle(game);
    }
}

// A dead player waits on the prompt until the pad presses A, Z or Start: the
// decomp's death flow has no timer that respawns them, and B does not.
void CheckRespawnNeedsPress(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    for (int victim = 0; victim < game.Players(); victim++) {
        Kill(game, victim, (victim + 1) % game.Players());
        report.Check(WaitForDeathScreen(game, victim), "player %d's death screen comes up", victim + 1);
        game.RunDrawingTheDead(3600);
        report.Check(IsDead(game, victim), "player %d is still dead a minute on, with no press", victim + 1);
        Scripts scripts;
        scripts[static_cast<size_t>(victim)].Press(0, 3, Level::Buttons::B);
        game.RunDrawingTheDead(10, scripts);
        report.Check(IsDead(game, victim), "B does not respawn player %d", victim + 1);
        const uint16_t buttons[] = { Level::Buttons::A, Level::Buttons::Z, Level::Buttons::Start };
        Scripts press;
        press[static_cast<size_t>(victim)].Press(0, 3, buttons[victim % 3]);
        game.RunDrawingTheDead(10, press);
        report.Check(!IsDead(game, victim), "A, Z or Start respawns player %d", victim + 1);
        LetHitsSettle(game);
    }
}

// A dying player's blood advances a frame each tick, each into the buffer the
// last was not drawn from (the texture cache keys on the address).
void CheckDeathBlood(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    for (int victim = 0; victim < game.Players(); victim++) {
        Kill(game, victim, (victim + 1) % game.Players());
        Level::PlayerState& state = PlayerOf(game, victim);
        const uint8_t* previous = nullptr;
        std::vector<uint8_t> previousPicture;
        int frames = 0;
        int repeats = 0;
        int sameBuffer = 0;
        for (int tick = 0; tick < kDeathLimit && !state.health.RedBloodFinished(); tick++) {
            game.RunDrawingTheDead(1);
            if (!state.blood || !state.bloodOverlay) {
                continue;
            }
            const uint8_t* texture = state.blood->Texture();
            const std::vector<uint8_t> picture(texture, texture + Game::Menu::kBloodTextureBytes);
            if (previous != nullptr) {
                frames++;
                repeats += picture == previousPicture ? 1 : 0;
                sameBuffer += texture == previous ? 1 : 0;
            }
            previous = texture;
            previousPicture = picture;
        }
        report.Check(frames > 10, "player %d's blood runs for %d frames", victim + 1, frames);
        report.Check(repeats <= frames / 4, "player %d's blood changes nearly every frame (%d of %d repeat)",
                     victim + 1, repeats, frames);
        report.Check(sameBuffer == 0, "player %d's blood alternates buffers", victim + 1);
        LetHitsSettle(game);
        PressToRespawn(game, victim);
    }
}

// Every character of the roster has a head and a body in the tables.
void CheckRoster(Report& report, MultiplayerSession& game) {
    const Game::ChrRecords* records = game.GetStage().PlacedChrs().Records();
    report.Check(records != nullptr, "the stage has the characters' records");
    if (records == nullptr) {
        return;
    }
    MultiplayerSettings settings = game.Settings();
    for (int row = 0; row < MultiplayerSettings::kCharacterRows; row++) {
        settings.character = { row, row, row, row };
        const Game::ChrRecord* body = records->At(records->Slot(settings.CharacterBody(0)));
        const Game::ChrRecord* head = records->At(records->Slot(settings.CharacterHead(0)));
        report.Check(body != nullptr && head != nullptr, "character %d has a body (%s) and a head (%s)", row,
                     std::string(settings.CharacterBody(0)).c_str(), std::string(settings.CharacterHead(0)).c_str());
        report.Check(settings.CharacterHeight(0) > 0.5f, "character %d has a height", row);
    }
}

// What the others see of a player follows what the player does: the body
// stands, walks and holds the gun the way the animation tables say.
void CheckBodyAnimation(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    Level::Stage& stage = game.GetStage();
    report.Check(StandBeside(game, 1, 0, 150.0f), "player 2 finds a place beside player 1");
    Level::PlayerState& walker = stage.PlayerAt(1);
    walker.intro.inventory.AddItem(Level::kItemPp7);
    walker.weapons.GiveAmmo(walker.weapons.Table().AmmoType(Level::kItemPp7), 200);
    walker.weapons.EquipWeapon(Level::HandRight, Level::kItemPp7);
    walker.weapons.EquipWeapon(Level::HandLeft, Level::kItemUnarmed);
    const auto settle = [&](int ticks, const Scripts& scripts = {}) {
        for (int tick = 0; tick < ticks; tick++) {
            game.Run(1, scripts);
            game.Render();
        }
    };
    settle(90);
    const Level::Chr& chr = stage.PlacedChrs().Slots()[static_cast<size_t>(walker.bondChr)];
    const auto* standing = walker.bodyClip;
    report.Check(standing != nullptr, "the body of a standing player plays an animation");
    report.Check(chr.actionType == Level::ChrAction::BondMulti, "the body is posed as another player's");
    report.Check((chr.hidden & Level::ChrHidden::Freeze) != 0, "the body is held until the next tick");

    Scripts walk;
    walk[1].Walk(0, 40);
    settle(40, walk);
    report.Check(walker.bodyClip != nullptr && walker.bodyClip != standing,
                 "the body of a walking player changes its animation");
    const auto* walking = walker.bodyClip;

    Scripts sidestep;
    sidestep[1].Press(0, 40, Level::Buttons::RightC);
    settle(40, sidestep);
    report.Check(walker.bodyClip != nullptr && walker.bodyClip != walking,
                 "the body of a sidestepping player changes its animation");

    // A shot is seen on the body: the muzzle flash shows while it fires.
    bool flashed = false;
    for (int burst = 0; burst < 6; burst++) {
        Scripts fire;
        fire[1].Press(0, 1, Level::Buttons::Z);
        settle(1, fire);
        flashed = flashed || walker.weapons.Hands(Level::HandRight).flashVisible;
        settle(8);
    }
    report.Check(flashed, "player 2's gun flashed");
}

// -- Weapon sets, pickups and respawning props --------------------------------------------

// A respawning pickup comes back this many ticks after it is taken (0x4B0).
constexpr int kRespawnTicks = 1200;
// A player this close to a pickup holds its return back.
constexpr float kRespawnHoldDistance = 400.0f;

std::function<void(MultiplayerSettings&)> WithWeaponSet(int set) {
    return [set](MultiplayerSettings& settings) { settings.weaponSet = set; };
}

// The objects the setup made (not ones a throw or a drop made) of a record
// type that no character holds.
std::vector<const Level::PlacedObject*> StockedObjects(MultiplayerSession& game, Level::PropDefType type) {
    std::vector<const Level::PlacedObject*> found;
    for (const Level::PlacedObject& object : game.GetStage().PlacedObjects().All()) {
        if (object.def != Level::kNoDef && object.prop >= 0 && object.type == static_cast<uint8_t>(type) &&
            (object.flags & Level::ObjectFlag::AssignedToChr) == 0) {
            found.push_back(&object);
        }
    }
    return found;
}

const Level::MpWeaponSetEntry* RowForItem(int set, int32_t item) {
    for (const Level::MpWeaponSetEntry& row : Level::MpWeaponSetData(set)) {
        if (row.item == item) {
            return &row;
        }
    }
    return nullptr;
}

const Level::Prop& PropOf(MultiplayerSession& game, const Level::PlacedObject& object) {
    return game.GetStage().PlacedObjects().Props()[object.prop];
}

// The weapons and ammunition crates the stage made are the weapon set's: every
// slot of the setup holds the set's weapon, the ones only the crates give are
// not made, and the crate after a weapon holds the set's ammunition.
void CheckWeaponStock(Report& report, MultiplayerSession& game) {
    const int set = game.Settings().weaponSet;
    const Level::Stage& stage = game.GetStage();
    const Level::Objects& objects = stage.PlacedObjects();
    const auto& defs = stage.Setup()->PropDefs();
    const Level::MpWeaponSetEntry* last = nullptr;
    int madeWeapons = 0;
    for (size_t index = 0; index < defs.size(); index++) {
        const Level::PropDef& def = defs[index];
        if (!Level::IsObjectRecord(def.type) || def.Is(Level::PropDefType::Door)) {
            continue;
        }
        const Level::PlacedObject* made = objects.ForDef(index);
        if (!Level::StageSetup::LoadsObject(def, Game::Save::kDifficultyMulti, game.Players())) {
            report.Check(made == nullptr, "record %zu is skipped at %d players and is not made", index, game.Players());
            continue;
        }
        if (made != nullptr && (made->flags & Level::ObjectFlag::AssignedToChr) == 0) {
            report.Check((made->state & Level::ObjectState::Respawn) != 0, "object %zu (type %d) respawns", index,
                         def.type);
        }
        if (def.Is(Level::PropDefType::Collectable) && (def.flags & Level::ObjectFlag::AssignedToChr) == 0) {
            report.Check(static_cast<uint8_t>(def.weaponNum) < 0xF0 || static_cast<uint8_t>(def.weaponNum) > 0xF7,
                         "weapon record %zu is not still a set slot (%d)", index, def.weaponNum);
            if (def.weaponNum == Level::kItemToken) {
                report.Check(made == nullptr, "the token is not made outside Flag Tag");
                last = nullptr;
                continue;
            }
            last = RowForItem(set, def.weaponNum);
            if (last == nullptr) {
                continue;
            }
            const bool expected = last->allowPickup && last->item != Level::kItemUnarmed;
            report.Check((made != nullptr) == expected, "weapon record %zu (item %d) %s made in set %d", index,
                         def.weaponNum, expected ? "is" : "is not", set);
            if (made != nullptr) {
                madeWeapons++;
                report.Check(made->weaponNum == last->item && made->model == last->prop,
                             "weapon record %zu is item %d with model %d, the set's %d and %d", index, made->weaponNum,
                             made->model, last->item, last->prop);
                const Level::PropModel* model = objects.Model(last->prop);
                report.Check(model != nullptr && std::fabs(made->scale - (model->scale * last->size)) < 1e-4f,
                             "weapon record %zu is drawn at %.1f times its model's size", index, last->size);
            }
        } else if (def.Is(Level::PropDefType::Ammo) && last != nullptr) {
            const bool expected = last->ammoAmount > 0;
            report.Check((made != nullptr) == expected, "the crate %zu after item %d %s made", index, last->item,
                         expected ? "is" : "is not");
            if (last->ammoType > 0) {
                const int quantity = def.ammoSlots[static_cast<size_t>(last->ammoType - 1)].quantity;
                report.Check(quantity == last->ammoAmount, "the crate %zu holds %d of ammo type %d, not %d", index,
                             last->ammoAmount, last->ammoType, quantity);
            }
        }
    }
    bool anyWeapon = false;
    for (const Level::MpWeaponSetEntry& row : Level::MpWeaponSetData(set)) {
        anyWeapon = anyWeapon || (row.allowPickup && row.item != Level::kItemUnarmed);
    }
    report.Check(!anyWeapon || madeWeapons > 0, "the stage has weapons from set %d", set);
}

struct Home {
    Position at;
    int tile;
};

std::vector<Home> HomesOf(MultiplayerSession& game) {
    std::vector<Home> homes;
    for (int player = 0; player < game.Players(); player++) {
        homes.push_back({ game.PositionOf(player), game.GetStage().PlayerAt(player).move.Tile() });
    }
    return homes;
}

// Puts the player at `at` on `tile` as a new life does, on foot with the eyes
// at their height above the floor there. (The body keeps its own idea of the
// floor under it, so moving only the position puts it back where it was.)
void MoveTo(MultiplayerSession& game, int player, const Position& at, int tile) {
    constexpr float kStanHeightFilterFactor = 0.170000016689f;
    Level::Stage& stage = game.GetStage();
    const Level::Stan& stan = stage.Bg().StanFile();
    Level::PlayerState& state = stage.PlayerAt(player);
    Level::PlayerSpawn spawn = state.spawn;
    spawn.stanTile = tile;
    spawn.stanHeight = stan.PositionY(tile, at[0], at[2]);
    spawn.stanHeightRemainder = spawn.stanHeight;
    spawn.stanHeightFilter = spawn.stanHeight / kStanHeightFilterFactor;
    spawn.position = { at[0], spawn.stanHeight + spawn.eyeHeight, at[2] };
    state.move.BeginLife(spawn, stan);
}

void GoHome(MultiplayerSession& game, int player, const std::vector<Home>& homes) {
    const Home& home = homes[static_cast<size_t>(player)];
    MoveTo(game, player, home.at, home.tile);
}

void StandOn(MultiplayerSession& game, int player, const Level::PlacedObject& object) {
    MoveTo(game, player, { object.runtimePosition[0], object.runtimePosition[1], object.runtimePosition[2] },
           PropOf(game, object).stan);
}

// Whether every home is well outside the hold distance of the object.
bool FarFromHomes(const std::vector<Home>& homes, const Level::PlacedObject& object) {
    const Position at = { object.runtimePosition[0], object.runtimePosition[1], object.runtimePosition[2] };
    return std::all_of(homes.begin(), homes.end(),
                       [&](const Home& home) { return Distance(home.at, at) > kRespawnHoldDistance * 1.5f; });
}

std::vector<bool> HoldersOf(MultiplayerSession& game, int32_t item) {
    std::vector<bool> holds;
    for (int player = 0; player < game.Players(); player++) {
        holds.push_back(game.GetStage().PlayerAt(player).intro.inventory.HasItem(item));
    }
    return holds;
}

std::vector<int32_t> AmmoOf(MultiplayerSession& game, int32_t ammoType) {
    std::vector<int32_t> held;
    for (int player = 0; player < game.Players(); player++) {
        held.push_back(game.GetStage().PlayerAt(player).weapons.AmmoHeld(ammoType));
    }
    return held;
}

// A player who steps on a stocked weapon takes it, with its ammunition, and
// nobody else does; the weapon then respawns where it was.
void CheckWeaponPickup(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    const int set = game.Settings().weaponSet;
    Level::Stage& stage = game.GetStage();
    const std::vector<Home> homes = HomesOf(game);
    std::vector<const Level::PlacedObject*> weapons;
    for (const Level::PlacedObject* object : StockedObjects(game, Level::PropDefType::Collectable)) {
        if (object->weaponNum != Level::kItemUnarmed && FarFromHomes(homes, *object)) {
            weapons.push_back(object);
        }
    }
    report.Check(!weapons.empty(), "set %d has a weapon away from the start pads", set);
    struct Taken {
        const Level::PlacedObject* object;
        int tick;
    };
    std::vector<Taken> taken;
    for (int player = 0; player < game.Players(); player++) {
        // The first weapon of an item no one has taken yet.
        const Level::PlacedObject* weapon = nullptr;
        for (const Level::PlacedObject* candidate : weapons) {
            const bool again = std::any_of(taken.begin(), taken.end(),
                                           [&](const Taken& t) { return t.object->weaponNum == candidate->weaponNum; });
            if (!again) {
                weapon = candidate;
                break;
            }
        }
        if (weapon == nullptr) {
            break;
        }
        const int32_t item = weapon->weaponNum;
        const int32_t ammoType = stage.PlayerAt(player).weapons.Table().AmmoType(item);
        const std::vector<bool> heldBefore = HoldersOf(game, item);
        const std::vector<int32_t> ammoBefore = AmmoOf(game, ammoType);
        StandOn(game, player, *weapon);
        game.RunDrawingEveryone(3);
        const std::vector<bool> heldAfter = HoldersOf(game, item);
        const std::vector<int32_t> ammoAfter = AmmoOf(game, ammoType);
        for (int other = 0; other < game.Players(); other++) {
            const size_t index = static_cast<size_t>(other);
            if (other == player) {
                report.Check(heldAfter[index], "player %d took item %d", player + 1, item);
                report.Check(ammoType == 0 || ammoAfter[index] > ammoBefore[index], "and its ammunition (%d to %d)",
                             ammoBefore[index], ammoAfter[index]);
            } else {
                report.Check(heldAfter[index] == heldBefore[index] && ammoAfter[index] == ammoBefore[index],
                             "player %d keeps what they had when player %d takes item %d", other + 1, player + 1, item);
            }
        }
        const Level::Prop& prop = PropOf(game, *weapon);
        report.Check(prop.timeToRegen > kRespawnTicks - 20 && prop.timeToRegen <= kRespawnTicks,
                     "the weapon waits to respawn (%d ticks)", prop.timeToRegen);
        report.Check((prop.flags & Level::PropFlag::Enabled) == 0, "and is off the floor");
        taken.push_back({ weapon, game.Ticks() });
        GoHome(game, player, homes);
        game.Run(2);
    }
    if (taken.empty()) {
        return;
    }

    // Someone standing on a weapon that is waiting to respawn takes nothing.
    const int other = game.Players() - 1;
    const int32_t item = taken[0].object->weaponNum;
    const std::vector<bool> heldBefore = HoldersOf(game, item);
    StandOn(game, other, *taken[0].object);
    game.RunDrawingEveryone(10);
    report.Check(HoldersOf(game, item) == heldBefore, "nothing is taken from a weapon that is respawning");
    GoHome(game, other, homes);

    // They all come back after 1200 ticks.
    std::vector<int> returned(taken.size(), -1);
    for (int tick = 0; tick < kRespawnTicks + 100; tick++) {
        game.Run(1);
        for (size_t i = 0; i < taken.size(); i++) {
            const Level::Prop& prop = PropOf(game, *taken[i].object);
            if (returned[i] < 0 && prop.timeToRegen == 0 && (prop.flags & Level::PropFlag::Enabled) != 0) {
                returned[i] = game.Ticks() - taken[i].tick;
            }
        }
    }
    for (size_t i = 0; i < taken.size(); i++) {
        report.Check(returned[i] >= kRespawnTicks - 15 && returned[i] <= kRespawnTicks + 15,
                     "weapon %zu respawns after %d ticks, not about %d", i + 1, returned[i], kRespawnTicks);
    }

    // And a player who does not have the weapon takes it again.
    const int again = other;
    stage.PlayerAt(again).intro.inventory.RemoveItem(item);
    StandOn(game, again, *taken[0].object);
    game.RunDrawingEveryone(3);
    report.Check(stage.PlayerAt(again).intro.inventory.HasItem(item), "player %d takes the respawned item %d",
                 again + 1, item);
}

// An ammunition crate gives its ammunition to the player who steps on it, and
// respawns.
void CheckCratePickup(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    Level::Stage& stage = game.GetStage();
    const std::vector<Home> homes = HomesOf(game);
    const Level::PlacedObject* crate = nullptr;
    int32_t ammoType = 0;
    int32_t amount = 0;
    for (const Level::PlacedObject* candidate : StockedObjects(game, Level::PropDefType::Ammo)) {
        for (size_t slot = 0; slot < candidate->ammoSlots.size() && crate == nullptr; slot++) {
            if (candidate->ammoSlots[slot].quantity > 0 && FarFromHomes(homes, *candidate)) {
                crate = candidate;
                ammoType = slot == 1 ? 1 : static_cast<int32_t>(slot) + 1;
                amount = candidate->ammoSlots[slot].quantity;
            }
        }
    }
    report.Check(crate != nullptr, "set %d has an ammunition crate", game.Settings().weaponSet);
    if (crate == nullptr) {
        return;
    }
    for (int player = 0; player < game.Players(); player++) {
        if (player > 0) {
            game.Run(kRespawnTicks + 100);
        }
        const std::vector<int32_t> before = AmmoOf(game, ammoType);
        StandOn(game, player, *crate);
        game.RunDrawingEveryone(3);
        const std::vector<int32_t> after = AmmoOf(game, ammoType);
        const size_t me = static_cast<size_t>(player);
        const int32_t room = stage.PlayerAt(player).weapons.Table().MaxAmmo(ammoType) - before[me];
        report.Check(after[me] == before[me] + std::min(amount, room),
                     "player %d takes %d of ammo type %d from the crate (%d to %d)", player + 1, amount, ammoType,
                     before[me], after[me]);
        for (int other = 0; other < game.Players(); other++) {
            const size_t index = static_cast<size_t>(other);
            report.Check(other == player || after[index] == before[index],
                         "player %d keeps their ammunition when player %d takes the crate", other + 1, player + 1);
        }
        report.Check(PropOf(game, *crate).timeToRegen > 0, "the crate waits to respawn");
        GoHome(game, player, homes);
    }
}

// The weapons only the crates give (the sets of thrown weapons and the throwing
// knives) are not lying about, a crate gives them, and they can be thrown.
void CheckThrownWeapons(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    const int set = game.Settings().weaponSet;
    const Level::MpWeaponSetEntry& thrown = Level::MpWeaponSetData(set)[7];
    Level::Stage& stage = game.GetStage();
    for (const Level::PlacedObject* weapon : StockedObjects(game, Level::PropDefType::Collectable)) {
        report.Check(weapon->weaponNum != thrown.item, "no item %d is lying about", thrown.item);
    }
    const std::vector<Home> homes = HomesOf(game);
    const Level::PlacedObject* crate = nullptr;
    const size_t slot = static_cast<size_t>(thrown.ammoType - 1);
    for (const Level::PlacedObject* candidate : StockedObjects(game, Level::PropDefType::Ammo)) {
        if (candidate->ammoSlots[slot].quantity == thrown.ammoAmount && FarFromHomes(homes, *candidate)) {
            crate = candidate;
        }
    }
    report.Check(crate != nullptr, "the stage has a crate of item %d", thrown.item);
    if (crate == nullptr) {
        return;
    }
    for (int player = 0; player < game.Players(); player++) {
        if (player > 0) {
            game.Run(kRespawnTicks + 100);
        }
        const std::vector<bool> heldBefore = HoldersOf(game, thrown.item);
        report.Check(!heldBefore[static_cast<size_t>(player)], "player %d has no item %d to begin with", player + 1,
                     thrown.item);
        StandOn(game, player, *crate);
        game.RunDrawingEveryone(3);
        Level::PlayerState& state = stage.PlayerAt(player);
        report.Check(state.intro.inventory.HasItem(thrown.item), "player %d gets item %d from the crate", player + 1,
                     thrown.item);
        report.Check(state.weapons.AmmoHeld(thrown.ammoType) == thrown.ammoAmount, "and %d of ammo type %d (not %d)",
                     thrown.ammoAmount, thrown.ammoType, state.weapons.AmmoHeld(thrown.ammoType));
        for (int other = 0; other < game.Players(); other++) {
            const size_t index = static_cast<size_t>(other);
            report.Check(other == player || HoldersOf(game, thrown.item)[index] == heldBefore[index],
                         "player %d gets nothing when player %d takes the crate", other + 1, player + 1);
        }
        GoHome(game, player, homes);
        game.Run(2);

        // Throw one.
        state.weapons.EquipWeapon(Level::HandRight, thrown.item);
        state.weapons.EquipWeapon(Level::HandLeft, Level::kItemUnarmed);
        game.Run(90);
        const auto total = [&] {
            return state.weapons.AmmoHeld(thrown.ammoType) + state.weapons.AmmoInMagazine(Level::HandRight);
        };
        const auto inFlight = [&] {
            int count = 0;
            for (const Level::PlacedObject& object : stage.PlacedObjects().All()) {
                count += (object.prop >= 0 && object.weaponNum == thrown.item) ? 1 : 0;
            }
            return count;
        };
        const int32_t before = total();
        const int flying = inFlight();
        Scripts scripts;
        scripts[static_cast<size_t>(player)].Press(0, 2, Level::Buttons::Z);
        game.RunDrawingEveryone(2, scripts);
        // The throw takes a while to leave the hand; the mine fuse is 180 ticks.
        int released = -1;
        for (int tick = 0; tick < 120 && released < 0; tick++) {
            game.RunDrawingEveryone(1);
            released = inFlight() > flying ? tick : -1;
        }
        report.Check(released >= 0, "and it is an object in the stage");
        report.Check(total() == before - 1, "player %d throws one of item %d (%d to %d)", player + 1, thrown.item,
                     before, total());
        state.weapons.EquipWeapon(Level::HandRight, Level::kItemUnarmed);
        game.Run(60);
    }
}

// Pulling the trigger spends the shooter's ammunition and nobody else's. Each
// player picks a pistol of the set up from the floor first.
void CheckFireAlone(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    const std::vector<Home> homes = HomesOf(game);
    const auto pistols = StockedObjects(game, Level::PropDefType::Collectable);
    report.Check(static_cast<int>(pistols.size()) >= game.Players(), "the stage has a pistol for each player");
    for (int player = 0; player < game.Players() && static_cast<size_t>(player) < pistols.size(); player++) {
        const Level::PlacedObject& pistol = *pistols[static_cast<size_t>(player)];
        StandOn(game, player, pistol);
        game.RunDrawingEveryone(3);
        GoHome(game, player, homes);
        Level::PlayerState& state = game.GetStage().PlayerAt(player);
        report.Check(state.intro.inventory.HasItem(pistol.weaponNum), "player %d picked up item %d", player + 1,
                     pistol.weaponNum);
        state.weapons.EquipWeapon(Level::HandRight, pistol.weaponNum);
    }
    game.Run(90);
    for (int shooter = 0; shooter < game.Players(); shooter++) {
        std::vector<int32_t> before;
        for (int player = 0; player < game.Players(); player++) {
            before.push_back(game.GetStage().PlayerAt(player).weapons.AmmoInMagazine(Level::HandRight));
        }
        Scripts scripts;
        scripts[static_cast<size_t>(shooter)].Press(0, 30, Level::Buttons::Z);
        game.Run(60, scripts);
        for (int player = 0; player < game.Players(); player++) {
            const int32_t now = game.GetStage().PlayerAt(player).weapons.AmmoInMagazine(Level::HandRight);
            const int32_t was = before[static_cast<size_t>(player)];
            if (player == shooter) {
                report.Check(now < was, "player %d spends ammunition firing (%d to %d)", player + 1, was, now);
            } else {
                report.Check(now == was, "player %d keeps their ammunition when player %d fires (%d to %d)", player + 1,
                             shooter + 1, was, now);
            }
        }
    }
}

// proplvreset2 skips an object flagged "not in multiplayer" or "not on N
// players" for the game's N; the stage loads exactly the others.
void CheckSkipMask(Report& report, MultiplayerSession& game) {
    constexpr uint32_t kNotInMultiplayer = 0x00000008;
    constexpr uint32_t kNotOnPlayers[] = { 0, 0, 0x00400000, 0x00800000, 0x01000000 };
    const uint32_t skipped = kNotInMultiplayer | kNotOnPlayers[game.Players()];
    std::vector<size_t> expected;
    const auto& defs = game.GetStage().Setup()->PropDefs();
    for (size_t index = 0; index < defs.size(); index++) {
        if (Level::IsObjectRecord(defs[index].type) && (defs[index].flags2 & skipped) == 0) {
            expected.push_back(index);
        }
    }
    report.Check(game.GetStage().ObjectDefs() == expected,
                 "the stage loads the %zu objects not flagged out at %d players", expected.size(), game.Players());
}

// The flag of Flag Tag and the golden gun of The Man With The Golden Gun are
// placed by their scenarios and do not respawn; every other pickup does.
void CheckScenarioPickupsStay(Report& report, MultiplayerSession& game, int32_t item) {
    int stays = 0;
    for (const Level::PlacedObject* object : StockedObjects(game, Level::PropDefType::Collectable)) {
        const bool respawns = (object->state & Level::ObjectState::Respawn) != 0;
        if (object->weaponNum == item) {
            stays++;
            report.Check(!respawns, "item %d does not respawn in this game", item);
        } else {
            report.Check(respawns, "item %d respawns", object->weaponNum);
        }
    }
    report.Check(stays > 0, "the stage has item %d", item);
}

void CheckFlagStays(Report& report, MultiplayerSession& game) {
    CheckScenarioPickupsStay(report, game, Level::kItemToken);
}

void CheckGoldenGunStays(Report& report, MultiplayerSession& game) {
    CheckScenarioPickupsStay(report, game, Level::kItemGoldenGun);
}

// -- The end of a game: limits, scores, ranks, awards and the way out -----------------------

// A limit far enough off to see the minute warning come before the end.
constexpr int kLimitTicks = 3700;
// The ticks before the end the "One minute left" message shows.
constexpr int kMinuteTicks = 0xE10;

// Presses `buttons` on the player's pad for a tick and lets go.
void Tap(MultiplayerSession& game, int player, uint16_t buttons) {
    Scripts press;
    press[static_cast<size_t>(player)].Press(0, 1, buttons);
    game.RunDrawingTheDead(1, press);
    game.RunDrawingTheDead(2);
}

const Level::MpMenuState& MenuOf(MultiplayerSession& game, int player) {
    return game.GetStage().MenuOf(player);
}

bool Respawn(MultiplayerSession& game, int player) {
    return WaitForDeathScreen(game, player) && PressToRespawn(game, player);
}

// Whether the player is dead and still dying (the blood, the fall and the
// fade to black not yet over).
bool Dying(MultiplayerSession& game, int player) {
    const Level::PlayerState& state = PlayerOf(game, player);
    return state.health.IsDead() &&
           (!state.health.RedBloodFinished() || !state.health.DeathAnimFinished() || !state.colour.IsComplete());
}

// Ticks (drawing the dead) until the game is over. False if it never is.
bool WaitForGameOver(MultiplayerSession& game, int limit = kDeathLimit) {
    for (int waited = 0; waited < limit; waited++) {
        if (game.GetStage().GameOver()) {
            return true;
        }
        game.RunDrawingTheDead(1);
    }
    return game.GetStage().GameOver();
}

// The state a game that has just ended is in: every player on the scores page
// with the controls locked and the music stopped.
void CheckGameEnded(Report& report, MultiplayerSession& game) {
    Level::Stage& stage = game.GetStage();
    report.Check(stage.GameOver(), "the game is over");
    report.Check(stage.ControlsLocked() && stage.Paused(), "the controls are locked and the game paused");
    report.Check(stage.MissionState() == Game::MissionState::Stopped, "the music stops");
    for (int player = 0; player < game.Players(); player++) {
        const Level::MpMenuState& menu = MenuOf(game, player);
        report.Check(menu.on && menu.mode == Level::MpMenuMode::Scores, "player %d is on the scores page", player + 1);
        report.Check(
            menu.firstAward >= -1 && menu.firstAward < Level::kAwardCount && menu.secondAward >= -1 &&
                menu.secondAward < Level::kAwardCount && (menu.firstAward != menu.secondAward || menu.firstAward == -1),
            "player %d's awards are two different ones (%d, %d)", player + 1, menu.firstAward, menu.secondAward);
        report.Check(menu.firstAward >= 0 || menu.secondAward < 0, "player %d has a first award before a second",
                     player + 1);
    }
}

// The time limit: the minute warning shows to every player a minute before the
// end, and the game ends on the tick the clock reaches the limit.
void CheckTimeLimit(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int limit = stage.MultiplayerTicks() + kLimitTicks;
    const int warning = limit - kMinuteTicks;
    stage.SetTimeLimit(limit);

    while (stage.MultiplayerTicks() < warning - 1) {
        game.Run(1);
    }
    for (int player = 0; player < game.Players(); player++) {
        report.Check(PlayerOf(game, player).hud.BottomMessage() != "One minute left",
                     "player %d has no warning a tick before the minute is up", player + 1);
    }
    game.Run(1);
    for (int player = 0; player < game.Players(); player++) {
        const Level::Hud& hud = PlayerOf(game, player).hud;
        report.Check(hud.BottomMessage() == "One minute left", "player %d is told 'One minute left'", player + 1);
    }

    while (stage.MultiplayerTicks() < limit - 1) {
        game.Run(1);
    }
    report.Check(!stage.GameOver() && !stage.ControlsLocked(), "the game is on a tick before the limit");
    game.Run(1);
    report.Check(stage.MultiplayerTicks() == limit, "the clock reads the limit, not %d", stage.MultiplayerTicks());
    CheckGameEnded(report, game);
    report.Check(stage.GameOverFlag() == 1, "the scores show at once");

    // The game is frozen: no time passes, nobody moves and nobody is hurt.
    const Position before = game.PositionOf(0);
    Kill(game, 1, 0);
    report.Check(!IsDead(game, 1) && game.HealthOf(1) > 0.99f, "a hit after the end does no damage");
    Scripts walk;
    walk[0].Walk(0, 30);
    game.Run(30, walk);
    report.Check(stage.MultiplayerTicks() == limit, "no time passes once the game is over");
    report.Check(Distance(before, game.PositionOf(0)) < kStill, "the players stand still once the game is over");
}

// The point limit: reaching it stops the pads, the game waits for the dying to
// finish, and ends on the first tick none is.
void CheckPointLimit(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    stage.SetPointLimit(players - 1);

    for (int victim = 1; victim < players; victim++) {
        report.Check(!stage.StopPlay() && !stage.GameOver(), "the game goes on at %d of %d points", victim - 1,
                     players - 1);
        Kill(game, victim, 0);
        game.RunDrawingTheDead(1);
        report.Check(stage.PointsFor(0) == victim, "player 1 has %d points, not %d", victim, stage.PointsFor(0));
    }
    report.Check(stage.StopPlay(), "the game stops at the limit");
    report.Check(!stage.GameOver(), "and waits for the dying to finish");

    const Position before = game.PositionOf(0);
    Scripts walk;
    walk[0].Walk(0, 20);
    game.RunDrawingTheDead(20, walk);
    report.Check(Distance(before, game.PositionOf(0)) < kStill, "the pad moves nobody once the game has stopped");
    stage.DamagePlayer(0, 1, kLethal, 0.0f, 1.0f);
    report.Check(!IsDead(game, 0), "a hit after the game has stopped does no damage");

    report.Check(WaitForGameOver(game), "the game ends once the dying have finished");
    for (int player = 0; player < players; player++) {
        report.Check(!Dying(game, player), "nobody is still dying when the game ends");
    }
    CheckGameEnded(report, game);
    report.Check(stage.PointsFor(0) == players - 1, "player 1 won with %d points", stage.PointsFor(0));
    report.Check(stage.RankOf(0) == 0, "player 1 is first");
    for (int player = 1; player < players; player++) {
        report.Check(stage.PointsFor(player) == 0, "player %d has no points", player + 1);
        report.Check(stage.RankOf(player) == 1, "players who tie on nothing share second place");
    }
    const std::string first = stage.RankText(0);
    report.Check(first.find("1st") != std::string::npos, "player 1's rank reads '%s'", first.c_str());
    report.Check(stage.RankText(1).find("2nd") != std::string::npos, "the others read second");
}

// Each player scores one less than the one before: the ranks follow the points,
// and every place from first to the count is used.
void CheckRankOrder(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    for (int attacker = 0; attacker < players - 1; attacker++) {
        for (int victim = attacker + 1; victim < players; victim++) {
            Kill(game, victim, attacker);
        }
        for (int player = 0; player < players; player++) {
            if (IsDead(game, player)) {
                report.Check(Respawn(game, player), "player %d respawns", player + 1);
            }
        }
        LetHitsSettle(game);
    }
    static const char* const kPlaces[4] = { "1st", "2nd", "3rd", "4th" };
    for (int player = 0; player < players; player++) {
        report.Check(stage.PointsFor(player) == players - 1 - player, "player %d has %d points, not %d", player + 1,
                     players - 1 - player, stage.PointsFor(player));
        report.Check(stage.RankOf(player) == player, "player %d is placed %d, not %d", player + 1, player + 1,
                     stage.RankOf(player) + 1);
        report.Check(stage.RankText(player).find(kPlaces[player]) != std::string::npos, "player %d's rank reads '%s'",
                     player + 1, stage.RankText(player).c_str());
    }
}

// Players level on points share a place, and the next place is skipped.
void CheckTies(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();

    for (int player = 0; player < players; player++) {
        report.Check(stage.RankOf(player) == 0, "nobody has scored, so player %d is first", player + 1);
    }

    Kill(game, 1, 0);
    report.Check(Respawn(game, 1), "player 2 respawns");
    LetHitsSettle(game);
    Kill(game, 0, 1);
    report.Check(stage.PointsFor(0) == 1 && stage.PointsFor(1) == 1, "players 1 and 2 have a point each");
    report.Check(stage.RankOf(0) == 0 && stage.RankOf(1) == 0, "and share first place");
    for (int player = 2; player < players; player++) {
        report.Check(stage.RankOf(player) == 2, "player %d is third behind the pair", player + 1);
    }

    // Player 1 takes the lead.
    report.Check(Respawn(game, 0), "player 1 respawns");
    LetHitsSettle(game);
    Kill(game, 1, 0);
    report.Check(stage.RankOf(0) == 0 && stage.RankOf(1) == 1, "the lead is first and the other second");
    for (int player = 2; player < players; player++) {
        report.Check(stage.RankOf(player) == 2, "player %d is still third", player + 1);
    }

    // The rest tie below: the pair of them share third, with no fourth.
    if (players == 4) {
        report.Check(stage.RankOf(2) == stage.RankOf(3), "players 3 and 4 share a place");
        report.Check(stage.RankText(3).find("3rd") != std::string::npos, "and it is third, not fourth");
    }

    // A suicide takes a point off.
    report.Check(Respawn(game, 1), "player 2 respawns");
    LetHitsSettle(game);
    const int before = stage.PointsFor(1);
    stage.DamagePlayer(1, 1, kLethal, 0.0f, 1.0f);
    report.Check(stage.PointsFor(1) == before - 1, "a suicide costs a point");
}

// A team scenario: players on a team share a score, a kill of the other team
// scores for it, and the two tie when each has killed once.
void CheckTeamScores(Report& report, MultiplayerSession& game) {
    const Game::MultiplayerSettings& settings = game.Settings();
    if (!settings.IsTeamScenario()) {
        return;
    }
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    for (int player = 0; player < players; player++) {
        report.Check(StatsOf(game, player).haveTokenOrGoldenGun == settings.TeamFlag(player), "player %d is on team %d",
                     player + 1, settings.TeamFlag(player));
    }
    const int attacker = 0;
    int victim = -1;
    for (int player = 1; player < players && victim < 0; player++) {
        if (settings.TeamFlag(player) != settings.TeamFlag(attacker)) {
            victim = player;
        }
    }
    report.Check(victim > 0, "there is a player on the other team");
    if (victim < 0) {
        return;
    }
    const auto sameTeam = [&](int a, int b) { return settings.TeamFlag(a) == settings.TeamFlag(b); };

    Kill(game, victim, attacker);
    for (int player = 0; player < players; player++) {
        const int expected = sameTeam(player, attacker) ? 1 : 0;
        report.Check(stage.PointsFor(player) == expected, "player %d's team has %d points, not %d", player + 1,
                     expected, stage.PointsFor(player));
        report.Check(stage.RankOf(player) == (sameTeam(player, attacker) ? 0 : 1), "player %d's team is placed %d",
                     player + 1, sameTeam(player, attacker) ? 1 : 2);
    }

    // The other team kills back: the teams tie and every player is first.
    report.Check(Respawn(game, victim), "player %d respawns", victim + 1);
    LetHitsSettle(game);
    Kill(game, attacker, victim);
    for (int player = 0; player < players; player++) {
        report.Check(stage.PointsFor(player) == 1, "each team has a point");
        report.Check(stage.RankOf(player) == 0, "so every player is first");
    }

    // A teammate's kill costs the team a point.
    int mate = -1;
    for (int player = 1; player < players; player++) {
        if (player != victim && sameTeam(player, attacker)) {
            mate = player;
        }
    }
    report.Check(mate >= 0, "the attacker has a teammate");
    if (mate >= 0) {
        report.Check(Respawn(game, attacker), "player 1 respawns");
        LetHitsSettle(game);
        Kill(game, mate, attacker);
        report.Check(stage.PointsFor(attacker) == 0, "killing a teammate costs the team a point (%d)",
                     stage.PointsFor(attacker));
        report.Check(stage.PointsFor(mate) == stage.PointsFor(attacker), "and the team shares it");
        report.Check(Respawn(game, mate), "the teammate respawns");
        LetHitsSettle(game);
    }

    // A team that reaches the point limit ends the game.
    stage.SetPointLimit(2);
    Kill(game, victim, attacker);
    game.RunDrawingTheDead(1);
    report.Check(!stage.StopPlay() && stage.PointsFor(attacker) == 1, "one point short of the limit the game goes on");
    report.Check(Respawn(game, victim), "player %d respawns", victim + 1);
    LetHitsSettle(game);
    Kill(game, victim, attacker);
    game.RunDrawingTheDead(1);
    report.Check(stage.StopPlay(), "a team that reaches the limit stops the game");
    report.Check(WaitForGameOver(game), "and the game ends");
    for (int player = 0; player < players; player++) {
        report.Check(stage.RankOf(player) == (sameTeam(player, attacker) ? 0 : 1), "the winning team is first");
    }
}

// The three team scenarios, where the player count allows.
void ConfigureTwoVsTwo(MultiplayerSettings& settings) {
    if (settings.playerCount == 4) {
        settings.scenario = Game::MpScenario::TwoVsTwo;
        settings.teamSize = 2;
    }
}

void ConfigureThreeVsOne(MultiplayerSettings& settings) {
    if (settings.playerCount == 4) {
        settings.scenario = Game::MpScenario::ThreeVsOne;
        settings.teamSize = 2;
    }
}

void ConfigureTwoVsOne(MultiplayerSettings& settings) {
    if (settings.playerCount == 3) {
        settings.scenario = Game::MpScenario::TwoVsOne;
        settings.teamSize = 1;
    }
}

// MISSION_STATE_6 while a player is dead, and back to the theme once every dead
// player's blood has finished.
void CheckMissionState(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    report.Check(stage.MissionState() != Game::MissionState::Dead, "the music is not the death music at the start");

    Kill(game, 1, 0);
    game.RunDrawingTheDead(1);
    report.Check(stage.MissionState() == Game::MissionState::Dead, "a death starts the death music");

    // A second player dies a little later, so the first one's blood finishes
    // first.
    const int second = players >= 3 ? 2 : -1;
    bool heldForTheSecond = second < 0;
    bool killedSecond = false;
    int ticks = 0;
    const auto bloodDone = [&](int player) { return PlayerOf(game, player).health.RedBloodFinished(); };
    while (!(bloodDone(1) && (second < 0 || bloodDone(second))) && ticks < kDeathLimit) {
        if (second >= 0 && !killedSecond && ticks == 8) {
            Kill(game, second, 0);
            killedSecond = true;
        }
        if (killedSecond && bloodDone(1) && !bloodDone(second)) {
            report.Check(stage.MissionState() == Game::MissionState::Dead,
                         "the death music plays on while another dead player's blood runs");
            heldForTheSecond = true;
        }
        game.RunDrawingTheDead(1);
        ticks++;
    }
    report.Check(heldForTheSecond, "one dead player's blood finished before the other's");
    game.RunDrawingTheDead(1);
    report.Check(stage.MissionState() == Game::MissionState::Theme,
                 "the theme returns once every dead player's blood has finished");
}

// longest_inning and most_killed_one_life keep up with the life being played.
void CheckInnings(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    game.Run(100);
    for (int player = 0; player < players; player++) {
        report.Check(StatsOf(game, player).longestInning >= 100, "player %d's longest life so far is %d", player + 1,
                     StatsOf(game, player).longestInning);
        report.Check(StatsOf(game, player).longestInning <= stage.MissionTimer(),
                     "no life is longer than the game (%d)", StatsOf(game, player).longestInning);
    }
    for (int victim = 1; victim < players; victim++) {
        Kill(game, victim, 0);
    }
    game.RunDrawingTheDead(1);
    report.Check(StatsOf(game, 0).mostKilledOneLife == players - 1, "player 1 killed %d in one life, not %d",
                 players - 1, StatsOf(game, 0).mostKilledOneLife);
    const int longest = StatsOf(game, 0).longestInning;
    game.RunDrawingTheDead(60);
    report.Check(StatsOf(game, 0).longestInning >= longest + 50, "the longest life keeps growing while it is lived");
}

// Start opens a player's scores, which hide that player's messages, and closes
// when they die.
void CheckMenuOpens(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int actor = game.Players() - 1;
    Tap(game, actor, Level::Buttons::Start);
    for (int player = 0; player < game.Players(); player++) {
        report.Check(MenuOf(game, player).on == (player == actor), "only player %d has a menu open", actor + 1);
    }
    report.Check(MenuOf(game, actor).mode == Level::MpMenuMode::Scores, "it opens on the scores");
    Kill(game, actor, 0);
    game.RunDrawingTheDead(1);
    report.Check(!MenuOf(game, actor).on, "a player who dies loses their menu");
    report.Check(!game.GetStage().GameOver(), "and the game goes on");
}

// The pages: right and left move through them, the pause page pauses everyone's
// clock for the player who paused, and Exit asks again before it ends the game.
void CheckQuitConfirm(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    using Level::MpMenuMode;
    Level::Stage& stage = game.GetStage();
    const int actor = game.Players() - 1;
    const auto mode = [&] { return MenuOf(game, actor).mode; };

    Tap(game, actor, Level::Buttons::Start);
    Tap(game, actor, Level::Buttons::LeftPad);
    report.Check(mode() == MpMenuMode::Kills, "left goes to the kills");
    Tap(game, actor, Level::Buttons::LeftPad);
    report.Check(mode() == MpMenuMode::Losses, "and the losses");
    Tap(game, actor, Level::Buttons::LeftPad);
    report.Check(mode() == MpMenuMode::Losses, "which is the end of the line while the game goes on");
    for (int step = 0; step < 2; step++) {
        Tap(game, actor, Level::Buttons::RightPad);
    }
    report.Check(mode() == MpMenuMode::Scores, "right comes back to the scores");
    Tap(game, actor, Level::Buttons::RightPad);
    report.Check(mode() == MpMenuMode::Pause, "and on to the pause");

    // Pausing stops the clock for everyone; the same player resumes it.
    int clock = stage.MultiplayerTicks();
    Tap(game, actor, Level::Buttons::A);
    report.Check(stage.Paused() && stage.ControlsLocked(), "A on the pause page pauses the game");
    clock = stage.MultiplayerTicks();
    game.RunDrawingTheDead(20);
    report.Check(stage.MultiplayerTicks() == clock, "the clock stops while it is paused");
    Tap(game, actor, Level::Buttons::A);
    report.Check(!stage.Paused() && !stage.ControlsLocked(), "A again resumes it");
    game.RunDrawingTheDead(5);
    report.Check(stage.MultiplayerTicks() > clock, "and the clock runs");

    // Exit asks for a second yes, and a no goes back to the game.
    Tap(game, actor, Level::Buttons::RightPad);
    report.Check(mode() == MpMenuMode::Exit, "right of the pause is the exit");
    Tap(game, actor, Level::Buttons::A);
    report.Check(mode() == MpMenuMode::ExitConfirm && !MenuOf(game, actor).quitConfirm, "A asks to confirm, on no");
    Tap(game, actor, Level::Buttons::RightPad);
    report.Check(MenuOf(game, actor).quitConfirm, "right chooses yes");
    Tap(game, actor, Level::Buttons::LeftPad);
    report.Check(!MenuOf(game, actor).quitConfirm, "left chooses no");
    Tap(game, actor, Level::Buttons::A);
    report.Check(!MenuOf(game, actor).on, "A on no closes the menu");
    report.Check(!stage.GameOver(), "and the game goes on");
    report.Check(PlayerOf(game, actor).hud.Health().displayTime == 60, "the gauges show for a second");

    // The same again with yes ends the game for everyone.
    Tap(game, actor, Level::Buttons::Start);
    Tap(game, actor, Level::Buttons::RightPad);
    Tap(game, actor, Level::Buttons::RightPad);
    Tap(game, actor, Level::Buttons::A);
    Tap(game, actor, Level::Buttons::RightPad);
    report.Check(mode() == MpMenuMode::ExitConfirm && MenuOf(game, actor).quitConfirm, "the exit is confirmed");
    Tap(game, actor, Level::Buttons::A);
    CheckGameEnded(report, game);
    report.Check(stage.GameOverFlag() == 1, "and the scores show at once");

    // The scores can be paged left to the weapon of choice, but not right.
    for (int player = 0; player < game.Players(); player++) {
        Tap(game, player, Level::Buttons::RightPad);
        report.Check(MenuOf(game, player).mode == MpMenuMode::Scores, "the scores are the last page on the right");
    }
    Tap(game, 0, Level::Buttons::LeftPad);
    Tap(game, 0, Level::Buttons::LeftPad);
    Tap(game, 0, Level::Buttons::LeftPad);
    report.Check(MenuOf(game, 0).mode == MpMenuMode::GoWoc, "the game over pages reach the weapon of choice");
    Tap(game, 0, Level::Buttons::RightPad);
    Tap(game, 0, Level::Buttons::RightPad);
    Tap(game, 0, Level::Buttons::RightPad);
    report.Check(MenuOf(game, 0).mode == MpMenuMode::Scores, "and back");

    // Every player dismisses the scores; B takes one back; the last one out ends the stage.
    for (int player = 0; player < game.Players(); player++) {
        report.Check(!game.Done(), "the stage runs until the last player has finished (%d finished)", player);
        Tap(game, player, Level::Buttons::A);
        report.Check(MenuOf(game, player).mode == MpMenuMode::Finished, "player %d has finished", player + 1);
        if (player == 0) {
            Tap(game, 0, Level::Buttons::B);
            report.Check(MenuOf(game, 0).mode == MpMenuMode::Scores, "B takes the scores back");
            Tap(game, 0, Level::Buttons::Start);
            report.Check(MenuOf(game, 0).mode == MpMenuMode::Finished, "and Start finishes again");
        }
    }
    report.Check(game.Done(), "the stage ends once every player has finished");
}

// A game's end stores each player's weapon of choice.
void CheckFavouriteWeapons(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    game.Run(120);
    Level::Stage& stage = game.GetStage();
    for (int player = 0; player < game.Players(); player++) {
        report.Check(game.Favourites().Weapon(player, 0) == 0, "player %d has no favourite before the end", player + 1);
    }
    stage.CalculateAwards(false);
    for (int player = 0; player < game.Players(); player++) {
        const std::array<int32_t, 2> choice = PlayerOf(game, player).weapons.WeaponOfChoice();
        report.Check(game.Favourites().Weapon(player, 0) == choice[0] &&
                         game.Favourites().Weapon(player, 1) == choice[1],
                     "player %d's favourite weapon is stored", player + 1);
        report.Check(choice[0] != 0, "player %d held a weapon", player + 1);
    }
}

// The awards a game leaves are the ones the rules choose from what the stage
// counted: the same numbers drawn from the same state give the same awards.
void CheckAwards(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    game.Run(60);
    // Player 1 kills everyone else and then themselves, all at once: a streak
    // as long as the game is big.
    for (int victim = 1; victim < players; victim++) {
        Kill(game, victim, 0);
    }
    stage.DamagePlayer(0, 0, kLethal, 0.0f, 1.0f);
    game.RunDrawingTheDead(30);
    report.Check(StatsOf(game, 0).mostKilledOneTime == players, "player 1's streak is %d, not %d", players,
                 StatsOf(game, 0).mostKilledOneTime);

    std::array<Level::AwardInput, Level::kMpPlayers> inputs{};
    std::array<std::array<int32_t, Level::kMpPlayers>, Level::kMpPlayers> kills{};
    for (int player = 0; player < players; player++) {
        const Level::PlayerStats& stats = StatsOf(game, player);
        Level::AwardInput& input = inputs[static_cast<size_t>(player)];
        input.shotsTotal = stats.shotCount[0];
        input.shotsHead = stats.shotCount[1];
        input.damageToBackside = stats.damageToBackside;
        input.timeOtherPlayersOnScreen = stats.timeOtherPlayersOnScreen;
        input.distanceTraveled = stats.distanceTraveled;
        input.bodyArmourPickups = stats.bodyArmorPickups;
        input.longestInning = stats.longestInning;
        input.shortestInning = stats.shortestInning;
        input.mostKilledOneTime = stats.mostKilledOneTime;
        kills[static_cast<size_t>(player)] = stats.killCounts;
    }

    RandomSetSeed(0x4242);
    std::array<Level::AwardMetrics, Level::kMpPlayers> metrics =
        Level::BuildMetrics(players, inputs, kills, stage.MissionTimer());
    Level::ChooseEligibleAwards(players, metrics, inputs, RandomGetNext);
    const std::array<Level::AwardPick, Level::kMpPlayers> expected = Level::PickAwards(players, metrics, RandomGetNext);

    RandomSetSeed(0x4242);
    stage.CalculateAwards(false);
    for (int player = 0; player < players; player++) {
        report.Check(MenuOf(game, player).firstAward == expected[static_cast<size_t>(player)].first &&
                         MenuOf(game, player).secondAward == expected[static_cast<size_t>(player)].second,
                     "player %d's awards are %d and %d, not %d and %d", player + 1, MenuOf(game, player).firstAward,
                     MenuOf(game, player).secondAward, expected[static_cast<size_t>(player)].first,
                     expected[static_cast<size_t>(player)].second);
    }
    CheckGameEnded(report, game);
    if (players == 4) {
        report.Check(MenuOf(game, 0).firstAward == 16, "a streak of four is always the first award");
        report.Check(stage.AwardText(0, 0).find("Quad") != std::string_view::npos, "and it is named, '%s'",
                     std::string(stage.AwardText(0, 0)).c_str());
    }
}

// The end of the game from both ends: the delayed scores of mpCalculateAwards
// count down before the pages open.
void CheckGameOverDelay(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    stage.CalculateAwards(true);
    report.Check(stage.GameOverFlag() == 300, "the delay starts at 300");
    for (int player = 0; player < game.Players(); player++) {
        Tap(game, player, Level::Buttons::A);
        report.Check(MenuOf(game, player).mode == Level::MpMenuMode::Scores, "buttons do nothing during the delay");
    }
    game.Run(300);
    report.Check(stage.GameOverFlag() == 1, "the delay runs down to the scores");
    Tap(game, 0, Level::Buttons::A);
    report.Check(MenuOf(game, 0).mode == Level::MpMenuMode::Finished, "and then the pages answer");
}

// -- The multiplayer menu's display ---------------------------------------------------------

// The text of an LMPMENU slot, as the stage loaded it.
std::string MenuString(MultiplayerSession& game, int32_t slot) {
    const Level::Lang& lang = game.GetStage().GetLang();
    return std::string(lang.Get(Level::StringId(Level::TextBank::MpMenu, slot)));
}

// What player's menu drew the last time their view was drawn, which this draws.
const Level::MpWatchPlan& DrawnMenu(MultiplayerSession& game, int player) {
    game.RenderPlayer(player);
    return game.GetStage().MenuDrawnFor(player);
}

const Level::MpMenuText* LineOf(const Level::MpWatchPlan& plan, const std::string& text) {
    for (const Level::MpMenuText& line : plan.texts) {
        if (line.text == text) {
            return &line;
        }
    }
    return nullptr;
}

// The width of `text` in the menu's font, which centres a line.
int TextWidth(MultiplayerSession& game, int player, const std::string& text) {
    return Game::Text::Measure(*game.GetStage().PlayerAt(player).hud.GothicFont(), text).width;
}

// Every line starts inside the player's own view and none of another player's
// view has a line of this player's menu.
void CheckInsideView(Report& report, MultiplayerSession& game, int player, const Level::MpWatchPlan& plan) {
    const Level::ViewMetrics& view = game.GetStage().PlayerAt(player).camera.Metrics();
    for (const Level::MpMenuText& line : plan.texts) {
        report.Check(line.x >= view.viewLeft && line.x < view.viewLeft + view.viewWidth && line.y >= view.viewTop &&
                         line.y < view.viewTop + view.viewHeight,
                     "player %d's line '%s' at %d,%d is inside their view", player + 1, line.text.c_str(), line.x,
                     line.y);
    }
}

// The fills of one frame that dim a view: a black fill at 160 of 255, with the
// rectangle it covers.
std::vector<ViewRect> DimmedViews(const std::vector<Gfx>& list) {
    std::vector<ViewRect> dimmed;
    bool primed = false;
    for (const Gfx& command : list) {
        const uint32_t op = command.words.w0 >> 24;
        if (op == static_cast<uint8_t>(G_SETPRIMCOLOR)) {
            primed = command.words.w1 == 0x000000A0u;
        } else if (op == static_cast<uint8_t>(G_FILLRECT)) {
            if (primed) {
                const int32_t right = static_cast<int32_t>((command.words.w0 >> 12) & 0xFFF) >> 2;
                const int32_t bottom = static_cast<int32_t>(command.words.w0 & 0xFFF) >> 2;
                const int32_t left = static_cast<int32_t>((command.words.w1 >> 12) & 0xFFF) >> 2;
                const int32_t top = static_cast<int32_t>(command.words.w1 & 0xFFF) >> 2;
                dimmed.push_back({ left, top, right - left, bottom - top });
            }
            primed = false;
        }
    }
    return dimmed;
}

// A game in which every player has killed and been killed, so the pages have
// numbers that tell the players and their rows apart.
bool PlayWithKills(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return false;
    }
    const int players = game.Players();
    for (int victim = 0; victim < players; victim++) {
        Kill(game, victim, (victim + 1) % players);
        if (!Respawn(game, victim)) {
            report.Check(false, "player %d respawns", victim + 1);
            return false;
        }
    }
    Kill(game, 1, 0);
    return Respawn(game, 1);
}

// The pages of a player's menu, left to right, drawn in the player's own view at
// each count: the title, the chevrons, the rank and the numbers of the scores,
// kills and losses pages, the pause page and its two states, and the exit and
// its confirm; with the view dimmed behind it and nobody else's view touched.
void CheckMenuDisplay(Report& report, MultiplayerSession& game) {
    if (!PlayWithKills(report, game)) {
        return;
    }
    using Level::MpMenuMode;
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();

    for (int player = 0; player < players; player++) {
        const Level::MpWatchPlan& plan = DrawnMenu(game, player);
        report.Check(plan.texts.empty() && !plan.menuOpen && !plan.continuePrompt,
                     "player %d's closed menu draws nothing", player + 1);
    }
    const std::vector<Gfx> before = game.RenderFrame();
    report.Check(DimmedViews(before).empty(), "no view is dimmed while no menu is open");

    for (int actor = 0; actor < players; actor++) {
        const Level::ViewMetrics view = stage.PlayerAt(actor).camera.Metrics();
        const int32_t column = players == 2 ? 160 : 80;
        Tap(game, actor, Level::Buttons::Start);

        // The scores page.
        const Level::MpWatchPlan& scores = DrawnMenu(game, actor);
        report.Check(scores.menuOpen && !scores.continuePrompt, "player %d's menu draws", actor + 1);
        CheckInsideView(report, game, actor, scores);
        const Level::MpMenuText* title = LineOf(scores, MenuString(game, Level::MpMenuString::kPlay));
        report.Check(title != nullptr && title->text == "PLAY", "the title is PLAY");
        if (title != nullptr) {
            report.Check(title->x == view.viewLeft + column - (TextWidth(game, actor, "PLAY") >> 1),
                         "player %d's title is centred in their view", actor + 1);
        }
        const std::string rank = stage.RankText(actor);
        report.Check(LineOf(scores, rank) != nullptr, "player %d's rank '%s' is drawn", actor + 1, rank.c_str());
        report.Check(LineOf(scores, "SCORES") != nullptr, "SCORES is drawn");
        report.Check(LineOf(scores, "<") != nullptr && LineOf(scores, ">") != nullptr,
                     "both chevrons are drawn while the game goes on");
        for (int slot = 0; slot < players; slot++) {
            const std::string points = std::to_string(stage.PointsFor(slot));
            const int32_t x = players == 2 ? view.viewLeft + 160 : view.viewLeft + ((slot & 1) != 0 ? 96 : 64);
            const int32_t y = players == 2 ? view.viewTop + (slot == 0 ? 70 : 86) : view.viewTop + (slot < 2 ? 70 : 86);
            bool found = false;
            for (const Level::MpMenuText& line : scores.texts) {
                if (line.text == points && line.y == y && line.x == x - (TextWidth(game, actor, points) >> 1) &&
                    line.outlined == (slot == actor)) {
                    found = true;
                }
            }
            report.Check(found, "player %d's scores page has %s for player %d where it belongs", actor + 1,
                         points.c_str(), slot + 1);
        }

        // The view is dimmed, and only the actor's.
        const std::vector<ViewRect> dimmed = DimmedViews(game.RenderFrame());
        const ViewRect want = ExpectedView(players, actor);
        report.Check(dimmed.size() == 1 && dimmed[0].left == want.left && dimmed[0].top == want.top &&
                         dimmed[0].width == want.width && dimmed[0].height == want.height,
                     "player %d's view, and no other, is dimmed (%zu fills)", actor + 1, dimmed.size());
        for (int other = 0; other < players; other++) {
            if (other != actor) {
                report.Check(!DrawnMenu(game, other).menuOpen, "player %d's view has no menu", other + 1);
            }
        }

        // Left: kills, then losses.
        for (const bool losses : { false, true }) {
            Tap(game, actor, Level::Buttons::LeftPad);
            const Level::MpWatchPlan& page = DrawnMenu(game, actor);
            CheckInsideView(report, game, actor, page);
            const std::string heading = "P" + std::to_string(actor + 1) + (losses ? " LOSSES" : " KILLS");
            const Level::MpMenuText* line = LineOf(page, heading);
            report.Check(line != nullptr, "player %d's %s page is headed '%s'", actor + 1, losses ? "losses" : "kills",
                         heading.c_str());
            if (line != nullptr && losses) {
                report.Check(line->colour == Level::kMpMenuRed, "the losses heading is red");
            }
            for (int slot = 0; slot < players; slot++) {
                const int killer = losses ? slot : actor;
                const int victim = losses ? actor : slot;
                const int32_t count = StatsOf(game, killer).killCounts[static_cast<size_t>(victim)];
                const std::string text = std::to_string(count);
                const int32_t x = players == 2 ? view.viewLeft + 160 : view.viewLeft + ((slot & 1) != 0 ? 96 : 64);
                const int32_t y =
                    players == 2 ? view.viewTop + (slot == 0 ? 70 : 86) : view.viewTop + (slot < 2 ? 70 : 86);
                bool found = false;
                for (const Level::MpMenuText& candidate : page.texts) {
                    found = found || (candidate.text == text && candidate.y == y &&
                                      candidate.x == x - (TextWidth(game, actor, text) >> 1));
                }
                const bool drawn = slot != actor || (losses && count > 0);
                report.Check(found == drawn, "player %d's %s page %s the number of player %d", actor + 1,
                             losses ? "losses" : "kills", drawn ? "has" : "leaves out", slot + 1);
            }
        }
        report.Check(MenuOf(game, actor).mode == MpMenuMode::Losses, "the pages ran left to the losses");
        Tap(game, actor, Level::Buttons::LeftPad);
        report.Check(!DrawnMenu(game, actor).texts.empty() && LineOf(DrawnMenu(game, actor), "<") == nullptr,
                     "the losses are the last page to the left, so no chevron points there");

        // Right, to the pause page and its states.
        for (int step = 0; step < 3; step++) {
            Tap(game, actor, Level::Buttons::RightPad);
        }
        report.Check(MenuOf(game, actor).mode == MpMenuMode::Pause, "three to the right is the pause");
        const Level::MpMenuText* pause = LineOf(DrawnMenu(game, actor), "PAUSE");
        report.Check(pause != nullptr && !pause->outlined, "the pause page reads PAUSE");
        report.Check(LineOf(DrawnMenu(game, actor), "SCORES") != nullptr, "and shows the scores");
        Tap(game, actor, Level::Buttons::A);
        const Level::MpMenuText* paused = LineOf(DrawnMenu(game, actor), "PAUSED");
        report.Check(paused != nullptr && paused->outlined, "the player who paused reads PAUSED, highlighted");
        for (int other = 0; other < players; other++) {
            if (other != actor) {
                // The other players' menus are closed: the pause shows only on this page.
                report.Check(!DrawnMenu(game, other).menuOpen, "the pause opens no other player's menu");
            }
        }
        Tap(game, actor, Level::Buttons::A);
        report.Check(LineOf(DrawnMenu(game, actor), "PAUSE") != nullptr, "and resuming reads PAUSE");

        // The exit and its confirm.
        Tap(game, actor, Level::Buttons::RightPad);
        const Level::MpWatchPlan& exit = DrawnMenu(game, actor);
        report.Check(LineOf(exit, "EXIT") != nullptr && LineOf(exit, "<") != nullptr && LineOf(exit, ">") == nullptr,
                     "the exit page has a chevron to the left only");
        Tap(game, actor, Level::Buttons::A);
        const Level::MpWatchPlan& confirm = DrawnMenu(game, actor);
        const Level::MpMenuText* cancel = LineOf(confirm, "cancel");
        const Level::MpMenuText* yes = LineOf(confirm, "confirm");
        report.Check(cancel != nullptr && yes != nullptr && cancel->outlined && !yes->outlined,
                     "the confirm page offers both, cancel chosen");
        if (cancel != nullptr && yes != nullptr) {
            const int32_t offset = players == 2 ? 80 : 0;
            report.Check(cancel->x == view.viewLeft + offset + 54 - (TextWidth(game, actor, "cancel") >> 1) &&
                             yes->x == view.viewLeft + offset + 104 - (TextWidth(game, actor, "confirm") >> 1),
                         "cancel and confirm are where the layout puts them");
        }
        Tap(game, actor, Level::Buttons::RightPad);
        const Level::MpWatchPlan& chosen = DrawnMenu(game, actor);
        report.Check(LineOf(chosen, "confirm") != nullptr && LineOf(chosen, "confirm")->outlined &&
                         !LineOf(chosen, "cancel")->outlined,
                     "right chooses confirm");
        CheckInsideView(report, game, actor, chosen);

        // B closes the menu and the view is its own again.
        Tap(game, actor, Level::Buttons::LeftPad);
        Tap(game, actor, Level::Buttons::B);
        report.Check(!MenuOf(game, actor).on && !DrawnMenu(game, actor).menuOpen &&
                         DimmedViews(game.RenderFrame()).empty(),
                     "B closes the menu and takes the dimming with it");
    }
}

// The way out of a game, drawn: every player's scores read GAME OVER and
// START TO EXIT in turn with the chevrons further out, the pages to the left
// reach each player's weapon of choice and their two awards, and the dimming
// covers every view.
void CheckMenuGameOver(Report& report, MultiplayerSession& game) {
    if (!PlayWithKills(report, game)) {
        return;
    }
    using Level::MpMenuMode;
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    game.Run(120);
    stage.CalculateAwards(false);
    report.Check(stage.GameOverFlag() == 1, "the game is over");

    const std::vector<ViewRect> dimmed = DimmedViews(game.RenderFrame());
    report.Check(static_cast<int>(dimmed.size()) == players, "every view is dimmed, not %zu", dimmed.size());

    // The message flips between GAME OVER and START TO EXIT every 120 ticks.
    game.Run(100);
    for (int player = 0; player < players; player++) {
        const Level::MpWatchPlan& flipped = DrawnMenu(game, player);
        report.Check(LineOf(flipped, "START TO EXIT") != nullptr && LineOf(flipped, "GAME OVER") == nullptr,
                     "player %d's title flips to START TO EXIT", player + 1);
    }
    game.Run(120);
    report.Check(LineOf(DrawnMenu(game, 0), "GAME OVER") != nullptr, "and back");

    for (int player = 0; player < players; player++) {
        const Level::ViewMetrics view = stage.PlayerAt(player).camera.Metrics();
        const int32_t offset = players == 2 ? 80 : 0;
        const Level::MpWatchPlan& scores = DrawnMenu(game, player);
        report.Check(LineOf(scores, "GAME OVER") != nullptr, "player %d's scores say GAME OVER", player + 1);
        report.Check(LineOf(scores, "PLAY") == nullptr, "and not PLAY");
        report.Check(LineOf(scores, "SCORES") != nullptr && LineOf(scores, ">") == nullptr,
                     "the scores are the last page to the right");
        const Level::MpMenuText* left = LineOf(scores, "<");
        report.Check(left != nullptr && left->x == view.viewLeft + offset + 40 - 10 - 8,
                     "the left chevron sits further out at the game over");
        CheckInsideView(report, game, player, scores);

        // Left to the weapon of choice and the awards.
        for (int step = 0; step < 3; step++) {
            Tap(game, player, Level::Buttons::LeftPad);
        }
        report.Check(MenuOf(game, player).mode == MpMenuMode::GoWoc, "three to the left is the weapon of choice");
        const Level::MpWatchPlan& awards = DrawnMenu(game, player);
        CheckInsideView(report, game, player, awards);
        const Level::MpMenuText* heading = LineOf(awards, "Weapon of choice:");
        report.Check(heading != nullptr, "the weapon of choice is headed");
        const int32_t nudge = players >= 3 ? ((player & 1) != 0 ? -7 : 7) : 0;
        if (heading != nullptr) {
            report.Check(heading->x ==
                             view.viewLeft + offset + nudge + 80 - (TextWidth(game, player, "Weapon of choice:") >> 1),
                         "player %d's heading is %d from the middle of their column", player + 1, nudge);
        }
        const int32_t item = game.Favourites().Weapon(player, 0);
        const std::string weapon = std::string(
            stage.GetLang().Get(static_cast<int32_t>(stage.PlayerAt(player).weapons.Table().Watch(item).longName)));
        report.Check(!weapon.empty() && LineOf(awards, weapon) != nullptr, "the weapon of choice, '%s', is drawn",
                     weapon.c_str());
        for (int which = 0; which < 2; which++) {
            const std::string award(stage.AwardText(player, which));
            if (!award.empty()) {
                const Level::MpMenuText* line = LineOf(awards, award);
                report.Check(line != nullptr, "award %d, '%s', is drawn", which + 1, award.c_str());
            }
        }
        report.Check(LineOf(awards, ">") != nullptr && LineOf(awards, "<") == nullptr,
                     "the weapon page has a chevron to the right only");
        for (int step = 0; step < 3; step++) {
            Tap(game, player, Level::Buttons::RightPad);
        }
    }
    // A player who has finished sees a blank page.
    Tap(game, 0, Level::Buttons::A);
    const Level::MpWatchPlan& done = DrawnMenu(game, 0);
    report.Check(MenuOf(game, 0).mode == MpMenuMode::Finished && LineOf(done, "<") == nullptr &&
                     LineOf(done, ">") == nullptr && LineOf(done, "SCORES") == nullptr,
                 "a finished player's page is blank");
}

// A dead player whose death has played out is asked to press start, in the
// middle of their own view; no one else is.
void CheckMenuPrompt(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    for (int victim = 0; victim < players; victim++) {
        Kill(game, victim, (victim + 1) % players);
        report.Check(!DrawnMenu(game, victim).continuePrompt, "player %d is not asked while they are dying",
                     victim + 1);
        report.Check(WaitForDeathScreen(game, victim), "player %d's death plays out", victim + 1);
        const Level::MpWatchPlan& plan = DrawnMenu(game, victim);
        const Level::ViewMetrics view = stage.PlayerAt(victim).camera.Metrics();
        report.Check(plan.continuePrompt && plan.texts.size() == 1, "player %d is asked to continue", victim + 1);
        if (plan.texts.size() == 1) {
            const std::string prompt = MenuString(game, Level::MpMenuString::kPressStart);
            const Game::Text::Extent extent = Game::Text::Measure(*stage.PlayerAt(victim).hud.GothicFont(), prompt);
            report.Check(plan.texts[0].text == prompt &&
                             plan.texts[0].x == view.viewLeft + (view.viewWidth >> 1) - (extent.width >> 1) &&
                             plan.texts[0].y == view.viewTop + (view.viewHeight >> 1) - (extent.height >> 1),
                         "player %d's prompt is in the middle of their view", victim + 1);
        }
        for (int other = 0; other < players; other++) {
            if (other != victim) {
                report.Check(!DrawnMenu(game, other).continuePrompt, "player %d is not asked", other + 1);
            }
        }
        report.Check(PressToRespawn(game, victim), "A brings player %d back", victim + 1);
        report.Check(!DrawnMenu(game, victim).continuePrompt, "and the prompt is gone");
    }
}

// An open menu stops the player's guns firing and flashing; closed, the same
// trigger fires with a flash.
void CheckMenuGuns(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int actor = game.Players() - 1;
    Level::PlayerState& state = stage.PlayerAt(actor);
    state.intro.inventory.AddItem(Level::kItemPp7);
    state.weapons.GiveAmmo(state.weapons.Table().AmmoType(Level::kItemPp7), 100);
    state.weapons.EquipWeapon(Level::HandLeft, Level::kItemUnarmed);
    state.weapons.EquipWeapon(Level::HandRight, Level::kItemPp7);
    game.Run(90);
    report.Check(state.weapons.WeaponId(Level::HandRight) == Level::kItemPp7 &&
                     state.weapons.AmmoInMagazine(Level::HandRight) > 0,
                 "the player holds a loaded pistol");
    const auto holdTrigger = [&](int ticks) {
        bool flashed = false;
        for (int tick = 0; tick < ticks; tick++) {
            Scripts shoot;
            shoot[static_cast<size_t>(actor)].Press(0, 1, Level::Buttons::Z);
            game.Run(1, shoot);
            flashed = flashed || state.weapons.Hands(Level::HandRight).flashVisible;
        }
        return flashed;
    };

    const int32_t ammo = state.weapons.AmmoInMagazine(Level::HandRight);
    Tap(game, actor, Level::Buttons::Start);
    report.Check(MenuOf(game, actor).on, "the menu is open");
    report.Check(!holdTrigger(30), "the trigger shows no flash with the menu open");
    report.Check(state.weapons.AmmoInMagazine(Level::HandRight) == ammo, "and fires nothing");

    Tap(game, actor, Level::Buttons::B);
    game.Run(60);
    report.Check(!MenuOf(game, actor).on, "B closes the menu");
    report.Check(holdTrigger(30), "the trigger flashes with it closed");
    report.Check(state.weapons.AmmoInMagazine(Level::HandRight) < ammo, "and fires");
}

// -- Scenarios ------------------------------------------------------------------------------

// The collectable weapons of `item` lying free in the stage: the one the setup
// placed, or one a death dropped.
std::vector<const Level::PlacedObject*> LooseItems(MultiplayerSession& game, int32_t item) {
    const Level::Objects& objects = game.GetStage().PlacedObjects();
    std::vector<const Level::PlacedObject*> found;
    for (const int id : objects.Props().Active()) {
        const Level::Prop& prop = objects.Props()[id];
        const Level::PlacedObject* object = objects.ForProp(id);
        if (object != nullptr && object->type == static_cast<uint8_t>(Level::PropDefType::Collectable) &&
            object->weaponNum == item && prop.parent < 0 && (prop.flags & Level::PropFlag::Enabled) != 0) {
            found.push_back(object);
        }
    }
    return found;
}

// Where an object lies and the tile under it.
struct Spot {
    Position at;
    int tile;
};

Spot SpotOf(MultiplayerSession& game, const Level::PlacedObject& object) {
    return { { object.runtimePosition[0], object.runtimePosition[1], object.runtimePosition[2] },
             PropOf(game, object).stan };
}

void StandAt(MultiplayerSession& game, int player, const Spot& spot) {
    MoveTo(game, player, spot.at, spot.tile);
}

int HoldFlagOf(MultiplayerSession& game, int player) {
    return StatsOf(game, player).haveTokenOrGoldenGun;
}

// Every player but `holder` (-1 for none) holds nothing.
void CheckOnlyHolder(Report& report, MultiplayerSession& game, int holder, int32_t item, const char* name) {
    for (int player = 0; player < game.Players(); player++) {
        const bool holds = player == holder;
        report.Check(PlayerOf(game, player).intro.inventory.HasItem(item) == holds, "%s: player %d %s it", name,
                     player + 1, holds ? "holds" : "does not hold");
        report.Check(HoldFlagOf(game, player) == (holds ? 1 : 0), "%s: player %d's holder flag is %d", name, player + 1,
                     holds ? 1 : 0);
    }
}

// The player holding the flag or the golden gun leaves body armour on the
// floor, and one who does not takes it.
void CheckArmourRule(Report& report, MultiplayerSession& game, int holder, int other) {
    const std::vector<const Level::PlacedObject*> armours = StockedObjects(game, Level::PropDefType::Armour);
    report.Check(!armours.empty(), "the stage has body armour");
    if (armours.empty()) {
        return;
    }
    const Spot spot = SpotOf(game, *armours[0]);
    const float amount = armours[0]->armourAmount;
    const std::vector<Home> homes = HomesOf(game);

    PlayerOf(game, holder).health.SetArmour(0.0f);
    PlayerOf(game, other).health.SetArmour(0.0f);
    StandAt(game, holder, spot);
    game.RunDrawingEveryone(3);
    report.Check(PlayerOf(game, holder).health.Armour() == 0.0f, "the holder leaves the body armour (%.2f)",
                 PlayerOf(game, holder).health.Armour());
    report.Check((PropOf(game, *armours[0]).flags & Level::PropFlag::Enabled) != 0, "and it is still on the floor");
    GoHome(game, holder, homes);
    game.Run(2);

    StandAt(game, other, spot);
    game.RunDrawingEveryone(3);
    report.Check(std::fabs(PlayerOf(game, other).health.Armour() - amount) < 1e-4f,
                 "a player without the item takes it (%.2f of %.2f)", PlayerOf(game, other).health.Armour(), amount);
    GoHome(game, other, homes);
    game.Run(2);
}

void ConfigureFlagTag(MultiplayerSettings& settings) {
    settings.scenario = Game::MpScenario::TheLivingDaylights;
}

// Flag Tag: the flag is taken from the floor, held in the right hand, counted
// for its carrier, kept from cycling away, dropped where its carrier dies and
// taken up again by someone else; it does not come back by itself.
void CheckFlagTag(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    const int carrier = 0;
    const int other = 1;
    const std::vector<Home> homes = HomesOf(game);

    const auto flags = LooseItems(game, Level::kItemToken);
    report.Check(flags.size() == 1, "the stage has one flag (%zu)", flags.size());
    if (flags.size() != 1) {
        return;
    }
    const Spot flagSpot = SpotOf(game, *flags[0]);
    for (int player = 0; player < players; player++) {
        report.Check(HoldFlagOf(game, player) == 0 && StatsOf(game, player).flagCounter == 0,
                     "player %d starts without the flag or any time with it", player + 1);
    }

    StandAt(game, carrier, flagSpot);
    game.RunDrawingEveryone(3);
    report.Check(LooseItems(game, Level::kItemToken).empty(), "the flag leaves the floor when taken");
    GoHome(game, carrier, homes);
    game.Run(60);
    CheckOnlyHolder(report, game, carrier, Level::kItemToken, "after the pickup");
    report.Check(PlayerOf(game, carrier).weapons.Hands(Level::HandRight).weaponNum == Level::kItemToken,
                 "the carrier holds the flag in their right hand");

    // The flag counts the ticks it is carried, and does not respawn.
    const int before = StatsOf(game, carrier).flagCounter;
    game.Run(kRespawnTicks + 100);
    report.Check(StatsOf(game, carrier).flagCounter - before == kRespawnTicks + 100,
                 "the flag counts every tick it is carried (%d)", StatsOf(game, carrier).flagCounter - before);
    for (int player = 0; player < players; player++) {
        if (player != carrier) {
            report.Check(StatsOf(game, player).flagCounter == 0, "player %d has no time with the flag", player + 1);
        }
    }
    report.Check(LooseItems(game, Level::kItemToken).empty(), "the flag does not come back by itself");

    // Cycling weapons does nothing for the carrier and does for anyone else.
    for (const int player : { carrier, other }) {
        PlayerOf(game, player).intro.inventory.AddItem(Level::kItemPp7);
        PlayerOf(game, player).intro.inventory.AddItem(Level::kItemShotgun);
    }
    const int32_t otherBefore = PlayerOf(game, other).weapons.Hands(Level::HandRight).weaponNum;
    Scripts cycle;
    cycle[carrier].Press(0, 3, Level::Buttons::A);
    cycle[other].Press(0, 3, Level::Buttons::A);
    game.Run(80, cycle);
    report.Check(PlayerOf(game, carrier).weapons.Hands(Level::HandRight).weaponNum == Level::kItemToken,
                 "the carrier cannot cycle away from the flag");
    report.Check(PlayerOf(game, other).weapons.Hands(Level::HandRight).weaponNum != otherBefore,
                 "while another player can");

    CheckArmourRule(report, game, carrier, other);

    // The carrier's death drops the flag and ends the count.
    Kill(game, carrier, other);
    report.Check(WaitForDeathScreen(game, carrier), "the carrier's death screen ends");
    const int counted = StatsOf(game, carrier).flagCounter;
    game.RunDrawingTheDead(60);
    report.Check(StatsOf(game, carrier).flagCounter == counted, "a dead carrier counts no more time");
    report.Check(HoldFlagOf(game, carrier) == 0, "and no longer holds it");
    const auto dropped = LooseItems(game, Level::kItemToken);
    report.Check(dropped.size() == 1, "the flag lies where the carrier died (%zu)", dropped.size());
    if (dropped.size() != 1) {
        return;
    }
    report.Check(FloorDistance(SpotOf(game, *dropped[0]).at, game.PositionOf(carrier)) < 300.0f,
                 "near the carrier's body");

    // Someone else takes it up and carries on the count; the dead carrier,
    // respawned, is not holding it.
    StandAt(game, other, SpotOf(game, *dropped[0]));
    game.RunDrawingEveryone(3);
    GoHome(game, other, homes);
    report.Check(PressToRespawn(game, carrier), "the carrier respawns");
    game.Run(60);
    CheckOnlyHolder(report, game, other, Level::kItemToken, "after the second pickup");
    report.Check(PlayerOf(game, other).weapons.Hands(Level::HandRight).weaponNum == Level::kItemToken,
                 "the new carrier holds the flag");
    report.Check(StatsOf(game, other).flagCounter > 0 && StatsOf(game, carrier).flagCounter == counted,
                 "the count carries on for the new carrier alone");
    report.Check(LooseItems(game, Level::kItemToken).empty(), "and the flag is off the floor again");
}

void ConfigureGoldenGunHold(MultiplayerSettings& settings) {
    settings.scenario = Game::MpScenario::TheManWithTheGoldenGun;
    settings.weaponSet = 13;
}

// The Man With The Golden Gun: the gun is taken from the floor, makes its
// holder the holder, does not respawn, is dropped where its holder is killed and
// counts for whoever kills them.
void CheckGoldenGunHold(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int holder = 0;
    const int other = 1;
    const std::vector<Home> homes = HomesOf(game);

    const auto guns = LooseItems(game, Level::kItemGoldenGun);
    report.Check(!guns.empty(), "the stage has a golden gun");
    if (guns.empty()) {
        return;
    }
    const size_t gunCount = guns.size();
    const Spot gunSpot = SpotOf(game, *guns[0]);
    for (int player = 0; player < game.Players(); player++) {
        report.Check(HoldFlagOf(game, player) == 0, "player %d starts as no holder", player + 1);
    }

    StandAt(game, holder, gunSpot);
    game.RunDrawingEveryone(3);
    GoHome(game, holder, homes);
    game.Run(5);
    CheckOnlyHolder(report, game, holder, Level::kItemGoldenGun, "after the pickup");
    report.Check(LooseItems(game, Level::kItemGoldenGun).size() == gunCount - 1, "the gun leaves the floor");
    game.Run(kRespawnTicks + 100);
    report.Check(LooseItems(game, Level::kItemGoldenGun).size() == gunCount - 1, "and does not come back");

    CheckArmourRule(report, game, holder, other);

    // Killing the holder is counted and the gun drops.
    Kill(game, holder, other);
    report.Check(StatsOf(game, other).killedGoldenGunOwner == 1, "killing the holder is counted");
    report.Check(WaitForDeathScreen(game, holder), "the holder's death screen ends");
    game.RunDrawingTheDead(60);
    const auto dropped = LooseItems(game, Level::kItemGoldenGun);
    report.Check(dropped.size() == gunCount, "the gun lies where the holder died (%zu of %zu)", dropped.size(),
                 gunCount);
    report.Check(PressToRespawn(game, holder), "the holder respawns");
    LetHitsSettle(game);
    CheckOnlyHolder(report, game, -1, Level::kItemGoldenGun, "after the respawn");
    if (dropped.empty()) {
        return;
    }

    // The killer takes the gun and is killed for it in turn.
    StandAt(game, other, SpotOf(game, *dropped.back()));
    game.RunDrawingEveryone(3);
    GoHome(game, other, homes);
    game.Run(5);
    CheckOnlyHolder(report, game, other, Level::kItemGoldenGun, "after the second pickup");
    Kill(game, other, holder);
    report.Check(StatsOf(game, holder).killedGoldenGunOwner == 1, "the first holder is credited for the second");
    report.Check(StatsOf(game, other).killedGoldenGunOwner == 1, "and the second keeps their own");
}

void ConfigureLicenceToKill(MultiplayerSettings& settings) {
    settings.scenario = Game::MpScenario::LicenceToKill;
}

void CheckLicenceToKill(Report& report, MultiplayerSession& game);

// You Only Live Twice: players are put out one at a time by the last one, each
// is placed by how many were alive, and the game ends when only one is left.
void CheckYoltRound(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    const int survivor = players - 1;
    report.Check(game.GetStage().YoltRoundOutcome() == Level::YoltOutcome::Continue, "the round starts");
    for (int player = 0; player < players; player++) {
        report.Check(StatsOf(game, player).orderOutInYolt == 0, "player %d is not out", player + 1);
    }
    for (int out = 0; out < players - 1; out++) {
        Kill(game, out, survivor);
        report.Check(WaitForDeathScreen(game, out) && PressToRespawn(game, out), "player %d respawns once", out + 1);
        LetHitsSettle(game);
        report.Check(StatsOf(game, out).orderOutInYolt == 0, "killed once, player %d is not out", out + 1);
        Kill(game, out, survivor);
        report.Check(WaitForDeathScreen(game, out), "player %d's second death screen ends", out + 1);
        report.Check(StatsOf(game, out).orderOutInYolt == players - out, "player %d is placed %d of %d (%d)", out + 1,
                     players - out, players, StatsOf(game, out).orderOutInYolt);
        // The tick after the last player finishes dying is the one that ends the game.
        game.RunDrawingTheDead(2);
        const bool last = out == players - 2;
        report.Check(game.GetStage().YoltRoundOutcome() ==
                         (last ? Level::YoltOutcome::End : Level::YoltOutcome::Continue),
                     "the round %s after player %d is out", last ? "ends" : "goes on", out + 1);
        report.Check(game.GetStage().GameOver() == last, "the game %s after player %d is out",
                     last ? "is over" : "is still on", out + 1);
        report.Check(game.GetStage().StopPlay() == last, "play %s after player %d is out",
                     last ? "has stopped" : "goes on", out + 1);
    }
    report.Check(StatsOf(game, survivor).orderOutInYolt == 0, "the survivor is not placed");
}

// The teams change nothing in play: each player is on the team the team select
// gave them, a teammate's hit hurts and kills like an opponent's and is
// credited the same, and a death and respawn leave the team as it was.
void CheckTeamPlay(Report& report, MultiplayerSession& game) {
    if (!game.Settings().IsTeamScenario()) {
        return;
    }
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    const MultiplayerSettings& settings = game.Settings();
    int onFirst = 0;
    for (int player = 0; player < players; player++) {
        report.Check(HoldFlagOf(game, player) == settings.TeamFlag(player), "player %d is on team %d", player + 1,
                     settings.TeamFlag(player));
        onFirst += settings.TeamFlag(player) == 0 ? 1 : 0;
    }
    report.Check(onFirst > 0 && onFirst < players, "both teams have players");

    for (int victim = 0; victim < players; victim++) {
        for (int attacker = 0; attacker < players; attacker++) {
            if (attacker == victim) {
                continue;
            }
            const bool teammates = settings.TeamFlag(victim) == settings.TeamFlag(attacker);
            const float health = game.HealthOf(victim);
            game.GetStage().DamagePlayer(victim, attacker, 0.05f, 0.0f, 1.0f);
            report.Check(game.HealthOf(victim) < health, "a hit from %s %d hurts player %d",
                         teammates ? "teammate" : "opponent", attacker + 1, victim + 1);
            LetHitsSettle(game);
        }
        const int attacker = (victim + 1) % players;
        const int kills = StatsOf(game, attacker).killCount;
        const int deaths = PlayerOf(game, victim).deathCount;
        Kill(game, victim, attacker);
        report.Check(IsDead(game, victim) && StatsOf(game, attacker).killCount == kills + 1 &&
                         PlayerOf(game, victim).deathCount == deaths + 1,
                     "player %d's kill of player %d (%s) is credited", attacker + 1, victim + 1,
                     settings.TeamFlag(victim) == settings.TeamFlag(attacker) ? "a teammate" : "an opponent");
        report.Check(WaitForDeathScreen(game, victim) && PressToRespawn(game, victim), "player %d respawns",
                     victim + 1);
        LetHitsSettle(game);
        for (int player = 0; player < players; player++) {
            report.Check(HoldFlagOf(game, player) == settings.TeamFlag(player), "player %d stays on team %d",
                         player + 1, settings.TeamFlag(player));
        }
    }
}

// The commands of each player's pass of a frame, split where each pass sets its
// viewport.
struct HudPass {
    int player = 0;
    ViewRect view{};
    std::vector<Gfx> commands;
    std::vector<TestSupport::DrawnRect> rects;
};

std::vector<HudPass> RenderHudPasses(MultiplayerSession& game) {
    const std::vector<Gfx> list = game.RenderFrame();
    std::vector<size_t> starts;
    for (size_t at = 0; at < list.size(); at++) {
        if ((list[at].words.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX_G_MOVEMEM) &&
            ((list[at].words.w0 >> 16) & 0xFF) == static_cast<uint32_t>(Fast::F3DEX_G_MV_VIEWPORT)) {
            starts.push_back(at);
        }
    }
    std::vector<HudPass> passes;
    for (size_t pass = 0; pass < starts.size(); pass++) {
        const size_t end = pass + 1 < starts.size() ? starts[pass + 1] : list.size();
        HudPass out;
        out.player = game.GetStage().NthPlayerShuffled(static_cast<int>(pass));
        out.view = ExpectedView(game.Players(), out.player);
        out.commands.assign(list.begin() + static_cast<std::ptrdiff_t>(starts[pass]),
                            list.begin() + static_cast<std::ptrdiff_t>(end));
        out.rects = TestSupport::ReadRects(out.commands.data(), out.commands.data() + out.commands.size());
        passes.push_back(std::move(out));
    }
    return passes;
}

bool CallsImage(const HudPass& pass, const char* path) {
    const uint64_t hash = CRC64(path);
    for (size_t at = 0; at + 1 < pass.commands.size(); at++) {
        if ((pass.commands[at].words.w0 >> 24) == 0x31 &&
            pass.commands[at + 1].words.w0 == static_cast<uint32_t>(hash >> 32) &&
            pass.commands[at + 1].words.w1 == static_cast<uint32_t>(hash)) {
            return true;
        }
    }
    return false;
}

// A fill rectangle of a colour with these edges, up to forty high (a single line
// of text measures no height).
bool HasBox(const HudPass& pass, int left, int bottom, uint32_t colour) {
    for (const TestSupport::DrawnRect& rect : pass.rects) {
        if (!rect.textured && rect.colour == colour && rect.left == left && rect.bottom == bottom &&
            rect.bottom - rect.top <= 40 && rect.right > rect.left) {
            return true;
        }
    }
    return false;
}

bool HasRect(const HudPass& pass, int left, int top, int right, int bottom, bool textured) {
    for (const TestSupport::DrawnRect& rect : pass.rects) {
        if (rect.textured == textured && rect.left == left && rect.top == top && rect.right == right &&
            rect.bottom == bottom) {
            return true;
        }
    }
    return false;
}

// A fill of a colour a few pixels square whose centre is within a pixel of (x, y).
bool HasDot(const HudPass& pass, int size, int x, int y, uint32_t colour) {
    for (const TestSupport::DrawnRect& rect : pass.rects) {
        if (!rect.textured && rect.colour == colour && rect.right - rect.left == size &&
            rect.bottom - rect.top == size && std::abs(((rect.left + rect.right) / 2) - x) <= 1 &&
            std::abs(((rect.top + rect.bottom) / 2) - y) <= 1) {
            return true;
        }
    }
    return false;
}

// Every player's HUD has something to draw: a pistol in the right hand with
// ammunition, a message, and the sight showing.
void PrepareHud(MultiplayerSession& game) {
    for (int player = 0; player < game.Players(); player++) {
        Level::PlayerState& state = game.GetStage().PlayerAt(player);
        state.intro.inventory.AddItem(Level::kItemPp7);
        state.weapons.GiveAmmo(state.weapons.Table().AmmoType(Level::kItemPp7), 50);
        state.weapons.EquipWeapon(Level::HandRight, Level::kItemPp7);
    }
    game.Run(60);
    for (int player = 0; player < game.Players(); player++) {
        Level::PlayerState& state = game.GetStage().PlayerAt(player);
        state.hud.BottomShow("Kill count: 1");
        state.hud.SetSightVisible(Level::SightReason::NotAiming, true);
    }
}

// Where display_red_blue_on_radar puts a dot, worked out again with the C
// library's trigonometry.
struct ExpectedBlip {
    int x;
    int y;
    bool inside;
};

ExpectedBlip BlipFor(const Level::Stage& stage, int player, int other, int circleX, int circleY) {
    const Level::PlayerState& self = stage.PlayerAt(player);
    const Level::Vec3 here = stage.PlacedObjects().Props()[self.move.PropId()].position;
    const Level::Vec3 there = stage.PlacedObjects().Props()[stage.PlayerAt(other).move.PropId()].position;
    const float dx = there[0] - here[0];
    const float dz = there[2] - here[2];
    const float degrees = (std::atan2(dx, dz) * 180.0f / 3.14159265f) + self.move.Theta() + 180.0f;
    float distance = std::sqrt((dx * dx) + (dz * dz)) * 16.0f / 4000.0f;
    const bool inside = distance < 16.0f;
    distance = std::min(distance, 16.0f);
    const float radians = degrees * 3.14159265f / 180.0f;
    return { circleX + static_cast<int>(std::sin(radians) * distance),
             circleY + static_cast<int>(std::cos(radians) * distance), inside };
}

bool TeamColours(Game::MpScenario scenario) {
    return scenario == Game::MpScenario::TwoVsTwo || scenario == Game::MpScenario::ThreeVsOne ||
           scenario == Game::MpScenario::TwoVsOne || scenario == Game::MpScenario::TheLivingDaylights ||
           scenario == Game::MpScenario::TheManWithTheGoldenGun;
}

uint32_t ExpectedBlipColour(bool teams, int item, bool inside) {
    if (teams) {
        if (item == 0) {
            return inside ? 0xFF0000A0 : 0xFF000060;
        }
        return inside ? 0x2828FFFF : 0x2828FFB0;
    }
    return inside ? 0xFFFF00A0 : 0xFFFF0060;
}

// The radar of one player's pass: the circle where the view's size puts it, a
// dot for every other living player, and none for the dead.
void CheckRadarOf(Report& report, MultiplayerSession& game, const HudPass& pass) {
    const Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    const int player = pass.player;
    const bool teams = TeamColours(game.Settings().scenario);
    int circleX = pass.view.left + pass.view.width - 41;
    if (players >= 3 && player % 2 == 0) {
        circleX += 15;
    }
    const int circleY = pass.view.top + 26;
    report.Check(HasRect(pass, circleX - 16, circleY - 16, circleX + 16, circleY + 16, true),
                 "player %d's radar circle is 32 square at %d,%d", player + 1, circleX, circleY);
    report.Check(circleX - 16 >= pass.view.left && circleX + 16 <= pass.view.left + pass.view.width,
                 "player %d's radar is inside their view", player + 1);
    const int selfItem = stage.Stats(player).haveTokenOrGoldenGun;
    const uint32_t selfColour = teams ? (selfItem == 0 ? 0xFF7777FF : 0x8888FFFF) : 0xFFFFFFA0;
    report.Check(HasDot(pass, 2, circleX, circleY, selfColour), "player %d's own dot is at the circle's centre",
                 player + 1);
    for (int other = 0; other < players; other++) {
        if (other == player) {
            continue;
        }
        const bool dead = stage.PlayerAt(other).health.IsDead();
        const ExpectedBlip blip = BlipFor(stage, player, other, circleX, circleY);
        const uint32_t colour = ExpectedBlipColour(teams, stage.Stats(other).haveTokenOrGoldenGun, blip.inside);
        const bool drawn = HasDot(pass, 2, blip.x, blip.y, colour) && HasDot(pass, 4, blip.x, blip.y, 0x40);
        report.Check(drawn == !dead, "player %d's radar %s a dot for player %d (%d,%d, %08x)", player + 1,
                     dead ? "has" : "lacks", other + 1, blip.x, blip.y, colour);
    }
}

// Where the bottom message's box starts and ends in a player's pass.
void MessageCorner(const MultiplayerSession& game, const Level::Stage& stage, const HudPass& pass, int* left,
                   int* bottom) {
    const int players = game.Players();
    const int player = pass.player;
    if (players < 3) {
        const Level::PlayerWeapons& weapons = stage.PlayerAt(player).weapons;
        const bool leftAmmo = weapons.Table().AmmoType(weapons.WeaponId(Level::HandLeft)) != 0;
        *left = pass.view.left + 30;
        *bottom = pass.view.top + pass.view.height - (leftAmmo ? 40 : 12) - (player == 1 ? 8 : 0);
    } else {
        *left = pass.view.left + (player % 2 == 1 ? 10 : 30);
        *bottom = pass.view.top + 16;
    }
}

// Each player's HUD in each pass: the message at the corner of the view, the
// right hand's ammo counter in its column, the sight inside the view and the
// radar.
void CheckHudLayout(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    PrepareHud(game);
    const std::vector<HudPass> passes = RenderHudPasses(game);
    report.Check(passes.size() == static_cast<size_t>(game.Players()), "the frame has a pass for each player");
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    for (const HudPass& pass : passes) {
        const int player = pass.player;
        const Level::PlayerWeapons& weapons = stage.PlayerAt(player).weapons;

        int left;
        int bottom;
        MessageCorner(game, stage, pass, &left, &bottom);
        report.Check(HasBox(pass, left, bottom, 0), "player %d's message box has its left at %d and bottom at %d",
                     player + 1, left, bottom);

        const int rightItem = weapons.WeaponId(Level::HandRight);
        report.Check(rightItem != Level::kItemUnarmed && weapons.Table().AmmoType(rightItem) != 0,
                     "player %d holds a weapon with ammunition", player + 1);
        const Level::AmmoColumns columns = Level::AmmoCounterColumns(players, player);
        const int iconX = pass.view.left + pass.view.width - columns.right;
        bool icon = false;
        for (const TestSupport::DrawnRect& rect : pass.rects) {
            if (!rect.textured && rect.colour == 0 && rect.bottom - rect.top == 14 && rect.right - rect.left <= 40 &&
                std::abs(((rect.left + rect.right) / 2) - iconX) <= 1) {
                icon = true;
            }
        }
        report.Check(icon, "player %d's right hand ammunition icon is %d in from the right of their view", player + 1,
                     columns.right);

        report.Check(CallsImage(pass, "hud/img/0d2c"), "player %d's sight is drawn", player + 1);
        bool sightInside = false;
        for (const TestSupport::DrawnRect& rect : pass.rects) {
            if (rect.textured && rect.right - rect.left == 32 && rect.bottom - rect.top == 32 &&
                (rect.left + rect.right) / 2 >= pass.view.left &&
                (rect.left + rect.right) / 2 < pass.view.left + pass.view.width &&
                (rect.top + rect.bottom) / 2 >= pass.view.top &&
                (rect.top + rect.bottom) / 2 < pass.view.top + pass.view.height) {
                sightInside = true;
            }
        }
        report.Check(sightInside, "player %d's crosshair is inside their view", player + 1);

        CheckRadarOf(report, game, pass);
    }
}

void ConfigureHud(MultiplayerSettings& settings) {
    settings.weaponSet = 1;
}

void ConfigureHudTeams2v2(MultiplayerSettings& settings) {
    ConfigureHud(settings);
    ConfigureTwoVsTwo(settings);
}

void ConfigureHudTeams3v1(MultiplayerSettings& settings) {
    ConfigureHud(settings);
    ConfigureThreeVsOne(settings);
}

void ConfigureHudTeams2v1(MultiplayerSettings& settings) {
    ConfigureHud(settings);
    ConfigureTwoVsOne(settings);
}

void ConfigureHudFlagTag(MultiplayerSettings& settings) {
    ConfigureHud(settings);
    settings.scenario = Game::MpScenario::TheLivingDaylights;
}

void ConfigureHudGoldenGun(MultiplayerSettings& settings) {
    ConfigureHud(settings);
    settings.scenario = Game::MpScenario::TheManWithTheGoldenGun;
}

// A dead player has no radar and is no dot on the others'; a player with the
// item or on the other team is coloured for it.
void CheckHudRadarDead(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    Kill(game, players - 1, 0);
    game.RunDrawingTheDead(2);
    report.Check(IsDead(game, players - 1), "the last player is dead");
    const std::vector<HudPass> passes = RenderHudPasses(game);
    for (const HudPass& pass : passes) {
        if (pass.player != players - 1) {
            CheckRadarOf(report, game, pass);
        } else {
            bool circle = false;
            for (const TestSupport::DrawnRect& rect : pass.rects) {
                circle = circle || (rect.textured && rect.right - rect.left == 32 && rect.bottom - rect.top == 32 &&
                                    rect.top == pass.view.top + 26 - 16);
            }
            report.Check(!circle, "the dead player has no radar circle");
        }
    }
}

// An open multiplayer menu shades the view and hides the message, the ammunition,
// the sight and the radar of that player alone.
void CheckHudMenuOpen(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    PrepareHud(game);
    const int menu = game.Players() - 1;
    game.GetStage().PlayerAt(menu).menu.on = true;
    const std::vector<HudPass> passes = RenderHudPasses(game);
    for (const HudPass& pass : passes) {
        const bool open = pass.player == menu;
        bool shade = false;
        for (const TestSupport::DrawnRect& rect : pass.rects) {
            shade = shade || (!rect.textured && rect.colour == 0x000000A0 && rect.left == pass.view.left &&
                              rect.top == pass.view.top && rect.right == pass.view.left + pass.view.width &&
                              rect.bottom == pass.view.top + pass.view.height);
        }
        report.Check(shade == open, "player %d's view %s shaded", pass.player + 1, open ? "is" : "is not");
        report.Check(CallsImage(pass, "hud/img/0d2c") == !open, "player %d's sight %s", pass.player + 1,
                     open ? "is hidden" : "shows");
        report.Check(CallsImage(pass, "hud/img/0c84") == !open, "player %d's ammunition icon %s", pass.player + 1,
                     open ? "is hidden" : "shows");
        int radarCircles = 0;
        for (const TestSupport::DrawnRect& rect : pass.rects) {
            if (rect.textured && rect.right - rect.left == 32 && rect.bottom - rect.top == 32 &&
                rect.top == pass.view.top + 26 - 16) {
                radarCircles++;
            }
        }
        report.Check(radarCircles == (open ? 0 : 1), "player %d has %d radar circles", pass.player + 1, radarCircles);
        int left;
        int bottom;
        MessageCorner(game, game.GetStage(), pass, &left, &bottom);
        report.Check(HasBox(pass, left, bottom, 0) == !open, "player %d's message %s", pass.player + 1,
                     open ? "is hidden" : "shows");
    }
}

// A message a player is given (a kill, a pickup) lasts 120 ticks in their own
// view, a new one replaces it, and no other player's HUD gets it.
void CheckHudMessages(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    game.GetStage().PlayerAt(0).hud.BottomShow("one");
    for (int player = 1; player < players; player++) {
        report.Check(game.GetStage().PlayerAt(player).hud.BottomMessage().empty(),
                     "player %d does not get player 1's message", player + 1);
    }
    game.GetStage().PlayerAt(0).hud.BottomShow("two");
    report.Check(game.GetStage().PlayerAt(0).hud.BottomMessage() == "two" &&
                     game.GetStage().PlayerAt(0).hud.BottomMessageCount() == 0,
                 "a second message replaces the first");
    // The player's kill, death and suicide texts come to the player's own strip.
    Kill(game, 1, 0);
    report.Check(!game.GetStage().PlayerAt(0).hud.BottomMessage().empty() &&
                     game.GetStage().PlayerAt(0).hud.BottomMessage() != "two",
                 "a kill shows its count on the killer's strip");
    report.Check(!game.GetStage().PlayerAt(1).hud.BottomMessage().empty(), "a death shows on the victim's strip");
    for (int player = 2; player < players; player++) {
        report.Check(game.GetStage().PlayerAt(player).hud.BottomMessage().empty(),
                     "player %d, who was not involved, sees nothing", player + 1);
    }
}

// ---- Cheats ----------------------------------------------------------------

namespace CheatId = Game::Menu::Cheat;

std::function<void(MultiplayerSettings&)> WithCheats(std::vector<int32_t> cheats) {
    return [cheats](MultiplayerSettings& settings) {
        settings.weaponSet = 1;
        settings.cheats = cheats;
    };
}

// The same game with no cheat on, to compare a cheat's effect against.
std::unique_ptr<MultiplayerSession> StartWithoutCheats(Report& report, MultiplayerSession& game) {
    MultiplayerSettings plain = game.Settings();
    plain.cheats.clear();
    RandomSetSeed(0x1234);
    auto control = MultiplayerSession::Start(gResources, plain);
    report.Check(control != nullptr, "the game without the cheats loads");
    return control;
}

// The Extra multiplayer characters cheat opens every row of the character select
// when the stage starts; a game without it leaves the roster as the save had it.
void CheckCheatExtraCharacters(Report& report, MultiplayerSession& game) {
    report.Check(game.Unlocks().NumCharsSelectable() == Game::kMpCharsAll, "every character is selectable (%d)",
                 game.Unlocks().NumCharsSelectable());
    auto control = StartWithoutCheats(report, game);
    if (control) {
        report.Check(control->Unlocks().NumCharsSelectable() == Game::kMpCharsDefault,
                     "without the cheat the roster is the default (%d)", control->Unlocks().NumCharsSelectable());
    }
}

bool Told(MultiplayerSession& game, int player) {
    return !PlayerOf(game, player).hud.BottomMessage().empty();
}

bool Marked(MultiplayerSession& game, int32_t cheat, int player) {
    return game.GetStage().GetCheats().IsActive(cheat, player);
}

float BodyScale(MultiplayerSession& game, int player) {
    const int slot = PlayerOf(game, player).bondChr;
    return slot < 0 ? 0.0f : game.GetStage().PlacedChrs()[slot].model->Playback().scale;
}

uint8_t BodyAlpha(MultiplayerSession& game, int player) {
    const int slot = PlayerOf(game, player).bondChr;
    return slot < 0 ? 0 : game.GetStage().PlacedChrs()[slot].fadeAlpha;
}

// Invincibility is a cheat of each player's own that every player gets: a hit
// that kills at full health does nothing to any of them.
void CheckCheatInvincible(Report& report, MultiplayerSession& game) {
    game.Run(1);
    for (int player = 0; player < game.Players(); player++) {
        report.Check(PlayerOf(game, player).cheatInvincible, "player %d is invincible", player + 1);
        report.Check(Marked(game, CheatId::kInvincibility, player), "player %d has it marked", player + 1);
        report.Check(Told(game, player), "player %d is told on their own strip", player + 1);
    }
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    for (int victim = 0; victim < players; victim++) {
        Kill(game, victim, (victim + 1) % players);
        game.RunDrawingTheDead(2);
        report.Check(!IsDead(game, victim), "player %d survives a lethal hit", victim + 1);
        report.Check(game.HealthOf(victim) > 0.99f, "and loses no health (%.2f)", game.HealthOf(victim));
        LetHitsSettle(game);
    }
}

// The health cheats set every player's own health and armour.
void CheckCheatHealth(Report& report, MultiplayerSession& game) {
    game.Run(1);
    for (int player = 0; player < game.Players(); player++) {
        const Level::PlayerHealth& health = PlayerOf(game, player).health;
        report.Check(health.ActualHealth() == 10.0f, "player %d has 10x health (%.1f)", player + 1,
                     health.ActualHealth());
        report.Check(health.ActualArmour() == 2.0f, "player %d has 2x armour (%.1f)", player + 1,
                     health.ActualArmour());
        report.Check(health.Health() == 1.0f && health.Armour() == 1.0f, "and full bars");
        report.Check(Told(game, player), "player %d is told", player + 1);
    }
}

// Maximum ammo fills every player's stock when it switches on, and infinite
// ammo keeps filling it in each player's own pass.
void CheckCheatAmmo(Report& report, MultiplayerSession& game) {
    game.Run(1);
    constexpr int kAmmoTypes = 14;
    for (int player = 0; player < game.Players(); player++) {
        const Level::PlayerWeapons& weapons = PlayerOf(game, player).weapons;
        for (int type = 1; type < kAmmoTypes; type++) {
            report.Check(weapons.AmmoHeld(type) == weapons.Table().MaxAmmo(type),
                         "player %d holds the most of ammunition type %d (%d of %d)", player + 1, type,
                         weapons.AmmoHeld(type), weapons.Table().MaxAmmo(type));
        }
    }
    auto control = StartWithoutCheats(report, game);
    for (int player = 0; player < game.Players(); player++) {
        PlayerOf(game, player).weapons.ClearAmmoHeld();
        if (control) {
            PlayerOf(*control, player).weapons.ClearAmmoHeld();
        }
    }
    game.RunDrawingEveryone(2);
    if (control) {
        control->RunDrawingEveryone(2);
    }
    for (int player = 0; player < game.Players(); player++) {
        const Level::PlayerWeapons& weapons = PlayerOf(game, player).weapons;
        for (int type = 1; type < kAmmoTypes; type++) {
            report.Check(weapons.AmmoHeld(type) == weapons.Table().MaxAmmo(type),
                         "infinite ammo refills player %d's type %d (%d)", player + 1, type, weapons.AmmoHeld(type));
            if (control) {
                report.Check(PlayerOf(*control, player).weapons.AmmoHeld(type) == 0,
                             "without it player %d's type %d stays empty", player + 1, type);
            }
        }
    }
}

// The cheats that belong to the game, not to a player, are on for everyone, are
// told once, and take effect in the whole game.
void CheckCheatGlobal(Report& report, MultiplayerSession& game) {
    game.Run(1);
    const int players = game.Players();
    for (const int32_t cheat :
         { CheatId::kDkMode, CheatId::kPaintball, CheatId::kLineMode, CheatId::kTurboMode, CheatId::kNoRadarMp }) {
        for (int player = 0; player < players; player++) {
            report.Check(Marked(game, cheat, player), "cheat %d is on for player %d", cheat, player + 1);
        }
    }
    report.Check(game.GetStage().LineMode(), "line mode is on");
    report.Check(game.GetStage().FastBond(), "turbo mode is on");
    report.Check(Told(game, 0), "the message shows to the player the stage was working on");
    for (int player = 1; player < players; player++) {
        report.Check(!Told(game, player), "and only to them, not to player %d", player + 1);
    }

    auto control = StartWithoutCheats(report, game);
    if (control) {
        control->Run(1);
        report.Check(!control->GetStage().LineMode() && !control->GetStage().FastBond(),
                     "neither is on without the cheats");
        for (int player = 0; player < players; player++) {
            const float plain = BodyScale(*control, player);
            report.Check(plain > 0.0f && std::fabs(BodyScale(game, player) - plain * 0.8f) < 0.0001f * plain,
                         "player %d's body is 0.8 of its size with DK mode (%.4f of %.4f)", player + 1,
                         BodyScale(game, player), plain);
        }
    }

    if (!StartPlaying(report, game)) {
        return;
    }
    const std::vector<HudPass> passes = RenderHudPasses(game);
    report.Check(passes.size() == static_cast<size_t>(players), "the frame has a pass for each player");
    for (const HudPass& pass : passes) {
        int circleX = pass.view.left + pass.view.width - 41;
        if (players >= 3 && pass.player % 2 == 0) {
            circleX += 15;
        }
        const int circleY = pass.view.top + 26;
        report.Check(!HasRect(pass, circleX - 16, circleY - 16, circleX + 16, circleY + 16, true),
                     "player %d has no radar", pass.player + 1);
    }
}

// Bond phase fades each player's own body.
void CheckCheatBondPhase(Report& report, MultiplayerSession& game) {
    game.Run(1);
    const int players = game.Players();
    for (int player = 0; player < players; player++) {
        report.Check(Marked(game, CheatId::kBondPhase, player), "player %d has bond phase marked", player + 1);
        report.Check(BodyAlpha(game, player) >= 250, "player %d starts opaque", player + 1);
    }
    game.Run(320);
    for (int player = 0; player < players; player++) {
        report.Check(BodyAlpha(game, player) < 20, "player %d's body has faded (%d)", player + 1,
                     BodyAlpha(game, player));
    }
}

// The cheats the game keeps out of multiplayer stay off, while the ones that
// apply next to them still do.
void CheckCheatsNotInMultiplayer(Report& report, MultiplayerSession& game) {
    game.Run(1);
    const int players = game.Players();
    auto control = StartWithoutCheats(report, game);
    if (control) {
        control->Run(1);
    }
    for (int32_t cheat = 1; cheat <= CheatId::k2xLaser; cheat++) {
        if ((Level::CheatMaskField(cheat) & Level::CheatMask::kMultiplayer) != 0) {
            continue;
        }
        for (int player = 0; player < players; player++) {
            report.Check(!Marked(game, cheat, player), "cheat %d is not on for player %d", cheat, player + 1);
        }
    }
    report.Check(game.GetStage().VisibleToGuards(), "invisibility is not on");
    report.Check(game.GetStage().PlacedChrs().AnimationRate() == 1.0f, "the animation rate is untouched");
    for (int player = 0; player < players; player++) {
        const Level::PlayerState& state = PlayerOf(game, player);
        report.Check(!state.intro.inventory.AllGuns(), "player %d does not have all guns", player + 1);
        if (control) {
            report.Check(game.GetStage().Stats(player).perspectiveHeight ==
                             control->GetStage().Stats(player).perspectiveHeight,
                         "player %d is not tiny", player + 1);
            for (int32_t item = 1; item < 64; item++) {
                report.Check(state.intro.inventory.HasItem(item) ==
                                 PlayerOf(*control, player).intro.inventory.HasItem(item),
                             "player %d's item %d is as without the cheats", player + 1, item);
            }
        }
        report.Check(state.cheatInvincible, "but player %d is invincible", player + 1);
    }
}

// Cheats are switched on in id order, so deactivate invincibility, which
// follows invincibility, leaves it on only for the player it worked from.
void CheckCheatDeactivate(Report& report, MultiplayerSession& game) {
    game.Run(1);
    report.Check(PlayerOf(game, 0).cheatInvincible, "player 1 stays invincible");
    for (int player = 1; player < game.Players(); player++) {
        report.Check(!PlayerOf(game, player).cheatInvincible, "player %d is not", player + 1);
        report.Check(!Marked(game, CheatId::kDeactivateInvincibility, player),
                     "a cheat that shows text leaves no mark for player %d", player + 1);
    }
}

// Types a cheat's code on one player's pad, a button every other tick.
void TypeCheatCode(MultiplayerSession& game, int player, int32_t cheat) {
    const uint16_t* code = Level::CheatCodeFor(cheat);
    Scripts scripts;
    for (int step = 0; step < 10; step++) {
        scripts[static_cast<size_t>(player)].Press(step * 2, step * 2 + 1, code[step]);
    }
    game.Run(24, scripts);
}

// The codes of the stage: in a game of several they act on the player who
// types them, the ones that are for a solo game do nothing, and the ones that
// are for several act.
void CheckCheatCodes(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    for (int typist = 0; typist < players; typist++) {
        TypeCheatCode(game, typist, CheatId::kInvincibility);
        for (int player = 0; player < players; player++) {
            report.Check(PlayerOf(game, player).cheatInvincible == (player <= typist),
                         "after player %d's code player %d is %sinvincible", typist + 1, player + 1,
                         player <= typist ? "" : "not ");
            report.Check(Marked(game, CheatId::kInvincibility, player) == (player <= typist), "and marked accordingly");
        }
        report.Check(Told(game, typist), "the typist is told");
    }
    // Typing it again switches only the typist's off.
    TypeCheatCode(game, players - 1, CheatId::kInvincibility);
    for (int player = 0; player < players; player++) {
        report.Check(PlayerOf(game, player).cheatInvincible == (player != players - 1),
                     "typed again by player %d, player %d is %sinvincible", players, player + 1,
                     player != players - 1 ? "" : "not ");
    }
    TypeCheatCode(game, players - 1, CheatId::kInvincibility);
    report.Check(PlayerOf(game, players - 1).cheatInvincible, "and on once more");

    // Deactivate invincibility takes it from everyone but the typist.
    TypeCheatCode(game, 0, CheatId::kDeactivateInvincibility);
    report.Check(PlayerOf(game, 0).cheatInvincible, "the typist of deactivate invincibility keeps theirs");
    for (int player = 1; player < players; player++) {
        report.Check(!PlayerOf(game, player).cheatInvincible, "player %d loses theirs", player + 1);
    }

    // All guns is for a solo game.
    TypeCheatCode(game, 0, CheatId::kAllGuns);
    for (int player = 0; player < players; player++) {
        report.Check(!PlayerOf(game, player).intro.inventory.AllGuns(), "all guns stays off for player %d", player + 1);
    }

    // Bond phase fades the typist's body alone.
    const int phaser = 1;
    TypeCheatCode(game, phaser, CheatId::kBondPhase);
    game.Run(320);
    for (int player = 0; player < players; player++) {
        if (player == phaser) {
            report.Check(BodyAlpha(game, player) < 20, "player %d's body fades (%d)", player + 1,
                         BodyAlpha(game, player));
        } else {
            report.Check(BodyAlpha(game, player) == 255, "player %d's body does not (%d)", player + 1,
                         BodyAlpha(game, player));
        }
    }

    // Line mode is the game's: any player switches it, and it is on for all.
    TypeCheatCode(game, players - 1, CheatId::kLineMode);
    report.Check(game.GetStage().LineMode(), "line mode is on");
    for (int player = 0; player < players; player++) {
        report.Check(Marked(game, CheatId::kLineMode, player), "line mode is marked for player %d", player + 1);
    }
    TypeCheatCode(game, 0, CheatId::kLineMode);
    report.Check(!game.GetStage().LineMode(), "and off again");
    for (int player = 0; player < players; player++) {
        report.Check(!Marked(game, CheatId::kLineMode, player), "unmarked for player %d", player + 1);
    }
}

// The stage's teardown switches the toggle cheats off, for every player.
void CheckCheatsDisabledAtEnd(Report& report, MultiplayerSession& game) {
    game.Run(1);
    report.Check(game.GetStage().LineMode() && game.GetStage().FastBond(), "line mode and turbo mode are on");
    game.GetStage().Unload();
    report.Check(!game.GetStage().LineMode() && !game.GetStage().FastBond(), "unloading the stage switches them off");
    for (int player = 0; player < game.Players(); player++) {
        report.Check(!Marked(game, CheatId::kLineMode, player) && !Marked(game, CheatId::kTurboMode, player),
                     "and unmarks them for player %d", player + 1);
    }
}

std::vector<int32_t> AllCheats() {
    std::vector<int32_t> all;
    for (int32_t cheat = 1; cheat <= CheatId::k2xLaser; cheat++) {
        // Deactivate invincibility takes it from the others, which its own
        // case checks.
        if (cheat != CheatId::kDeactivateInvincibility) {
            all.push_back(cheat);
        }
    }
    return all;
}

// -- Audio and rumble ------------------------------------------------------------------------

using Platform::Input::GlobalRumble;
using Platform::Input::kPortCount;
using Platform::Input::Rumble;
using SoundCall = AudioRig::Call::Kind;

// A hit that hurts without killing.
constexpr float kScratch = 0.2f;
// joyRumblePakStart(port, 0.25) and joyRumblePakStart(port, 0.1f), in ticks.
constexpr int kHitRumbleTicks = 15;
constexpr int kShotRumbleTicks = 6;

// Every motor at rest, and the ports in `ready` the ones with a controller.
void ResetRumble(uint8_t ready = 0x0F) {
    Rumble& rumble = GlobalRumble();
    rumble.SetReady(0x0F);
    for (int tick = 0; tick < 60; tick++) {
        rumble.Tick([](int, bool) { return true; });
    }
    rumble.SetReady(ready);
}

// The ports whose motors have been asked to run, as bits.
int RumblingPorts() {
    int ports = 0;
    for (int port = 0; port < kPortCount; port++) {
        if (GlobalRumble().Timer(port) > 0) {
            ports |= 1 << port;
        }
    }
    return ports;
}

// The ports a player's rumble goes to: their own controller, and for the
// two-controller styles the one at the player's number plus the player count.
int RumblePortsOf(int player, int players, bool twoControllers) {
    int ports = 1 << player;
    if (twoControllers && player + players < kPortCount) {
        ports |= 1 << (player + players);
    }
    return ports;
}

void SetStyle(MultiplayerSession& game, int player, Level::ControlConfig style) {
    PlayerOf(game, player).options.controlType = style;
}

// A hit rumbles the controller of the player hit, and for a two-controller
// control style the second one; only ports with a controller rumble.
void CheckRumbleHits(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    for (int victim = 0; victim < players; victim++) {
        const int attacker = (victim + 1) % players;
        for (const bool two : { false, true }) {
            SetStyle(game, victim, two ? Level::ControlConfig::Plenty : Level::ControlConfig::Honey);
            ResetRumble();
            stage.DamagePlayer(victim, attacker, kScratch, 0.0f, 1.0f);
            report.Check(RumblingPorts() == RumblePortsOf(victim, players, two),
                         "a hit on player %d (%s) rumbles ports %x, not %x", victim + 1, two ? "2.1" : "1.1",
                         RumblePortsOf(victim, players, two), RumblingPorts());
            report.Check(GlobalRumble().Timer(victim) == kHitRumbleTicks, "for a quarter of a second (%d ticks)",
                         GlobalRumble().Timer(victim));
            LetHitsSettle(game);
        }
        SetStyle(game, victim, Level::ControlConfig::Honey);

        // A port with no controller does not rumble.
        ResetRumble(static_cast<uint8_t>(1 << attacker));
        stage.DamagePlayer(victim, attacker, kScratch, 0.0f, 1.0f);
        report.Check(RumblingPorts() == 0, "no port rumbles when only player %d's controller is there", attacker + 1);
        LetHitsSettle(game);
    }

    // The second controller of a two-controller style rumbles alone when it is
    // the only one there.
    if (players < 3) {
        SetStyle(game, 0, Level::ControlConfig::Galore);
        ResetRumble(static_cast<uint8_t>(1 << players));
        stage.DamagePlayer(0, 1, kScratch, 0.0f, 1.0f);
        report.Check(RumblingPorts() == (1 << players), "the second controller rumbles when it is the only one");
        SetStyle(game, 0, Level::ControlConfig::Honey);
        LetHitsSettle(game);
    }

    // The style the controls read is the one that counts: the menu's override
    // replaces the player's own.
    const int victim = players - 1;
    PlayerOf(game, victim).options.controlOverride = Level::ControlConfig::Domino;
    ResetRumble();
    stage.DamagePlayer(victim, 0, kScratch, 0.0f, 1.0f);
    report.Check(RumblingPorts() == RumblePortsOf(victim, players, true),
                 "an override to a two-controller style rumbles both");
    LetHitsSettle(game);
    SetStyle(game, victim, Level::ControlConfig::Plenty);
    PlayerOf(game, victim).options.controlOverride = Level::ControlConfig::Kissy;
    ResetRumble();
    stage.DamagePlayer(victim, 0, kScratch, 0.0f, 1.0f);
    report.Check(RumblingPorts() == RumblePortsOf(victim, players, false),
                 "an override to a one-controller style rumbles one");
    PlayerOf(game, victim).options.controlOverride.reset();
    SetStyle(game, victim, Level::ControlConfig::Honey);
    LetHitsSettle(game);

    // A player already dead feels nothing, and nobody feels a hit once the pads
    // are stopped.
    Kill(game, victim, 0);
    ResetRumble();
    stage.DamagePlayer(victim, 0, kScratch, 0.0f, 1.0f);
    report.Check(RumblingPorts() == 0, "a dead player's controller does not rumble");
    stage.SetStopPlay();
    ResetRumble();
    stage.DamagePlayer(0, 1, kScratch, 0.0f, 1.0f);
    report.Check(RumblingPorts() == 0, "no controller rumbles once play has stopped");
}

// A pistol shot rumbles the shooter's controller (and the second one of a
// two-controller style) for a tenth of a second; the camera's shutter does not.
void CheckRumbleShots(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    for (int shooter = 0; shooter < players; shooter++) {
        Level::PlayerState& state = PlayerOf(game, shooter);
        state.intro.inventory.AddItem(Level::kItemPp7);
        state.weapons.GiveAmmo(state.weapons.Table().AmmoType(Level::kItemPp7), 100);
        state.weapons.EquipWeapon(Level::HandLeft, Level::kItemUnarmed);
        state.weapons.EquipWeapon(Level::HandRight, Level::kItemPp7);
    }
    game.Run(90);
    for (int shooter = 0; shooter < players; shooter++) {
        for (const bool two : { false, true }) {
            // A two-controller style fires from the second controller, which
            // the last players of a larger game have no port for.
            if (two && shooter + players >= kPortCount) {
                continue;
            }
            SetStyle(game, shooter, two ? Level::ControlConfig::Goodhead : Level::ControlConfig::Solitare);
            ResetRumble();
            Scripts scripts;
            scripts[static_cast<size_t>(two ? shooter + players : shooter)].Press(0, 20, Level::Buttons::Z);
            game.Run(20, scripts);
            report.Check(RumblingPorts() == RumblePortsOf(shooter, players, two),
                         "player %d's shots (%s) rumble ports %x, not %x", shooter + 1, two ? "2.4" : "1.2",
                         RumblePortsOf(shooter, players, two), RumblingPorts());
            report.Check(GlobalRumble().Timer(shooter) == kShotRumbleTicks, "for a tenth of a second (%d ticks)",
                         GlobalRumble().Timer(shooter));
            game.Run(60);
        }
        SetStyle(game, shooter, Level::ControlConfig::Honey);
    }

    const int photographer = players - 1;
    Level::PlayerState& state = PlayerOf(game, photographer);
    state.intro.inventory.AddItem(Level::kItemCamera);
    state.weapons.EquipWeapon(Level::HandRight, Level::kItemCamera);
    game.Run(90);
    ResetRumble();
    Scripts scripts;
    scripts[static_cast<size_t>(photographer)].Press(0, 20, Level::Buttons::Z);
    game.Run(20, scripts);
    report.Check(RumblingPorts() == 0, "the camera's shutter does not rumble");
}

// Where the stage hears a sound from: the volume of a sound is worked out from
// the nearest player, whichever player that is.
void CheckListeners(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    const Level::PropList& props = stage.PlacedObjects().Props();
    std::vector<Position> at;
    for (int player = 0; player < players; player++) {
        const int prop = PlayerOf(game, player).move.PropId();
        report.Check(prop >= 0, "player %d has a prop", player + 1);
        if (prop < 0) {
            return;
        }
        at.push_back(props[prop].position);
    }
    constexpr float kAside = 300.0f;
    for (int player = 0; player < players; player++) {
        const Position here = at[static_cast<size_t>(player)];
        const Position sound = { here[0], here[1], here[2] + kAside };
        int nearest = 0;
        float shortest = Distance(sound, at[0]);
        for (int other = 1; other < players; other++) {
            const float distance = Distance(sound, at[static_cast<size_t>(other)]);
            if (distance < shortest) {
                shortest = distance;
                nearest = other;
            }
        }
        report.Check(nearest == player, "player %d is the one nearest a sound beside them", player + 1);
        const int32_t heard = Game::Sound::VolumeAtPositionDefault(sound);
        const int32_t expected = Game::Sound::VolumeForDistanceDefault(shortest);
        report.Check(heard == expected, "a sound beside player %d is heard at the volume for %.0f units (%d, not %d)",
                     player + 1, shortest, heard, expected);
        report.Check(heard > 0 && heard < 32767, "and neither silent nor full");
        report.Check(Game::Sound::VolumeAtPositionDefault(here) == 32767, "a sound on player %d is full volume",
                     player + 1);
    }
    const Position far = { at[0][0] + 100000.0f, at[0][1], at[0][2] };
    report.Check(Game::Sound::VolumeAtPositionDefault(far) == 0, "a sound far from every player is silent");
}

// Every hit that registers sounds, including the second of two in a tick.
void CheckHitSound(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const auto hits = [&] { return gAudio->Count(SoundCall::PlaySfx, AudioDriver::kSfxBondGetHit1); };
    const int players = game.Players();
    for (int victim = 0; victim < players; victim++) {
        const int attacker = (victim + 1) % players;
        gAudio->Clear();
        stage.DamagePlayer(victim, attacker, kScratch, 0.0f, 1.0f);
        report.Check(hits() == 1, "a hit on player %d sounds", victim + 1);
        stage.DamagePlayer(victim, attacker, kScratch, 0.0f, 1.0f);
        report.Check(hits() == 2, "a second hit in the same tick registers and sounds (%d sounds)", hits());
        game.Run(3);
        gAudio->Clear();
        stage.DamagePlayer(victim, attacker, kScratch, 0.0f, 1.0f);
        report.Check(hits() == 0, "a hit while the last one still shows does not register or sound");
        LetHitsSettle(game);
    }
    Kill(game, 0, 1);
    gAudio->Clear();
    stage.DamagePlayer(0, 1, kScratch, 0.0f, 1.0f);
    report.Check(hits() == 0, "a hit on a dead player is silent");
}

// The alarm sounds once for the last ten seconds, and a paused or ended game
// silences it.
void CheckAlarmSound(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const auto alarms = [&] { return gAudio->Count(SoundCall::PlaySfx, AudioDriver::kSfxAlarm1); };
    const int limit = stage.MultiplayerTicks() + kLimitTicks;
    constexpr int kAlarmTicks = 0x258;
    stage.SetTimeLimit(limit);
    while (stage.MultiplayerTicks() < limit - kAlarmTicks - 2) {
        game.Run(1);
    }
    report.Check(alarms() == 0, "no alarm before the last ten seconds");
    game.Run(3);
    report.Check(alarms() == 1, "the alarm starts for the last ten seconds");
    game.Run(120);
    report.Check(alarms() == 1, "and is not started again while it sounds");

    // Pausing from one player's menu stops it for all.
    const int actor = game.Players() - 1;
    Tap(game, actor, Level::Buttons::Start);
    Tap(game, actor, Level::Buttons::RightPad);
    gAudio->Clear();
    Tap(game, actor, Level::Buttons::A);
    report.Check(stage.Paused() && stage.ControlsLocked(), "the game is paused");
    report.Check(gAudio->Count(SoundCall::Deactivate) >= 1, "pausing stops the alarm");
    Tap(game, actor, Level::Buttons::A);
    report.Check(!stage.Paused(), "the game resumes");

    // The end of the game stops it, and it does not start again.
    while (stage.MultiplayerTicks() < limit + 1 && !stage.GameOver()) {
        game.Run(1);
    }
    gAudio->Clear();
    game.Run(60);
    report.Check(stage.GameOver(), "the game ends at the limit");
    report.Check(alarms() == 0, "the alarm is not started again once the game is over");
    report.Check(gAudio->Count(SoundCall::Deactivate) >= 1, "the alarm is still being stopped once the game is over");
}

// Every player's sounds are cut when the stage is unloaded: each player's gun
// and the alarm, and the music stops.
void CheckUnloadSounds(Report& report, MultiplayerSession& first) {
    RandomSetSeed(0x1234);
    auto own = MultiplayerSession::Start(gResources, first.Settings());
    report.Check(own != nullptr, "the stage loads");
    if (!own || !StartPlaying(report, *own)) {
        return;
    }
    MultiplayerSession& game = *own;
    Level::Stage& stage = game.GetStage();
    const int players = game.Players();
    for (int player = 0; player < players; player++) {
        Level::PlayerState& state = PlayerOf(game, player);
        state.intro.inventory.AddItem(Level::kItemPp7);
        state.weapons.GiveAmmo(state.weapons.Table().AmmoType(Level::kItemPp7), 100);
        state.weapons.EquipWeapon(Level::HandLeft, Level::kItemUnarmed);
        state.weapons.EquipWeapon(Level::HandRight, Level::kItemPp7);
    }
    game.Run(90);
    stage.SetTimeLimit(stage.MultiplayerTicks() + 0x258 + 5);
    Scripts scripts;
    for (int player = 0; player < players; player++) {
        scripts[static_cast<size_t>(player)].Press(0, 8, Level::Buttons::Z);
    }
    game.Run(8, scripts);
    int sounding = 0;
    for (int player = 0; player < players; player++) {
        sounding += PlayerOf(game, player).weapons.Hands(Level::HandRight).audioHandle.IsPlaying() ? 1 : 0;
    }
    report.Check(sounding == players, "every player's gun is sounding (%d of %d)", sounding, players);
    report.Check(gAudio->Count(SoundCall::PlaySfx, AudioDriver::kSfxAlarm1) == 1, "and the alarm");

    gAudio->Clear();
    own.reset();
    report.Check(gAudio->Count(SoundCall::Deactivate) >= players + 1,
                 "unloading cuts every player's gun and the alarm (%d sounds cut)",
                 gAudio->Count(SoundCall::Deactivate));
    for (int slot = 0; slot < 3; slot++) {
        report.Check(gAudio->Count(SoundCall::MusicStop, slot) >= 1, "and stops music slot %d", slot);
    }
}

// A death starts the multiplayer death music over the faded theme, and the
// theme comes back once the blood has run out; the game's end plays the jingle
// and stops the stage's music.
void CheckMusicCalls(Report& report, MultiplayerSession& game) {
    report.Check(gAudio->Count(SoundCall::MusicPlay, 0) == 1, "the stage's theme starts on the first slot (%d)",
                 gAudio->Count(SoundCall::MusicPlay, 0));
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    report.Check(gAudio->Count(SoundCall::MusicPlay, 1) == 0, "no tense or death music at the start");

    gAudio->Clear();
    Kill(game, 1, 0);
    game.RunDrawingTheDead(1);
    report.Check(gAudio->Count(SoundCall::MusicFadeOut, 0) == 1, "a death fades out the theme");
    report.Check(gAudio->Count(SoundCall::MusicPlay, 1, AudioDriver::kMusicMpDeath) == 1,
                 "and starts the multiplayer death music");
    report.Check(gAudio->Count(SoundCall::MusicPlay, -1, AudioDriver::kMusicDeathSolo) == 0, "not the single player's");

    int ticks = 0;
    while (stage.MissionState() == Game::MissionState::Dead && ticks < kDeathLimit) {
        game.RunDrawingTheDead(1);
        ticks++;
    }
    report.Check(stage.MissionState() == Game::MissionState::Theme, "the theme returns");
    report.Check(gAudio->Count(SoundCall::MusicFadeIn, 0) == 1 && gAudio->Count(SoundCall::MusicFadeOut, 1) == 1,
                 "by fading it in over the death music");

    gAudio->Clear();
    stage.SetTimeLimit(stage.MultiplayerTicks() + 3);
    game.RunDrawingTheDead(10);
    report.Check(stage.GameOver(), "the game ends");
    report.Check(gAudio->Count(SoundCall::DeactivateAllByFlag, 1) == 1, "its sounds with flag 1 are cut");
    for (int slot = 0; slot < 3; slot++) {
        report.Check(gAudio->Count(SoundCall::MusicStop, slot) >= 1, "music slot %d is stopped", slot);
    }
    report.Check(gAudio->Count(SoundCall::MusicPlay, 0, AudioDriver::kMusicIntroSwoosh) == 1, "and the jingle plays");
}

// A pause silences the other players' guns: a burst that sounds in an unpaused
// game makes no sound while it is paused.
void CheckPausedGunSounds(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    Level::Stage& stage = game.GetStage();
    const int shooter = 0;
    const int pauser = game.Players() - 1;
    Level::PlayerState& state = PlayerOf(game, shooter);
    state.intro.inventory.AddItem(Level::kItemRcp90);
    state.weapons.GiveAmmo(state.weapons.Table().AmmoType(Level::kItemRcp90), 500);
    state.weapons.EquipWeapon(Level::HandLeft, Level::kItemUnarmed);
    state.weapons.EquipWeapon(Level::HandRight, Level::kItemRcp90);
    game.Run(90);

    gAudio->Clear();
    Scripts scripts;
    scripts[static_cast<size_t>(shooter)].Press(0, 30, Level::Buttons::Z);
    game.Run(30, scripts);
    report.Check(gAudio->Count(SoundCall::PlaySfx) > 0, "a shot sounds in an unpaused game");

    Tap(game, pauser, Level::Buttons::Start);
    Tap(game, pauser, Level::Buttons::RightPad);
    Tap(game, pauser, Level::Buttons::A);
    report.Check(stage.Paused(), "the game is paused");
    game.Run(60);
    gAudio->Clear();
    game.Run(30, scripts);
    report.Check(gAudio->Count(SoundCall::PlaySfx) == 0, "a paused game makes no shot sound (%d sounds)",
                 gAudio->Count(SoundCall::PlaySfx));
}

const std::vector<Case> kCases = {
    { "spawn", "core", nullptr, CheckSpawn },
    { "idle", "core", nullptr, CheckIdle },
    { "walk-alone", "core", nullptr, CheckWalkAlone },
    { "turn-alone", "core", nullptr, CheckTurnAlone },
    { "all-at-once", "core", nullptr, CheckAllAtOnce },
    { "control-styles", "core", ConfigureStyles, CheckStyles },
    { "fire-alone", "core", WithWeaponSet(1), CheckFireAlone },
    { "deterministic", "core", nullptr, CheckDeterministic },
    { "render", "core", nullptr, CheckRender },
    { "viewports", "core", nullptr, CheckViewports },
    { "passes", "core", nullptr, CheckPasses },
    { "switch-times", "core", nullptr, CheckSwitchTimes },
    { "dual-wield", "core", nullptr, CheckNoDualWield },
    { "casings", "core", nullptr, CheckNoCasings },
    { "scorches", "core", nullptr, CheckNoScorches },
    { "fog", "core", nullptr, CheckFogRecord },
    { "spawn-pads", "core", nullptr, CheckSpawnPads },
    { "kill", "core", nullptr, CheckKill },
    { "suicide", "core", nullptr, CheckSuicide },
    { "drop-inventory", "core", nullptr, CheckDropInventory },
    { "respawn", "core", nullptr, CheckRespawn },
    { "yolt", "core", ConfigureYoltScenario, CheckYolt },
    { "golden-gun", "core", ConfigureGoldenGunScenario, CheckGoldenGunKill },
    { "kill-stats", "core", nullptr, CheckKillStats },
    { "distance", "core", nullptr, CheckDistance },
    { "explosion", "core", nullptr, CheckExplosionCredit },
    { "characters", "core", ConfigureCharacters, CheckCharacters },
    { "held-weapons", "core", nullptr, CheckHeldWeapons },
    { "bodies-drawn", "core", nullptr, CheckBodiesDrawn },
    { "shoot-player", "core", nullptr, CheckShootPlayer },
    { "player-damage", "core", ConfigurePlayerDamage, CheckPlayerDamage },
    { "hurt-sounds", "core", ConfigureHurtSounds, CheckHurtSounds, true },
    { "respawn-needs-press", "core", nullptr, CheckRespawnNeedsPress },
    { "death-blood", "core", nullptr, CheckDeathBlood },
    { "roster", "core", nullptr, CheckRoster },
    { "body-animation", "core", nullptr, CheckBodyAnimation },
    { "time-limit", "core", nullptr, CheckTimeLimit },
    { "point-limit", "core", nullptr, CheckPointLimit },
    { "rank-order", "core", nullptr, CheckRankOrder },
    { "ties", "core", nullptr, CheckTies },
    { "teams-2v2", "core", ConfigureTwoVsTwo, CheckTeamScores },
    { "teams-3v1", "core", ConfigureThreeVsOne, CheckTeamScores },
    { "teams-2v1", "core", ConfigureTwoVsOne, CheckTeamScores },
    { "mission-state", "core", nullptr, CheckMissionState },
    { "rumble-hits", "core", nullptr, CheckRumbleHits },
    { "rumble-shots", "core", nullptr, CheckRumbleShots },
    { "listeners", "core", nullptr, CheckListeners },
    { "hit-sound", "core", nullptr, CheckHitSound, true },
    { "alarm-sound", "core", nullptr, CheckAlarmSound, true },
    { "unload-sounds", "core", nullptr, CheckUnloadSounds, true },
    { "music-calls", "core", nullptr, CheckMusicCalls, true },
    { "paused-gun-sounds", "core", nullptr, CheckPausedGunSounds, true },
    { "innings", "core", nullptr, CheckInnings },
    { "menu-opens", "core", nullptr, CheckMenuOpens },
    { "quit-confirm", "core", nullptr, CheckQuitConfirm },
    { "favourite-weapons", "core", nullptr, CheckFavouriteWeapons },
    { "awards", "core", nullptr, CheckAwards },
    { "game-over-delay", "core", nullptr, CheckGameOverDelay },
    { "menu-display", "core", nullptr, CheckMenuDisplay },
    { "menu-game-over", "core", nullptr, CheckMenuGameOver },
    { "menu-prompt", "core", nullptr, CheckMenuPrompt },
    { "menu-guns", "core", WithWeaponSet(1), CheckMenuGuns },
    { "trace", "debug", nullptr, TracePlayers },
    { "weapon-stock", "core", nullptr, CheckWeaponStock },
    { "skip-mask", "core", nullptr, CheckSkipMask },
    { "flag-stays", "core",
      [](MultiplayerSettings& settings) { settings.scenario = Game::MpScenario::TheLivingDaylights; }, CheckFlagStays },
    { "golden-gun-stays", "core",
      [](MultiplayerSettings& settings) {
          settings.scenario = Game::MpScenario::TheManWithTheGoldenGun;
          settings.weaponSet = 13;
      },
      CheckGoldenGunStays },
    { "flag-tag", "core", ConfigureFlagTag, CheckFlagTag },
    { "golden-gun-hold", "core", ConfigureGoldenGunHold, CheckGoldenGunHold },
    { "licence-to-kill", "core", ConfigureLicenceToKill, CheckLicenceToKill },
    { "yolt-round", "core", ConfigureYoltScenario, CheckYoltRound },
    { "team-play-2v2", "core", ConfigureTwoVsTwo, CheckTeamPlay },
    { "team-play-3v1", "core", ConfigureThreeVsOne, CheckTeamPlay },
    { "team-play-2v1", "core", ConfigureTwoVsOne, CheckTeamPlay },
    { "weapon-pickup", "core", WithWeaponSet(1), CheckWeaponPickup },
    { "crate-pickup", "core", nullptr, CheckCratePickup },
    { "thrown-weapons", "core", WithWeaponSet(9), CheckThrownWeapons },
    { "setup-dump", "debug", nullptr, DumpSetup },
    { "hud-layout", "core", ConfigureHud, CheckHudLayout },
    { "hud-layout-2v2", "core", ConfigureHudTeams2v2, CheckHudLayout },
    { "hud-layout-3v1", "core", ConfigureHudTeams3v1, CheckHudLayout },
    { "hud-layout-2v1", "core", ConfigureHudTeams2v1, CheckHudLayout },
    { "hud-layout-flag-tag", "core", ConfigureHudFlagTag, CheckHudLayout },
    { "hud-layout-golden-gun", "core", ConfigureHudGoldenGun, CheckHudLayout },
    { "hud-radar-dead", "core", ConfigureHud, CheckHudRadarDead },
    { "hud-menu", "core", ConfigureHud, CheckHudMenuOpen },
    { "hud-messages", "core", ConfigureHud, CheckHudMessages },
    { "cheats-invincible", "core", WithCheats({ CheatId::kInvincibility }), CheckCheatInvincible },
    { "cheats-health", "core", WithCheats({ CheatId::k10xHealth, CheatId::k2xArmor }), CheckCheatHealth },
    { "cheats-ammo", "core", WithCheats({ CheatId::kMaxAmmo, CheatId::kInfiniteAmmo }), CheckCheatAmmo },
    { "cheats-global", "core",
      WithCheats(
          { CheatId::kDkMode, CheatId::kPaintball, CheatId::kLineMode, CheatId::kTurboMode, CheatId::kNoRadarMp }),
      CheckCheatGlobal },
    { "cheats-bond-phase", "core", WithCheats({ CheatId::kBondPhase }), CheckCheatBondPhase },
    { "cheats-not-in-multiplayer", "core", WithCheats(AllCheats()), CheckCheatsNotInMultiplayer },
    { "cheats-deactivate", "core", WithCheats({ CheatId::kInvincibility, CheatId::kDeactivateInvincibility }),
      CheckCheatDeactivate },
    { "cheats-codes", "core", WithCheats({}), CheckCheatCodes },
    { "cheats-extra-characters", "core", WithCheats({ CheatId::kExtraMpChars }), CheckCheatExtraCharacters },
    { "cheats-disabled-at-end", "core", WithCheats({ CheatId::kLineMode, CheatId::kTurboMode }),
      CheckCheatsDisabledAtEnd },
};

// kCases plus the weapon set cases of the "weapons" group: the stock of every
// set, a pickup of what each set puts on the floor, and the thrown weapons of
// the sets that only have them in crates.
const std::vector<Case>& AllCases() {
    static const std::vector<Case> cases = [] {
        static std::vector<std::string> names;
        names.reserve(4 * MultiplayerSettings::kWeaponSets);
        std::vector<Case> all = kCases;
        for (int set = 0; set < MultiplayerSettings::kWeaponSets; set++) {
            const Level::MpWeaponSetEntry& last = Level::MpWeaponSetData(set)[7];
            const std::string name = Game::WeaponSetName(set);
            names.push_back("stock-" + name);
            all.push_back({ names.back().c_str(), "weapons", WithWeaponSet(set), CheckWeaponStock });
            if (set == 0) {
                continue;
            }
            const Level::MpWeaponSet& rows = Level::MpWeaponSetData(set);
            if (std::any_of(rows.begin(), rows.end(),
                            [](const Level::MpWeaponSetEntry& row) { return row.allowPickup; })) {
                names.push_back("pickup-" + name);
                all.push_back({ names.back().c_str(), "weapons", WithWeaponSet(set), CheckWeaponPickup });
            }
            if (last.ammoAmount > 0) {
                names.push_back("crate-" + name);
                all.push_back({ names.back().c_str(), "weapons", WithWeaponSet(set), CheckCratePickup });
            }
            if (!last.allowPickup) {
                names.push_back("thrown-" + name);
                all.push_back({ names.back().c_str(), "weapons", WithWeaponSet(set), CheckThrownWeapons });
            }
        }
        return all;
    }();
    return cases;
}

bool Selected(const std::vector<std::string>& wanted, const Case& testCase) {
    if (wanted.empty()) {
        return std::string(testCase.group) == "core";
    }
    for (const std::string& name : wanted) {
        if (name == testCase.name || name == testCase.group) {
            return true;
        }
    }
    return false;
}

void CheckDeterministic(Report& report, MultiplayerSession& first) {
    first.Run(80, DeterminismScripts(first.Players()));
    const std::vector<float> expected = Snapshot(first);
    RandomSetSeed(0x1234);
    auto second = MultiplayerSession::Start(gResources, first.Settings());
    report.Check(second != nullptr, "the stage loads again");
    if (second) {
        second->Run(80, DeterminismScripts(first.Players()));
        report.Check(Snapshot(*second) == expected, "the same scripts from the same seed end in the same places");
    }
}

// License to Kill: every hit kills whatever the health and armour, and the same
// hit in another scenario does not.
void CheckLicenceToKill(Report& report, MultiplayerSession& game) {
    if (!StartPlaying(report, game)) {
        return;
    }
    const int players = game.Players();
    for (int player = 0; player < players; player++) {
        report.Check(StatsOf(game, player).handicap == 200.0f, "player %d's handicap is 200", player + 1);
    }
    for (int victim = 0; victim < players; victim++) {
        const int attacker = (victim + 1) % players;
        PlayerOf(game, victim).health.SetArmour(1.0f);
        const int kills = StatsOf(game, attacker).killCount;
        game.GetStage().DamagePlayer(victim, attacker, 0.001f, 0.0f, 1.0f);
        report.Check(IsDead(game, victim), "a scratch kills player %d with full health and armour", victim + 1);
        report.Check(StatsOf(game, attacker).killCount == kills + 1, "and is credited to player %d", attacker + 1);
        for (int other = 0; other < players; other++) {
            report.Check(other == victim || !IsDead(game, other), "player %d is unharmed", other + 1);
        }
        report.Check(WaitForDeathScreen(game, victim) && PressToRespawn(game, victim), "player %d respawns",
                     victim + 1);
        LetHitsSettle(game);

        PlayerOf(game, victim).health.SetArmour(1.0f);
        game.GetStage().DamagePlayer(victim, attacker, 0.001f, 0.0f, 1.0f, false);
        report.Check(IsDead(game, victim), "so does gas");
        report.Check(WaitForDeathScreen(game, victim) && PressToRespawn(game, victim), "player %d respawns again",
                     victim + 1);
        LetHitsSettle(game);
    }

    MultiplayerSettings normal = game.Settings();
    normal.scenario = Game::MpScenario::Normal;
    RandomSetSeed(0x1234);
    auto control = MultiplayerSession::Start(gResources, normal);
    report.Check(control != nullptr, "the control game loads");
    if (control && StartPlaying(report, *control)) {
        PlayerOf(*control, 0).health.SetArmour(1.0f);
        control->GetStage().DamagePlayer(0, 1, 0.001f, 0.0f, 1.0f);
        report.Check(!IsDead(*control, 0), "the same scratch kills no one in the normal scenario");
    }
}

void RunCase(Report& report, const Case& testCase, int players) {
    report.Begin(testCase.name, players);
    MultiplayerSettings settings = MultiplayerSettings::Defaults(players);
    if (testCase.configure) {
        testCase.configure(settings);
    }
    RandomSetSeed(0x1234);
    if (testCase.audio) {
        gAudio = std::make_unique<AudioRig>(gResources);
        if (!gAudio->Ok()) {
            std::printf("SKIP %s: no audio in the archives\n", testCase.name);
            gAudio.reset();
            return;
        }
    }
    {
        auto session = MultiplayerSession::Start(gResources, settings);
        report.Check(session != nullptr, "the stage loads");
        if (session) {
            testCase.run(report, *session);
        }
    }
    gAudio.reset();
}

// Every multiplayer stage loads and runs at each player count it allows.
void RunStages(Report& report) {
    for (int stage = static_cast<int>(Game::MpStage::Temple); stage < static_cast<int>(Game::MpStage::Count); stage++) {
        for (int players = 2; players <= Game::kMpMaxPlayers; players++) {
            if (Game::MultiplayerSettings::StagePlayers(static_cast<Game::MpStage>(stage)).max < players) {
                continue;
            }
            report.Begin(std::string("stage ") + Game::StageName(static_cast<Game::MpStage>(stage)), players);
            MultiplayerSettings settings = MultiplayerSettings::Defaults(players);
            settings.stage = static_cast<Game::MpStage>(stage);
            RandomSetSeed(0x1234);
            auto session = MultiplayerSession::Start(gResources, settings);
            report.Check(session != nullptr, "the stage loads");
            if (!session) {
                continue;
            }
            CheckSpawn(report, *session);
            CheckFogRecord(report, *session);
            CheckWindowPortals(report, *session);
            CheckSkipMask(report, *session);
            CheckWeaponStock(report, *session);
            session->Run(kSettleTicks);
            report.Check(session->Render() > 0, "the stage draws");
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <archive dir> [case or group ...]\n", argv[0]);
        return 2;
    }
    const std::filesystem::path archiveDir = argv[1];
    std::vector<std::string> wanted(argv + 2, argv + argc);

    std::vector<std::string> archives;
    for (const char* name :
         { "goldeneye_textures.o2r", "goldeneye_objects.o2r", "goldeneye_geometry.o2r", "goldeneye_audio.o2r" }) {
        if (std::filesystem::exists(archiveDir / name)) {
            archives.push_back((archiveDir / name).string());
        }
    }
    if (archives.size() < 3) {
        std::printf("SKIP: no packed archives in %s (run `just pack_archives`)\n", archiveDir.string().c_str());
        return kSkipped;
    }

    // The context writes its config and log under SHIP_HOME.
    const std::filesystem::path home = std::filesystem::temp_directory_path() / "frigate-multiplayer-harness";
    std::filesystem::create_directories(home);
    setenv("SHIP_HOME", home.string().c_str(), 1);
    auto context = Frigate::Platform::CreateContext("frigate-harness", "frigate-harness", "harness.json", archives,
                                                    /*withWindow=*/false);
    if (context == nullptr) {
        std::fprintf(stderr, "harness: no context\n");
        return 1;
    }
    gResources = context->GetChildren().GetFirst<Ship::ResourceManager>();

    Report report;
    const bool stages = std::find(wanted.begin(), wanted.end(), "stages") != wanted.end();
    if (stages) {
        RunStages(report);
    }
    for (const Case& testCase : AllCases()) {
        if (!Selected(wanted, testCase)) {
            continue;
        }
        for (int players = 2; players <= Game::kMpMaxPlayers; players++) {
            RunCase(report, testCase, players);
        }
    }
    std::printf("%d checks, %d failures\n", report.Checks(), report.Failures());
    gResources.reset();
    // The Context destructor logs after the logger is gone; nothing here needs it.
    spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& logger) { logger->flush(); });
    std::fflush(nullptr);
    std::_Exit(report.Failures() == 0 ? 0 : 1);
}
