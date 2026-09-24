#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "level/LevelInfo.h"

namespace Frigate::Level {

// One row of the game's ramrom_table. On-disk layout: docs/formats/goldeneye-ramrom.md.
struct RamromEntry {
    std::string name;
    // The highest stage unlocked a save must have reached before the demo
    // can play.
    int locked = 0;
};

// The `ramrom/table` resource.
std::vector<RamromEntry> ParseRamromTable(std::string_view text);

// What a demo file's header says to load.
struct DemoHeader {
    LevelId stage = LevelId::Max;
    int32_t difficulty = 0;
    // slotnum: what check_ramrom_flags returns while the demo plays, and which
    // of the stage setup's spawn and item records apply.
    int32_t slot = 0;
};

std::optional<DemoHeader> ParseDemoHeader(const std::vector<uint8_t>& file);

// select_ramrom_to_play's pick: `random` modulo the count of leading demos
// the save has unlocked. Empty when none is unlocked.
std::optional<size_t> SelectRamrom(const std::vector<RamromEntry>& table, int highestStageUnlocked, uint32_t random);

// Where the packed demos come from.
class DemoSource {
  public:
    virtual ~DemoSource() = default;
    virtual std::optional<std::string> LoadTable() = 0;
    virtual std::optional<std::vector<uint8_t>> LoadDemo(const std::string& name) = 0;
};

// A demo chosen to play: what replay_recorded_ramrom_at_address reads from
// the file before it changes to the stage.
struct SelectedDemo {
    size_t index = 0;
    std::string name;
    DemoHeader header;
};

// Demo `index` of the table.
std::optional<SelectedDemo> LoadAttractDemo(DemoSource& source, size_t index);

// select_ramrom_to_play. `highestStageUnlocked` is
// fileGetHighestStageUnlockedAnyFolder().
std::optional<SelectedDemo> SelectAttractDemo(DemoSource& source, int highestStageUnlocked, uint32_t random);

} // namespace Frigate::Level
