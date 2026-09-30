#include "level/LevelModels.h"

#include <spdlog/spdlog.h>

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

const AnimationClip* LevelModelFactory::LoadAnimation(Model::ModelInstance& model, const std::string& name) {
    return model.LoadClip(resourceManager_, name);
}

} // namespace Frigate::Level
