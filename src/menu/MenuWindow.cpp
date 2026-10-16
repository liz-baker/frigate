#include "menu/MenuWindow.h"

#include <algorithm>
#include <cfloat>

#include <imgui.h>

#include "menu/CVars.h"
#include "menu/MenuDraw.h"
#include "menu/SettingsModel.h"
#include "ship/window/gui/IconsFontAwesome4.h"

namespace Frigate::ConfigMenu {

namespace {

constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

// One colour per role; hover and active states of the accent roles are
// brighter steps of the same hue.
struct Palette {
    ImVec4 window, popup, border, button, buttonHover, buttonActive, frame, frameHover, frameActive, grab, grabActive,
        check, nav, textDisabled;
};

// Indexed by the theme setting; the order matches kMenuThemes.
constexpr Palette kPalettes[] = {
    // Blue
    { { 0.04f, 0.07f, 0.14f, 1.0f },
      { 0.06f, 0.10f, 0.20f, 0.98f },
      { 0.25f, 0.40f, 0.70f, 0.55f },
      { 0.14f, 0.28f, 0.55f, 0.85f },
      { 0.22f, 0.42f, 0.80f, 1.0f },
      { 0.28f, 0.52f, 0.95f, 1.0f },
      { 0.10f, 0.20f, 0.40f, 0.95f },
      { 0.15f, 0.27f, 0.50f, 1.0f },
      { 0.16f, 0.28f, 0.52f, 1.0f },
      { 0.40f, 0.65f, 1.00f, 1.0f },
      { 0.55f, 0.78f, 1.00f, 1.0f },
      { 0.45f, 0.72f, 1.00f, 1.0f },
      { 0.60f, 0.82f, 1.00f, 1.0f },
      { 0.50f, 0.58f, 0.72f, 1.0f } },
    // Graphite
    { { 0.07f, 0.07f, 0.08f, 1.0f },
      { 0.11f, 0.11f, 0.12f, 0.98f },
      { 0.40f, 0.40f, 0.43f, 0.55f },
      { 0.26f, 0.26f, 0.29f, 0.85f },
      { 0.38f, 0.38f, 0.42f, 1.0f },
      { 0.48f, 0.48f, 0.53f, 1.0f },
      { 0.16f, 0.16f, 0.18f, 0.95f },
      { 0.22f, 0.22f, 0.25f, 1.0f },
      { 0.24f, 0.24f, 0.27f, 1.0f },
      { 0.70f, 0.70f, 0.74f, 1.0f },
      { 0.85f, 0.85f, 0.90f, 1.0f },
      { 0.80f, 0.80f, 0.85f, 1.0f },
      { 0.90f, 0.90f, 0.95f, 1.0f },
      { 0.55f, 0.55f, 0.60f, 1.0f } },
    // Crimson
    { { 0.14f, 0.04f, 0.05f, 1.0f },
      { 0.20f, 0.06f, 0.08f, 0.98f },
      { 0.70f, 0.28f, 0.30f, 0.55f },
      { 0.55f, 0.14f, 0.17f, 0.85f },
      { 0.80f, 0.22f, 0.26f, 1.0f },
      { 0.95f, 0.28f, 0.32f, 1.0f },
      { 0.40f, 0.10f, 0.12f, 0.95f },
      { 0.50f, 0.15f, 0.17f, 1.0f },
      { 0.52f, 0.16f, 0.18f, 1.0f },
      { 1.00f, 0.50f, 0.52f, 1.0f },
      { 1.00f, 0.68f, 0.70f, 1.0f },
      { 1.00f, 0.62f, 0.64f, 1.0f },
      { 1.00f, 0.75f, 0.76f, 1.0f },
      { 0.72f, 0.52f, 0.54f, 1.0f } },
};
static_assert(sizeof(kPalettes) / sizeof(kPalettes[0]) == kMenuThemes.size(), "one palette per theme name");

// Scoped so the theme never leaks into the other ImGui windows.
class ThemeScope {
  public:
    ThemeScope(int theme, float opacity) {
        const Palette& p = kPalettes[std::clamp(theme, 0, static_cast<int>(kMenuThemes.size()) - 1)];
        ImVec4 window = p.window;
        window.w = opacity;
        Color(ImGuiCol_WindowBg, window);
        Color(ImGuiCol_PopupBg, p.popup);
        Color(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        Color(ImGuiCol_Border, p.border);
        Color(ImGuiCol_Separator, p.border);
        Color(ImGuiCol_Button, p.button);
        Color(ImGuiCol_ButtonHovered, p.buttonHover);
        Color(ImGuiCol_ButtonActive, p.buttonActive);
        Color(ImGuiCol_FrameBg, p.frame);
        Color(ImGuiCol_FrameBgHovered, p.frameHover);
        Color(ImGuiCol_FrameBgActive, p.frameActive);
        Color(ImGuiCol_Header, p.button);
        Color(ImGuiCol_HeaderHovered, p.buttonHover);
        Color(ImGuiCol_HeaderActive, p.buttonActive);
        Color(ImGuiCol_SliderGrab, p.grab);
        Color(ImGuiCol_SliderGrabActive, p.grabActive);
        Color(ImGuiCol_CheckMark, p.check);
        Color(ImGuiCol_NavHighlight, p.nav);
        Color(ImGuiCol_TextDisabled, p.textDisabled);
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
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
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
        ShowTooltip(tooltip);
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
        mWasVisible = false;
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ThemeScope theme(mStore->GetInt(kThemeCVar, 0),
                     std::clamp(mStore->GetFloat(kOpacityCVar, kDefaultOpacity), 0.0f, 1.0f));
    GuiWindow::Draw();
}

void MenuWindow::UpdateElement() {
}

bool MenuWindow::SearchInSidebar() const {
    return mStore->GetInt(kSearchInSidebarCVar, 0) != 0;
}

void MenuWindow::DrawSearchBox(float width) {
    if (mFocusSearch) {
        ImGui::SetKeyboardFocusHere();
        mFocusSearch = false;
    }
    ImGui::SetNextItemWidth(width);
    ImGui::InputTextWithHint("##search", ICON_FA_SEARCH " Search", mSearch, sizeof(mSearch));
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

    if (!mWasVisible && mStore->GetInt(kSearchAutofocusCVar, 0) != 0) {
        mFocusSearch = true;
    }
    mWasVisible = true;

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
    const bool inTopBar = !SearchInSidebar();
    const float cluster = (inTopBar ? searchWidth + spacing : 0.0f) + 3.0f * (button + spacing);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - cluster));

    if (inTopBar) {
        DrawSearchBox(searchWidth);
        ImGui::SameLine();
    }
    if (ActionButton(ICON_FA_POWER_OFF "##quit", mActions.quit != nullptr, "Quit the game")) {
        mActions.quit();
    }
    ImGui::SameLine();
    const bool canReset = mActions.reset != nullptr;
    if (ActionButton(ICON_FA_REFRESH "##reset", canReset, canReset ? "Reset the game" : "Reset is not available")) {
        mActions.reset();
    }
    ImGui::SameLine();
    if (ActionButton(ICON_FA_TIMES "##close", true, "Close the menu")) {
        Hide();
    }
}

void MenuWindow::DrawSearchResults() {
    if (SearchInSidebar()) {
        DrawSearchBox(-FLT_MIN);
    }
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
    if (SearchInSidebar()) {
        DrawSearchBox(-FLT_MIN);
    }
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
