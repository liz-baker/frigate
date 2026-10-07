// Headless multiplayer checks against the real stages: `frigate_multiplayer_harness
// <archive dir> [case or group ...]`. Needs the packed archives (`just
// pack_archives`) and skips (exit 77) without them. Cases are listed in kCases;
// see MultiplayerHarness.h for what a case can drive and read.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "MultiplayerHarness.h"
#include "game/MultiplayerQuickStart.h"
#include "level/Fog.h"
#include "level/PlayerMove.h"
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
    { "switch-times", "core", nullptr, CheckSwitchTimes },
    { "dual-wield", "core", nullptr, CheckNoDualWield },
    { "casings", "core", nullptr, CheckNoCasings },
    { "scorches", "core", nullptr, CheckNoScorches },
    { "fog", "core", nullptr, CheckFogRecord },
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
