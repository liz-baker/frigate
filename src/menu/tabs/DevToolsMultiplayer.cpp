#include <array>
#include <memory>
#include <string>

#include "game/MultiplayerQuickStart.h"
#include "menu/tabs/DevToolsTab.h"

namespace Frigate::ConfigMenu {

namespace {

using Game::MpGameLength;
using Game::MpScenario;
using Game::MpStage;
using Game::MultiplayerSettings;

constexpr const char* kTab = "Dev Tools";

struct State {
    MultiplayerSettings settings = MultiplayerSettings::Defaults(2);
    // The team letters of a team scenario: 0 is A, 1 is B.
    std::array<int, Game::kMpMaxPlayers> team{ 0, 0, 1, 1 };
    std::string status;

    // The settings the widgets describe, with `status` saying why not when
    // they can't start a game.
    bool Build(MultiplayerSettings& out) {
        out = settings;
        status.clear();
        if (out.IsTeamScenario()) {
            std::string teams;
            for (int player = 0; player < out.playerCount; player++) {
                teams += team[static_cast<size_t>(player)] == 0 ? 'A' : 'B';
            }
            const auto teamSize = Game::TeamSizeFor(out.scenario, out.playerCount, teams, &status);
            if (!teamSize) {
                return false;
            }
            out.teamSize = *teamSize;
        }
        const char* reason = nullptr;
        if (!out.Valid(&reason)) {
            status = reason;
            return false;
        }
        return true;
    }
};

std::vector<std::string> Labels(int count, const char* (*name)(int)) {
    std::vector<std::string> labels;
    for (int row = 0; row < count; row++) {
        labels.push_back(name(row));
    }
    return labels;
}

const char* StageLabel(int row) {
    static const char* const kLabels[] = { "Random", "Temple",   "Complex", "Caves",    "Library", "Basement",
                                           "Stack",  "Facility", "Bunker",  "Archives", "Caverns", "Egypt" };
    return kLabels[row];
}

const char* ScenarioLabel(int row) {
    static const char* const kLabels[] = {
        "Normal",          "You Only Live Twice", "The Living Daylights", "The Man with the Golden Gun",
        "License to Kill", "Teams, 2 vs 2",       "Teams, 3 vs 1",        "Teams, 2 vs 1"
    };
    return kLabels[row];
}

const char* GameLengthLabel(int row) {
    static const char* const kLabels[] = { "Unlimited", "5 minutes", "10 minutes", "20 minutes",
                                           "5 points",  "10 points", "20 points",  "Last one alive" };
    return kLabels[row];
}

const char* WeaponSetLabel(int row) {
    static const char* const kLabels[] = { "Slappers only", "Pistols",         "Throwing knives",
                                           "Automatics",    "Power weapons",   "Sniper rifles",
                                           "Grenades",      "Remote mines",    "Grenade launchers",
                                           "Timed mines",   "Proximity mines", "Rockets",
                                           "Lasers",        "Golden Gun" };
    return kLabels[row];
}

const char* AimLabel(int row) {
    static const char* const kLabels[] = { "Auto-aim off, sight off", "Auto-aim off, sight on",
                                           "Auto-aim on, sight off", "Auto-aim on, sight on" };
    return kLabels[row];
}

const char* ControlStyleLabel(int row) {
    static const char* const kLabels[] = { "1.1", "1.2", "1.3", "1.4", "2.1", "2.2", "2.3", "2.4" };
    return kLabels[row];
}

} // namespace

void RegisterMultiplayerStart(Registry& registry, const std::function<void(const MultiplayerSettings&)>& start) {
    const std::string sidebar = "Multiplayer game";
    registry.AddSidebar(kTab, sidebar);
    auto state = std::make_shared<State>();

    registry.AddWidget(kTab, sidebar,
                       "Starts a multiplayer game straight away, dropping whatever is running. Controller ports 1 to "
                       "the player count read as connected whether or not a controller is attached; give each "
                       "player a device on the Controls tab.",
                       WidgetType::Text);

    // A widget that mirrors one field of the settings.
    const auto field = [&](const std::string& name, WidgetType type, std::function<int(State&)> read,
                           std::function<void(State&, int)> write) {
        auto builder = registry.AddWidget(kTab, sidebar, name, type);
        Widget* widget = &builder.Get();
        builder.PreFunc([state, read](Widget& self) { self.intValue = read(*state); }).Callback([state, widget, write] {
            write(*state, widget->intValue);
        });
        return builder;
    };

    field(
        "Players", WidgetType::Combo, [](State& s) { return s.settings.playerCount - 2; },
        [](State& s, int value) { s.settings.playerCount = value + 2; })
        .Options({ "2", "3", "4" });
    field(
        "Stage", WidgetType::Combo, [](State& s) { return static_cast<int>(s.settings.stage); },
        [](State& s, int value) { s.settings.stage = static_cast<MpStage>(value); })
        .Options(Labels(static_cast<int>(MpStage::Count), StageLabel))
        .Tooltip("Random picks a stage the player count fits.");
    field(
        "Scenario", WidgetType::Combo, [](State& s) { return static_cast<int>(s.settings.scenario); },
        [](State& s, int value) { s.settings.scenario = static_cast<MpScenario>(value); })
        .Options(Labels(static_cast<int>(MpScenario::Count), ScenarioLabel));
    field(
        "Game length", WidgetType::Combo, [](State& s) { return static_cast<int>(s.settings.gameLength); },
        [](State& s, int value) { s.settings.gameLength = static_cast<MpGameLength>(value); })
        .Options(Labels(static_cast<int>(MpGameLength::Count), GameLengthLabel));
    field(
        "Weapon set", WidgetType::Combo, [](State& s) { return s.settings.weaponSet; },
        [](State& s, int value) { s.settings.weaponSet = value; })
        .Options(Labels(MultiplayerSettings::kWeaponSets, WeaponSetLabel));
    field(
        "Aim", WidgetType::Combo, [](State& s) { return s.settings.aimSetting; },
        [](State& s, int value) { s.settings.aimSetting = value; })
        .Options(Labels(MultiplayerSettings::kAimRows, AimLabel));

    for (int player = 0; player < Game::kMpMaxPlayers; player++) {
        const size_t index = static_cast<size_t>(player);
        auto header = registry.AddWidget(kTab, sidebar, "Player " + std::to_string(player + 1), WidgetType::Header);
        header.DefaultOpen().HiddenWhen([state, player] { return player >= state->settings.playerCount; });

        const auto child = [&](const std::string& name, WidgetType type, std::function<int(State&)> read,
                               std::function<void(State&, int)> write) {
            auto builder = header.Child(name, type);
            Widget* widget = &builder.Get();
            builder.PreFunc([state, read](Widget& self) { self.intValue = read(*state); })
                .Callback([state, widget, write] { write(*state, widget->intValue); });
            return builder;
        };
        child(
            "Character", WidgetType::SliderInt, [index](State& s) { return s.settings.character[index]; },
            [index](State& s, int value) { s.settings.character[index] = value; })
            .Range(-1, MultiplayerSettings::kCharacterRows - 1, 1)
            .Default(player)
            .Tooltip("A row of the character table; -1 is the player's own row.");
        child(
            "Handicap", WidgetType::SliderInt, [index](State& s) { return s.settings.handicap[index]; },
            [index](State& s, int value) { s.settings.handicap[index] = value; })
            .Range(0, MultiplayerSettings::kHandicapRows - 1, 1)
            .Default(MultiplayerSettings::kNormalHandicap)
            .Tooltip("5 is normal; lower rows take more damage, higher rows less.");
        child(
            "Control style", WidgetType::Combo,
            [index](State& s) { return static_cast<int>(s.settings.controlStyle[index]); },
            [index](State& s, int value) { s.settings.controlStyle[index] = static_cast<Level::ControlConfig>(value); })
            .Options(Labels(8, ControlStyleLabel));
        child(
            "Team", WidgetType::Combo, [index](State& s) { return s.team[index]; },
            [index](State& s, int value) { s.team[index] = value; })
            .Options({ "A", "B" })
            .HiddenWhen([state] { return !state->settings.IsTeamScenario(); });
    }

    registry.AddWidget(kTab, sidebar, "", WidgetType::Text).PreFunc([state](Widget& widget) {
        MultiplayerSettings unused;
        state->Build(unused);
        widget.hidden = state->status.empty();
        widget.name = "Can't start: " + state->status;
    });
    registry.AddWidget(kTab, sidebar, "Start game", WidgetType::Button)
        .Tooltip("Restarts into this game; the legal screen follows when it ends.")
        .PreFunc([state, start](Widget& widget) {
            MultiplayerSettings unused;
            if (!start) {
                widget.disabled = true;
                widget.disabledReason = "The game isn't running here.";
            } else if (!state->Build(unused)) {
                widget.disabled = true;
                widget.disabledReason = state->status;
            }
        })
        .Callback([state, start] {
            MultiplayerSettings settings;
            if (start && state->Build(settings)) {
                start(settings);
            }
        });
}

} // namespace Frigate::ConfigMenu
