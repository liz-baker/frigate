#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Frigate::Level {

// The ids the AI and the character code start animations by: which clip each
// names (animation_table_ptrs1 for characters and animation_table_ptrs2 for
// aircraft). Format: docs/formats/goldeneye-animation-table.md.
class AnimationTable {
  public:
    // The `animations/table` resource's text; empty when it is malformed.
    static std::optional<AnimationTable> Parse(std::string_view text);

    // The clip animation id `id` of the characters' table names; empty for an
    // id with no clip or outside the table.
    const std::string& Character(int id) const {
        return Lookup(characters_, id);
    }
    // The same for the aircraft's table.
    const std::string& Aircraft(int index) const {
        return Lookup(aircraft_, index);
    }

  private:
    static const std::string& Lookup(const std::vector<std::string>& names, int id);

    std::vector<std::string> characters_;
    std::vector<std::string> aircraft_;
};

} // namespace Frigate::Level
