#pragma once

#include <memory>

#include "level/ModelHandle.h"
#include "model/ModelInstance.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

// A ModelInstance held by the level code.
class LevelModel final : public ModelHandle {
  public:
    explicit LevelModel(std::shared_ptr<Model::ModelInstance> instance) : instance_(std::move(instance)) {
    }

    ModelPlayback& Playback() override {
        return instance_->playback;
    }
    bool AttachTo(ModelHandle& holder, size_t slot, float roll) override;
    void SetSwitchVisible(size_t slot, bool visible) override {
        instance_->SetSwitchVisible(slot, visible);
    }
    void SetGunfireVisible(size_t slot, bool visible) override {
        instance_->SetGunfireVisible(slot, visible);
    }

    const std::shared_ptr<Model::ModelInstance>& Instance() const {
        return instance_;
    }

  private:
    std::shared_ptr<Model::ModelInstance> instance_;
};

// Loads the level's models from the packed archives.
class LevelModelFactory final : public ModelFactory {
  public:
    explicit LevelModelFactory(std::shared_ptr<Ship::ResourceManager> resourceManager)
        : resourceManager_(std::move(resourceManager)) {
    }

    std::shared_ptr<ModelHandle> CreateBody(const std::string& bodyPath, const std::string& headPath,
                                            bool sunglasses) override;
    std::shared_ptr<ModelHandle> CreateProp(const std::string& modelPath) override;

  private:
    std::shared_ptr<Ship::ResourceManager> resourceManager_;
};

} // namespace Frigate::Level
