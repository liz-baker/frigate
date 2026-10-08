#include <cstdio>
#include <optional>
#include <string>

#include "game/MultiplayerQuickStart.h"
#include "menu/MenuModel.h"
#include "menu/tabs/DevToolsTab.h"

using namespace Frigate;
using namespace Frigate::ConfigMenu;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

constexpr const char* kTab = "Dev Tools";
constexpr const char* kSidebar = "Multiplayer game";

Widget* Find(Registry& registry, const std::string& name) {
    Sidebar* sidebar = registry.FindTab(kTab)->FindSidebar(kSidebar);
    for (auto& widget : sidebar->widgets) {
        if (widget->name == name) {
            return widget.get();
        }
        for (auto& child : widget->children) {
            if (child->name == name) {
                return child.get();
            }
        }
    }
    return nullptr;
}

// Picks `value` in the combo or slider the way the menu draw does.
void Set(Registry& registry, MemoryValueStore& store, const std::string& name, int value) {
    Widget* widget = Find(registry, name);
    Check(widget != nullptr, "the widget exists");
    if (widget != nullptr) {
        widget->SetInt(store, value);
    }
}

struct Fixture {
    Registry registry;
    MemoryValueStore store;
    std::optional<Game::MultiplayerSettings> started;
    int starts = 0;

    Fixture() {
        RegisterMultiplayerStart(registry, [this](const Game::MultiplayerSettings& settings) {
            started = settings;
            starts++;
        });
        registry.Refresh();
    }

    Widget& StartButton() {
        return *Find(registry, "Start game");
    }
    void Press() {
        registry.Refresh();
        Widget& button = StartButton();
        if (!button.disabled) {
            button.callback();
        }
    }
};

void TestStartsWhatTheWidgetsDescribe() {
    for (int players = 2; players <= 4; players++) {
        Fixture f;
        Set(f.registry, f.store, "Players", players - 2);
        Set(f.registry, f.store, "Stage", static_cast<int>(Game::MpStage::Complex));
        Set(f.registry, f.store, "Game length", static_cast<int>(Game::MpGameLength::TwentyPoints));
        Set(f.registry, f.store, "Weapon set", 4);
        f.Press();
        Check(f.starts == 1 && f.started.has_value(), "the button starts a game");
        if (!f.started) {
            continue;
        }
        Check(f.started->playerCount == players, "the player count");
        Check(f.started->stage == Game::MpStage::Complex && f.started->PointLimit() == 20 && f.started->weaponSet == 4,
              "stage, limit and weapon set");
    }
}

void TestPerPlayerWidgets() {
    Fixture f;
    Set(f.registry, f.store, "Players", 2);
    f.registry.Refresh();
    Widget* header = nullptr;
    for (auto& widget : f.registry.FindTab(kTab)->FindSidebar(kSidebar)->widgets) {
        if (widget->name == "Player 4") {
            header = widget.get();
        }
    }
    Check(header != nullptr && !header->hidden, "four players show the fourth player's rows");
    Set(f.registry, f.store, "Players", 0);
    f.registry.Refresh();
    Check(header != nullptr && header->hidden, "two players hide the fourth player's rows");
}

void TestCheats() {
    Fixture f;
    f.Press();
    Check(f.started && f.started->cheats.empty(), "no cheat is on by default");
    Set(f.registry, f.store, "Invincibility", 1);
    Set(f.registry, f.store, "No radar", 1);
    Set(f.registry, f.store, "All guns", 1);
    f.Press();
    Check(f.started && f.started->cheats == std::vector<int32_t>{ 2, 3, 23 }, "the ticked cheats start with the game");
    Set(f.registry, f.store, "Invincibility", 0);
    f.Press();
    Check(f.started && f.started->cheats == std::vector<int32_t>{ 3, 23 }, "and unticking one drops it");
}

void TestTeams() {
    Fixture f;
    Set(f.registry, f.store, "Players", 2);
    Set(f.registry, f.store, "Scenario", static_cast<int>(Game::MpScenario::TwoVsTwo));
    f.Press();
    Check(f.started && f.started->scenario == Game::MpScenario::TwoVsTwo, "a team game starts with the default teams");
    Check(f.started && f.started->TeamFlag(0) == f.started->TeamFlag(1) &&
              f.started->TeamFlag(2) != f.started->TeamFlag(0),
          "players 1 and 2 against 3 and 4");
    // Player 1 alone on B against three on A is not a 2 vs 2.
    Set(f.registry, f.store, "Team", 1);
    f.registry.Refresh();
    Check(f.StartButton().disabled && f.StartButton().disabledReason.find("teams") != std::string::npos,
          "teams that don't split the scenario's way disable the button");
}

// A multiplayer game's settings are not saved: no widget keeps its value in a
// CVar (which the config file would carry to the next run), and a new run's
// sidebar starts from the cold boot values whatever an earlier one chose.
void TestNothingIsRemembered() {
    Fixture first;
    Sidebar* sidebar = first.registry.FindTab(kTab)->FindSidebar(kSidebar);
    int widgets = 0;
    int saved = 0;
    for (auto& widget : sidebar->widgets) {
        widgets++;
        saved += widget->cvar.empty() ? 0 : 1;
        for (auto& child : widget->children) {
            widgets++;
            saved += child->cvar.empty() ? 0 : 1;
        }
    }
    Check(widgets > 20 && saved == 0, "no widget of the multiplayer game saves its value in a CVar");

    const auto expectColdBoot = [](Fixture& f, const char* when) {
        f.Press();
        Check(f.started.has_value(), when);
        if (!f.started) {
            return;
        }
        const Game::MultiplayerSettings& settings = *f.started;
        Check(settings.playerCount == 2 && settings.stage == Game::MpStage::Temple &&
                  settings.scenario == Game::MpScenario::Normal &&
                  settings.gameLength == Game::MpGameLength::TenMinutes && settings.weaponSet == 0xB &&
                  settings.aimSetting == 3,
              when);
        Check(settings.character[0] == 0 && settings.character[1] == 1 && settings.handicap[0] == 5 &&
                  settings.handicap[1] == 5 && settings.controlStyle[0] == Level::ControlConfig::Honey,
              when);
    };
    expectColdBoot(first, "the first sidebar starts from the cold boot values");

    Set(first.registry, first.store, "Players", 2);
    Set(first.registry, first.store, "Stage", static_cast<int>(Game::MpStage::Caves));
    Set(first.registry, first.store, "Weapon set", 1);
    Set(first.registry, first.store, "Game length", static_cast<int>(Game::MpGameLength::FivePoints));
    Set(first.registry, first.store, "Aim", 0);
    first.Press();
    Check(first.started && first.started->stage == Game::MpStage::Caves && first.started->weaponSet == 1,
          "the first sidebar starts what it was given");

    Fixture next;
    expectColdBoot(next, "a sidebar of a later run starts from the cold boot values again");
}

void TestRefusals() {
    {
        Fixture f;
        Set(f.registry, f.store, "Players", 2);
        Set(f.registry, f.store, "Stage", static_cast<int>(Game::MpStage::Egypt));
        f.registry.Refresh();
        Check(f.StartButton().disabled && !f.StartButton().disabledReason.empty(),
              "a stage the player count does not fit disables the button with a reason");
        f.Press();
        Check(f.starts == 0, "a disabled button starts nothing");
    }
    {
        Fixture f;
        Set(f.registry, f.store, "Scenario", static_cast<int>(Game::MpScenario::TwoVsTwo));
        f.registry.Refresh();
        Check(f.StartButton().disabled, "a team scenario needs the players it is for");
    }
    {
        Registry registry;
        RegisterMultiplayerStart(registry, nullptr);
        registry.Refresh();
        Check(Find(registry, "Start game")->disabled, "without a running game the button is disabled");
    }
}

} // namespace

int main() {
    TestStartsWhatTheWidgetsDescribe();
    TestPerPlayerWidgets();
    TestCheats();
    TestTeams();
    TestNothingIsRemembered();
    TestRefusals();
    if (gFailures == 0) {
        std::printf("all tests passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
