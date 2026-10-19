#pragma once

// The menu's data model: tabs, sidebars and widget descriptors, plus search
// and the hidden/disabled logic. No ImGui and no libultraship here; MenuDraw
// renders it and CVarStore backs it with CVars. How to add to it: docs/menu.md.

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frigate::ConfigMenu {

enum class WidgetType {
    Checkbox,
    SliderInt,
    SliderFloat,
    Combo,
    Button,
    // Wrapped static text; the widget's name is the text.
    Text,
    // A titled divider.
    Section,
    // A collapsible header holding child widgets.
    Header,
    // A bordered box holding child widgets.
    Group,
    // Drawn by the widget's own draw function.
    Custom,
};

// Where widgets with a CVar keep their value. CVarStore backs it with
// libultraship's console variables; tests use MemoryValueStore.
class ValueStore {
  public:
    virtual ~ValueStore() = default;
    virtual int GetInt(const std::string& name, int fallback) = 0;
    virtual void SetInt(const std::string& name, int value) = 0;
    virtual float GetFloat(const std::string& name, float fallback) = 0;
    virtual void SetFloat(const std::string& name, float value) = 0;
};

class MemoryValueStore : public ValueStore {
  public:
    int GetInt(const std::string& name, int fallback) override;
    void SetInt(const std::string& name, int value) override;
    float GetFloat(const std::string& name, float fallback) override;
    void SetFloat(const std::string& name, float value) override;

  private:
    std::unordered_map<std::string, int> mInts;
    std::unordered_map<std::string, float> mFloats;
};

struct Widget {
    std::string name;
    WidgetType type = WidgetType::Text;

    // Empty: the value lives in intValue/floatValue instead of a CVar.
    std::string cvar;
    int intValue = 0;
    float floatValue = 0.0f;
    int intDefault = 0;
    float floatDefault = 0.0f;
    int intMin = 0;
    int intMax = 100;
    int intStep = 1;
    float floatMin = 0.0f;
    float floatMax = 1.0f;
    float floatStep = 0.01f;
    // printf-style, for the slider's value readout; empty picks a default.
    std::string format;
    std::vector<std::string> options;
    std::string tooltip;
    // A Button with a message asks for confirmation before running its callback.
    std::string confirm;
    // Which of the sidebar's columns the widget goes in.
    int column = 0;
    bool defaultOpen = false;

    std::function<void()> callback;
    // Runs every frame before the widget is drawn; may change `hidden`,
    // `disabled` and `disabledReason` (or the value) to react to game state.
    std::function<void(Widget&)> preFunc;
    std::function<void()> draw;
    std::function<bool()> hiddenWhen;
    std::function<bool()> disabledWhen;
    std::string disabledWhenReason;

    bool hidden = false;
    bool disabled = false;
    // Shown as a tooltip while the widget is disabled.
    std::string disabledReason;

    std::vector<std::unique_ptr<Widget>> children;

    // Recomputes hidden/disabled for this frame: both start clear, then
    // preFunc runs, then hiddenWhen/disabledWhen can set them. Children are
    // refreshed too, and a disabled parent disables them.
    void Refresh(bool parentDisabled = false, const std::string& parentReason = {});

    bool IsContainer() const {
        return type == WidgetType::Header || type == WidgetType::Group;
    }

    int GetInt(ValueStore& store) const;
    float GetFloat(ValueStore& store) const;
    // Clamp to the widget's range, store, then run the callback.
    void SetInt(ValueStore& store, int value);
    void SetFloat(ValueStore& store, float value);
    // Nudge by one step; `direction` is +1 or -1.
    void StepInt(ValueStore& store, int direction);
    void StepFloat(ValueStore& store, int direction);
    void ResetToDefault(ValueStore& store);
};

struct Sidebar {
    std::string name;
    int columns = 1;
    std::function<bool()> hiddenWhen;
    std::vector<std::unique_ptr<Widget>> widgets;

    bool IsHidden() const {
        return hiddenWhen && hiddenWhen();
    }
};

struct Tab {
    std::string name;
    std::function<bool()> hiddenWhen;
    std::vector<std::unique_ptr<Sidebar>> sidebars;

    bool IsHidden() const {
        return hiddenWhen && hiddenWhen();
    }
    Sidebar* FindSidebar(const std::string& name);
    const Sidebar* FindSidebar(const std::string& name) const;
};

// Chainable setters over a registered widget. Holds a pointer into the
// registry, which owns the widget for its whole life.
class WidgetBuilder {
  public:
    explicit WidgetBuilder(Widget* widget) : mWidget(widget) {
    }

    WidgetBuilder& CVar(const std::string& cvar);
    WidgetBuilder& Options(std::vector<std::string> options);
    WidgetBuilder& Range(int min, int max, int step = 1);
    WidgetBuilder& Range(float min, float max, float step = 0.01f);
    WidgetBuilder& Default(int value);
    WidgetBuilder& Default(float value);
    WidgetBuilder& Format(const std::string& format);
    WidgetBuilder& Tooltip(const std::string& text);
    WidgetBuilder& Confirm(const std::string& message);
    WidgetBuilder& Column(int column);
    WidgetBuilder& DefaultOpen(bool open = true);
    WidgetBuilder& Callback(std::function<void()> callback);
    WidgetBuilder& PreFunc(std::function<void(Widget&)> preFunc);
    WidgetBuilder& Draw(std::function<void()> draw);
    WidgetBuilder& HiddenWhen(std::function<bool()> predicate);
    // Always disabled, with the reason as its tooltip.
    WidgetBuilder& Disabled(const std::string& reason);
    WidgetBuilder& DisabledWhen(std::function<bool()> predicate, const std::string& reason);
    // Adds a widget inside a Header or Group.
    WidgetBuilder Child(const std::string& name, WidgetType type);

    Widget& Get() {
        return *mWidget;
    }

  private:
    Widget* mWidget;
};

// A widget found by a search, with where it lives.
struct SearchResult {
    Tab* tab;
    Sidebar* sidebar;
    // The containers the match sits in, outermost first, then the match
    // itself, so a result can be drawn in context.
    std::vector<Widget*> path;
};

// True if every whitespace-separated word of `query` appears in `text`,
// ignoring case. An empty query matches everything.
bool MatchesQuery(const std::string& text, const std::string& query);

class Registry {
  public:
    // Adds a tab, or returns the existing one of that name, so several files
    // can add sidebars to the same tab.
    Tab& AddTab(const std::string& name);
    // The tab is created if it doesn't exist. Returns the existing sidebar
    // of that name if there is one (its column count is left alone).
    Sidebar& AddSidebar(const std::string& tab, const std::string& name, int columns = 1);
    // The tab and sidebar are created if they don't exist.
    WidgetBuilder AddWidget(const std::string& tab, const std::string& sidebar, const std::string& name,
                            WidgetType type);

    const std::vector<std::unique_ptr<Tab>>& Tabs() const {
        return mTabs;
    }
    Tab* FindTab(const std::string& name);
    const Tab* FindTab(const std::string& name) const;

    // Refreshes every widget; call once per frame before drawing.
    void Refresh();

    // Widgets whose name matches `query`, in registration order. Hidden
    // widgets and widgets in hidden tabs or sidebars are skipped, as are
    // structural entries (sections, headers, groups, text); a widget inside a
    // container is found by its own name.
    std::vector<SearchResult> Search(const std::string& query);

  private:
    std::vector<std::unique_ptr<Tab>> mTabs;
};

// Which tab and sidebar the menu is showing, kept valid as tabs and sidebars
// are hidden by their predicates.
class Navigation {
  public:
    // Moves to the first visible tab/sidebar when the current one is out of
    // range or hidden.
    void Validate(const Registry& registry);
    void SelectTab(const Registry& registry, size_t index);
    void SelectSidebar(const Registry& registry, size_t index);
    // Next or previous visible tab, wrapping; `direction` is +1 or -1.
    void CycleTab(const Registry& registry, int direction);

    size_t TabIndex() const {
        return mTab;
    }
    size_t SidebarIndex() const {
        return mSidebar;
    }

  private:
    size_t mTab = 0;
    size_t mSidebar = 0;
};

} // namespace Frigate::ConfigMenu
