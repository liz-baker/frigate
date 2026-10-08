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
    TestTeams();
    TestRefusals();
    if (gFailures == 0) {
        std::printf("all tests passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
