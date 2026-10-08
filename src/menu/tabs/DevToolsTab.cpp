#include "menu/tabs/DevToolsTab.h"

#include <memory>
#include <string>
#include <vector>

#include "game/save/ActiveSave.h"
#include "game/save/SaveEditor.h"

namespace Frigate::ConfigMenu {

namespace {

using Game::Save::SaveEditor;

constexpr const char* kTab = "Dev Tools";
constexpr size_t kChangeRows = 16;
constexpr const char* kNoGame = "The game isn't running yet.";

// Everything the tab's widgets share. The editor is rebuilt if the game's
// save manager is replaced.
struct State {
    std::shared_ptr<Game::Save::SaveManager> manager;
    std::unique_ptr<SaveEditor> editor;
    std::vector<std::string> changes;
    // Set by a successful Apply, cleared by the next edit or folder change.
    bool applied = false;
    std::string error;

    SaveEditor* Editor() {
        auto active = Game::Save::ActiveSaveManager();
        if (active != manager) {
            manager = active;
            editor = manager ? std::make_unique<SaveEditor>(manager) : nullptr;
            applied = false;
            error.clear();
        }
        return editor.get();
    }

    // Called from every edit.
    void Edited() {
        applied = false;
        error.clear();
    }
};

void NeedsGame(const std::shared_ptr<State>& state, Widget& widget) {
    if (state->Editor() == nullptr) {
        widget.disabled = true;
        widget.disabledReason = kNoGame;
    }
}

void RegisterFolder(Registry& registry, const std::shared_ptr<State>& state, const std::function<void()>& reset) {
    const std::string sidebar = "Save file";
    registry.AddSidebar(kTab, sidebar);

    registry.AddWidget(kTab, sidebar,
                       "Edits here change a copy of the folder's save. Nothing is written until Apply, and the "
                       "game has to be restarted afterwards to pick the change up.",
                       WidgetType::Text);

    auto folder = registry.AddWidget(kTab, sidebar, "Folder", WidgetType::Combo);
    Widget* folderWidget = &folder.Get();
    folder.Options({ "Folder 1", "Folder 2", "Folder 3", "Folder 4" })
        .Tooltip("Switching folders drops unapplied edits.")
        .PreFunc([state](Widget& widget) {
            NeedsGame(state, widget);
            if (SaveEditor* editor = state->Editor()) {
                widget.intValue = editor->Folder();
            }
        })
        .Callback([state, folderWidget] {
            if (SaveEditor* editor = state->Editor()) {
                editor->SelectFolder(folderWidget->intValue);
                state->Edited();
            }
        });

    registry.AddWidget(kTab, sidebar, "Pending changes", WidgetType::Section);
    registry.AddWidget(kTab, sidebar, "No changes.", WidgetType::Text).PreFunc([state](Widget& widget) {
        SaveEditor* editor = state->Editor();
        widget.hidden = editor != nullptr && editor->IsDirty();
        // One pass per frame computes the list the rows below show.
        state->changes = editor != nullptr ? editor->Changes() : std::vector<std::string>{};
    });
    for (size_t row = 0; row < kChangeRows; row++) {
        registry.AddWidget(kTab, sidebar, "", WidgetType::Text).PreFunc([state, row](Widget& widget) {
            const auto& changes = state->changes;
            if (row >= changes.size()) {
                widget.hidden = true;
            } else if (row + 1 == kChangeRows && changes.size() > kChangeRows) {
                widget.name = "...and " + std::to_string(changes.size() - kChangeRows + 1) + " more";
            } else {
                widget.name = changes[row];
            }
        });
    }

    registry.AddWidget(kTab, sidebar, "Apply", WidgetType::Button)
        .Tooltip("Writes the edits to the folder's save.")
        .PreFunc([state](Widget& widget) {
            SaveEditor* editor = state->Editor();
            if (editor == nullptr || !editor->IsDirty()) {
                widget.disabled = true;
                widget.disabledReason = editor == nullptr ? kNoGame : "No changes to apply.";
            }
        })
        .Callback([state] {
            SaveEditor* editor = state->Editor();
            if (editor == nullptr) {
                return;
            }
            state->error.clear();
            state->applied = editor->Apply();
            if (!state->applied) {
                state->error = editor->LastError();
            }
        });
    registry.AddWidget(kTab, sidebar, "Revert", WidgetType::Button)
        .Tooltip("Drops the edits and reloads the folder's save.")
        .PreFunc([state](Widget& widget) {
            SaveEditor* editor = state->Editor();
            if (editor == nullptr || !editor->IsDirty()) {
                widget.disabled = true;
                widget.disabledReason = editor == nullptr ? kNoGame : "No changes to revert.";
            }
        })
        .Callback([state] {
            if (SaveEditor* editor = state->Editor()) {
                editor->Revert();
                state->Edited();
            }
        });

    registry.AddWidget(kTab, sidebar, "Saved. Restart the game for the change to take effect.", WidgetType::Text)
        .PreFunc([state](Widget& widget) { widget.hidden = !state->applied; });
    registry.AddWidget(kTab, sidebar, "Restart now", WidgetType::Button)
        .Tooltip("Resets the game from the legal screen so it reads the saved change.")
        .PreFunc([state, reset](Widget& widget) {
            widget.hidden = !state->applied;
            if (!reset) {
                widget.disabled = true;
                widget.disabledReason = "Restart isn't available here; restart the game by hand.";
            }
        })
        .Callback([state, reset] {
            if (reset) {
                state->applied = false;
                reset();
            }
        });
    registry.AddWidget(kTab, sidebar, "", WidgetType::Text).PreFunc([state](Widget& widget) {
        widget.hidden = state->error.empty();
        widget.name = "Couldn't save: " + state->error;
    });
}

// A widget whose value mirrors the editor each frame and writes back through
// `apply` when changed.
WidgetBuilder AddEditorWidget(Registry& registry, const std::shared_ptr<State>& state, const std::string& sidebar,
                              const std::string& name, WidgetType type, std::function<int(SaveEditor&)> read,
                              std::function<void(SaveEditor&, int)> apply) {
    auto builder = registry.AddWidget(kTab, sidebar, name, type);
    Widget* widget = &builder.Get();
    builder
        .PreFunc([state, read = std::move(read)](Widget& self) {
            NeedsGame(state, self);
            if (SaveEditor* editor = state->Editor()) {
                self.intValue = read(*editor);
            }
        })
        .Callback([state, widget, apply = std::move(apply)] {
            if (SaveEditor* editor = state->Editor()) {
                apply(*editor, widget->intValue);
                state->Edited();
            }
        });
    return builder;
}

void RegisterCheats(Registry& registry, const std::shared_ptr<State>& state) {
    const std::string sidebar = "Cheats";
    registry.AddSidebar(kTab, sidebar, 2);
    registry.AddWidget(kTab, sidebar,
                       "Each cheat a level awards for beating its target time. The Magnum, Laser and Golden Gun "
                       "have no cheat bit; see Completion.",
                       WidgetType::Text);
    for (int32_t level = Game::Save::kLevelDam; level < Game::Save::kSoloLevelCount; level++) {
        if (std::string(Game::Save::RewardCheatName(level)) == "?") {
            continue;
        }
        AddEditorWidget(
            registry, state, sidebar,
            std::string(Game::Save::RewardCheatName(level)) + " (" + Game::Save::LevelName(level) + ")",
            WidgetType::Checkbox, [level](SaveEditor& editor) { return editor.IsCheatUnlocked(level) ? 1 : 0; },
            [level](SaveEditor& editor, int value) { editor.SetCheatUnlocked(level, value != 0); })
            .Column(level % 2);
    }
}

void RegisterCompletion(Registry& registry, const std::shared_ptr<State>& state) {
    const std::string sidebar = "Completion";
    registry.AddSidebar(kTab, sidebar);
    registry.AddWidget(kTab, sidebar,
                       "Completing a level on a difficulty completes it on the easier ones, as the game does. "
                       "Times are in seconds.",
                       WidgetType::Text);
    for (int32_t difficulty = Game::Save::kDifficultyAgent; difficulty <= Game::Save::kDifficulty00; difficulty++) {
        const std::string name = Game::Save::DifficultyName(difficulty);
        registry.AddWidget(kTab, sidebar, "Complete every level on " + name, WidgetType::Button)
            .PreFunc([state](Widget& widget) { NeedsGame(state, widget); })
            .Callback([state, difficulty] {
                if (SaveEditor* editor = state->Editor()) {
                    editor->CompleteAll(difficulty);
                    state->Edited();
                }
            });
        registry.AddWidget(kTab, sidebar, "Clear every level on " + name, WidgetType::Button)
            .Tooltip("Also clears the harder difficulties.")
            .PreFunc([state](Widget& widget) { NeedsGame(state, widget); })
            .Callback([state, difficulty] {
                if (SaveEditor* editor = state->Editor()) {
                    editor->ClearAll(difficulty);
                    state->Edited();
                }
            });
    }

    for (int32_t level = Game::Save::kLevelDam; level < Game::Save::kSoloLevelCount; level++) {
        const std::string levelName = Game::Save::LevelName(level);
        auto header = registry.AddWidget(kTab, sidebar, levelName, WidgetType::Header);
        for (int32_t difficulty = Game::Save::kDifficultyAgent; difficulty <= Game::Save::kDifficulty00; difficulty++) {
            const std::string label = levelName + ", " + Game::Save::DifficultyName(difficulty);
            auto completed = header.Child(label + ": completed", WidgetType::Checkbox);
            Widget* completedWidget = &completed.Get();
            completed
                .PreFunc([state, level, difficulty](Widget& self) {
                    NeedsGame(state, self);
                    if (SaveEditor* editor = state->Editor()) {
                        self.intValue = editor->IsStageCompleted(level, difficulty) ? 1 : 0;
                    }
                })
                .Callback([state, completedWidget, level, difficulty] {
                    if (SaveEditor* editor = state->Editor()) {
                        editor->SetStageCompleted(level, difficulty, completedWidget->intValue != 0);
                        state->Edited();
                    }
                });

            auto time = header.Child(label + ": best time (seconds)", WidgetType::SliderInt);
            Widget* timeWidget = &time.Get();
            time.Range(SaveEditor::kMinTime, SaveEditor::kMaxTime, 1)
                .Default(SaveEditor::kMinTime)
                .Format("%d s")
                .PreFunc([state, level, difficulty](Widget& self) {
                    NeedsGame(state, self);
                    if (SaveEditor* editor = state->Editor()) {
                        const int32_t seconds = editor->StageTime(level, difficulty);
                        self.intValue = seconds != 0 ? seconds : SaveEditor::kMinTime;
                        if (seconds == 0) {
                            self.disabled = true;
                            self.disabledReason = "Not completed on this difficulty.";
                        }
                    }
                })
                .Callback([state, timeWidget, level, difficulty] {
                    if (SaveEditor* editor = state->Editor()) {
                        editor->SetStageTime(level, difficulty, timeWidget->intValue);
                        state->Edited();
                    }
                });
        }
    }
}

void RegisterFlags(Registry& registry, const std::shared_ptr<State>& state) {
    const std::string sidebar = "Game flags";
    registry.AddSidebar(kTab, sidebar);
    registry.AddWidget(kTab, sidebar, "007 mode flag", WidgetType::Section);
    AddEditorWidget(
        registry, state, sidebar, "007 mode unlocked flag", WidgetType::Checkbox,
        [](SaveEditor& editor) { return editor.Flag007() ? 1 : 0; },
        [](SaveEditor& editor, int value) { editor.SetFlag007(value != 0); })
        .Tooltip("007 mode is also open once every level is completed on 00 Agent, whatever this says.");
    registry.AddWidget(kTab, sidebar, "", WidgetType::Text).PreFunc([state](Widget& widget) {
        SaveEditor* editor = state->Editor();
        widget.hidden = editor == nullptr;
        if (editor != nullptr) {
            widget.name =
                std::string("007 mode is ") + (editor->Is007ModeUnlocked() ? "open" : "locked") + " with these edits.";
        }
    });

    registry.AddWidget(kTab, sidebar, "Derived from completions", WidgetType::Section);
    registry.AddWidget(kTab, sidebar,
                       "The expanded intro cast and the Magnum, Laser and Golden Gun come from level completions, "
                       "not their own flags. Set them on the Completion sidebar.",
                       WidgetType::Text);
    registry.AddWidget(kTab, sidebar, "", WidgetType::Text).PreFunc([state](Widget& widget) {
        SaveEditor* editor = state->Editor();
        widget.hidden = editor == nullptr;
        if (editor != nullptr) {
            auto yesNo = [](bool value) { return value ? "yes" : "no"; };
            widget.name = std::string("Cradle completed: ") + yesNo(editor->CradleCompleted()) +
                          ". Aztec on Secret Agent or 00 Agent: " + yesNo(editor->AztecCompletedOnSecretOr00()) +
                          ". Egypt on 00 Agent: " + yesNo(editor->EgyptCompletedOn00()) + ".";
        }
    });
}

} // namespace

void RegisterDevToolsTab(Registry& registry, std::function<void()> reset,
                         std::function<void(const Game::MultiplayerSettings&)> startMultiplayer) {
    auto state = std::make_shared<State>();
    registry.AddTab(kTab);
    RegisterMultiplayerStart(registry, startMultiplayer);
    RegisterFolder(registry, state, reset);
    RegisterCheats(registry, state);
    RegisterCompletion(registry, state);
    RegisterFlags(registry, state);
}

} // namespace Frigate::ConfigMenu
