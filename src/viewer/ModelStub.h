// Temporary viewer glue, see GbiF3dex.h.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libultraship/libultra/gbi.h"
#include "model/ModelDraw.h"
#include "viewer/Camera.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Viewer {

// Owns the camera data and the ModelDraw whose commands point into it, so
// the pointers stay valid for as long as the list is drawn.
class ModelStub {
  public:
    // `prefix` runs first each frame (the framebuffer clear). Returns nullptr
    // (after logging why) if the model has no lists or any of them fails to
    // load.
    static std::unique_ptr<ModelStub> Create(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                             const std::string& modelPath, float distance, bool textures,
                                             const std::vector<Gfx>& prefix);

    Gfx* Commands() {
        return mDraw.commands.data();
    }

    size_t ListCount() const {
        return mDraw.lists.size();
    }

  private:
    Camera mCamera{};
    Model::ModelDraw mDraw;
};

} // namespace Frigate::Viewer
