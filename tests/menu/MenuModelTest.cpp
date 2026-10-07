#include "menu/MenuModel.h"

#include <cstdio>

#include "menu/Tabs.h"
#include "menu/CVars.h"
#include "menu/tabs/ControlsTab.h"

using namespace Frigate::ConfigMenu;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

void TestRegistration() {
    Registry registry;
    registry.AddTab("Alpha");
    registry.AddTab("Beta");
    Check(registry.Tabs().size() == 2 && registry.Tabs()[0]->name == "Alpha", "tabs keep registration order");

    registry.AddTab("Alpha");
    Check(registry.Tabs().size() == 2, "adding a tab twice returns the first");

    registry.AddSidebar("Alpha", "One", 2);
    registry.AddSidebar("Alpha", "One", 3);
    Tab* alpha = registry.FindTab("Alpha");
    Check(alpha->sidebars.size() == 1 && alpha->sidebars[0]->columns == 2, "a repeated sidebar is kept as first made");

    registry.AddWidget("Gamma", "Auto", "Made on demand", WidgetType::Button);
    Check(registry.FindTab("Gamma") != nullptr && registry.FindTab("Gamma")->FindSidebar("Auto") != nullptr,
          "a widget creates its tab and sidebar");
    Check(registry.FindTab("Nope") == nullptr, "an unknown tab is null");

    Registry zero;
    Check(zero.AddSidebar("T", "S", 0).columns == 1, "a sidebar has at least one column");
}

void TestBuilder() {
    Registry registry;
    auto builder = registry.AddWidget("T", "S", "Volume", WidgetType::SliderInt)
                       .CVar("gVolume")
                       .Range(0, 10, 2)
                       .Default(4)
                       .Format("%d%%")
                       .Tooltip("tip")
                       .Column(1);
    Widget& widget = builder.Get();
    Check(widget.cvar == "gVolume" && widget.intMin == 0 && widget.intMax == 10 && widget.intStep == 2,
          "the builder sets cvar and range");
    Check(widget.intDefault == 4 && widget.format == "%d%%" && widget.tooltip == "tip" && widget.column == 1,
          "the builder sets default, format, tooltip and column");

    Sidebar* sidebar = registry.FindTab("T")->FindSidebar("S");
    Check(sidebar->widgets.size() == 1 && sidebar->widgets[0].get() == &widget, "the widget lives in its sidebar");

    auto header = registry.AddWidget("T", "S", "More", WidgetType::Header);
    header.Child("Inner", WidgetType::Checkbox);
    Check(header.Get().children.size() == 1 && header.Get().children[0]->name == "Inner", "children nest");
}

void TestValues() {
    MemoryValueStore store;
    Registry registry;
    Widget& slider =
        registry.AddWidget("T", "S", "Level", WidgetType::SliderInt).CVar("gLevel").Range(0, 10, 3).Default(5).Get();
    Check(slider.GetInt(store) == 5, "an unset CVar reads as the default");

    slider.StepInt(store, 1);
    Check(slider.GetInt(store) == 8, "stepping adds the step");
    slider.StepInt(store, 1);
    Check(slider.GetInt(store) == 10, "stepping stops at the maximum");
    slider.SetInt(store, -4);
    Check(slider.GetInt(store) == 0, "setting clamps to the minimum");
    slider.ResetToDefault(store);
    Check(slider.GetInt(store) == 5, "reset restores the default");

    int calls = 0;
    slider.callback = [&calls] { calls++; };
    slider.SetInt(store, 2);
    slider.StepInt(store, -1);
    Check(calls == 2, "the callback runs on each change");

    Widget& fader = registry.AddWidget("T", "S", "Fade", WidgetType::SliderFloat)
                        .CVar("gFade")
                        .Range(0.0f, 1.0f, 0.25f)
                        .Default(0.5f)
                        .Get();
    fader.StepFloat(store, 1);
    fader.StepFloat(store, 1);
    fader.StepFloat(store, 1);
    Check(fader.GetFloat(store) == 1.0f, "a float slider clamps too");

    Widget& local = registry.AddWidget("T", "S", "Local", WidgetType::Checkbox).Default(1).Get();
    Check(local.GetInt(store) == 1, "a widget with no CVar reads its own value");
    local.SetInt(store, 0);
    Check(local.GetInt(store) == 0 && store.GetInt("", -1) == -1, "and writes it there, not to the store");
}

void TestDisabledAndHidden() {
    Registry registry;
    bool locked = true;
    bool secret = true;
    Widget& gated = registry.AddWidget("T", "S", "Gated", WidgetType::Checkbox)
                        .DisabledWhen([&] { return locked; }, "locked")
                        .Get();
    Widget& hiding =
        registry.AddWidget("T", "S", "Hiding", WidgetType::Checkbox).HiddenWhen([&] { return secret; }).Get();
    Widget& always = registry.AddWidget("T", "S", "Always", WidgetType::Button).Disabled("never available").Get();
    Widget& plain = registry.AddWidget("T", "S", "Plain", WidgetType::Button).Get();

    registry.Refresh();
    Check(gated.disabled && gated.disabledReason == "locked", "a predicate disables with its reason");
    Check(hiding.hidden && !plain.hidden && !plain.disabled, "a predicate hides, and others are untouched");
    Check(always.disabled && always.disabledReason == "never available", "Disabled is always on");

    locked = false;
    secret = false;
    registry.Refresh();
    Check(!gated.disabled && gated.disabledReason.empty(), "disabled clears when the predicate does");
    Check(!hiding.hidden, "hidden clears when the predicate does");

    int seen = 0;
    plain.preFunc = [&seen](Widget& self) {
        seen++;
        self.disabled = true;
        self.disabledReason = "from preFunc";
    };
    registry.Refresh();
    Check(seen == 1 && plain.disabled && plain.disabledReason == "from preFunc",
          "preFunc runs each frame and can disable");
    plain.preFunc = nullptr;
    registry.Refresh();
    Check(!plain.disabled, "state is recomputed every frame, not latched");
}

void TestContainerDisabling() {
    Registry registry;
    auto group = registry.AddWidget("T", "S", "Box", WidgetType::Group).Disabled("box locked");
    Widget& inner = group.Child("Inside", WidgetType::Checkbox).Get();
    Widget& own = group.Child("Own reason", WidgetType::Checkbox).Disabled("child locked").Get();
    registry.Refresh();
    Check(inner.disabled && inner.disabledReason == "box locked", "a disabled container disables its children");
    Check(own.disabledReason == "child locked", "a child's own reason wins");
}

void TestMatchesQuery() {
    Check(MatchesQuery("Background opacity", ""), "an empty query matches");
    Check(MatchesQuery("Background opacity", "OPAC"), "matching ignores case");
    Check(MatchesQuery("Background opacity", "opac back"), "words match in any order");
    Check(!MatchesQuery("Background opacity", "opac zzz"), "every word must match");
    Check(MatchesQuery("Background opacity", "  back  "), "extra spaces are ignored");
}

void TestSearch() {
    Registry registry;
    registry.AddWidget("Audio", "Mix", "Music volume", WidgetType::SliderInt);
    registry.AddWidget("Audio", "Mix", "Mix heading", WidgetType::Section);
    registry.AddWidget("Audio", "Mix", "Volume note", WidgetType::Text);
    registry.AddWidget("Video", "Display", "Screen volume", WidgetType::SliderInt);
    registry.AddWidget("Video", "Display", "Hidden volume", WidgetType::SliderInt).HiddenWhen([] { return true; });
    auto group = registry.AddWidget("Video", "Display", "Volume box", WidgetType::Group);
    group.Child("Nested volume", WidgetType::Checkbox);
    registry.AddWidget("Gone", "Gone side", "Gone volume", WidgetType::Checkbox);
    registry.FindTab("Gone")->hiddenWhen = [] { return true; };
    registry.Refresh();

    auto results = registry.Search("volume");
    Check(results.size() == 3, "search finds leaf widgets across tabs, skipping structure, hidden and hidden tabs");
    if (results.size() == 3) {
        Check(results[0].path.back()->name == "Music volume" && results[0].tab->name == "Audio" &&
                  results[0].sidebar->name == "Mix",
              "results come in registration order with their location");
        Check(results[1].path.back()->name == "Screen volume", "across tabs");
        Check(results[2].path.size() == 2 && results[2].path[0]->name == "Volume box" &&
                  results[2].path[1]->name == "Nested volume",
              "a nested match carries its containers");
    }
    Check(registry.Search("zzz").empty(), "no match gives no results");
    Check(registry.Search("screen vol").size() == 1, "multi-word queries narrow");
}

void TestNavigation() {
    Registry registry;
    registry.AddWidget("A", "A1", "x", WidgetType::Button);
    registry.AddWidget("A", "A2", "y", WidgetType::Button);
    registry.AddWidget("B", "B1", "z", WidgetType::Button);
    registry.AddWidget("C", "C1", "w", WidgetType::Button);

    Navigation nav;
    nav.Validate(registry);
    Check(nav.TabIndex() == 0 && nav.SidebarIndex() == 0, "starts on the first tab and sidebar");

    nav.SelectSidebar(registry, 1);
    Check(nav.SidebarIndex() == 1, "selects a sidebar");
    nav.SelectTab(registry, 1);
    Check(nav.TabIndex() == 1 && nav.SidebarIndex() == 0, "a new tab starts on its first sidebar");
    nav.SelectTab(registry, 9);
    Check(nav.TabIndex() == 1, "an out-of-range tab is ignored");

    nav.CycleTab(registry, 1);
    nav.CycleTab(registry, 1);
    Check(nav.TabIndex() == 0, "cycling wraps forward");
    nav.CycleTab(registry, -1);
    Check(nav.TabIndex() == 2, "cycling wraps back");

    registry.FindTab("B")->hiddenWhen = [] { return true; };
    nav.SelectTab(registry, 0);
    nav.CycleTab(registry, 1);
    Check(nav.TabIndex() == 2, "cycling skips hidden tabs");
    nav.SelectTab(registry, 1);
    Check(nav.TabIndex() == 2, "a hidden tab can't be selected");

    registry.FindTab("C")->hiddenWhen = [] { return true; };
    nav.Validate(registry);
    Check(nav.TabIndex() == 0, "a tab that becomes hidden moves the selection");

    registry.FindTab("A")->sidebars[0]->hiddenWhen = [] { return true; };
    nav.Validate(registry);
    Check(nav.SidebarIndex() == 1, "so does a hidden sidebar");
}

void TestRegisteredTabs() {
    Registry registry;
    RegisterTabs(registry, std::make_shared<System>());
    Check(!registry.Tabs().empty(), "the real tabs register");
    for (const auto& tab : registry.Tabs()) {
        Check(!tab->sidebars.empty(), "every registered tab has a sidebar");
        for (const auto& sidebar : tab->sidebars) {
            Check(!sidebar->widgets.empty(), "every registered sidebar has widgets");
        }
    }
    Check(!registry.Search("opacity").empty(), "registered widgets are searchable");
}

void TestControlsTab() {
    Registry registry;
    RegisterTabs(registry, std::make_shared<System>());
    Tab* tab = registry.FindTab("Controls");
    Check(tab != nullptr, "the Controls tab registers");
    if (tab == nullptr) {
        return;
    }
    Widget* editor = nullptr;
    Widget* fallback = nullptr;
    for (const auto& widget : tab->sidebars[0]->widgets) {
        if (widget->type == WidgetType::Custom) {
            editor = widget.get();
        } else if (widget->type == WidgetType::Text) {
            fallback = widget.get();
        }
    }
    Check(editor != nullptr && editor->draw != nullptr, "the editor widget has a draw function");
    Check(fallback != nullptr, "there is a fallback message");
    if (editor == nullptr || fallback == nullptr) {
        return;
    }

    int drawn = 0;
    editor->draw();
    Check(drawn == 0, "drawing without an editor does nothing");
    registry.Refresh();
    Check(!fallback->hidden, "the fallback shows without an editor");

    SetInputEditorDraw([&drawn] { drawn++; });
    editor->draw();
    registry.Refresh();
    Check(drawn == 1, "the editor widget draws the supplied editor");
    Check(fallback->hidden, "the fallback hides once an editor is supplied");
    SetInputEditorDraw(nullptr);
}

void TestControlStyle() {
    struct StyleSystem : System {
        void ApplyControlStyle() override {
            applied++;
        }
        int applied = 0;
    };
    auto system = std::make_shared<StyleSystem>();
    Registry registry;
    RegisterTabs(registry, system);
    Tab* tab = registry.FindTab("Controls");
    Sidebar* sidebar = tab != nullptr ? tab->FindSidebar("Control Style") : nullptr;
    Check(sidebar != nullptr && sidebar->widgets.size() == 1, "the Controls tab has a control style combo");
    if (sidebar == nullptr || sidebar->widgets.empty()) {
        return;
    }
    Widget& combo = *sidebar->widgets[0];
    Check(combo.type == WidgetType::Combo && combo.options.size() == 9, "one choice per style plus the folder's");
    Check(combo.cvar == kControlStyleCVar && combo.intDefault == 0, "it defaults to the folder's option");
    combo.callback();
    Check(system->applied == 1, "choosing a style pushes it to the game");
}

Widget* FindControls(Registry& registry, const std::string& name) {
    for (auto& widget : registry.FindTab("Controls")->FindSidebar("Bindings")->widgets) {
        if (widget->name == name) {
            return widget.get();
        }
    }
    return nullptr;
}

void TestControllerPorts() {
    struct PortSystem : System {
        std::vector<std::string> GamepadNames() override {
            return { "Pad A", "Pad B" };
        }
        void ApplyControllerPorts() override {
            applied++;
        }
        void SwapControllerPorts(int a, int b) override {
            swapped = { a, b };
        }
        int applied = 0;
        std::pair<int, int> swapped = { -1, -1 };
    };
    auto system = std::make_shared<PortSystem>();
    Registry registry;
    RegisterTabs(registry, system);
    registry.Refresh();

    // Ports 3 and 4 are registered exactly like port 2.
    for (int port = 1; port <= 4; port++) {
        const std::string label = "Port " + std::to_string(port);
        Widget* connected = FindControls(registry, label + " connected");
        Widget* device = FindControls(registry, label + " device");
        Check(connected != nullptr && connected->type == WidgetType::Checkbox, "each port has a connected checkbox");
        Check(device != nullptr && device->type == WidgetType::Combo, "each port has a device combo");
        if (connected == nullptr || device == nullptr) {
            return;
        }
        Check(connected->cvar == kPortConnectedCVars[static_cast<size_t>(port - 1)], "connected has its own CVar");
        Check(device->cvar == kPortDeviceCVars[static_cast<size_t>(port - 1)], "device has its own CVar");
        Check(device->options.size() == 5 && device->options[3] == "Gamepad 1: Pad A",
              "the combo lists the fixed choices and each attached gamepad");
        connected->callback();
        device->callback();
    }
    Check(system->applied == 8, "changing a port pushes it to the game");

    Widget* swap = FindControls(registry, "Swap devices");
    Check(swap != nullptr && swap->type == WidgetType::Button, "there is a swap button");
    FindControls(registry, "Swap port")->intValue = 2;
    FindControls(registry, "With port")->intValue = 3;
    swap->callback();
    Check(system->swapped == std::make_pair(2, 3), "the swap button passes the chosen ports");
}

} // namespace

int main() {
    TestRegistration();
    TestBuilder();
    TestValues();
    TestDisabledAndHidden();
    TestContainerDisabling();
    TestMatchesQuery();
    TestSearch();
    TestNavigation();
    TestRegisteredTabs();
    TestControlsTab();
    TestControlStyle();
    TestControllerPorts();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    return 0;
}
