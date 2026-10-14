#include "level/ScreenSetup.h"

#include "gfx/GbiF3dex.h"

namespace Frigate::Level {

Gfx* ClearPlayerZBuffer(Gfx* dl, int playerCount, int player, bool hiRes, bool cameraBuffer) {
    using namespace Frigate::F3dex;
    const Extent screen = ScreenExtent(cameraBuffer);
    const Extent zbuffer = ZBufferAllocation(playerCount, hiRes);
    const ZBufferRect rect = ZBufferClearRect(playerCount, player, hiRes, cameraBuffer);
    auto* const depth = reinterpret_cast<void*>(kZBufferAddress);

    // zbufInit.
    gDPPipeSync(dl++);
    gDPSetDepthImage(dl++, depth);
    // zbufClearCurrentPlayer.
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_NOOP | G_RM_NOOP2);
    gDPSetColorImage(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, zbuffer.width, depth);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_FILL);
    gDPSetFillColor(dl++, (GPACK_ZDZ(G_MAXFBZ, 0) << 16) | GPACK_ZDZ(G_MAXFBZ, 0));
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, screen.width, screen.height);
    gDPFillRectangle(dl++, rect.left, rect.top, rect.right, rect.bottom);
    gDPPipeSync(dl++);
    return dl;
}

Gfx* SelectFramebuffer(Gfx* dl, bool cameraBuffer) {
    using namespace Frigate::F3dex;
    gDPSetColorImage(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, ScreenExtent(cameraBuffer).width,
                     reinterpret_cast<void*>(kFramebufferAddress));
    return dl;
}

Gfx* FillUncoveredScreen(Gfx* dl, const ViewMetrics& vi, int playerCount, int position) {
    using namespace Frigate::F3dex;
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_FILL);
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, vi.x, vi.y);
    gDPSetFillColor(dl++, (GPACK_RGBA5551(0, 0, 0, 1) << 16) | GPACK_RGBA5551(0, 0, 0, 1));

    if (vi.viewTop > 0 && position == 0) {
        gDPFillRectangle(dl++, 0, 0, vi.x - 1, vi.viewTop - 1);
        gDPPipeSync(dl++);
    }

    if (playerCount == 1) {
        if (vi.viewTop + vi.viewHeight < vi.y) {
            gDPFillRectangle(dl++, 0, vi.viewTop + vi.viewHeight, vi.x - 1, vi.y - 1);
            gDPPipeSync(dl++);
        }
    } else if (position == 0) {
        if (vi.viewTop + vi.viewHeight + vi.viewHeight < vi.y) {
            gDPFillRectangle(dl++, 0, vi.viewTop + vi.viewHeight + vi.viewHeight, vi.x - 1, vi.y - 1);
            gDPPipeSync(dl++);
        }

        // The rows and columns between the views.
        gDPFillRectangle(dl++, 0, (kScreenHeight / 2) - 1, vi.x - 1, kScreenHeight / 2);
        gDPPipeSync(dl++);

        if (playerCount >= 3) {
            gDPFillRectangle(dl++, (kScreenWidth / 2) - 1, 0, kScreenWidth / 2, vi.y - 1);
            gDPPipeSync(dl++);
        }

        // The fourth view's quadrant, empty in a game of three, and the rows
        // under it.
        if (playerCount == 3) {
            gDPFillRectangle(dl++, (kScreenWidth / 2) + 1, (kScreenHeight / 2) + 1, vi.x - 1, vi.y - 1);
            gDPPipeSync(dl++);
        }
    }
    return dl;
}

} // namespace Frigate::Level
