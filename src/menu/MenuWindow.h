#pragma once

#include <functional>
#include <memory>
#include <string>

#include "menu/MenuModel.h"
#include "ship/window/gui/GuiWindow.h"

namespace Frigate::ConfigMenu {

// What the top-right buttons do. An unset action leaves its button disabled.
struct MenuActions {
    std::function<void()> quit;
    std::function<void()> reset;
};

// The full-screen translucent menu panel drawn over the game: tab row, search
// box and buttons along the top, a sidebar list on the left, and the selected
// sidebar's widgets in the rest. Hidden until toggled; libultraship toggles it
// (Escape, and the gamepad's Back button while controller navigation is on).
class MenuWindow : public Ship::GuiWindow {
  public:
    MenuWindow(std::shared_ptr<Registry> registry, std::shared_ptr<ValueStore> store, MenuActions actions);

    // Pushes the theme around the window's own drawing.
    void Draw() override;

  protected:
    void UpdateElement() override;
    void DrawElement() override;

  private:
    void DrawTopBar();
    void DrawSearchResults();
    void DrawBody();

    std::shared_ptr<Registry> mRegistry;
    std::shared_ptr<ValueStore> mStore;
    MenuActions mActions;
    Navigation mNavigation;
    char mSearch[128] = {};
};

} // namespace Frigate::ConfigMenu
