#include "level/PropModels.h"

#include "level/TextTable.h"

namespace Frigate::Level {

std::shared_ptr<const PropModels> PropModels::Parse(std::string_view text) {
    std::shared_ptr<PropModels> table(new PropModels());
    for (std::string_view line : Split(text, '\n')) {
        if (line.empty()) {
            continue;
        }
        const auto fields = Split(line, '\t');
        size_t index = 0;
        PropModel model;
        if ((fields.size() != 12 && fields.size() != 18 && fields.size() != 19) || fields[0] != "model" || !ParseNumber(fields[1], index) ||
            index != table->models_.size() || !ParseFloat(fields[3], model.scale)) {
            return nullptr;
        }
        model.name = std::string(fields[2]);
        if (fields[4] != "-") {
            ModelBounds bounds;
            float* const values[6] = { &bounds.xmin, &bounds.xmax, &bounds.ymin,
                                       &bounds.ymax, &bounds.zmin, &bounds.zmax };
            for (int i = 0; i < 6; i++) {
                if (!ParseFloat(fields[4 + static_cast<size_t>(i)], *values[i])) {
                    return nullptr;
                }
            }
            model.bounds = bounds;
        }
        size_t explosion = 0;
        if (!ParseNumber(fields[11], explosion)) {
            return nullptr;
        }
        model.explosion = static_cast<int>(explosion);
        if (fields.size() == 19 && !ParseFloat(fields[18], model.radius)) {
            return nullptr;
        }
        for (size_t i = 0; fields.size() >= 18 && i < model.deformSeeds.size(); i++) {
            size_t seed = 0;
            if (!ParseNumber(fields[12 + i], seed) || seed > 0xFFFF) {
                return nullptr;
            }
            model.deformSeeds[i] = static_cast<uint16_t>(seed);
        }
        model.weaponSkeleton = fields[10] == "prop_weapon";
        if (fields[10] != "-") {
            model.skeleton = std::string(fields[10]);
        }
        table->models_.push_back(std::move(model));
    }
    if (table->models_.empty()) {
        return nullptr;
    }
    return table;
}

} // namespace Frigate::Level
