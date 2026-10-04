#include "menu/MenuWindow.h"

#include <algorithm>
#include <cfloat>

#include <imgui.h>

#include "menu/CVars.h"
#include "menu/MenuDraw.h"
#include "ship/window/gui/IconsFontAwesome4.h"

namespace Frigate::ConfigMenu {

namespace {

constexpr ImVec4 kAccent(0.20f, 0.45f, 0.90f, 1.00f);

constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

// Scoped so the theme never leaks into the other ImGui windows.
class ThemeScope {
  public:
    explicit ThemeScope(float opacity) {
        Color(ImGuiCol_WindowBg, ImVec4(0.04f, 0.07f, 0.14f, opacity));
        Color(ImGuiCol_PopupBg, ImVec4(0.06f, 0.10f, 0.20f, 0.98f));
        Color(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        Color(ImGuiCol_Border, ImVec4(0.25f, 0.40f, 0.70f, 0.55f));
        Color(ImGuiCol_Separator, ImVec4(0.25f, 0.40f, 0.70f, 0.55f));
        Color(ImGuiCol_Button, ImVec4(0.14f, 0.28f, 0.55f, 0.85f));
        Color(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.42f, 0.80f, 1.00f));
        Color(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.52f, 0.95f, 1.00f));
        Color(ImGuiCol_FrameBg, ImVec4(0.10f, 0.20f, 0.40f, 0.95f));
        Color(ImGuiCol_FrameBgHovered, ImVec4(0.15f, 0.27f, 0.50f, 1.00f));
        Color(ImGuiCol_FrameBgActive, ImVec4(0.16f, 0.28f, 0.52f, 1.00f));
        Color(ImGuiCol_Header, ImVec4(0.14f, 0.28f, 0.55f, 0.85f));
        Color(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.42f, 0.80f, 1.00f));
        Color(ImGuiCol_HeaderActive, ImVec4(0.28f, 0.52f, 0.95f, 1.00f));
        Color(ImGuiCol_SliderGrab, ImVec4(0.40f, 0.65f, 1.00f, 1.00f));
        Color(ImGuiCol_SliderGrabActive, ImVec4(0.55f, 0.78f, 1.00f, 1.00f));
        Color(ImGuiCol_CheckMark, ImVec4(0.45f, 0.72f, 1.00f, 1.00f));
        Color(ImGuiCol_NavHighlight, ImVec4(0.60f, 0.82f, 1.00f, 1.00f));
        Color(ImGuiCol_TextDisabled, ImVec4(0.50f, 0.58f, 0.72f, 1.00f));
        Var(ImGuiStyleVar_WindowRounding, 0.0f);
        Var(ImGuiStyleVar_WindowBorderSize, 0.0f);
        Var(ImGuiStyleVar_FrameRounding, 4.0f);
        Var(ImGuiStyleVar_ChildRounding, 4.0f);
        Var(ImGuiStyleVar_GrabRounding, 4.0f);
        Var(ImGuiStyleVar_PopupRounding, 4.0f);
    }
    ~ThemeScope() {
        ImGui::PopStyleVar(mVars);
        ImGui::PopStyleColor(mColors);
    }
    ThemeScope(const ThemeScope&) = delete;
    ThemeScope& operator=(const ThemeScope&) = delete;

  private:
    void Color(ImGuiCol index, const ImVec4& color) {
        ImGui::PushStyleColor(index, color);
        mColors++;
    }
    void Var(ImGuiStyleVar index, float value) {
        ImGui::PushStyleVar(index, value);
        mVars++;
    }
    int mColors = 0;
    int mVars = 0;
};

// A button drawn in the accent colour while `selected`.
bool SelectedButton(const char* label, bool selected) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    }
    const bool pressed = ImGui::Button(label);
    if (selected) {
        ImGui::PopStyleColor();
    }
    return pressed;
}

// A button that is disabled, with a tooltip saying why, when `available` is false.
bool ActionButton(const char* label, bool available, const char* tooltip) {
    if (!available) {
        ImGui::BeginDisabled();
    }
    const bool pressed = ImGui::Button(label);
    if (!available) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed && available;
}

} // namespace

MenuWindow::MenuWindow(std::shared_ptr<Registry> registry, std::shared_ptr<ValueStore> store, MenuActions actions)
    // An empty visibility CVar keeps the open state out of the config, so the
    // menu is closed on every launch.
    : Ship::GuiWindow("", false, "Frigate Menu", ImVec2{ -1, -1 }, kWindowFlags), mRegistry(std::move(registry)),
      mStore(std::move(store)), mActions(std::move(actions)) {
}

void MenuWindow::Draw() {
    if (!IsVisible()) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ThemeScope theme(std::clamp(mStore->GetFloat(kOpacityCVar, kDefaultOpacity), 0.0f, 1.0f));
    GuiWindow::Draw();
}

void MenuWindow::UpdateElement() {
}

void MenuWindow::DrawElement() {
    mRegistry->Refresh();
    mNavigation.Validate(*mRegistry);

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) {
        mNavigation.CycleTab(*mRegistry, -1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
        mNavigation.CycleTab(*mRegistry, 1);
    }

    DrawTopBar();
    ImGui::Separator();
    if (mSearch[0] == '\0') {
        DrawBody();
    } else {
        DrawSearchResults();
    }
}

void MenuWindow::DrawTopBar() {
    const auto& tabs = mRegistry->Tabs();
    for (size_t i = 0; i < tabs.size(); i++) {
        if (tabs[i]->IsHidden()) {
            continue;
        }
        if (SelectedButton(tabs[i]->name.c_str(), i == mNavigation.TabIndex() && mSearch[0] == '\0')) {
            mSearch[0] = '\0';
            mNavigation.SelectTab(*mRegistry, i);
        }
        ImGui::SameLine();
    }

    const float button = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float searchWidth = ImGui::GetFontSize() * 12.0f;
    const float cluster = searchWidth + 3.0f * (button + spacing);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - cluster));

    ImGui::SetNextItemWidth(searchWidth);
    ImGui::InputTextWithHint("##search", ICON_FA_SEARCH " Search", mSearch, sizeof(mSearch));
    ImGui::SameLine();
    if (ActionButton(ICON_FA_POWER_OFF "##quit", mActions.quit != nullptr, "Quit the game")) {
        mActions.quit();
    }
    ImGui::SameLine();
    if (ActionButton(ICON_FA_REFRESH "##reset", mActions.reset != nullptr, "Reset is not available")) {
        mActions.reset();
    }
    ImGui::SameLine();
    if (ActionButton(ICON_FA_TIMES "##close", true, "Close the menu")) {
        Hide();
    }
}

void MenuWindow::DrawSearchResults() {
    const auto results = mRegistry->Search(mSearch);
    ImGui::BeginChild("##results", ImVec2(0.0f, 0.0f));
    if (results.empty()) {
        ImGui::TextDisabled("No settings match \"%s\"", mSearch);
    }
    Sidebar* heading = nullptr;
    for (const auto& result : results) {
        if (result.sidebar != heading) {
            heading = result.sidebar;
            const std::string title = result.tab->name + " > " + result.sidebar->name;
            ImGui::SeparatorText(title.c_str());
        }
        DrawWidget(*result.path.back(), *mStore);
    }
    ImGui::EndChild();
}

void MenuWindow::DrawBody() {
    const auto& tabs = mRegistry->Tabs();
    if (mNavigation.TabIndex() >= tabs.size()) {
        return;
    }
    Tab& tab = *tabs[mNavigation.TabIndex()];

    const float sidebarWidth = ImGui::GetFontSize() * 13.0f;
    ImGui::BeginChild("##sidebars", ImVec2(sidebarWidth, 0.0f));
    for (size_t i = 0; i < tab.sidebars.size(); i++) {
        if (tab.sidebars[i]->IsHidden()) {
            continue;
        }
        if (ImGui::Selectable(tab.sidebars[i]->name.c_str(), i == mNavigation.SidebarIndex())) {
            mNavigation.SelectSidebar(*mRegistry, i);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (mNavigation.SidebarIndex() < tab.sidebars.size()) {
        DrawSidebar(*tab.sidebars[mNavigation.SidebarIndex()], *mStore);
    }
    ImGui::EndChild();
}

} // namespace Frigate::ConfigMenu
