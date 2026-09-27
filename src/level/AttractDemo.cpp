#include "level/AttractDemo.h"

namespace Frigate::Level {

namespace {

// Offsets in the ramrom file header.
constexpr size_t kStageOffset = 0x10;
constexpr size_t kDifficultyOffset = 0x14;
constexpr size_t kRandomSeedOffset = 0x00;
constexpr size_t kChrObjSeedOffset = 0x08;
constexpr size_t kSlotOffset = 0x88;
constexpr size_t kControlStyleOffset = 0xC0;
constexpr size_t kHeaderFieldsEnd = 0xC4;

int32_t ReadS32(const std::vector<uint8_t>& bytes, size_t at) {
    return static_cast<int32_t>((uint32_t{ bytes[at] } << 24) | (uint32_t{ bytes[at + 1] } << 16) |
                                (uint32_t{ bytes[at + 2] } << 8) | uint32_t{ bytes[at + 3] });
}

uint64_t ReadU64(const std::vector<uint8_t>& bytes, size_t at) {
    return (uint64_t{ static_cast<uint32_t>(ReadS32(bytes, at)) } << 32) |
           static_cast<uint32_t>(ReadS32(bytes, at + 4));
}

} // namespace

std::vector<RamromEntry> ParseRamromTable(std::string_view text) {
    std::vector<RamromEntry> table;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const size_t tab = line.find('\t');
        if (tab == std::string_view::npos) {
            continue;
        }
        RamromEntry entry;
        entry.name = std::string(line.substr(0, tab));
        entry.locked = std::stoi(std::string(line.substr(tab + 1)));
        table.push_back(std::move(entry));
    }
    return table;
}

std::optional<DemoHeader> ParseDemoHeader(const std::vector<uint8_t>& file) {
    if (file.size() < kHeaderFieldsEnd) {
        return std::nullopt;
    }
    DemoHeader header;
    header.stage = static_cast<LevelId>(ReadS32(file, kStageOffset));
    header.difficulty = ReadS32(file, kDifficultyOffset);
    header.slot = ReadS32(file, kSlotOffset);
    header.randomSeed = ReadU64(file, kRandomSeedOffset);
    header.chrObjRandomSeed = ReadU64(file, kChrObjSeedOffset);
    header.controlStyle = ReadS32(file, kControlStyleOffset);
    return header;
}

std::optional<size_t> SelectRamrom(const std::vector<RamromEntry>& table, int highestStageUnlocked, uint32_t random) {
    size_t unlocked = 0;
    while (unlocked < table.size() && highestStageUnlocked >= table[unlocked].locked) {
        unlocked++;
    }
    if (unlocked == 0) {
        return std::nullopt;
    }
    return random % unlocked;
}

std::optional<SelectedDemo> LoadAttractDemo(DemoSource& source, size_t index) {
    const auto text = source.LoadTable();
    if (!text) {
        return std::nullopt;
    }
    const std::vector<RamromEntry> table = ParseRamromTable(*text);
    if (index >= table.size()) {
        return std::nullopt;
    }
    const auto file = source.LoadDemo(table[index].name);
    if (!file) {
        return std::nullopt;
    }
    const auto header = ParseDemoHeader(*file);
    if (!header) {
        return std::nullopt;
    }
    return SelectedDemo{ index, table[index].name, *header };
}

std::optional<SelectedDemo> SelectAttractDemo(DemoSource& source, int highestStageUnlocked, uint32_t random) {
    const auto text = source.LoadTable();
    if (!text) {
        return std::nullopt;
    }
    const auto index = SelectRamrom(ParseRamromTable(*text), highestStageUnlocked, random);
    if (!index) {
        return std::nullopt;
    }
    return LoadAttractDemo(source, *index);
}

} // namespace Frigate::Level
