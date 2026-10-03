#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderTabs.h"
#include "level/Lang.h"
#include "level/Objectives.h"

namespace Frigate::Game::Menu {

// WATCH_BRIEFING_PAGE.
enum class BriefingPage : int32_t { Title, Overview, M, Q, Moneypenny };

constexpr size_t kObjectivesMax = Level::Objectives::kMax;

// struct BriefingData: a mission's briefing file. On-disk layout:
// docs/formats/goldeneye-briefing.md.
struct BriefingData {
    struct Objective {
        // getStringID of the objective's text, 0 for none.
        uint16_t textId = 0;
        // enabled_difficulty: the lowest difficulty it is shown on.
        uint16_t enabledDifficulty = 0;
    };

    // The Background, M briefing, Q branch and Moneypenny pages' text ids.
    std::array<uint16_t, 4> brief{};
    std::array<Objective, kObjectivesMax> objectives{};

    static constexpr size_t kByteSize = 8 + (kObjectivesMax * 4);

    // Null when `bytes` is shorter than the file.
    static std::optional<BriefingData> Parse(std::span<const uint8_t> bytes);
};

// The text of the page heading and, for the pages after the objectives, of its
// body (langGet of the briefing's text id for the page).
std::string_view PageHeading(const Level::Lang& lang, BriefingPage page);
std::string_view PageBody(const Level::Lang& lang, const BriefingData& data, BriefingPage page);

// The tab highlights interface_menu0A_briefing derives from the cursor.
struct BriefingTabs {
    bool previous = false;
    bool next = false;
    bool start = false;
};

// The briefing's state and logic (init_menu0A_briefing and
// interface_menu0A_briefing) apart from the drawing and the wallet model: the
// current page, the tab under the cursor and the hand-offs to the stage and
// back to the mission select.
class BriefingModel {
  public:
    // final_menu_briefing_page: one past the last page.
    static constexpr int32_t kFinalPage = 5;

    enum class Next {
        Stay,
        RunStage,
        MissionSelect,
    };

    // init_menu0A_briefing: the first page, no tab selected.
    void Init();

    // One frame of interface_menu0A_briefing, with the cursor where it was at
    // the end of the last. Moves `cursor` and raises sounds into Sfx().
    // `missionNum` is the briefed stage's LEVEL_SOLO_SEQUENCE value, where the
    // mission select puts the cursor on the way back.
    Next Update(const Input::Sample& input, MenuCursor& cursor, int32_t missionNum);

    // current_menu_briefing_page.
    BriefingPage Page() const {
        return page_;
    }
    bool OnTitlePage() const {
        return page_ == BriefingPage::Title;
    }
    // tab_prev_highlight, tab_next_highlight and tab_start_highlight.
    const BriefingTabs& Tabs() const {
        return tabs_;
    }
    // Whether the NEXT tab is drawn: every page but the last
    // (constructor_menu0A_briefing).
    bool ShowsNextTab() const {
        return page_ < BriefingPage::Moneypenny;
    }
    // tab_next_selected and tab_prev_selected.
    bool TabNextSelected() const {
        return tabNextSelected_;
    }
    bool TabPrevSelected() const {
        return tabPrevSelected_;
    }

    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

    // The LTITLE slot of a page's heading.
    static size_t HeadingSlot(BriefingPage page);
    // The index into BriefingData::brief of a text page, or none for the
    // objectives page.
    static std::optional<size_t> BodyIndex(BriefingPage page);

  private:
    void SetPage(int32_t page) {
        page_ = static_cast<BriefingPage>(page);
    }
    void StepBack();

    BriefingPage page_ = BriefingPage::Title;
    BriefingTabs tabs_;
    bool tabNextSelected_ = false;
    bool tabPrevSelected_ = false;
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
