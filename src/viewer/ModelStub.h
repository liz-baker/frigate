// Temporary viewer glue, see GbiF3dex.h.
// TODO(https://github.com/liz-baker/frigate/issues/12): replaced by the
// scene-graph walk; until then a model is just all of its node lists drawn
// untransformed.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libultraship/libultra/gbi.h"
#include "viewer/Camera.h"

namespace Ship {
class IResource;
class ResourceManager;
} // namespace Ship

namespace Frigate::Viewer {

// Owns the hand-built list and the camera data it points into, so the
// pointers stay valid for as long as the list is drawn.
class ModelStub {
  public:
    // `prefix` runs first each frame (the framebuffer clear). Returns nullptr
    // (after logging why) if the model has no lists or any of them fails to
    // load.
    static std::unique_ptr<ModelStub> Create(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                             const std::string& modelPath, float distance, bool textures,
                                             const std::vector<Gfx>& prefix);

    Gfx* Commands() {
        return mCommands.data();
    }

    size_t ListCount() const {
        return mListCount;
    }

  private:
    // Held so the resource cache can't drop a list (and its edits) between
    // loading it here and the interpreter resolving it by hash.
    std::vector<std::shared_ptr<Ship::IResource>> mLists;
    Camera mCamera{};
    std::vector<Gfx> mCommands;
    size_t mListCount = 0;
};

} // namespace Frigate::Viewer
