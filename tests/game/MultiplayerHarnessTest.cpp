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
#include <vector>

#include <spdlog/spdlog.h>

#include "MultiplayerHarness.h"
#include "game/ChrRecords.h"
#include "game/MultiplayerQuickStart.h"
#include "level/Fog.h"
#include "level/PlayerMove.h"
#include "level/ScreenLayout.h"
#include "level/Stage.h"
#include "level/WeaponTable.h"
#include "model/Random.h"
#include "platform/Boot.h"
#include "ship/core/Context.h"
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
};

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

// Pulling the trigger spends the shooter's ammunition and nobody else's.
// TODO(https://github.com/liz-baker/frigate/issues/390): the stage hands
// players the weapon set's guns at the start; until it does, the case arms
// them itself.
void CheckFireAlone(Report& report, MultiplayerSession& game) {
    report.Check(game.WaitForControl(kSettleTicks) >= 0, "the opening ends and the players take control");
    for (int player = 0; player < game.Players(); player++) {
        Level::PlayerState& state = game.GetStage().PlayerAt(player);
        state.intro.inventory.AddItem(Level::kItemPp7);
        state.weapons.AddAmmoToWeapon(Level::kItemPp7, 100);
        state.weapons.EquipWeapon(Level::HandRight, Level::kItemPp7);
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

const std::vector<Case> kCases = {
    { "spawn", "core", nullptr, CheckSpawn },
    { "idle", "core", nullptr, CheckIdle },
    { "walk-alone", "core", nullptr, CheckWalkAlone },
    { "turn-alone", "core", nullptr, CheckTurnAlone },
    { "all-at-once", "core", nullptr, CheckAllAtOnce },
    { "control-styles", "core", ConfigureStyles, CheckStyles },
    { "fire-alone", "core", nullptr, CheckFireAlone },
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
    { "roster", "core", nullptr, CheckRoster },
    { "body-animation", "core", nullptr, CheckBodyAnimation },
    { "trace", "debug", nullptr, TracePlayers },
};

std::shared_ptr<Ship::ResourceManager> gResources;

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

void RunCase(Report& report, const Case& testCase, int players) {
    report.Begin(testCase.name, players);
    MultiplayerSettings settings = MultiplayerSettings::Defaults(players);
    if (testCase.configure) {
        testCase.configure(settings);
    }
    RandomSetSeed(0x1234);
    auto session = MultiplayerSession::Start(gResources, settings);
    report.Check(session != nullptr, "the stage loads");
    if (session) {
        testCase.run(report, *session);
    }
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
    for (const Case& testCase : kCases) {
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
