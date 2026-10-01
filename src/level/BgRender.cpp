#include "level/BgRender.h"

#include <algorithm>

#include "gfx/GbiF3dex.h"

namespace Frigate::Level {

namespace {

// D_8004485C: nothing in the game ever clears it, so every room refreshes
// every frame; the FrameStagger() check behind it never decides anything.
constexpr bool kRefreshEveryFrame = true;

Gfx* SetProjection(Gfx* dl, const Mtx::FixedMtx* matrix) {
    *dl++ = F3dex::Matrix(matrix, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    return dl;
}

} // namespace

BgRender::BgRender(BgLevel& level, BgVisibility& visibility, Objects& objects, Doors& doors, Chrs& chrs, const Fog& fog)
    : level_(level), visibility_(visibility), fog_(fog), propRender_(objects, doors, chrs, fog) {
}

void BgRender::Reset() {
    slots_.Reset(level_.MaxNumRooms());
}

void BgRender::Tick() {
    slots_.Tick();
}

void BgRender::UpdateOnScreenProps(const PropCamera& camera, const Vec3& modelPos, const Gu::MatrixF& modelviewF) {
    propRender_.UpdateOnScreenList(visibility_, camera, modelPos, modelviewF);
}

Gfx* BgRender::Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const RenderView& view) {
    // gSPSetLights1(GlobalLight), gSPLookAt(): written out as individual
    // commands, as the front-end screens do.
    gSPNumLights(dl++, NUMLIGHTS_1);
    gSPLight(dl++, &globalLight_.l[0], LIGHT_1);
    gSPLight(dl++, &globalLight_.a, LIGHT_1 + 1);
    gSPLookAt(dl++, view.lookAt);
    // gSPSegment(SPSEGMENT_BG_DL) has no counterpart: the packed lists name
    // their vertices and textures directly.

    // TODO(https://github.com/liz-baker/frigate/issues/128): a bg file that starts with a display list draws it
    // here in place of the rooms.
    if (level_.IsLoaded() && level_.Header().firstWord == 0) {
        dl = fog_.SetRenderFogColor(dl);

        // sub_GAME_7F0B8D78: Dam's lake is one giant room, drawn first.
        if (level_.LevelIndex() == FindLevelIndex(LevelId::Dam)) {
            constexpr int kDamLakeRoom = 0x23;
            visibility_.SetRoomDepth(kDamLakeRoom, 0);
        }
        dl = SetScissorCommand(RenderRooms(dl, resourceManager, view), ViewScissor(view.player));
        dl = fog_.ClearFogMode(dl);
    }

    dl = SetProjection(dl, view.viProjection);
    // bondviewGfxPlayerField5cMatrix.
    *dl++ = F3dex::Matrix(view.modelview, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    return dl;
}

Gfx* BgRender::RenderRooms(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                           const RenderView& view) {
    const std::vector<RoomDraw>& drawn = visibility_.RoomsDrawn();
    const PropFrame frame = { &visibility_, view.player, view.lodScaleZ };

    int minDepth = 99999999;
    int maxDepth = 0;
    for (const RoomDraw& draw : drawn) {
        maxDepth = std::max(maxDepth, draw.depth);
        minDepth = std::min(minDepth, draw.depth);
    }

    for (int depth = minDepth; depth <= maxDepth; depth++) {
        for (const RoomDraw& draw : drawn) {
            if (draw.depth != depth) {
                continue;
            }
            dl = SetProjection(dl, view.propProjection);
            dl = fog_.ClearFogMode(dl);
            if (view.drawEnabled) {
                dl = propRender_.RenderPass(dl, resourceManager, visibility_, draw.room, /*pass=*/0, view.stage,
                                            view.modelPos, *view.modelviewF, frame);
                dl = SetScissorCommand(dl, ViewScissor(view.player));
            }

            dl = SetProjection(dl, view.worldProjection);
            dl = fog_.SetRenderFogColor(SetScissorCommand(dl, BoundsScissor(view.player, draw.bounds)));
            if (view.drawEnabled) {
                dl = RenderRoomPrimary(dl, draw.room, view);
            }

            dl = SetProjection(dl, view.propProjection);
            dl = fog_.ClearFogMode(dl);
            if (view.drawEnabled) {
                dl = propRender_.RenderPass(dl, resourceManager, visibility_, draw.room, /*pass=*/2, view.stage,
                                            view.modelPos, *view.modelviewF, frame);
                dl = SetScissorCommand(dl, ViewScissor(view.player));
            }
        }
    }

    dl = SetScissorCommand(fog_.ClearFogMode(dl), ViewScissor(view.player));
    dl = SetProjection(dl, view.worldProjection);
    if (view.drawEnabled && roomMarks_) {
        dl = roomMarks_(dl);
    }

    for (int depth = maxDepth; depth >= minDepth; depth--) {
        for (const RoomDraw& draw : drawn) {
            if (draw.depth != depth) {
                continue;
            }
            dl = SetProjection(dl, view.worldProjection);
            dl = fog_.SetRenderFogColor(SetScissorCommand(dl, BoundsScissor(view.player, draw.bounds)));
            if (view.drawEnabled) {
                dl = RenderRoomSecondary(dl, draw.room, view);
            }

            dl = SetProjection(dl, view.propProjection);
            dl = fog_.ClearFogMode(dl);
            if (view.drawEnabled) {
                dl = propRender_.RenderPass(dl, resourceManager, visibility_, draw.room, /*pass=*/1, view.stage,
                                            view.modelPos, *view.modelviewF, frame);
                dl = SetScissorCommand(dl, ViewScissor(view.player));
            }
        }
    }
    return dl;
}

Gfx* BgRender::RenderRoomPrimary(Gfx* dl, int room, const RenderView& view) {
    if (room >= level_.MaxNumRooms()) {
        return dl;
    }
    if (!kRefreshEveryFrame && visibility_.FrameStagger() != room % 10) {
        return dl;
    }

    if (level_.Room(room).modelBinLoaded == 0 && level_.RoomLoadBudget() > 0) {
        level_.SetRoomLoadBudget(level_.RoomLoadBudget() - 1);
        level_.LoadRoomModelData(room);
    }
    if (level_.Room(room).modelBinLoaded == 0) {
        return dl;
    }

    dl = ApplyRoomMatrix(dl, room, view);
    dl = CallRoomList(dl, level_.Room(room).geometry.primaryList);
    level_.MarkRoomDrawn(room);
    return dl;
}

Gfx* BgRender::RenderRoomSecondary(Gfx* dl, int room, const RenderView& view) {
    if (room >= level_.MaxNumRooms()) {
        return dl;
    }
    if (level_.Room(room).geometry.secondaryList.Empty()) {
        return dl;
    }
    if (!kRefreshEveryFrame && visibility_.FrameStagger() != room % 10) {
        return dl;
    }

    if (level_.Room(room).modelBinLoaded == 0) {
        level_.LoadRoomModelData(room);
        return dl;
    }
    dl = ApplyRoomMatrix(dl, room, view);
    dl = CallRoomList(dl, level_.Room(room).geometry.secondaryList);
    level_.MarkRoomDrawn(room);
    return dl;
}

Gfx* BgRender::ApplyRoomMatrix(Gfx* dl, int room, const RenderView& view) {
    bool rebuild = false;
    const int slot = slots_.Acquire(room, view.currentRoom, rebuild);
    if (rebuild) {
        float matrix[4][4];
        RoomTransform(level_.RoomEntry(room).position, 1.0f / level_.Level().scale, view.modelPos, matrix);
        roomMatrices_[slot] = Mtx::ToFixedMtx(matrix);
    }
    *dl++ = F3dex::Matrix(&roomMatrices_[slot], Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    return dl;
}

Gfx* BgRender::CallRoomList(Gfx* dl, const RoomBlock& list) {
    if (list.Empty()) {
        return dl;
    }
    *dl++ = F3dex::DisplayList(list.data);
    return dl;
}

} // namespace Frigate::Level
