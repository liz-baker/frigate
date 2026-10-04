#include "game/menu/BriefingModel.h"

#include "game/menu/FileSelectModel.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/MissionSelectModel.h"

namespace Frigate::Game::Menu {

namespace {

namespace Button = Input::Button;

// TITLE_STR_93_PRIMARYOBJECTIVES through TITLE_STR_97_MONEYPENNY.
constexpr size_t kHeadingSlots[] = { 93, 94, 95, 96, 97 };

uint16_t ReadU16(std::span<const uint8_t> bytes, size_t offset) {
    return static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

} // namespace

std::optional<BriefingData> BriefingData::Parse(std::span<const uint8_t> bytes) {
    if (bytes.size() < kByteSize) {
        return std::nullopt;
    }
    BriefingData data;
    for (size_t i = 0; i < data.brief.size(); i++) {
        data.brief[i] = ReadU16(bytes, i * 2);
    }
    for (size_t i = 0; i < data.objectives.size(); i++) {
        data.objectives[i].textId = ReadU16(bytes, 8 + (i * 4));
        data.objectives[i].enabledDifficulty = ReadU16(bytes, 8 + (i * 4) + 2);
    }
    return data;
}

void BriefingModel::Init() {
    tabNextSelected_ = false;
    tabPrevSelected_ = false;
    SetPage(static_cast<int32_t>(BriefingPage::Title));
    tabs_ = {};
}

void BriefingModel::StepBack() {
    if (page_ > BriefingPage::Title) {
        SetPage(static_cast<int32_t>(page_) - 1);
    } else {
        tabPrevSelected_ = true;
    }
}

BriefingModel::Next BriefingModel::Update(const Input::Sample& input, MenuCursor& cursor, int32_t missionNum) {
    sfx_.clear();
    const bool notLastPage = static_cast<int32_t>(page_) < kFinalPage - 1;

    tabs_ = {};
    if (CursorOnPreviousTab(cursor)) {
        tabs_.previous = true;
    } else if (CursorOnNextTab(cursor) && notLastPage) {
        tabs_.next = true;
    } else if (CursorOnStartTab(cursor)) {
        tabs_.start = true;
    } else if (notLastPage) {
        tabs_.next = true;
    } else {
        tabs_.start = true;
    }

    if (input.Pressed(Button::kStart)) {
        tabNextSelected_ = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    } else if (input.Pressed(Button::kZ | Button::kA)) {
        if (tabs_.next) {
            SetPage(static_cast<int32_t>(page_) + 1);
        } else if (tabs_.previous) {
            StepBack();
        } else if (tabs_.start) {
            tabNextSelected_ = true;
        }
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    } else if (input.Pressed(Button::kB)) {
        StepBack();
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }

    FileSelectModel::MoveCursorWithStick(input, cursor);

    if (tabNextSelected_) {
        return Next::RunStage;
    }
    if (tabPrevSelected_) {
        MissionSelectModel::PlaceCursorOnStage(missionNum, cursor);
        return Next::MissionSelect;
    }
    return Next::Stay;
}

std::string_view PageHeading(const Level::Lang& lang, BriefingPage page) {
    return lang.Get(Level::StringId(Level::TextBank::Title, static_cast<int32_t>(BriefingModel::HeadingSlot(page))));
}

std::string_view PageBody(const Level::Lang& lang, const BriefingData& data, BriefingPage page) {
    const auto index = BriefingModel::BodyIndex(page);
    return index ? lang.Get(data.brief[*index]) : std::string_view();
}

size_t BriefingModel::HeadingSlot(BriefingPage page) {
    return kHeadingSlots[static_cast<size_t>(page)];
}

std::optional<size_t> BriefingModel::BodyIndex(BriefingPage page) {
    if (page == BriefingPage::Title) {
        return std::nullopt;
    }
    return static_cast<size_t>(page) - 1;
}

} // namespace Frigate::Game::Menu
