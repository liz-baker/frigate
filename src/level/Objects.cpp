#include "level/Objects.h"

#include <algorithm>
#include <cmath>

#include "level/BgLevel.h"
#include "level/Inventory.h"
#include "level/PlayerWeapons.h"
#include "level/Stan.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

namespace {

// Whether `type`'s own setup function (setupCctv, setupAutogun,
// setupHangingMonitors, setupMultiMonitor, or the propobj.c cases for
// PROPDEF_TANK/PROPDEF_VEHICHLE/PROPDEF_AIRCRAFT) places it exactly like
// domakedefaultobj and only diverges afterward, to set up runtime state this
// port doesn't read yet:
//   - Cctv's look-pad aim matrix and player detection --
//     https://github.com/liz-baker/frigate/issues/187.
//   - Autogun's targeting, aim and firing state --
//     https://github.com/liz-baker/frigate/issues/188.
//   - Tank's shell projectile model preload and turret/hull orientation,
//     which only matter once it's drivable --
//     https://github.com/liz-baker/frigate/issues/148.
//   - Vehicle's and Aircraft's ailist/path-driven patrol movement --
//     https://github.com/liz-baker/frigate/issues/165 and
//     https://github.com/liz-baker/frigate/issues/166.
//   - Aircraft's rotor animation, which needs a posed-model render path that
//     doesn't exist yet -- https://github.com/liz-baker/frigate/issues/189.
// PropDefType::Monitor is also placed this way once Create rules out its own
// embedment case (see the PROPFLAG_INSIDEANOTHEROBJ check there).
bool IsPlainObject(uint8_t type) {
    switch (static_cast<PropDefType>(type)) {
        case PropDefType::Prop:
        case PropDefType::Alarm:
        case PropDefType::Magazine:
        case PropDefType::GasReleasing:
        case PropDefType::Unk41:
        case PropDefType::Glass:
        case PropDefType::Safe:
        case PropDefType::TintedGlass:
        case PropDefType::Cctv:
        case PropDefType::Autogun:
        case PropDefType::Rack:
        case PropDefType::Monitor:
        case PropDefType::MultiMonitor:
        case PropDefType::Vehicle:
        case PropDefType::Aircraft:
        case PropDefType::Tank:
            return true;
        default:
            return false;
    }
}

bool IsPickup(uint8_t type) {
    switch (static_cast<PropDefType>(type)) {
        case PropDefType::Collectable:
        case PropDefType::Key:
        case PropDefType::Hat:
        case PropDefType::Ammo:
        case PropDefType::Armour:
            return true;
        default:
            return false;
    }
}

// The first bound pad number: a `pad` at or above it indexes the bound pads.
constexpr int kFirstBoundPad = 10000;
constexpr float kExtraScaleUnit = 0.00390625f;
// The tinted glass portal is looked up between two points this far along the
// glass's normal from its centre.
constexpr float kGlassPortalReach = 10.0f;
// objChangeShading places an object this far above its tile.
constexpr float kRestHeight = 4.0f;
// setupUpdateObjectRoomPosition pads an object's box by this much.
constexpr float kRoomBoxPadding = 30.0f;
constexpr float kMinimumScaleRatio = 0.000001f;
constexpr float kPi = 3.14159265358979323846f;
// The rotation that turns an in-air-rotated object onto its side (3 pi / 2).
constexpr float kQuarterTurnBack = 4.712389f;
constexpr float kAircraftDrop = 200.0f;

// AMMOTYPE values a multi-ammo crate's slots and a collected weapon's ammo
// type test against; slot i of a crate grants type i + 1.
constexpr int32_t kAmmo9mm = 1;
constexpr int32_t kAmmo9mm2 = 2;
constexpr int32_t kAmmoRifle = 3;
constexpr int32_t kAmmoShotgun = 4;
constexpr int32_t kAmmoGrenade = 5;
constexpr int32_t kAmmoRemoteMine = 7;
constexpr int32_t kAmmoProxMine = 8;
constexpr int32_t kAmmoTimedMine = 9;
constexpr int32_t kAmmoKnife = 10;
constexpr int32_t kAmmoGrenadeRound = 11;
constexpr int32_t kAmmoMagnum = 12;
constexpr int32_t kAmmoGgun = 13;
constexpr int32_t kAmmoDarts = 14;

// ITEM_IDS values add_ammo_to_inventory grants alongside ammo of a type
// that's also a throwable/plantable weapon (the multi-ammo crate's slots
// overlap these types).
constexpr int32_t kItemThrowKnife = 3;
constexpr int32_t kItemGrenade = 26;
constexpr int32_t kItemTimedMine = 27;
constexpr int32_t kItemProximityMine = 28;
constexpr int32_t kItemRemoteMine = 29;
constexpr int32_t kItemTrigger = 30;

// ammo_collected_from_weapon's per-type flat quantities (before the solo
// ammo multiplier), for a weapon whose ObjectFlag::WeaponNoAmmo isn't set.
int32_t AmmoCollectedFromWeapon(int32_t ammoType) {
    switch (ammoType) {
        case kAmmo9mm:
        case kAmmo9mm2:
        case kAmmoRifle:
            return 10;
        case kAmmoShotgun:
        case kAmmoMagnum:
            return 5;
        case kAmmoGgun:
            return 3;
        case kAmmoDarts:
            return 4;
        case kAmmoGrenadeRound:
            return 3;
        default:
            return 1;
    }
}

// A multi-ammo crate's slot index maps to AMMOTYPE i + 1, except slot 1
// (AMMO_9MM_2) grants the shared AMMO_9MM pool instead of its own.
int32_t AmmoCrateSlotType(size_t slot) {
    const int32_t type = static_cast<int32_t>(slot) + 1;
    return type == kAmmo9mm2 ? kAmmo9mm : type;
}

} // namespace

float SumMatrixPos(const ModelBounds& bounds, const Mtxf& matrix, int axis) {
    const float low[3] = { bounds.xmin, bounds.ymin, bounds.zmin };
    const float high[3] = { bounds.xmax, bounds.ymax, bounds.zmax };
    float sum = 0.0f;
    for (int i = 0; i < 3; i++) {
        const float entry = matrix.m[i][axis];
        sum += (entry >= 0.0f ? low[i] : high[i]) * entry;
    }
    return sum;
}

float SumMatrixNeg(const ModelBounds& bounds, const Mtxf& matrix, int axis) {
    const float low[3] = { bounds.xmin, bounds.ymin, bounds.zmin };
    const float high[3] = { bounds.xmax, bounds.ymax, bounds.zmax };
    float sum = 0.0f;
    for (int i = 0; i < 3; i++) {
        const float entry = matrix.m[i][axis];
        sum += (entry <= 0.0f ? low[i] : high[i]) * entry;
    }
    return sum;
}

void BuildCollisionPolygon(const ModelBounds& bounds, const Mtxf& matrix, CollisionBlock* out) {
    const double x[2] = { bounds.xmin, bounds.xmax };
    const double y[2] = { bounds.ymin, bounds.ymax };
    const double z[2] = { bounds.zmin, bounds.zmax };
    const double m00 = matrix.m[0][0];
    const double m02 = matrix.m[0][2];
    const double m10 = matrix.m[1][0];
    const double m12 = matrix.m[1][2];
    const double m20 = matrix.m[2][0];
    const double m22 = matrix.m[2][2];

    // The eight corners' x and z, with x the slowest-varying bound.
    double pts[8][2];
    for (int i = 0; i < 8; i++) {
        const int xi = (i >> 2) & 1;
        const int yi = (i >> 1) & 1;
        const int zi = i & 1;
        pts[i][0] = ((m00 * x[xi]) + (m10 * y[yi])) + (m20 * z[zi]);
        pts[i][1] = ((m02 * x[xi]) + (m12 * y[yi])) + (m22 * z[zi]);
    }

    int minx = 0;
    int maxx = 0;
    int minz = 0;
    int maxz = 0;
    for (int i = 1; i < 8; i++) {
        if (pts[i][0] < pts[minx][0] || (pts[i][0] == pts[minx][0] && pts[i][1] < pts[minx][1])) {
            minx = i;
        }
    }
    for (int i = 1; i < 8; i++) {
        if (pts[maxz][1] < pts[i][1] || (pts[i][1] == pts[maxz][1] && pts[i][0] < pts[maxz][0])) {
            maxz = i;
        }
    }
    for (int i = 1; i < 8; i++) {
        if (pts[maxx][0] < pts[i][0] || (pts[i][0] == pts[maxx][0] && pts[maxx][1] < pts[i][1])) {
            maxx = i;
        }
    }
    for (int i = 1; i < 8; i++) {
        if (pts[i][1] < pts[minz][1] || (pts[i][1] == pts[minz][1] && pts[minz][0] < pts[i][0])) {
            minz = i;
        }
    }

    // The corners that are not an extreme, in corner order.
    std::array<int, 8> rest{};
    int restCount = 0;
    for (int i = 0; i < 8; i++) {
        if (i != minx && i != maxx && i != maxz && i != minz) {
            rest[static_cast<size_t>(restCount++)] = i;
        }
    }

    int count = 0;
    auto add = [&](int corner) {
        out->polygon[static_cast<size_t>(count)] = { static_cast<float>(pts[corner][0]),
                                                     static_cast<float>(pts[corner][1]) };
        count++;
    };
    // After an extreme goes the first remaining corner on the far side of the
    // line from the next extreme `base` to it.
    auto addBeyond = [&](int extreme, int base) {
        for (int i = 0; i < 4; i++) {
            const int index = rest[static_cast<size_t>(i)];
            if ((pts[index][0] - pts[base][0]) * (pts[extreme][1] - pts[base][1]) <
                (pts[extreme][0] - pts[base][0]) * (pts[index][1] - pts[base][1])) {
                add(index);
                break;
            }
        }
    };

    add(minx);
    addBeyond(minx, minz);
    add(minz);
    addBeyond(minz, maxx);
    add(maxx);
    addBeyond(maxx, maxz);
    add(maxz);
    addBeyond(maxz, minx);

    out->edges = count;
    for (int i = 0; i < count; i++) {
        out->polygon[static_cast<size_t>(i)][0] += matrix.m[3][0];
        out->polygon[static_cast<size_t>(i)][1] += matrix.m[3][2];
    }
}

Vec3 BoundPadCentre(const BoundPad& pad) {
    const Vec3& up = pad.pad.up;
    const Vec3& look = pad.pad.look;
    Vec3 normal = { (up[1] * look[2]) - (up[2] * look[1]), (up[2] * look[0]) - (up[0] * look[2]),
                    (up[0] * look[1]) - (up[1] * look[0]) };
    const float scale = 1.0f / std::sqrt((normal[0] * normal[0]) + (normal[1] * normal[1]) + (normal[2] * normal[2]));
    for (float& component : normal) {
        component *= scale;
    }
    const auto& box = pad.box;
    Vec3 centre{};
    for (size_t i = 0; i < 3; i++) {
        centre[i] = pad.pad.position[i] +
                    ((box[0] + box[1]) * normal[i] + (box[2] + box[3]) * up[i] + (box[4] + box[5]) * look[i]) * 0.5f;
    }
    return centre;
}

bool PointInPolygon(const Vec3& point, const std::array<Vec2, 8>& polygon, int edges) {
    if (edges <= 0) {
        return false;
    }
    int inside = -1;
    for (int i = 0; i < edges; i++) {
        const Vec2& from = polygon[static_cast<size_t>(i)];
        const Vec2& to = polygon[static_cast<size_t>((i + 1) % edges)];
        const float diff = ((to[1] - from[1]) * (point[0] - from[0])) - ((to[0] - from[0]) * (point[2] - from[1]));
        if (diff != 0.0f) {
            if (i == 0 || inside < 0) {
                inside = diff > 0.0f ? 1 : 0;
                continue;
            }
            if ((inside != 0 && diff < 0.0f) || (inside == 0 && diff > 0.0f)) {
                return false;
            }
        }
    }
    return true;
}

bool Objects::Serves(uint8_t type) {
    return IsPlainObject(type) || IsPickup(type);
}

void Objects::Reset(int roomCount, Stan* stan, const BgLevel* level, std::shared_ptr<const PropModels> models) {
    stan_ = stan;
    level_ = level;
    models_ = std::move(models);
    objects_.clear();
    byDef_.clear();
    viewerFootprint_ = nullptr;
    doorPasses_ = nullptr;
    doorCollision_ = nullptr;
    props_.Reset(roomCount, stan, level);
}

bool Objects::Walk(int* tile, const Vec3& from, const Vec3& to) const {
    if (*tile == kNoTile) {
        return false;
    }
    return stan_->WalkTiles(tile, from[0], from[2], to[0], to[2]);
}

const ModelBounds* Objects::Bounds(const PlacedObject& object) const {
    const PropModel* model = models_ ? models_->Model(object.model) : nullptr;
    return model != nullptr && model->bounds ? &*model->bounds : nullptr;
}

int Objects::Init(PlacedObject* object, const PropModel& model) {
    const int prop = props_.Allocate();
    if (prop < 0) {
        return -1;
    }
    if ((object->flags & ObjectFlag::ForceCollisions) != 0) {
        object->collision = CollisionBlock{};
        object->state |= ObjectState::ExtCollisionBlock;
    } else {
        object->state &= static_cast<uint8_t>(~ObjectState::ExtCollisionBlock);
    }
    object->prop = prop;
    object->scale = model.scale;
    object->runtimePosition = {};
    Prop& record = props_[prop];
    record.type = PropType::Obj;
    record.position = {};
    record.stan = kNoTile;
    return prop;
}

PlacedObject& Objects::Store(const PlacedObject& object) {
    objects_.push_back(object);
    const int index = static_cast<int>(objects_.size()) - 1;
    props_[object.prop].object = index;
    if (byDef_.size() <= object.def) {
        byDef_.resize(object.def + 1, -1);
    }
    byDef_[object.def] = index;
    return objects_.back();
}

const PlacedObject* Objects::ForDef(size_t def) const {
    return def < byDef_.size() && byDef_[def] >= 0 ? &objects_[static_cast<size_t>(byDef_[def])] : nullptr;
}

void Objects::LinkEmbedded(const StageSetup& setup) {
    const auto& defs = setup.PropDefs();
    for (size_t index = 0; index < defs.size(); index++) {
        const PropDef& def = defs[index];
        switch (static_cast<PropDefType>(def.type)) {
            case PropDefType::Prop:
            case PropDefType::Key:
            case PropDefType::Magazine:
            case PropDefType::Collectable:
            case PropDefType::Monitor:
            case PropDefType::Ammo:
            case PropDefType::Armour:
            case PropDefType::GasReleasing:
            case PropDefType::Unk41:
            case PropDefType::Glass:
            case PropDefType::Safe:
            case PropDefType::TintedGlass:
            case PropDefType::Cctv:
            case PropDefType::Autogun:
            case PropDefType::Rack:
            case PropDefType::MultiMonitor:
            case PropDefType::Vehicle:
            case PropDefType::Aircraft:
            case PropDefType::Tank:
                break;
            default:
                continue;
        }
        if ((def.flags & ObjectFlag::InsideAnotherObject) == 0 || byDef_.size() <= index || byDef_[index] < 0) {
            continue;
        }
        const int64_t hostIndex = static_cast<int64_t>(index) + def.pad;
        if (hostIndex < 0) {
            continue;
        }
        const PlacedObject* host = ForDef(static_cast<size_t>(hostIndex));
        if (host == nullptr) {
            continue;
        }
        PlacedObject& child = objects_[static_cast<size_t>(byDef_[index])];
        child.runtimeFlags |= ObjectRuntime::HasOwner;
        props_.Reparent(child.prop, host->prop);
    }
}

void Objects::SetPosition(PlacedObject* object, const Vec3& position, const Mtxf& matrix, int stan) {
    Prop& prop = props_[object->prop];
    object->mtx = matrix;
    object->runtimePosition = position;
    prop.position = position;
    prop.stan = stan;
    // TODO(https://github.com/liz-baker/frigate/issues/126): the shade colour
    // (sub_GAME_7F0402B4) an object takes from the colour of its stan tile
    // unless it is illuminated, which drawing the object reads.
}

void Objects::UpdateCollision(PlacedObject* object) {
    if (!object->collision) {
        return;
    }
    const ModelBounds* bounds = Bounds(*object);
    if (bounds == nullptr) {
        return;
    }
    Mtxf matrix = object->mtx;
    for (size_t i = 0; i < 3; i++) {
        matrix.m[3][i] = object->runtimePosition[i];
    }
    CollisionBlock& block = *object->collision;
    BuildCollisionPolygon(*bounds, matrix, &block);
    block.bottom = object->runtimePosition[1] + SumMatrixPos(*bounds, matrix, 1);
    block.top = object->runtimePosition[1] + SumMatrixNeg(*bounds, matrix, 1);
    if (object->type == static_cast<uint8_t>(PropDefType::Aircraft)) {
        block.bottom -= kAircraftDrop;
    }
}

void Objects::UpdateRooms(PlacedObject* object) {
    Prop& prop = props_[object->prop];
    float radius = 0.0f;
    props_.DeregisterRooms(object->prop);

    if ((object->flags2 & ObjectFlag2::TileRoomsOnly) != 0) {
        if (prop.stan != kNoTile) {
            prop.rooms[0] = static_cast<uint8_t>(stan_->Tile(prop.stan).room);
            prop.rooms[1] = kNoRoom;
        } else {
            prop.rooms[0] = kNoRoom;
        }
    } else if (const ModelBounds* bounds = Bounds(*object)) {
        Vec3 low = { SumMatrixPos(*bounds, object->mtx, 0) - kRoomBoxPadding, SumMatrixPos(*bounds, object->mtx, 1),
                     SumMatrixPos(*bounds, object->mtx, 2) - kRoomBoxPadding };
        Vec3 high = { SumMatrixNeg(*bounds, object->mtx, 0) + kRoomBoxPadding, SumMatrixNeg(*bounds, object->mtx, 1),
                      SumMatrixNeg(*bounds, object->mtx, 2) + kRoomBoxPadding };
        radius = std::max({ radius, -low[0], -low[2], high[0], high[2] });
        for (size_t i = 0; i < 3; i++) {
            low[i] += object->runtimePosition[i];
            high[i] += object->runtimePosition[i];
        }
        props_.UpdateRoomList(object->prop, low, high, radius);
    }
    props_.RegisterRooms(object->prop);
}

const CollisionBlock* Objects::Collision(int prop) const {
    const Prop& record = props_[prop];
    if (record.type != PropType::Obj || record.object < 0) {
        return nullptr;
    }
    const PlacedObject& object = objects_[static_cast<size_t>(record.object)];
    if (object.collision && (object.flags & ObjectFlag::ForceCollisions) != 0 &&
        (object.state & ObjectState::State20) == 0) {
        return &*object.collision;
    }
    return nullptr;
}

const PlacedObject* Objects::ObjectBeneath(const Vec3& position, int room) const {
    const auto& active = props_.Active();
    for (auto it = active.rbegin(); it != active.rend(); ++it) {
        const Prop& prop = props_[*it];
        if (prop.type != PropType::Obj || prop.stan == kNoTile || stan_->Tile(prop.stan).room != room) {
            continue;
        }
        const CollisionBlock* block = Collision(*it);
        if (block != nullptr && PointInPolygon(position, block->polygon, block->edges)) {
            return &objects_[static_cast<size_t>(prop.object)];
        }
    }
    return nullptr;
}

void Objects::MoveToPad(PlacedObject* object, const Vec3& position, const Mtxf& matrix, int stan, const Vec3& anchor) {
    const ModelBounds& bounds = *Bounds(*object);
    // The bounds' y extent is what the placement treats as x and y.
    const float lowY = bounds.ymin;
    const float highY = bounds.ymax;
    int walked = stan;
    Mtxf placed = matrix;
    Vec3 target{};

    if ((object->flags & ObjectFlag::InAirUpsideDown) != 0) {
        placed = Multiply(matrix, RotationZ(kPi));
        for (size_t i = 0; i < 3; i++) {
            target[i] = anchor[i] - (placed.m[1][i] * highY);
        }
    } else if ((object->flags & ObjectFlag::InAir) != 0) {
        for (size_t i = 0; i < 3; i++) {
            target[i] = anchor[i] - (placed.m[1][i] * lowY);
        }
    } else {
        const float floorY = stan_->PositionY(walked, position[0], position[2]);
        target[0] = anchor[0] - (placed.m[1][0] * lowY);
        target[2] = anchor[2] - (placed.m[1][2] * lowY);
        const PlacedObject* beneath = ObjectBeneath(position, stan_->Tile(stan).room);
        bool resting = false;
        if (beneath != nullptr) {
            const CollisionBlock* block = Collision(beneath->prop);
            if (block != nullptr && floorY < block->top &&
                block->bottom < ((placed.m[1][1] * (highY - lowY)) + floorY + kRestHeight)) {
                target[1] = block->top - (placed.m[1][1] * lowY);
                object->runtimeFlags |= ObjectRuntime::RestingOnObject;
                resting = true;
            }
        }
        if (!resting) {
            target[1] = (floorY - (placed.m[1][1] * lowY)) + kRestHeight;
        }
    }

    if ((object->flags2 & ObjectFlag2::PlaceAtPad) == 0 && Walk(&walked, position, target)) {
        SetPosition(object, target, placed, walked);
    } else {
        SetPosition(object, position, placed, stan);
        if ((object->flags2 & ObjectFlag2::PlaceAtPad) != 0 || (object->flags & ObjectFlag::AbsolutePosition) != 0) {
            object->runtimePosition = target;
        }
    }
    UpdateCollision(object);
}

void Objects::MoveToPadRotated(PlacedObject* object, const Vec3& position, const Mtxf& matrix, int stan,
                               const Vec3& anchor) {
    const float lowZ = Bounds(*object)->zmin;
    Mtxf placed = RotationX(kQuarterTurnBack);
    placed = Multiply(RotationY(kPi), placed);
    placed = Multiply(matrix, placed);

    Vec3 target{};
    for (size_t i = 0; i < 3; i++) {
        target[i] = anchor[i] - (placed.m[2][i] * lowZ);
    }
    int walked = stan;
    if ((object->flags2 & ObjectFlag2::PlaceAtPad) == 0 && Walk(&walked, position, target)) {
        SetPosition(object, target, placed, walked);
    } else {
        SetPosition(object, position, placed, stan);
        object->runtimePosition = target;
    }
    UpdateCollision(object);
}

bool Objects::Create(const StageSetup& setup, size_t defIndex) {
    const PropDef& def = setup.PropDefs()[defIndex];
    // Doors (#158) are placed by the Doors class instead; Stage's setup loop
    // never routes a door record here.
    if (!Serves(def.type) || !models_) {
        return false;
    }
    // weaponAssignToHome places nothing for an unarmed collectable record.
    if (def.Is(PropDefType::Collectable) && def.weaponNum == kItemUnarmed) {
        return false;
    }
    if (def.Is(PropDefType::Monitor) && def.pad < 0 && (def.flags & ObjectFlag::InsideAnotherObject) == 0) {
        // setupSingleMonitor's other branch: a monitor with no pad of its own
        // attaches to a specific switch node of another already-placed
        // object (its OwnerOffset/OwnerPart fields) instead of sitting on a
        // pad. That needs the model switch-node attachment machinery
        // https://github.com/liz-baker/frigate/issues/176 tracks, which
        // doesn't exist yet.
        return false;
    }
    const PropModel* model = models_->Model(def.model);
    if (model == nullptr) {
        return false;
    }

    PlacedObject object;
    object.def = defIndex;
    object.type = def.type;
    object.model = def.model;
    object.flags = def.flags;
    object.flags2 = def.flags2;
    object.state = def.state;
    object.scale = model->scale;
    const float extraScale = static_cast<float>(def.extraScale) * kExtraScaleUnit;

    // TODO(https://github.com/liz-baker/frigate/issues/168): the object's
    // damage and the tinted glass record's second fixed-point field.
    // TODO(https://github.com/liz-baker/frigate/issues/136): the respawn state
    // multiplayer gives every object.

    if ((def.flags & ObjectFlag::InsideAnotherObject) != 0) {
        // LinkEmbedded attaches it once every object exists.
        if (Init(&object, *model) < 0) {
            return false;
        }
        object.scale *= extraScale;
        Store(object);
        return true;
    }
    if ((def.flags & ObjectFlag::AssignedToChr) != 0) {
        // TODO(https://github.com/liz-baker/frigate/issues/164):
        // chrFindByLiteralId(pad) and chrpropReparent, which attach the object
        // to the guard its pad number names.
        return false;
    }

    const bool onBoundPad = def.pad >= kFirstBoundPad;
    const BoundPad* bound = nullptr;
    Mtxf matrix{};
    Vec3 position{};
    Vec3 anchor{};
    int stan = kNoTile;
    if (!onBoundPad) {
        if (def.pad < 0 || static_cast<size_t>(def.pad) >= setup.Pads().size()) {
            return false;
        }
        const Pad& pad = setup.Pads()[static_cast<size_t>(def.pad)];
        matrix = BasisToward({ -pad.look[0], -pad.look[1], -pad.look[2] }, pad.up);
        position = pad.position;
        anchor = pad.position;
        stan = pad.stanTile;
    } else {
        const size_t index = static_cast<size_t>(def.pad - kFirstBoundPad);
        if (index >= setup.BoundPads().size()) {
            return false;
        }
        bound = &setup.BoundPads()[index];
        const Pad& pad = bound->pad;
        matrix = BasisToward({ -pad.look[0], -pad.look[1], -pad.look[2] }, pad.up);
        const Vec3 centre = BoundPadCentre(*bound);
        const float half = (bound->box[2] - bound->box[3]) * 0.5f;
        for (size_t i = 0; i < 3; i++) {
            anchor[i] = centre[i] + (pad.up[i] * half);
        }
        if ((def.flags2 & ObjectFlag2::PlaceAtPad) == 0) {
            position = centre;
            stan = pad.stanTile;
            if (!Walk(&stan, pad.position, position)) {
                position = pad.position;
                stan = pad.stanTile;
            }
        } else {
            position = pad.position;
            stan = pad.stanTile;
        }
    }
    // getposstan with no radius: the position needs a tile.
    if (stan == kNoTile || model->bounds == std::nullopt) {
        return false;
    }

    if (Init(&object, *model) < 0) {
        return false;
    }
    PlacedObject& placed = Store(object);

    if (bound != nullptr) {
        // Fit the model to the bound pad's box.
        const ModelBounds& box = *model->bounds;
        const bool rotated = (placed.flags & ObjectFlag::InAirRotated) != 0;
        float ratioX = 1.0f;
        float ratioY = 1.0f;
        float ratioZ = 1.0f;
        const auto& fit = bound->box;
        if ((placed.flags & (ObjectFlag::ScaleToPad | ObjectFlag::ScaleToPadX)) != 0 && box.xmin < box.xmax) {
            ratioX = (fit[1] - fit[0]) / ((box.xmax - box.xmin) * placed.scale);
        }
        if ((placed.flags & (ObjectFlag::ScaleToPad | ObjectFlag::ScaleToPadY)) != 0 && box.ymin < box.ymax) {
            if (rotated) {
                ratioZ = (fit[5] - fit[4]) / ((box.ymax - box.ymin) * placed.scale);
            } else {
                ratioY = (fit[3] - fit[2]) / ((box.ymax - box.ymin) * placed.scale);
            }
        }
        if ((placed.flags & (ObjectFlag::ScaleToPad | ObjectFlag::ScaleToPadZ)) != 0 && box.zmin < box.zmax) {
            if (rotated) {
                ratioY = (fit[3] - fit[2]) / ((box.zmax - box.zmin) * placed.scale);
            } else {
                ratioZ = (fit[5] - fit[4]) / ((box.zmax - box.zmin) * placed.scale);
            }
        }

        const float smallest = std::min({ ratioX, ratioY, ratioZ });
        const float largest = std::max({ ratioX, ratioY, ratioZ });
        if ((placed.flags & ObjectFlag::ScaleToPad) != 0) {
            ratioX = ratioY = ratioZ = smallest;
        } else {
            // An axis the pad does not scale, on a model with no extent
            // there, takes the largest ratio.
            if ((placed.flags & ObjectFlag::ScaleToPadX) == 0 && box.xmax == box.xmin) {
                ratioX = largest;
            }
            if ((placed.flags & ObjectFlag::ScaleToPadY) == 0 && box.ymax == box.ymin) {
                if (rotated) {
                    ratioZ = largest;
                } else {
                    ratioY = largest;
                }
            }
            if ((placed.flags & ObjectFlag::ScaleToPadZ) == 0 && box.zmax == box.zmin) {
                if (rotated) {
                    ratioY = largest;
                } else {
                    ratioZ = largest;
                }
            }
        }
        ratioX /= largest;
        ratioY /= largest;
        ratioZ /= largest;
        if (ratioX <= kMinimumScaleRatio || ratioY <= kMinimumScaleRatio || ratioZ <= kMinimumScaleRatio) {
            ratioX = ratioY = ratioZ = 1.0f;
        }
        ScaleRowBasis(&matrix, 0, ratioX);
        ScaleRowBasis(&matrix, 1, ratioY);
        ScaleRowBasis(&matrix, 2, ratioZ);
        placed.scale *= largest;
    }

    placed.scale *= extraScale;
    ScaleRows(&matrix, placed.scale);
    if ((placed.flags & ObjectFlag::InAirRotated) != 0) {
        MoveToPadRotated(&placed, position, matrix, stan, anchor);
    } else {
        MoveToPad(&placed, position, matrix, stan, anchor);
    }
    UpdateRooms(&placed);
    props_.Activate(placed.prop);
    props_.Enable(placed.prop);

    if (def.Is(PropDefType::TintedGlass) && (def.flags & ObjectFlag::GlassHasPortal) != 0 && onBoundPad &&
        level_ != nullptr) {
        const Vec3 centre = BoundPadCentre(*bound);
        const Vec3& up = bound->pad.up;
        Vec3 above{};
        Vec3 below{};
        for (size_t i = 0; i < 3; i++) {
            above[i] = (kGlassPortalReach * up[i]) + centre[i];
            below[i] = centre[i] - (kGlassPortalReach * up[i]);
        }
        placed.portal = level_->FindWindowPortal(below, above);
    }
    if (def.Is(PropDefType::Monitor) && (def.flags & ObjectFlag::MonitorRenderPostBg) != 0) {
        props_[placed.prop].flags |= PropFlag::RenderPostBg;
    }
    return true;
}

void Objects::Colliders(const int* rooms, int cdTypes, std::vector<PropCollider>* out) const {
    for (const int16_t propnum : props_.RoomProps(rooms)) {
        const Prop& prop = props_[propnum];
        if (prop.type == PropType::Viewer) {
            PropCollider collider;
            if ((cdTypes & CdType::Players) != 0 && viewerFootprint_ && viewerFootprint_(&collider)) {
                collider.prop = propnum;
                out->push_back(collider);
            }
            continue;
        }
        if (prop.type == PropType::Door) {
            if (!doorPasses_ || !doorPasses_(propnum, cdTypes) || !doorCollision_) {
                continue;
            }
            const CollisionBlock* block = doorCollision_(propnum);
            if (block == nullptr || block->edges <= 0) {
                continue;
            }
            PropCollider collider;
            collider.polygon = block->polygon;
            collider.edges = block->edges;
            collider.top = block->top;
            collider.bottom = block->bottom;
            collider.prop = propnum;
            out->push_back(collider);
            continue;
        }
        // TODO(https://github.com/liz-baker/frigate/issues/164): characters'
        // props.
        if (prop.type != PropType::Obj) {
            continue;
        }
        const PlacedObject& object = objects_[static_cast<size_t>(prop.object)];
        // propIsOfCdType, for an object.
        if ((cdTypes & CdType::AiOpaque) != 0 && (object.flags & ObjectFlag::AiSeesThrough) != 0) {
            continue;
        }
        if ((cdTypes & CdType::ObjsImmuneToExplosions) != 0 && (object.flags & ObjectFlag::Invincible) == 0) {
            continue;
        }
        if ((object.flags & ObjectFlag::FreeStandingGlass) != 0) {
            if ((cdTypes & CdType::PathBlocker) == 0) {
                continue;
            }
        } else if ((cdTypes & CdType::Objs) == 0) {
            continue;
        }
        const CollisionBlock* block = Collision(propnum);
        if (block == nullptr || block->edges <= 0) {
            continue;
        }
        PropCollider collider;
        collider.polygon = block->polygon;
        collider.edges = block->edges;
        collider.top = block->top;
        collider.bottom = block->bottom;
        collider.prop = propnum;
        out->push_back(collider);
    }
}

float SoloAmmoMultiplier(int difficulty) {
    // DEFAULT_AGENT_SOLO_AMMO_MULTIPLIER, DEFAULT_SECRET_AGENT_SOLO_AMMO_MULTIPLIER,
    // DEFAULT_00_AGENT_SOLO_AMMO_MULTIPLIER, indexed by difficulty 0 to 2.
    constexpr std::array<float, 3> kMultipliers = { 2.0f, 1.5f, 1.0f };
    if (difficulty < 0 || static_cast<size_t>(difficulty) >= kMultipliers.size()) {
        return 1.0f;
    }
    return kMultipliers[static_cast<size_t>(difficulty)];
}

bool Objects::TestPickup(const StageSetup& setup, const PlacedObject& object, const Vec3& playerPosition,
                         int playerStan, float playerArmour, const Inventory& inventory, const PlayerWeapons& weapons,
                         const WeaponTable& weaponTable) const {
    const auto type = static_cast<PropDefType>(object.type);
    if (!IsPickup(object.type)) {
        return false;
    }
    if (type != PropDefType::Hat) {
        if ((object.flags & ObjectFlag::Uncollectable) != 0) {
            return false;
        }
    } else if ((object.flags & ObjectFlag::AllowPickup) == 0) {
        return false;
    }
    if ((object.flags & ObjectFlag::CollectByInteractOnly) != 0) {
        return false;
    }
    // objCanPickupFromSafe: https://github.com/liz-baker/frigate/issues/184.

    const PropDef& def = setup.PropDefs()[object.def];
    if (type == PropDefType::Collectable) {
        // TODO(https://github.com/liz-baker/frigate/issues/183): the
        // still-armed/in-flight grenade, mine and dropped-round exclusions.
        if (inventory.HasItem(def.weaponNum)) {
            const int32_t ammoType = weaponTable.AmmoType(def.weaponNum);
            const bool ammoFull = ammoType != 0 && weapons.AmmoHeld(ammoType) >= weaponTable.MaxAmmo(ammoType);
            if (ammoFull) {
                if (def.weaponLinkedType < 0) {
                    return false;
                }
                const bool leftHanded = (object.flags & ObjectFlag::WeaponLeftHanded) != 0;
                const int32_t right = leftHanded ? def.weaponLinkedType : def.weaponNum;
                const int32_t left = leftHanded ? def.weaponNum : def.weaponLinkedType;
                if (inventory.HasDual(right, left)) {
                    return false;
                }
            }
        }
    } else if (type == PropDefType::Ammo) {
        bool anyRoom = false;
        for (size_t i = 0; i < PropDef::kAmmoSlotCount; i++) {
            if (def.ammoSlots[i].quantity == 0) {
                continue;
            }
            const int32_t ammoType = AmmoCrateSlotType(i);
            if (weapons.AmmoHeld(ammoType) < weaponTable.MaxAmmo(ammoType)) {
                anyRoom = true;
                break;
            }
        }
        if (!anyRoom) {
            return false;
        }
    } else if (type == PropDefType::Armour) {
        if (def.armourAmount <= playerArmour) {
            return false;
        }
    }

    // objTickPlayer's tail: the radial distance and height range around the
    // player, minus the magnet-attract widening -- #183.
    constexpr float kPickupRadiusSquared = 10000.0f;
    constexpr float kPickupHeightRange = 200.0f;
    const float dx = object.runtimePosition[0] - playerPosition[0];
    const float dy = object.runtimePosition[1] - playerPosition[1];
    const float dz = object.runtimePosition[2] - playerPosition[2];
    if (((dx * dx) + (dz * dz)) > kPickupRadiusSquared || dy < -kPickupHeightRange || dy > kPickupHeightRange) {
        return false;
    }

    if ((object.flags2 & ObjectFlag2::MagnetThroughWalls) != 0 || stan_ == nullptr) {
        return true;
    }
    // stanTestLineUnobstructed against closed doors only; a pickup behind an
    // open doorway or a window is still reachable.
    int tile = playerStan;
    const int reached =
        stan_->TestLineUnobstructed(&tile, playerPosition[0], playerPosition[2], object.runtimePosition[0],
                                    object.runtimePosition[2], CdType::Doors, 30.0f, 30.0f, 0.0f, 1.0f);
    const Prop& objectProp = props_[object.prop];
    return reached != 0 && tile == objectProp.stan;
}

PickupOutcome Objects::Collect(const StageSetup& setup, const PlacedObject& object, const WeaponTable& weaponTable,
                               PlayerWeapons* weapons, Inventory* inventory, float* armour,
                               float soloAmmoMultiplier) const {
    const auto type = static_cast<PropDefType>(object.type);
    const PropDef& def = setup.PropDefs()[object.def];

    if (type == PropDefType::Ammo) {
        // add_ammo_to_inventory: adds to the ammo held, gated on there being
        // room for it, and grants a matching weapon item for the ammo types
        // that are also throwable/plantable weapons.
        // TODO(https://github.com/liz-baker/frigate/issues/41): PICKUP_AMMO_SFX.
        for (size_t i = 0; i < PropDef::kAmmoSlotCount; i++) {
            const int32_t quantity = def.ammoSlots[i].quantity;
            if (quantity <= 0 || weapons == nullptr) {
                continue;
            }
            const int32_t ammoType = AmmoCrateSlotType(i);
            const int32_t held = weapons->AmmoHeld(ammoType);
            if (held >= weaponTable.MaxAmmo(ammoType)) {
                continue;
            }
            const auto amount = static_cast<int32_t>(static_cast<float>(quantity) * soloAmmoMultiplier);
            weapons->GiveAmmo(ammoType, held + amount);
            if (inventory == nullptr) {
                continue;
            }
            switch (ammoType) {
                case kAmmoGrenade:
                    inventory->AddItem(kItemGrenade);
                    break;
                case kAmmoRemoteMine:
                    inventory->AddItem(kItemRemoteMine);
                    inventory->AddItem(kItemTrigger);
                    break;
                case kAmmoProxMine:
                    inventory->AddItem(kItemProximityMine);
                    break;
                case kAmmoTimedMine:
                    inventory->AddItem(kItemTimedMine);
                    break;
                case kAmmoKnife:
                    inventory->AddItem(kItemThrowKnife);
                    break;
                default:
                    break;
            }
        }
        return PickupOutcome::Collected;
    }

    if (type == PropDefType::Armour) {
        if (armour != nullptr) {
            // bondviewAddCurrentPlayerArmor overwrites the player's armour;
            // it does not add to it.
            *armour = def.armourAmount;
        }
        // TODO(https://github.com/liz-baker/frigate/issues/41):
        // ARMOUR_COLLECT_SFX.
        // TODO(https://github.com/liz-baker/frigate/issues/182): the "Picked
        // up some body armor." HUD message (hudmsgBottomShow/langGet).
        return PickupOutcome::Collected;
    }

    if (type == PropDefType::Collectable) {
        // bondinvAddWeaponByProp: a fresh pickup prop's WeaponObjRecord never
        // has a runtime dual-weapon link (that's set when a hand equips two
        // guns), so weaponLinkedType alone decides pairing. The
        // RUNTIMEBITFLAG_DESTROYED branch (a guard's already-dropped weapon,
        // handed to the player outright rather than added by
        // bondinvAddWeaponByProp) never applies here: nothing sets that
        // runtime bit until guard drops exist (#164).
        // TODO(https://github.com/liz-baker/frigate/issues/41):
        // set_sound_effect_for_weapontype_collection.
        // TODO(https://github.com/liz-baker/frigate/issues/182): the
        // per-weapon pickup HUD text (bondinvGetActivatedTextWeapon/
        // display_text_for_weapon_in_lower_left_corner) and, when ammo is
        // collected too, display_text_when_ammo_collected.
        if (inventory != nullptr) {
            if (def.weaponLinkedType >= 0) {
                const bool leftHanded = (object.flags & ObjectFlag::WeaponLeftHanded) != 0;
                const int32_t right = leftHanded ? def.weaponLinkedType : def.weaponNum;
                const int32_t left = leftHanded ? def.weaponNum : def.weaponLinkedType;
                inventory->AddDoubles(right, left);
            } else {
                inventory->AddItem(def.weaponNum);
            }
        }
        // ITEM_REMOTEMINE's ITEM_TRIGGER grant needs the planted-weapon state
        // #183 tracks; ITEM_TOKEN's equip-on-pickup is multiplayer-only and
        // this port targets single player only (Stage::kPlayerCount == 1).
        if (weapons != nullptr && (object.flags & ObjectFlag::WeaponNoAmmo) == 0) {
            const int32_t ammoType = weaponTable.AmmoType(def.weaponNum);
            if (ammoType != 0) {
                float pickupQuantity = static_cast<float>(AmmoCollectedFromWeapon(ammoType));
                if (pickupQuantity > 1.0f) {
                    pickupQuantity *= soloAmmoMultiplier;
                }
                const int32_t held = weapons->AmmoHeld(ammoType);
                const int32_t max = weaponTable.MaxAmmo(ammoType);
                if (pickupQuantity > 0.0f && held < max) {
                    weapons->GiveAmmo(ammoType, held + static_cast<int32_t>(pickupQuantity));
                }
            }
        }
        return PickupOutcome::Collected;
    }

    // KEY, HAT and anything else fall to propPickupByPlayer's key/default
    // case, which both end up keeping the prop as a live, prop-backed
    // inventory entry (bondinvAddPropToInv) rather than freeing it outright,
    // and both also play a sound (KEYCARD_SFX) and show HUD text -- see
    // https://github.com/liz-baker/frigate/issues/41 and
    // https://github.com/liz-baker/frigate/issues/182.
    return PickupOutcome::None;
}

} // namespace Frigate::Level
