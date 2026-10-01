#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#include "level/ModelHandle.h"
#include "level/ModelHit.h"
#include "model/ModelInstance.h"
#include "resource/type/AnimationClip.h"

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
    void Detach() override {
        instance_->Detach();
    }
    bool SwitchOrigin(size_t slot, std::array<float, 3>* origin) const override {
        const ModelNode* node = instance_->SwitchNode(slot);
        if (node == nullptr) {
            return false;
        }
        *origin = { node->origin[0], node->origin[1], node->origin[2] };
        return true;
    }
    bool SwitchMatrix(size_t slot, std::array<float, 16>* matrix) const override {
        const int index = instance_->MatrixIndexOfSwitch(slot);
        const auto& matrices = instance_->Walk().matrices;
        if (index < 0 || static_cast<size_t>(index) >= matrices.size()) {
            return false;
        }
        std::memcpy(matrix->data(), matrices[static_cast<size_t>(index)].m, sizeof(float) * 16);
        return true;
    }
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
class LevelModelFactory final : public ModelFactory, public ModelGeometrySource {
  public:
    explicit LevelModelFactory(std::shared_ptr<Ship::ResourceManager> resourceManager)
        : resourceManager_(std::move(resourceManager)) {
    }

    std::shared_ptr<ModelHandle> CreateBody(const std::string& bodyPath, const std::string& headPath,
                                            bool sunglasses) override;
    std::shared_ptr<ModelHandle> CreateProp(const std::string& modelPath) override;
    std::shared_ptr<Model::ModelInstance> CreateGun(const std::string& modelPath) override;
    std::optional<VehicleGeometry> LoadVehicleGeometry(const std::string& modelPath) override;
    ModelGeometrySource* Geometry() override {
        return this;
    }
    std::shared_ptr<const NodeGeometry> LoadNode(const std::string& modelPath, const ModelNode& node) override;
    const AnimationClip* LoadClip(const std::string& name) override;
    const AnimationClip* LoadAnimation(Model::ModelInstance& model, const std::string& name) override;

  private:
    std::shared_ptr<Ship::ResourceManager> resourceManager_;
    // The clips LoadClip has loaded, which stay loaded for the factory's life.
    std::unordered_map<std::string, std::shared_ptr<AnimationClipResource>> clips_;
    // The nodes LoadNode has loaded, by model and node; null for one that did
    // not load.
    std::unordered_map<std::string, std::shared_ptr<const NodeGeometry>> nodes_;
};

} // namespace Frigate::Level
