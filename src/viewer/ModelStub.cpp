#include "viewer/ModelStub.h"

#include <cstring>

#include <spdlog/spdlog.h>

#include "gfx/GbiF3dex.h"

namespace Frigate::Viewer {

std::unique_ptr<ModelStub> ModelStub::Create(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                             const std::string& modelPath, float distance, bool textures,
                                             const std::vector<Gfx>& prefix) {
    auto stub = std::unique_ptr<ModelStub>(new ModelStub());
    stub->mCamera = MakeCamera(distance, 320.0f / 240.0f);
    Mat4 base;
    std::memcpy(base.m, stub->mCamera.view, sizeof(base.m));

    using namespace Frigate::F3dex;
    std::vector<Gfx> cameraPrefix = prefix;
    cameraPrefix.push_back(Viewport(&stub->mCamera.viewport));
    // Modelview first: the projection load multiplies by the top of the
    // modelview stack, which is empty until the first modelview load.
    cameraPrefix.push_back(Matrix(&stub->mCamera.modelview, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD));
    cameraPrefix.push_back(Matrix(&stub->mCamera.projection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD));

    auto draw = Model::BuildModelDraw(resourceManager, modelPath, base, textures, cameraPrefix);
    if (!draw) {
        return nullptr;
    }
    stub->mDraw = std::move(*draw);
    stub->mDraw.commands.push_back(EndDisplayList());
    return stub;
}

} // namespace Frigate::Viewer
