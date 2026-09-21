#include "viewer/Viewer.h"

#include <cstdint>
#include <vector>

#include <spdlog/spdlog.h>

#include "fast/Fast3dWindow.h"
#include "ship/core/Context.h"
#include "ship/resource/ResourceManager.h"
#include "viewer/GbiF3dex.h"
#include "viewer/ModelStub.h"

namespace Frigate::Viewer {

namespace {

std::vector<Gfx> BuildClear() {
    using namespace F3dex;
    Gfx buf[16];
    Gfx* gfx = buf;
    // A fill whose color image matches the (default, zero) depth-buffer address
    // is dropped as a redundant depth clear, so point it somewhere else.
    gDPSetColorImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, reinterpret_cast<void*>(uintptr_t{ 0x00100000 }));
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPPipeSync(gfx++);
    *gfx++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_FILL);
    // No depth update, or the fill leaves nearest-depth values behind and
    // every later depth-tested fragment is rejected.
    *gfx++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_NOOP | G_RM_NOOP2);
    gDPSetFillColor(gfx++, (GPACK_RGBA5551(255, 0, 255, 1) << 16) | GPACK_RGBA5551(255, 0, 255, 1));
    gDPFillRectangle(gfx++, 0, 0, 319, 239);
    gDPPipeSync(gfx++);
    return std::vector<Gfx>(buf, gfx);
}

} // namespace

int Run(const std::shared_ptr<Ship::Context>& context, const Options& options) {
    auto window = context->GetChildren().GetFirst<Fast::Fast3dWindow>();

    std::vector<Gfx> clearList = BuildClear();
    std::unique_ptr<ModelStub> stub;
    if (options.model) {
        auto resourceManager = context->GetChildren().GetFirst<Ship::ResourceManager>();
        stub = ModelStub::Create(resourceManager, *options.model, options.distance, options.textures, clearList);
        if (stub == nullptr) {
            return 1;
        }
        spdlog::info("viewer: drawing {} lists of {}", stub->ListCount(), *options.model);
    }

    clearList.push_back(F3dex::EndDisplayList());

    // DrawAndRunGraphicsCommands runs the interpreter's own StartFrame/EndFrame,
    // so the loop must not call them again. It doesn't poll window events,
    // though; without HandleEvents() the close button never reaches IsRunning().
    while (window->IsRunning()) {
        window->HandleEvents();
        window->DrawAndRunGraphicsCommands(stub ? stub->Commands() : clearList.data(), {});
    }

    window->Close();
    return 0;
}

} // namespace Frigate::Viewer
