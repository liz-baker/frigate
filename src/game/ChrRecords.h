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
    // ChrModelFileRecord::scale and ::pov: the scale an instance starts with
    // (before makeonebody's 0.10000001) and its animation translation scale.
    float scale = 1.0f;
    float pov = 1.0f;
    // The model header's BoundingVolumeRadius.
    float radius = 0.0f;
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

// One entry of headHat_array_8003E464: where a hat sits on a male head.
struct HatPlacement {
    float xoffset = 0.0f;
    float yoffset = 0.0f;
    float zoffset = 0.0f;
    float xsize = 1.0f;
    float ysize = 1.0f;
    float zsize = 1.0f;
};

// The hat placements, from the `chr/hats` resource
// (docs/formats/goldeneye-chr-records.md).
class ChrHats {
  public:
    // Returns nullptr (after logging why) if the resource is missing or malformed.
    static std::shared_ptr<ChrHats> Load(const std::shared_ptr<Ship::ResourceManager>& resourceManager);

    // Parses the resource's text. Returns nullptr if it is malformed.
    static std::shared_ptr<ChrHats> Parse(std::string_view text);

    // headHat_array_8003E464[index]; nullptr for an index outside the table.
    const HatPlacement* At(size_t index) const {
        return index < entries_.size() ? &entries_[index] : nullptr;
    }

  private:
    std::vector<HatPlacement> entries_;
};

} // namespace Frigate::Game
