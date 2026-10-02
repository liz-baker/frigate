#pragma once

#include "level/Stage.h"
#include "level/WatchMenu.h"

namespace Frigate::Level {

// What the watch menu reaches the stage through: the text, the objectives, the
// player's health and the abort.
class Stage::WatchBridge final : public WatchMenuHost {
  public:
    explicit WatchBridge(Stage& stage) : stage_(stage) {
    }

    std::string_view Text(int32_t id) const override;
    float Health() const override;
    float Armour() const override;
    int32_t Difficulty() const override;
    int32_t ObjectiveCount() const override;
    int32_t ObjectiveDifficulty(int32_t objective) const override;
    std::string_view ObjectiveText(int32_t objective) const override;
    ObjectiveStatus ObjectiveStatusOf(int32_t objective) const override;
    bool AllObjectivesComplete() const override;
    std::string_view BriefingPage(int32_t page) const override;
    bool PropListed(const InvItem& item) const override;
    void AbortMission() override;

  private:
    Stage& stage_;
};

} // namespace Frigate::Level
