#include "game/menu/FolderPaper.h"

#include <spdlog/spdlog.h>

#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/WalletBackground.h"
#include "game/menu/WalletBond.h"
#include "game/save/SaveProgress.h"
#include "model/ModelInstance.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

constexpr const char* kZurichPath = "font/fontZurichBold";
constexpr const char* kGothicPath = "font/fontBankGothic";
constexpr const char* kStringsPath = "obseg/text/LtitleE";

// Where the stage name lines print, and the text colour (opaque black).
constexpr int32_t kNameX = 0x37;
constexpr int32_t kDifficultyY = 0x57;
constexpr int32_t kChapterY = 0x67;
constexpr int32_t kPartY = 0x77;
constexpr uint32_t kBlack = 0x000000FF;

constexpr int32_t kBriefingInvalid = -1;

} // namespace

void FolderPaper::Load(const std::shared_ptr<Ship::ResourceManager>& resourceManager, FrontState& front) {
    zurich_ = resourceManager->LoadResource<Font>(kZurichPath);
    gothic_ = resourceManager->LoadResource<Font>(kGothicPath);
    strings_ = resourceManager->LoadResource<Frigate::Text>(kStringsPath);
    if (!Ready()) {
        spdlog::error("folder page: the fonts or text are missing from the loaded archives");
    }
    if (!front.walletBond->Load()) {
        spdlog::error("folder page: the wallet model is missing from the loaded archives");
    }

    background_.emplace(resourceManager);
    background_->Init();
}

void FolderPaper::Release() {
    background_.reset();
    zurich_.reset();
    gothic_.reset();
    strings_.reset();
}

void FolderPaper::ShowSwitches(FrontState& front, std::initializer_list<size_t> shown) {
    if (!front.walletBond->Loaded()) {
        return;
    }
    Model::ModelInstance& model = *front.walletBond->Instance(0);
    DisableAllSwitches(model);
    for (const size_t slot : shown) {
        model.SetSwitchVisible(slot, true);
    }
}

const std::string& FolderPaper::String(size_t slot) const {
    static const std::string kEmpty;
    return strings_ != nullptr && slot < strings_->Strings.size() ? strings_->Strings[slot] : kEmpty;
}

Gfx* FolderPaper::DrawBackground(Gfx* dl, FrontState& front) {
    if (!background_) {
        return dl;
    }
    return background_->Draw(dl, *front.walletBond, front.progress->CurrentFolder());
}

Gfx* FolderPaper::DrawStageName(Gfx* dl, const FrontState& front) const {
    const std::span<const MissionFolderSetup> entries = MissionFolderEntries();
    const int32_t page = front.briefingPage;
    if (page <= kBriefingInvalid || static_cast<size_t>(page) >= entries.size()) {
        return dl;
    }

    const auto draw = [&](int32_t y, const std::string& text) {
        int32_t x = kNameX;
        int32_t textY = y;
        dl = Text::Render(dl, &x, &textY, text, Zurich(), kBlack, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
    };

    const int32_t difficulty = front.progress->SelectedDifficulty();
    if (difficulty >= Save::kDifficultyAgent) {
        // get_ptr_difficulty_name: the names are the four after "Agent".
        draw(kDifficultyY, String(19 + static_cast<size_t>(difficulty)) + String(kStringJamesBond));
    }

    const int32_t chapter = ChapterBriefingEntry(page);
    if (chapter >= 0) {
        const MissionFolderSetup& heading = entries[static_cast<size_t>(chapter)];
        draw(kChapterY, String(kStringMission) + heading.label + ": " +
                            String(static_cast<size_t>(heading.folderTextSlot)) + "\n");
    }

    const MissionFolderSetup& part = entries[static_cast<size_t>(page)];
    draw(kPartY, String(kStringPart) + part.label + ": " + String(static_cast<size_t>(part.folderTextSlot)) + "\n");
    return dl;
}

} // namespace Frigate::Game::Menu
