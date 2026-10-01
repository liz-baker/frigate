#include "level/LevelModels.h"

#include <spdlog/spdlog.h>

#include <algorithm>

#include "resource/type/ModelTree.h"
#include "ship/resource/ResourceManager.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Level {

bool LevelModel::AttachTo(ModelHandle& holder, size_t slot, float roll) {
    return instance_->AttachTo(static_cast<LevelModel&>(holder).instance_, slot, roll);
}

std::shared_ptr<ModelHandle> LevelModelFactory::CreateBody(const std::string& bodyPath, const std::string& headPath,
                                                           bool sunglasses) {
    auto instance = Model::ModelInstance::Create(resourceManager_, bodyPath);
    if (instance == nullptr) {
        return nullptr;
    }
    if (!headPath.empty() && !instance->AttachHead(resourceManager_, headPath, sunglasses)) {
        spdlog::error("chr: can't attach the head at '{}' to '{}'", headPath, bodyPath);
        return nullptr;
    }
    return std::make_shared<LevelModel>(std::move(instance));
}

std::shared_ptr<ModelHandle> LevelModelFactory::CreateProp(const std::string& modelPath) {
    auto instance = Model::ModelInstance::Create(resourceManager_, modelPath);
    if (instance == nullptr) {
        return nullptr;
    }
    return std::make_shared<LevelModel>(std::move(instance));
}

std::shared_ptr<Model::ModelInstance> LevelModelFactory::CreateGun(const std::string& modelPath) {
    return Model::ModelInstance::Create(resourceManager_, modelPath);
}

std::optional<VehicleGeometry> LevelModelFactory::LoadVehicleGeometry(const std::string& modelPath) {
    const std::string path = modelPath + "/tree";
    auto tree = std::dynamic_pointer_cast<ModelTree>(resourceManager_->LoadResource(CRC64(path.c_str())));
    if (tree == nullptr) {
        spdlog::error("vehicle: no node tree at '{}' in the loaded archives", path);
        return std::nullopt;
    }
    const auto node = [&tree](size_t slot) -> const ModelNode* {
        if (slot >= tree->Switches.size() || tree->Switches[slot] < 0) {
            return nullptr;
        }
        return &tree->Nodes[static_cast<size_t>(tree->Switches[slot])];
    };

    VehicleGeometry geometry;
    for (size_t i = 0; i < geometry.points.size(); i++) {
        const ModelNode* point = node(i + 1);
        if (point == nullptr) {
            spdlog::error("vehicle: '{}' has no node in switch slot {}", path, i + 1);
            return std::nullopt;
        }
        std::copy(std::begin(point->origin), std::end(point->origin), geometry.points[i].begin());
    }
    const ModelNode* hull = node(6);
    const ModelNode* box = node(10);
    if (hull == nullptr || !hull->hasBounds || box == nullptr || !box->hasBounds) {
        spdlog::error("vehicle: '{}' has no bounding boxes in switch slots 6 and 10", path);
        return std::nullopt;
    }
    std::copy(std::begin(hull->bounds), std::end(hull->bounds), geometry.hull.begin());
    std::copy(std::begin(box->bounds), std::end(box->bounds), geometry.box.begin());
    return geometry;
}

const AnimationClip* LevelModelFactory::LoadClip(const std::string& name) {
    if (const auto found = clips_.find(name); found != clips_.end()) {
        return &found->second->View;
    }
    const std::string path = "animations/" + name;
    auto clip = std::dynamic_pointer_cast<AnimationClipResource>(resourceManager_->LoadResource(CRC64(path.c_str())));
    if (clip == nullptr) {
        spdlog::error("model: no animation clip '{}' in the loaded archives", path);
        return nullptr;
    }
    return &clips_.emplace(name, std::move(clip)).first->second->View;
}

const AnimationClip* LevelModelFactory::LoadAnimation(Model::ModelInstance& model, const std::string& name) {
    return model.LoadClip(resourceManager_, name);
}

} // namespace Frigate::Level
