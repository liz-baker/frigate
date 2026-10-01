#include "level/RoomLoad.h"

#include "level/BgApply.h"

namespace Frigate::Level {

void RoomLoad::StageLoading() {
    fixtures_.InitTables();
    hits_.Reset();
}

void RoomLoad::RoomLoaded(int room, RoomGeometry& geometry) {
    auto* primary = static_cast<Gfx*>(geometry.primaryList.data);
    auto* secondary = static_cast<Gfx*>(geometry.secondaryList.data);

    if (!geometry.vertices.Empty()) {
        fixtures_.Redarken(room, static_cast<Vtx*>(geometry.vertices.data), geometry.vertices.count);
    }
    if (!geometry.primaryList.Empty()) {
        fixtures_.ClearRoom(room);
        fixtures_.ScanList(primary, geometry.primaryList.count);
    }
    if (!geometry.secondaryList.Empty()) {
        fixtures_.ScanList(secondary, geometry.secondaryList.count);
    }

    if (fog_.Enabled()) {
        ApplyDynamicCCRMLUT(primary, geometry.primaryList.count, RoomLut::PrimaryAddFog);
        if (!geometry.secondaryList.Empty()) {
            ApplyDynamicCCRMLUT(secondary, geometry.secondaryList.count, RoomLut::SecondaryAddFog);
        }
    } else {
        ApplyDynamicCCRMLUT(primary, geometry.primaryList.count, RoomLut::Primary);
        if (!geometry.secondaryList.Empty()) {
            ApplyDynamicCCRMLUT(secondary, geometry.secondaryList.count, RoomLut::Secondary);
        }
    }

    hits_.BuildBounds(room, geometry);
}

void RoomLoad::RoomUnloaded(int room) {
    hits_.DropBounds(room);
}

} // namespace Frigate::Level
