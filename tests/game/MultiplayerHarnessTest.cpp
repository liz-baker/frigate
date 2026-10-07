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
#include "level/PlayerMove.h"
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
    { "spawn", "core", nullptr, CheckSpawn },           { "idle", "core", nullptr, CheckIdle },
    { "walk-alone", "core", nullptr, CheckWalkAlone },  { "turn-alone", "core", nullptr, CheckTurnAlone },
    { "all-at-once", "core", nullptr, CheckAllAtOnce }, { "control-styles", "core", ConfigureStyles, CheckStyles },
    { "fire-alone", "core", nullptr, CheckFireAlone },  { "deterministic", "core", nullptr, CheckDeterministic },
    { "render", "core", nullptr, CheckRender },         { "trace", "debug", nullptr, TracePlayers },
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
