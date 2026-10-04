#include "game/menu/NoControllers.h"

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "resource/type/Font.h"
#include "resource/type/Text.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

constexpr const char* kFontPath = "font/fontZurichBold";
constexpr const char* kStringsPath = "obseg/text/LtitleE";

// TITLE_STR_118_NOCONT and TITLE_STR_119_ATTACHCONT.
constexpr size_t kStringNoController = 118;
constexpr size_t kStringAttachController = 119;

// The lines are centred on x = 220, at y = 153 and y = 177.
constexpr int32_t kCentreX = 0xDC;
constexpr int32_t kNoControllerY = 0x99;
constexpr int32_t kAttachControllerY = 0xB1;

constexpr uint32_t kWhite = 0xFFFFFFFF;

Gfx* DrawCentred(Gfx* dl, const Font& font, const std::string& text, int32_t centreY) {
    const Text::Extent extent = Text::Measure(font, text);
    int32_t x = kCentreX - (extent.width >> 1);
    int32_t y = centreY - (extent.height >> 1);
    return Text::Render(dl, &x, &y, text, font, kWhite, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

} // namespace

void RegisterNoControllers(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    // The decomp reads which controllers are connected here, but every count it
    // can find picks the same two strings.
    menu.RegisterMenu(
        MenuId::NoControllers,
        {
            .onConstruct =
                [resourceManager](Gfx* dl, MenuStateMachine&) {
                    const auto font = resourceManager->LoadResource<Font>(kFontPath);
                    const auto strings = resourceManager->LoadResource<Frigate::Text>(kStringsPath);
                    if (font == nullptr || strings == nullptr || strings->Strings.size() <= kStringAttachController) {
                        spdlog::error("no controllers screen: the font or text is missing from the loaded archives");
                        return dl;
                    }
                    dl = Text::SetupRdp(dl);
                    dl = DrawCentred(dl, *font, strings->Strings[kStringNoController], kNoControllerY);
                    return DrawCentred(dl, *font, strings->Strings[kStringAttachController], kAttachControllerY);
                },
        });
}

} // namespace Frigate::Game::Menu
