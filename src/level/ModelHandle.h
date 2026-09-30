#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "model/Playback.h"

namespace Frigate {
struct AnimationClip;
} // namespace Frigate

namespace Frigate::Model {
class ModelInstance;
} // namespace Frigate::Model

namespace Frigate::Level {

// A model instance (Model::ModelInstance, which lives with the resource types)
// as the level code sees it: the animation state that places it, and how it
// hangs on another model.
class ModelHandle {
  public:
    virtual ~ModelHandle() = default;

    // The model's playback and root state.
    virtual ModelPlayback& Playback() = 0;
    // Marks this model as held by the node in switch-array slot `slot` of
    // `holder` (the decomp's attachedto and attachedto_objinst), turned by
    // `roll` radians about Z first. False if that slot is empty.
    virtual bool AttachTo(ModelHandle& holder, size_t slot, float roll) = 0;
    // Shows or hides the switch node in slot `slot` and what is below it.
    virtual void SetSwitchVisible(size_t slot, bool visible) = 0;
    // Shows or hides the muzzle flash of the gunfire node in slot `slot`.
    virtual void SetGunfireVisible(size_t slot, bool visible) = 0;
};

// Where the level's models come from.
class ModelFactory {
  public:
    virtual ~ModelFactory() = default;

    // makeonebody's instance: the model at `bodyPath`, with the head at
    // `headPath` (empty for none) on its head placeholder, whose sunglasses
    // show when `sunglasses`. Null, after logging why, when a model does not
    // load.
    virtual std::shared_ptr<ModelHandle> CreateBody(const std::string& bodyPath, const std::string& headPath,
                                                    bool sunglasses) = 0;
    // An instance of the object model at `modelPath`. Null, after logging why,
    // when it does not load.
    virtual std::shared_ptr<ModelHandle> CreateProp(const std::string& modelPath) = 0;
    // The first-person model at `modelPath` a hand holds. Null, after logging
    // why, when it does not load; a factory with no models returns null.
    virtual std::shared_ptr<Model::ModelInstance> CreateGun(const std::string& modelPath) {
        (void)modelPath;
        return nullptr;
    }
    // The animation clip `name` (`animations/<name>`) for `model`, which keeps
    // it loaded. Null, after logging why, when it does not load.
    virtual const AnimationClip* LoadAnimation(Model::ModelInstance& model, const std::string& name) {
        (void)model;
        (void)name;
        return nullptr;
    }
};

} // namespace Frigate::Level
