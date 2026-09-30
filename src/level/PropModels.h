#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Frigate::Level {

// ModelRoData_BoundingBoxRecord's box, in model units.
struct ModelBounds {
    float xmin = 0;
    float xmax = 0;
    float ymin = 0;
    float ymax = 0;
    float zmin = 0;
    float zmax = 0;
};

// One PitemZ_entries record.
struct PropModel {
    // The record's model name (`alarm1`).
    std::string name;
    // ItemModelFileRecord.scale.
    float scale = 0.1f;
    // chrobjGetBboxFromObjFile's box; empty for a model with none.
    std::optional<ModelBounds> bounds;
    // The model's skeleton is skeleton_prop_weapon: the models a character
    // holds (weaponSetGunfireVisible only acts on these).
    bool weaponSkeleton = false;
    // The name of the model header's SKELETON, empty when it has none.
    std::string skeleton;
};

// The object model table, from the `prop/models` resource
// (docs/formats/goldeneye-prop-models.md).
class PropModels {
  public:
    // Parses the resource's text. Returns nullptr if it is malformed.
    static std::shared_ptr<const PropModels> Parse(std::string_view text);

    // PitemZ_entries[index]; null for an index outside the table.
    const PropModel* Model(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= models_.size()) {
            return nullptr;
        }
        return &models_[static_cast<size_t>(index)];
    }
    size_t Count() const {
        return models_.size();
    }

  private:
    std::vector<PropModel> models_;
};

} // namespace Frigate::Level
