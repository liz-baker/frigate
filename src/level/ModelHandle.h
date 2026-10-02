#pragma once

#include <array>
#include <cstddef>
#include <optional>
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

class ModelGeometrySource;

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
    // The model is no longer held by the model it was attached to.
    virtual void Detach() = 0;
    // The origin of the node in switch-array slot `slot` and the matrix of the
    // node (or its nearest owning ancestor) as the model was last posed, row
    // major in the game's row-vector layout. False when the slot is empty or the
    // model was not posed.
    virtual bool SwitchOrigin(size_t slot, std::array<float, 3>* origin) const {
        (void)slot;
        (void)origin;
        return false;
    }
    virtual bool SwitchMatrix(size_t slot, std::array<float, 16>* matrix) const {
        (void)slot;
        (void)matrix;
        return false;
    }
    // Shows or hides the switch node in slot `slot` and what is below it.
    virtual void SetSwitchVisible(size_t slot, bool visible) = 0;
    // Shows or hides the muzzle flash of the gunfire node in slot `slot`.
    virtual void SetGunfireVisible(size_t slot, bool visible) = 0;
};

// The parts of a vehicle's model its movement reads: the origins of the nodes
// in switch slots 1 to 4 (the points it sits on the ground by), and the
// bounds (xmin, xmax, ymin, ymax, zmin, zmax) of the boxes in slots 6 and 10.
struct VehicleGeometry {
    std::array<std::array<float, 3>, 4> points{};
    std::array<float, 6> hull{};
    std::array<float, 6> box{};
};

// The parts of the tank's model its movement, collision and pose read: the
// origins of the nodes in switch slots 1 to 4 (the turret's pivot, the seat the
// player sits at, the gun's pivot and its muzzle), and the bounds (xmin, xmax,
// ymin, ymax, zmin, zmax) of the boxes in slots 5 (the hull) and 6 (the top
// the player stands on).
struct TankGeometry {
    std::array<std::array<float, 3>, 4> nodes{};
    std::array<float, 6> hull{};
    std::array<float, 6> top{};
};

// The part of a CCTV camera's model its swing reads: the origin of the node in
// switch slot 0, which the camera turns about.
struct CctvGeometry {
    std::array<float, 3> pivot{};
};

// The parts of an autogun's model its pose and its fire read, by switch slot:
// the origins of the nodes in slots 1 to 4 and 6 (the heading and pitch pivots
// and the barrels'), the offsets of the muzzle flash nodes in slots 5 and 7,
// and the matrix number each node is posed under (modelFindNodeMtx), -1 where
// the slot is empty.
struct AutogunGeometry {
    std::array<std::array<float, 3>, 8> origin{};
    std::array<int, 8> matrix{};
    AutogunGeometry() {
        matrix.fill(-1);
    }
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
    // The geometry of the vehicle model at `modelPath`; nullopt, after logging
    // why, when it does not load or is not a vehicle's.
    virtual std::optional<VehicleGeometry> LoadVehicleGeometry(const std::string& modelPath) {
        (void)modelPath;
        return std::nullopt;
    }
    // The geometry of the tank model at `modelPath`; nullopt, after logging
    // why, when it does not load or is not a tank's.
    virtual std::optional<TankGeometry> LoadTankGeometry(const std::string& modelPath) {
        (void)modelPath;
        return std::nullopt;
    }
    // The geometry of the camera model at `modelPath`; nullopt, after logging
    // why, when it does not load or is not a camera's.
    virtual std::optional<CctvGeometry> LoadCctvGeometry(const std::string& modelPath) {
        (void)modelPath;
        return std::nullopt;
    }
    // The geometry of the autogun model at `modelPath`; nullopt, after logging
    // why, when it does not load or is not an autogun's.
    virtual std::optional<AutogunGeometry> LoadAutogunGeometry(const std::string& modelPath) {
        (void)modelPath;
        return std::nullopt;
    }
    // Where the hit tests read a model's triangles from; null when the factory
    // has none, and a shot then meets only the models' bounding volumes.
    virtual ModelGeometrySource* Geometry() {
        return nullptr;
    }
    // The animation clip `name` (`animations/<name>`), which the factory keeps
    // loaded for the stage. Null, after logging why, when it does not load; a
    // factory with no clips returns null.
    virtual const AnimationClip* LoadClip(const std::string& name) {
        (void)name;
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
