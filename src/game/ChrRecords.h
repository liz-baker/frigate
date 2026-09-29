#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Game {

// One slot of the decomp's c_item_entries: a body or a head model.
struct ChrRecord {
    std::vector<std::string> identifiers; // the BODY_* and HEAD_* names of the slot
    std::string modelPath;                // `models/chr/<file>`
    bool isMale = false;
    // The model carries its own head. A body without one takes a separate
    // head model.
    bool hasHead = false;
};

// The body and head table, from the `chr/records` resource
// (docs/formats/goldeneye-chr-records.md). Slots are named by the decomp's
// own identifiers.
class ChrRecords {
  public:
    // Returns nullptr (after logging why) if the resource is missing or malformed.
    static std::shared_ptr<ChrRecords> Load(const std::shared_ptr<Ship::ResourceManager>& resourceManager);

    // Parses the resource's text. Returns nullptr if it is malformed.
    static std::shared_ptr<ChrRecords> Parse(std::string_view text);

    // The slot named `identifier` (e.g. "BODY_Boris"), or nullptr.
    const ChrRecord* Find(std::string_view identifier) const;
    // The slot's index (the decomp's BODY_*/HEAD_* value), or -1.
    int Slot(std::string_view identifier) const;
    // c_item_entries[slot]; nullptr for an index outside the table.
    const ChrRecord* At(int slot) const {
        return slot >= 0 && static_cast<size_t>(slot) < records_.size() ? &records_[static_cast<size_t>(slot)]
                                                                        : nullptr;
    }

  private:
    std::vector<ChrRecord> records_;
};

} // namespace Frigate::Game
