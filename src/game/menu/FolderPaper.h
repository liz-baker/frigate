#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>

#include "game/FrontState.h"
#include "libultraship/libultra/gbi.h"
#include "game/menu/WalletBackground.h"
#include "resource/type/Font.h"
#include "resource/type/Text.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game::Menu {

// The open folder the difficulty and 007 options screens (and the mission
// select and briefing) write on: the first wallet model posed for the current
// folder (frontSetupMenuBackground), with the fonts and title strings the
// pages print. One instance per screen; its matrices and joint draw are read
// by address while the interpreter draws the frame.
class FolderPaper {
  public:
    // LTITLE string slots the pages print.
    static constexpr size_t kStringStart = 4;
    static constexpr size_t kStringNext = 5;
    static constexpr size_t kStringPrevious = 6;
    static constexpr size_t kStringJamesBond = 32;
    static constexpr size_t kStringMission = 33;
    static constexpr size_t kStringPart = 34;

    // The screen's init: loads the fonts, strings and wallet models and sets up
    // the view (viSetFovY, viSetAspect, viSetZRange). Logs what is missing
    // from the loaded archives; Ready() says whether the page can be drawn.
    void Load(const std::shared_ptr<Ship::ResourceManager>& resourceManager, FrontState& front);
    // Lets go of everything Load took.
    void Release();
    bool Ready() const {
        return zurich_ != nullptr && gothic_ != nullptr && strings_ != nullptr;
    }

    // frontSetupMenuBackground for the current folder.
    Gfx* DrawBackground(Gfx* dl, FrontState& front);

    // print_current_solo_briefing_stage_name: the selected difficulty, the
    // chapter and the part of the briefing page's stage.
    Gfx* DrawStageName(Gfx* dl, const FrontState& front) const;

    // disable_all_switches and set_item_visibility_in_objinstance for the first
    // wallet: only `shown` are visible.
    static void ShowSwitches(FrontState& front, std::initializer_list<size_t> shown);

    const Font& Zurich() const {
        return *zurich_;
    }
    const Font& Gothic() const {
        return *gothic_;
    }
    // The title string in `slot`, or empty.
    const std::string& String(size_t slot) const;

  private:
    std::shared_ptr<Font> zurich_;
    std::shared_ptr<Font> gothic_;
    std::shared_ptr<Frigate::Text> strings_;

    std::optional<WalletBackground> background_;
};

} // namespace Frigate::Game::Menu
