#include "menu/MenuModel.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace Frigate::ConfigMenu {

int MemoryValueStore::GetInt(const std::string& name, int fallback) {
    const auto found = mInts.find(name);
    return found == mInts.end() ? fallback : found->second;
}

void MemoryValueStore::SetInt(const std::string& name, int value) {
    mInts[name] = value;
}

float MemoryValueStore::GetFloat(const std::string& name, float fallback) {
    const auto found = mFloats.find(name);
    return found == mFloats.end() ? fallback : found->second;
}

void MemoryValueStore::SetFloat(const std::string& name, float value) {
    mFloats[name] = value;
}

void Widget::Refresh(bool parentDisabled, const std::string& parentReason) {
    hidden = false;
    disabled = false;
    disabledReason.clear();

    if (preFunc) {
        preFunc(*this);
    }
    if (hiddenWhen && hiddenWhen()) {
        hidden = true;
    }
    if (disabledWhen && disabledWhen()) {
        disabled = true;
        disabledReason = disabledWhenReason;
    }
    if (parentDisabled && !disabled) {
        disabled = true;
        disabledReason = parentReason;
    }

    for (auto& child : children) {
        child->Refresh(disabled, disabledReason);
    }
}

int Widget::GetInt(ValueStore& store) const {
    return cvar.empty() ? intValue : store.GetInt(cvar, intDefault);
}

float Widget::GetFloat(ValueStore& store) const {
    return cvar.empty() ? floatValue : store.GetFloat(cvar, floatDefault);
}

void Widget::SetInt(ValueStore& store, int value) {
    // A checkbox or combo has no range of its own; sliders do.
    if (type == WidgetType::SliderInt) {
        value = std::clamp(value, intMin, intMax);
    }
    if (cvar.empty()) {
        intValue = value;
    } else {
        store.SetInt(cvar, value);
    }
    if (callback) {
        callback();
    }
}

void Widget::SetFloat(ValueStore& store, float value) {
    value = std::clamp(value, floatMin, floatMax);
    if (cvar.empty()) {
        floatValue = value;
    } else {
        store.SetFloat(cvar, value);
    }
    if (callback) {
        callback();
    }
}

void Widget::StepInt(ValueStore& store, int direction) {
    SetInt(store, GetInt(store) + direction * intStep);
}

void Widget::StepFloat(ValueStore& store, int direction) {
    SetFloat(store, GetFloat(store) + static_cast<float>(direction) * floatStep);
}

void Widget::ResetToDefault(ValueStore& store) {
    if (type == WidgetType::SliderFloat) {
        SetFloat(store, floatDefault);
    } else {
        SetInt(store, intDefault);
    }
}

Sidebar* Tab::FindSidebar(const std::string& sidebarName) {
    for (auto& sidebar : sidebars) {
        if (sidebar->name == sidebarName) {
            return sidebar.get();
        }
    }
    return nullptr;
}

const Sidebar* Tab::FindSidebar(const std::string& sidebarName) const {
    return const_cast<Tab*>(this)->FindSidebar(sidebarName);
}

WidgetBuilder& WidgetBuilder::CVar(const std::string& cvar) {
    mWidget->cvar = cvar;
    return *this;
}

WidgetBuilder& WidgetBuilder::Options(std::vector<std::string> options) {
    mWidget->options = std::move(options);
    return *this;
}

WidgetBuilder& WidgetBuilder::Range(int min, int max, int step) {
    mWidget->intMin = min;
    mWidget->intMax = max;
    mWidget->intStep = step;
    return *this;
}

WidgetBuilder& WidgetBuilder::Range(float min, float max, float step) {
    mWidget->floatMin = min;
    mWidget->floatMax = max;
    mWidget->floatStep = step;
    return *this;
}

WidgetBuilder& WidgetBuilder::Default(int value) {
    mWidget->intDefault = value;
    mWidget->intValue = value;
    return *this;
}

WidgetBuilder& WidgetBuilder::Default(float value) {
    mWidget->floatDefault = value;
    mWidget->floatValue = value;
    return *this;
}

WidgetBuilder& WidgetBuilder::Format(const std::string& format) {
    mWidget->format = format;
    return *this;
}

WidgetBuilder& WidgetBuilder::Tooltip(const std::string& text) {
    mWidget->tooltip = text;
    return *this;
}

WidgetBuilder& WidgetBuilder::Column(int column) {
    mWidget->column = column;
    return *this;
}

WidgetBuilder& WidgetBuilder::DefaultOpen(bool open) {
    mWidget->defaultOpen = open;
    return *this;
}

WidgetBuilder& WidgetBuilder::Callback(std::function<void()> callback) {
    mWidget->callback = std::move(callback);
    return *this;
}

WidgetBuilder& WidgetBuilder::PreFunc(std::function<void(Widget&)> preFunc) {
    mWidget->preFunc = std::move(preFunc);
    return *this;
}

WidgetBuilder& WidgetBuilder::Draw(std::function<void()> draw) {
    mWidget->draw = std::move(draw);
    return *this;
}

WidgetBuilder& WidgetBuilder::HiddenWhen(std::function<bool()> predicate) {
    mWidget->hiddenWhen = std::move(predicate);
    return *this;
}

WidgetBuilder& WidgetBuilder::Disabled(const std::string& reason) {
    return DisabledWhen([] { return true; }, reason);
}

WidgetBuilder& WidgetBuilder::DisabledWhen(std::function<bool()> predicate, const std::string& reason) {
    mWidget->disabledWhen = std::move(predicate);
    mWidget->disabledWhenReason = reason;
    return *this;
}

WidgetBuilder WidgetBuilder::Child(const std::string& name, WidgetType type) {
    auto child = std::make_unique<Widget>();
    child->name = name;
    child->type = type;
    Widget* raw = child.get();
    mWidget->children.push_back(std::move(child));
    return WidgetBuilder(raw);
}

namespace {

std::string Lowered(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

bool IsStructural(const Widget& widget) {
    return widget.type == WidgetType::Section || widget.type == WidgetType::Text || widget.IsContainer();
}

void CollectMatches(Tab& tab, Sidebar& sidebar, Widget& widget, const std::string& query, std::vector<Widget*>& path,
                    std::vector<SearchResult>& out) {
    if (widget.hidden) {
        return;
    }
    path.push_back(&widget);
    if (!IsStructural(widget) && MatchesQuery(widget.name, query)) {
        out.push_back({ &tab, &sidebar, path });
    }
    for (auto& child : widget.children) {
        CollectMatches(tab, sidebar, *child, query, path, out);
    }
    path.pop_back();
}

} // namespace

bool MatchesQuery(const std::string& text, const std::string& query) {
    const std::string haystack = Lowered(text);
    std::istringstream words(Lowered(query));
    std::string word;
    while (words >> word) {
        if (haystack.find(word) == std::string::npos) {
            return false;
        }
    }
    return true;
}

Tab& Registry::AddTab(const std::string& name) {
    if (Tab* existing = FindTab(name)) {
        return *existing;
    }
    mTabs.push_back(std::make_unique<Tab>());
    mTabs.back()->name = name;
    return *mTabs.back();
}

Sidebar& Registry::AddSidebar(const std::string& tabName, const std::string& name, int columns) {
    Tab& tab = AddTab(tabName);
    if (Sidebar* existing = tab.FindSidebar(name)) {
        return *existing;
    }
    tab.sidebars.push_back(std::make_unique<Sidebar>());
    tab.sidebars.back()->name = name;
    tab.sidebars.back()->columns = std::max(1, columns);
    return *tab.sidebars.back();
}

WidgetBuilder Registry::AddWidget(const std::string& tab, const std::string& sidebarName, const std::string& name,
                                  WidgetType type) {
    Sidebar& sidebar = AddSidebar(tab, sidebarName);
    auto widget = std::make_unique<Widget>();
    widget->name = name;
    widget->type = type;
    Widget* raw = widget.get();
    sidebar.widgets.push_back(std::move(widget));
    return WidgetBuilder(raw);
}

Tab* Registry::FindTab(const std::string& name) {
    for (auto& tab : mTabs) {
        if (tab->name == name) {
            return tab.get();
        }
    }
    return nullptr;
}

const Tab* Registry::FindTab(const std::string& name) const {
    return const_cast<Registry*>(this)->FindTab(name);
}

void Registry::Refresh() {
    for (auto& tab : mTabs) {
        for (auto& sidebar : tab->sidebars) {
            for (auto& widget : sidebar->widgets) {
                widget->Refresh();
            }
        }
    }
}

std::vector<SearchResult> Registry::Search(const std::string& query) {
    std::vector<SearchResult> results;
    for (auto& tab : mTabs) {
        if (tab->IsHidden()) {
            continue;
        }
        for (auto& sidebar : tab->sidebars) {
            if (sidebar->IsHidden()) {
                continue;
            }
            std::vector<Widget*> path;
            for (auto& widget : sidebar->widgets) {
                CollectMatches(*tab, *sidebar, *widget, query, path, results);
            }
        }
    }
    return results;
}

namespace {

std::vector<size_t> VisibleTabs(const Registry& registry) {
    std::vector<size_t> visible;
    for (size_t i = 0; i < registry.Tabs().size(); i++) {
        if (!registry.Tabs()[i]->IsHidden()) {
            visible.push_back(i);
        }
    }
    return visible;
}

std::vector<size_t> VisibleSidebars(const Tab& tab) {
    std::vector<size_t> visible;
    for (size_t i = 0; i < tab.sidebars.size(); i++) {
        if (!tab.sidebars[i]->IsHidden()) {
            visible.push_back(i);
        }
    }
    return visible;
}

bool Contains(const std::vector<size_t>& list, size_t value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

} // namespace

void Navigation::Validate(const Registry& registry) {
    const auto tabs = VisibleTabs(registry);
    if (tabs.empty()) {
        mTab = 0;
        mSidebar = 0;
        return;
    }
    if (!Contains(tabs, mTab)) {
        mTab = tabs.front();
        mSidebar = 0;
    }
    const auto sidebars = VisibleSidebars(*registry.Tabs()[mTab]);
    if (!sidebars.empty() && !Contains(sidebars, mSidebar)) {
        mSidebar = sidebars.front();
    }
}

void Navigation::SelectTab(const Registry& registry, size_t index) {
    if (index >= registry.Tabs().size() || registry.Tabs()[index]->IsHidden()) {
        return;
    }
    if (index != mTab) {
        mTab = index;
        mSidebar = 0;
    }
    Validate(registry);
}

void Navigation::SelectSidebar(const Registry& registry, size_t index) {
    Validate(registry);
    if (mTab >= registry.Tabs().size()) {
        return;
    }
    const Tab& tab = *registry.Tabs()[mTab];
    if (index < tab.sidebars.size() && !tab.sidebars[index]->IsHidden()) {
        mSidebar = index;
    }
}

void Navigation::CycleTab(const Registry& registry, int direction) {
    Validate(registry);
    const auto tabs = VisibleTabs(registry);
    if (tabs.empty()) {
        return;
    }
    const auto here = std::find(tabs.begin(), tabs.end(), mTab) - tabs.begin();
    const auto count = static_cast<std::ptrdiff_t>(tabs.size());
    SelectTab(registry, tabs[static_cast<size_t>((here + direction + count) % count)]);
}

} // namespace Frigate::ConfigMenu
