#include "level/Objects.h"

#include <cstring>

#include <algorithm>
#include <cmath>
#include <utility>

#include "level/BgLevel.h"
#include "level/Chrs.h"
#include "level/Inventory.h"
#include "level/PickupText.h"
#include "level/PlayerWeapons.h"
#include "level/Stan.h"
#include "level/StanIntersection.h"
#include "level/WeaponTable.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

// Whether `type`'s own setup function (setupCctv, setupAutogun,
// setupHangingMonitors, setupMultiMonitor, or the propobj.c cases for
// PROPDEF_TANK/PROPDEF_VEHICHLE/PROPDEF_AIRCRAFT) places it exactly like
// domakedefaultobj and only diverges afterward, to set up runtime state this
// port doesn't read yet:
//   - Vehicle's and Aircraft's movement along the path their AI list starts
//     (their lists run: Stage::TickProps) --
//     https://github.com/liz-baker/frigate/issues/232.
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
        case PropDefType::Magazine:
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
constexpr float kTau = 6.2831855f;
// The rotation that turns an in-air-rotated object onto its side (3 pi / 2).
constexpr float kQuarterTurnBack = 4.712389f;
constexpr float kAircraftDrop = 200.0f;
// A monitor embedded in another object leans back by this (20 degrees).
constexpr float kMonitorTilt = 0.36651915f;
// The switch-array slots of a character's model that hold its right and left
// hands' weapons and its hat, and the ones of a weapon's model
// weaponSetGunfireVisible hides.
constexpr size_t kRightHandSlot = 3;
constexpr size_t kLeftHandSlot = 5;
constexpr size_t kHatSlot = 6;
constexpr size_t kGunfireSlot = 0;
constexpr size_t kWeaponHiddenSlot = 2;

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
constexpr int32_t kAmmoBombCase = 16;
constexpr int32_t kAmmoBug = 20;
constexpr int32_t kAmmoMicroCamera = 21;
constexpr int32_t kAmmoGeKey = 22;
constexpr int32_t kAmmoPlastique = 23;
constexpr int32_t kAmmoToken = 29;

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

bool PointPolygonCollision(const Vec3& point, float radius, const std::array<Vec2, 8>& polygon, int edges) {
    const Vec2 position = { point[0], point[2] };
    for (int i = 0; i < edges; i++) {
        const Vec2& from = polygon[static_cast<size_t>(i)];
        const Vec2& to = polygon[static_cast<size_t>((i + 1) % edges)];
        const float distance = std::fabs(SignedPointLineDistance(from, to, position));
        if (distance < radius && (Distance2d(from, position) < radius || Distance2d(to, position) < radius ||
                                  PointProjectsOntoEdge(from, to, position))) {
            return true;
        }
    }
    return false;
}

namespace {

// chrobjSeparatingAxisTheorem: whether the edges of `a` alone separate the two
// polygons on the ground plane.
bool EdgesSeparate(const std::array<Vec2, 8>& a, int edgesA, const std::array<Vec2, 8>& b, int edgesB) {
    for (int i = 0; i < edgesA; i++) {
        const int next = (i + 1) % edgesA;
        const double diff1 = a[static_cast<size_t>(next)][1] - static_cast<double>(a[static_cast<size_t>(i)][1]);
        const double diff2 = a[static_cast<size_t>(i)][0] - static_cast<double>(a[static_cast<size_t>(next)][0]);
        if (diff1 == 0.0 && diff2 == 0.0) {
            const Vec3 point = { a[static_cast<size_t>(i)][0], 0.0f, a[static_cast<size_t>(i)][1] };
            if (PointInPolygon(point, b, edgesB)) {
                return false;
            }
            continue;
        }
        const double sum1 = a[static_cast<size_t>(i)][0] * diff1 + a[static_cast<size_t>(i)][1] * diff2;
        double sum2 = 0.0;
        int j = (next + 1) % edgesA;
        while (j != i) {
            sum2 = a[static_cast<size_t>(j)][0] * diff1 + a[static_cast<size_t>(j)][1] * diff2;
            if (sum2 != sum1) {
                break;
            }
            j = (j + 1) % edgesA;
        }
        int k = 0;
        for (; k < edgesB; k++) {
            const double sum3 = b[static_cast<size_t>(k)][0] * diff1 + b[static_cast<size_t>(k)][1] * diff2;
            if (sum2 == sum1) {
                sum2 = sum1 - sum3 + sum1;
            }
            if (sum3 < sum1 && sum2 < sum1) {
                break;
            }
            if (sum3 > sum1 && sum2 > sum1) {
                break;
            }
        }
        if (k == edgesB) {
            return true;
        }
    }
    return false;
}

} // namespace

bool PolygonsOverlap(const std::array<Vec2, 8>& a, int edgesA, const std::array<Vec2, 8>& b, int edgesB) {
    return !EdgesSeparate(a, edgesA, b, edgesB) && !EdgesSeparate(b, edgesB, a, edgesA);
}

bool Objects::FootprintClear(int prop) const {
    // chraiGetCollisionBounds for the props a door is tested against: an
    // object's or a door's footprint, none for a weapon.
    const auto footprint = [this](int id) -> const CollisionBlock* {
        const PropType type = props_[id].type;
        if (type == PropType::Door) {
            return doorCollision_ ? doorCollision_(id) : nullptr;
        }
        return type == PropType::Obj ? Collision(id) : nullptr;
    };
    const auto safeOrAircraft = [this](int id) {
        const Prop& record = props_[id];
        if (record.type != PropType::Obj || record.object < 0) {
            return false;
        }
        const uint8_t type = objects_[static_cast<size_t>(record.object)].type;
        return type == static_cast<uint8_t>(PropDefType::Safe) || type == static_cast<uint8_t>(PropDefType::Aircraft);
    };
    const CollisionBlock* own = footprint(prop);
    if (own == nullptr || own->edges <= 0) {
        return true;
    }
    const Prop& door = props_[prop];
    // The rooms the door stands in (chraiGetPropRoomIds).
    std::array<int, 9> rooms{};
    size_t roomCount = 0;
    for (const uint8_t room : door.rooms) {
        if (room == kNoRoom) {
            break;
        }
        rooms[roomCount++] = room;
    }
    rooms[roomCount] = -1;

    for (const int16_t candidate : props_.RoomProps(rooms.data())) {
        if (candidate < 0 || candidate == prop) {
            continue;
        }
        const Prop& other = props_[candidate];
        if (other.type == PropType::Viewer || other.type == PropType::Chr) {
            BodyBounds body;
            if (!bodyBounds_ || !bodyBounds_(candidate, &body)) {
                continue;
            }
            const float above = body.above + body.ground;
            const float height = body.height + body.ground;
            if (above > own->top || own->bottom > height) {
                continue;
            }
            if (PointInPolygon(other.position, own->polygon, own->edges) ||
                PointPolygonCollision(other.position, body.radius, own->polygon, own->edges)) {
                if (other.type == PropType::Chr && door.type == PropType::Door && body.hidden != nullptr) {
                    *body.hidden |= ChrHidden::OffscreenPatrol;
                }
                return false;
            }
        } else if ((other.type == PropType::Obj || other.type == PropType::Weapon || other.type == PropType::Door) &&
                   (door.type != PropType::Door || (other.type != PropType::Door && !safeOrAircraft(candidate)))) {
            const CollisionBlock* block = footprint(candidate);
            if (block != nullptr && block->edges > 0 && block->bottom <= own->top && own->bottom <= block->top &&
                PolygonsOverlap(block->polygon, block->edges, own->polygon, own->edges)) {
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
    chrCollider_ = nullptr;
    chrHost_ = nullptr;
    models_factory_ = nullptr;
    doorCollision_ = nullptr;
    safeCheck_ = nullptr;
    bodyBounds_ = nullptr;
    detachHook_ = nullptr;
    freeDeform_ = nullptr;
    projectiles_ = {};
    embedmentsUsed_ = 0;
    weaponSlots_.fill(-1);
    hatSlots_.fill(-1);
    ammoCrateSlots_.fill(-1);
    nextWeaponSlot_ = 0;
    nextHatSlot_ = 0;
    unmadeWeaponRecords_ = 0;
    unmadeHatRecords_ = 0;
    borrowedWeaponRecords_ = 0;
    borrowedHatRecords_ = 0;
    props_.Reset(roomCount, stan, level);
    props_.SetProjectileRooms([this](int prop) -> const std::array<uint8_t, 8>* {
        const PlacedObject* object = ForProp(prop);
        if (object == nullptr || (object->runtimeFlags & ObjectRuntime::HasProjectile) == 0 || object->projectile < 0) {
            return nullptr;
        }
        const Projectile& projectile = ProjectileAt(object->projectile);
        return (projectile.flags & ProjectileFlag::OffTiles) != 0 ? &projectile.rooms : nullptr;
    });
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
    if (object.def != kNoDef) {
        if (byDef_.size() <= object.def) {
            byDef_.resize(object.def + 1, -1);
        }
        byDef_[object.def] = index;
    }
    return objects_.back();
}

const PlacedObject* Objects::ForDef(size_t def) const {
    return def < byDef_.size() && byDef_[def] >= 0 ? &objects_[static_cast<size_t>(byDef_[def])] : nullptr;
}

const PlacedObject* Objects::ForProp(int prop) const {
    if (prop < 0) {
        return nullptr;
    }
    const Prop& record = props_[prop];
    const bool isObject = record.type == PropType::Obj || record.type == PropType::Weapon;
    return isObject && record.object >= 0 ? &objects_[static_cast<size_t>(record.object)] : nullptr;
}

PlacedObject* Objects::ForProp(int prop) {
    return const_cast<PlacedObject*>(std::as_const(*this).ForProp(prop));
}

std::shared_ptr<ModelHandle> Objects::Instantiate(const PropModel& model, float scale) const {
    if (models_factory_ == nullptr) {
        return nullptr;
    }
    auto instance = models_factory_->CreateProp("models/prop/P" + model.name + "Z");
    if (instance) {
        instance->Playback().scale = scale;
    }
    return instance;
}

bool Objects::AssignWeapon(const PropDef& def, size_t defIndex, const PropModel& model, const ChrHost& host) {
    // weaponAssignToHome needs a character with a prop and a model.
    if (host.prop < 0 || host.weaponsHeld == nullptr) {
        return false;
    }
    PlacedObject object;
    object.def = defIndex;
    object.type = def.type;
    object.model = def.model;
    object.flags = def.flags;
    object.flags2 = def.flags2;
    object.state = def.state;
    object.weaponNum = def.weaponNum;
    object.weaponLinkedType = def.weaponLinkedType;
    object.damage = def.damage;
    // TODO(https://github.com/liz-baker/frigate/issues/136): the enemy rockets
    // cheat, which swaps every held gun for a rocket launcher.
    // weaponLoadProjectileModels loads the models a weapon's projectiles use;
    // models load from the archive on demand.
    return EquipHeldWeapon(std::move(object), model, static_cast<float>(def.extraScale) * kExtraScaleUnit, host);
}

bool Objects::GiveWeapon(const ChrHost& host, int chrnum, int modelIndex, int item, uint32_t flags) {
    const PropModel* model = Model(modelIndex);
    if (model == nullptr || host.prop < 0 || host.weaponsHeld == nullptr) {
        return false;
    }
    // something_with_generating_object's blank weapon record (blank_08_object_preset_4001).
    PlacedObject object;
    object.def = kNoDef;
    object.type = static_cast<uint8_t>(PropDefType::Collectable);
    object.model = modelIndex;
    object.flags = flags | ObjectFlag::AssignedToChr;
    object.weaponNum = item;
    object.weaponLinkedType = -1;
    (void)chrnum;
    return EquipHeldWeapon(std::move(object), *model, 1.0f, host);
}

int Objects::CreateAmmoCrate(int modelIndex, size_t slot) {
    const PropModel* model = Model(modelIndex);
    if (model == nullptr) {
        return -1;
    }
    PlacedObject object;
    object.def = kNoDef;
    object.type = static_cast<uint8_t>(PropDefType::Magazine);
    object.model = modelIndex;
    object.flags = ObjectFlag::Fall;
    object.ammoType = AmmoCrateSlotType(slot);
    if (!AcquireRecord(Pool::AmmoCrate, &object) || Init(&object, *model) < 0) {
        return -1;
    }
    PlacedObject& placed = Store(object);
    OccupyRecord(Pool::AmmoCrate, placed, static_cast<int>(objects_.size()) - 1);
    return placed.prop;
}

int Objects::CreateItem(int modelIndex, int item) {
    const PropModel* model = Model(modelIndex);
    if (model == nullptr) {
        return -1;
    }
    // blank_08_object_preset_1.
    PlacedObject object;
    object.def = kNoDef;
    object.type = static_cast<uint8_t>(PropDefType::Collectable);
    object.model = modelIndex;
    object.weaponNum = item;
    object.weaponLinkedType = -1;
    if (!AcquireRecord(Pool::Weapon, &object) || Init(&object, *model) < 0) {
        return -1;
    }
    props_[object.prop].type = PropType::Weapon;
    object.instance = Instantiate(*model, object.scale);
    // weaponSetGunfireVisible(prop, 0).
    if (object.instance && model->weaponSkeleton) {
        object.instance->SetGunfireVisible(kGunfireSlot, false);
        object.instance->SetSwitchVisible(kWeaponHiddenSlot, false);
    }
    PlacedObject& placed = Store(object);
    OccupyRecord(Pool::Weapon, placed, static_cast<int>(objects_.size()) - 1);
    return placed.prop;
}

bool Objects::EquipHeldWeapon(PlacedObject object, const PropModel& model, float extraScale, const ChrHost& host) {
    const bool madeNow = object.def == kNoDef;
    if ((madeNow && !AcquireRecord(Pool::Weapon, &object)) || Init(&object, model) < 0) {
        return false;
    }
    // sub_GAME_7F051F30: the record's scale, then complete_object_data_block_
    // return_position_entry's weapon prop.
    object.scale = model.scale * extraScale;
    props_[object.prop].type = PropType::Weapon;
    object.instance = Instantiate(model, object.scale);
    // weaponSetGunfireVisible(prop, 0).
    if (object.instance && model.weaponSkeleton) {
        object.instance->SetGunfireVisible(kGunfireSlot, false);
        object.instance->SetSwitchVisible(kWeaponHiddenSlot, false);
    }
    PlacedObject& placed = Store(object);
    if (madeNow) {
        OccupyRecord(Pool::Weapon, placed, static_cast<int>(objects_.size()) - 1);
    }

    // chrEquipWeapon.
    const int hand = (placed.flags & ObjectFlag::WeaponLeftHanded) != 0 ? HandLeft : HandRight;
    if ((placed.flags & ObjectFlag::ConcealGun) == 0) {
        auto& held = *host.weaponsHeld;
        if (held[static_cast<size_t>(hand)] >= 0) {
            // "attempted multiple attach": the weapon stays where it is made,
            // unattached.
            return true;
        }
        if (placed.instance && host.model) {
            placed.instance->AttachTo(*host.model, hand == HandRight ? kRightHandSlot : kLeftHandSlot,
                                      hand == HandLeft ? kPi : 0.0f);
        }
        held[static_cast<size_t>(hand)] = placed.prop;
        if ((placed.flags & ObjectFlag::WeaponIsDouble) != 0 && held[static_cast<size_t>(1 - hand)] >= 0) {
            if (PlacedObject* other = ForProp(held[static_cast<size_t>(1 - hand)]); other != nullptr) {
                if (placed.def != kNoDef && other->def != kNoDef) {
                    SetDual(placed.def, placed.weaponNum, other->def, other->weaponNum);
                } else {
                    SetDualProps(placed.prop, other->prop);
                }
            }
        }
    }
    props_.Reparent(placed.prop, host.prop);
    return true;
}

bool Objects::AssignHat(const PropDef& def, size_t defIndex, const PropModel& model, const ChrHost& host) {
    if (host.prop < 0 || host.hat == nullptr) {
        return false;
    }
    PlacedObject object;
    object.def = defIndex;
    object.type = def.type;
    object.model = def.model;
    object.flags = def.flags;
    object.flags2 = def.flags2;
    object.state = def.state;
    object.damage = def.damage;
    return PutOnHat(std::move(object), model, static_cast<float>(def.extraScale) * kExtraScaleUnit, host);
}

bool Objects::GiveHat(const ChrHost& host, int modelIndex, uint32_t flags) {
    const PropModel* model = Model(modelIndex);
    if (model == nullptr || host.prop < 0 || host.hat == nullptr) {
        return false;
    }
    // hatCreateForChr's blank hat record.
    PlacedObject object;
    object.def = kNoDef;
    object.type = static_cast<uint8_t>(PropDefType::Hat);
    object.model = modelIndex;
    object.flags = flags | ObjectFlag::AssignedToChr;
    return PutOnHat(std::move(object), *model, 1.0f, host);
}

bool Objects::PutOnHat(PlacedObject object, const PropModel& model, float extraScale, const ChrHost& host) {
    const bool madeNow = object.def == kNoDef;
    if ((madeNow && !AcquireRecord(Pool::Hat, &object)) || Init(&object, model) < 0) {
        return false;
    }
    // hatApplyToChr.
    object.scale = model.scale * extraScale;
    object.instance = Instantiate(model, object.scale);
    PlacedObject& placed = Store(object);
    if (madeNow) {
        OccupyRecord(Pool::Hat, placed, static_cast<int>(objects_.size()) - 1);
    }
    if (placed.instance && host.model) {
        placed.instance->AttachTo(*host.model, kHatSlot, 0.0f);
    }
    props_.Reparent(placed.prop, host.prop);
    *host.hat = placed.prop;
    return true;
}

bool Objects::EmbedMonitor(const StageSetup& setup, const PropDef& def, size_t defIndex, const PropModel& model) {
    (void)setup;
    const int64_t ownerIndex = static_cast<int64_t>(defIndex) + def.monitorOwnerOffset;
    const PlacedObject* owner = ownerIndex >= 0 ? ForDef(static_cast<size_t>(ownerIndex)) : nullptr;
    if (owner == nullptr) {
        return false;
    }
    const int ownerProp = owner->prop;
    const float ownerScale = owner->scale;

    PlacedObject object;
    object.def = defIndex;
    object.type = def.type;
    object.model = def.model;
    object.flags = def.flags;
    object.flags2 = def.flags2;
    object.state = def.state;
    object.damage = def.damage;
    object.monitor.numbers = { def.monitorImages[0], 0, 0, 0 };
    if (Init(&object, model) < 0) {
        return false;
    }
    object.scale = model.scale * (static_cast<float>(def.extraScale) * kExtraScaleUnit);
    object.runtimeFlags |= ObjectRuntime::Embedded;
    object.hasEmbedment = AllocateEmbedment();
    // OwnerPart 0 to 2 name the owner's switches of that number, anything else
    // its fourth.
    object.embedSlot =
        def.monitorOwnerPart >= 0 && def.monitorOwnerPart <= 2 ? static_cast<size_t>(def.monitorOwnerPart) : 3;
    object.embedment = RotationX(kMonitorTilt);
    ScaleRows(&object.embedment, object.scale / ownerScale);
    PlacedObject& placed = Store(object);
    props_.Reparent(placed.prop, ownerProp);
    if ((def.flags & ObjectFlag::MonitorRenderPostBg) != 0) {
        props_[placed.prop].flags |= PropFlag::RenderPostBg;
    }
    return true;
}

void Objects::LinkEmbedded(const StageSetup& setup) {
    CountSpareRecords(setup);
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
    // objChangeShading: sub_GAME_7F0402B4's shading, which an illuminated
    // object ignores, is what the object is shaded with from the start.
    if ((object->flags & ObjectFlag::Illuminated) == 0 && stan != kNoTile) {
        object->nextColour = ObjectShadeFromTile(stan_->Tile(stan));
    }
    object->shadeColour = object->nextColour;
}

void Objects::UpdateShading(PlacedObject* object) {
    const Prop& prop = props_[object->prop];
    if ((object->flags & ObjectFlag::Illuminated) == 0 && prop.stan != kNoTile) {
        object->nextColour = ObjectShadeFromTile(stan_->Tile(prop.stan));
    }
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

bool Objects::CollisionBounds(int prop, PropCollider* out) const {
    const Prop& record = props_[prop];
    const CollisionBlock* block = nullptr;
    switch (record.type) {
        case PropType::Viewer:
            if (viewerFootprint_ && viewerFootprint_(out)) {
                out->prop = prop;
                return true;
            }
            return false;
        case PropType::Chr:
            return chrCollider_ && chrCollider_(prop, CdType::All, out);
        case PropType::Door:
            block = doorCollision_ ? doorCollision_(prop) : nullptr;
            break;
        default:
            block = Collision(prop);
            break;
    }
    if (block == nullptr) {
        return false;
    }
    out->polygon = block->polygon;
    out->edges = block->edges;
    out->top = block->top;
    out->bottom = block->bottom;
    out->prop = prop;
    return true;
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

bool Objects::MoveToPadPlacement(PlacedObject* object, const Pad& pad) {
    if (Bounds(*object) == nullptr || pad.stanTile == kNoTile) {
        return false;
    }
    Mtxf matrix = BasisToward({ -pad.look[0], -pad.look[1], -pad.look[2] }, pad.up);
    ScaleRows(&matrix, object->scale);
    MoveToPad(object, pad.position, matrix, pad.stanTile, pad.position);
    UpdateRooms(object);
    return true;
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
    // weaponAssignToHome places nothing for an unarmed collectable record that
    // no character holds.
    if (def.Is(PropDefType::Collectable) && def.weaponNum == kItemUnarmed &&
        (def.flags & ObjectFlag::AssignedToChr) == 0) {
        return false;
    }
    const PropModel* model = models_->Model(def.model);
    if (model == nullptr) {
        return false;
    }
    if (def.Is(PropDefType::Monitor) && def.pad < 0 && (def.flags & ObjectFlag::InsideAnotherObject) == 0) {
        // setupSingleMonitor's other branch: a monitor with no pad of its own
        // sits on a node of an earlier object's model.
        return EmbedMonitor(setup, def, defIndex, *model);
    }

    PlacedObject object;
    object.def = defIndex;
    object.type = def.type;
    object.model = def.model;
    object.flags = def.flags;
    object.flags2 = def.flags2;
    object.state = def.state;
    object.weaponNum = def.weaponNum;
    object.weaponLinkedType = def.weaponLinkedType;
    object.scale = model->scale;
    const float extraScale = static_cast<float>(def.extraScale) * kExtraScaleUnit;

    object.damage = def.damage;
    object.monitor.numbers = def.monitorImages;
    object.ammoType = def.ammoType;
    object.armourAmount = def.armourAmount;
    object.armourInitial = def.armourAmount;
    object.hostOffset = def.pad;
    object.ammoSlots = def.ammoSlots;
    if (def.Is(PropDefType::TintedGlass)) {
        object.tintDistance = def.tintDistance;
        object.cullDistance = def.cullDistance;
        if ((def.flags & ObjectFlag::GlassHasPortal) != 0 && def.pad >= kFirstBoundPad) {
            object.tintNearOpacity = static_cast<float>(static_cast<int32_t>(def.tintOpacityWord)) / 65536.0f;
        } else {
            // Left as the file holds it, read as a float.
            std::memcpy(&object.tintNearOpacity, &def.tintOpacityWord, sizeof(float));
        }
    }
    // TODO(https://github.com/liz-baker/frigate/issues/288): the respawn state
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
        const ChrHost host = chrHost_ ? chrHost_(def.pad) : ChrHost{};
        // weaponAssignToHome and setupHat have their own branches for these.
        if (def.Is(PropDefType::Collectable)) {
            return AssignWeapon(def, defIndex, *model, host);
        }
        if (def.Is(PropDefType::Hat)) {
            return AssignHat(def, defIndex, *model, host);
        }
        if (host.prop < 0 || Init(&object, *model) < 0) {
            return false;
        }
        object.scale *= extraScale;
        props_.Reparent(Store(object).prop, host.prop);
        return true;
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
    if (def.Is(PropDefType::Cctv) && def.cctvLookPad >= 0) {
        // setupCctv: the camera faces the pad it was given to look at.
        const CctvGeometry* geometry = CctvGeometryOf(placed);
        const Pad* lookPad = setup.FindPad(def.cctvLookPad);
        if (geometry != nullptr && lookPad != nullptr) {
            placed.cctv = MakeCctv(placed.mtx, props_[placed.prop].position, geometry->pivot, lookPad->position,
                                   placed.scale, def.cctvFixed);
        }
    }
    if (def.Is(PropDefType::Autogun)) {
        // setupAutogun: the gun starts aimed at the pad it was given.
        const Pad* aimPad = def.autogunPad >= 0 ? setup.FindPad(def.autogunPad) : nullptr;
        placed.autogun = MakeAutogun(props_[placed.prop].position, aimPad != nullptr ? &aimPad->position : nullptr,
                                     def.autogunFixed);
    }
    if (def.Is(PropDefType::Tank)) {
        // proplvreset2's PROPDEF_TANK case: the turret starts straight ahead and
        // the hull faces the way its pad does.
        // The shell's model (weaponLoadProjectileModels) loads when it is first fired.
        TankState& tank = placed.tank;
        tank.turretVerticalAngle = 0.0f;
        tank.turretOrientationAngle = 0.0f;
        tank.tankOrientationAngle = kTau - GameAtan2f(placed.mtx.m[2][0], placed.mtx.m[2][2]);
        const Prop& prop = props_[placed.prop];
        tank.stanY = prop.stan != kNoTile ? stan_->PositionY(prop.stan, prop.position[0], prop.position[2]) : 0.0f;
        tank.filteredHeight = tank.stanY / 0.17000002f;
        tank.ammo = def.tankAmmo;
    }
    return true;
}

const CctvGeometry* Objects::CctvGeometryOf(const PlacedObject& object) {
    const PropModel* model = Model(object.model);
    if (model == nullptr || models_factory_ == nullptr) {
        return nullptr;
    }
    auto found = cctvGeometry_.find(model->name);
    if (found == cctvGeometry_.end()) {
        found =
            cctvGeometry_.emplace(model->name, models_factory_->LoadCctvGeometry("models/prop/P" + model->name + "Z"))
                .first;
    }
    return found->second ? &*found->second : nullptr;
}

const SwitchGeometry* Objects::SwitchGeometryOf(const PlacedObject& object) {
    const PropModel* model = Model(object.model);
    if (model == nullptr || models_factory_ == nullptr) {
        return nullptr;
    }
    auto found = switchGeometry_.find(model->name);
    if (found == switchGeometry_.end()) {
        found = switchGeometry_
                    .emplace(model->name, models_factory_->LoadSwitchGeometry("models/prop/P" + model->name + "Z"))
                    .first;
    }
    return found->second ? &*found->second : nullptr;
}

const TankGeometry* Objects::TankGeometryOf(const PlacedObject& object) {
    const PropModel* model = Model(object.model);
    if (model == nullptr || models_factory_ == nullptr) {
        return nullptr;
    }
    auto found = tankGeometry_.find(model->name);
    if (found == tankGeometry_.end()) {
        found =
            tankGeometry_.emplace(model->name, models_factory_->LoadTankGeometry("models/prop/P" + model->name + "Z"))
                .first;
    }
    return found->second ? &*found->second : nullptr;
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
        if (prop.type == PropType::Chr) {
            PropCollider collider;
            if (chrCollider_ && chrCollider_(propnum, cdTypes, &collider)) {
                out->push_back(collider);
            }
            continue;
        }
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
                         const WeaponTable& weaponTable, float playerPitchRadians) const {
    const auto type = static_cast<PropDefType>(object.type);
    // Only a dropped weapon is picked up without a propdef record behind it.
    if (!IsPickup(object.type) || (object.def == kNoDef && type != PropDefType::Collectable)) {
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
    if ((object.runtimeFlags & ObjectRuntime::HasProjectile) != 0 && object.projectile >= 0) {
        // Something just thrown cannot be picked up until it has landed.
        const Projectile& projectile = ProjectileAt(object.projectile);
        if (projectile.refreshRate > 0 && projectile.bounces == 0) {
            return false;
        }
    }
    if (object.def != kNoDef && safeCheck_ && !safeCheck_(object.def)) {
        return false;
    }

    if (type == PropDefType::Collectable) {
        // A grenade or mine still counting down, and a rocket in flight, are not
        // picked up.
        const int32_t weapon = object.weaponNum;
        const bool armed = object.weaponTimer >= 0 || (object.runtimeFlags & ObjectRuntime::Remove) != 0;
        if ((weapon == kItemGrenade || weapon == kItemGrenadeRound) && armed) {
            return false;
        }
        if ((weapon == kItemRemoteMine || weapon == kItemProximityMine || weapon == kItemTimedMine ||
             weapon == kItemBombCase || weapon == kItemBug || weapon == kItemMicroCamera || weapon == kItemPlastique) &&
            armed) {
            return false;
        }
        if (weapon == kItemRocketRound && (object.runtimeFlags & ObjectRuntime::HasProjectile) != 0) {
            return false;
        }
        if (inventory.HasItem(object.weaponNum)) {
            const int32_t ammoType = weaponTable.AmmoType(object.weaponNum);
            const bool ammoFull = ammoType != 0 && weapons.AmmoHeld(ammoType) >= weaponTable.MaxAmmo(ammoType);
            if (ammoFull) {
                if (object.dualWeapon < 0 && object.dualObject < 0 && object.weaponLinkedType < 0) {
                    return false;
                }
                int32_t other = object.weaponLinkedType;
                if (object.dualObject >= 0) {
                    other = objects_[static_cast<size_t>(object.dualObject)].weaponNum;
                } else if (object.dualWeapon >= 0) {
                    other = setup.PropDefs()[static_cast<size_t>(object.dualWeapon)].weaponNum;
                }
                const bool leftHanded = (object.flags & ObjectFlag::WeaponLeftHanded) != 0;
                const int32_t right = leftHanded ? other : object.weaponNum;
                const int32_t left = leftHanded ? object.weaponNum : other;
                if (inventory.HasDual(right, left)) {
                    return false;
                }
            }
        }
    } else if (type == PropDefType::Magazine) {
        if (weapons.AmmoHeld(object.ammoType) >= weaponTable.MaxAmmo(object.ammoType)) {
            return false;
        }
    } else if (type == PropDefType::Ammo) {
        // A crate that has been shot is not picked up.
        if (DestroyedLevel(object) != 0) {
            return false;
        }
        const PropDef& def = setup.PropDefs()[object.def];
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
        if (object.armourAmount <= playerArmour) {
            return false;
        }
    }

    // Nothing is picked up while the player looks well down, unless the
    // watch's magnet is pulling.
    const int32_t magnetTime = weapons.MagnetAttractTime();
    if (playerPitchRadians < -0.7853982f && magnetTime < 0) {
        return false;
    }

    // objTickPlayer's tail: the radial distance and height range around the
    // player, which the magnet widens once it has been pulling for a second.
    constexpr int32_t kMagnetFullTicks = 0x3C;
    constexpr float kPickupRadiusSquared = 10000.0f;
    constexpr float kPickupHeightRange = 200.0f;
    constexpr float kMagnetRadiusSquared = 122500.0f;
    constexpr float kMagnetHeightRange = 500.0f;
    const bool magnet = magnetTime >= kMagnetFullTicks;
    const float radiusSquared = magnet ? kMagnetRadiusSquared : kPickupRadiusSquared;
    const float heightRange = magnet ? kMagnetHeightRange : kPickupHeightRange;
    const float dx = object.runtimePosition[0] - playerPosition[0];
    const float dy = object.runtimePosition[1] - playerPosition[1];
    const float dz = object.runtimePosition[2] - playerPosition[2];
    if (((dx * dx) + (dz * dz)) > radiusSquared || dy < -heightRange || dy > heightRange) {
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

void Objects::AddAmmoToInventory(int32_t ammoType, int32_t amount, const WeaponTable& weaponTable,
                                 PlayerWeapons* weapons, Inventory* inventory,
                                 const std::function<void(int32_t, int32_t)>& message) {
    // add_ammo_to_inventory: adds to the ammo held, gated on there being room
    // for it, and grants the matching item for the ammo types that are also
    // throwable or plantable.
    // TODO(https://github.com/liz-baker/frigate/issues/41): the pickup sound
    // (set_sound_effect_for_ammo_collection).
    if (amount <= 0) {
        return;
    }
    const int32_t held = weapons->AmmoHeld(ammoType);
    if (held >= weaponTable.MaxAmmo(ammoType)) {
        return;
    }
    weapons->GiveAmmo(ammoType, held + amount);
    if (message) {
        message(ammoType, amount);
    }
    if (inventory == nullptr) {
        return;
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
        case kAmmoBombCase:
            inventory->AddItem(kItemBombCase);
            break;
        case kAmmoBug:
            inventory->AddItem(kItemBug);
            break;
        case kAmmoMicroCamera:
            inventory->AddItem(kItemMicroCamera);
            break;
        case kAmmoGeKey:
            inventory->AddItem(kItemGoldenEyeKey);
            break;
        case kAmmoToken:
            inventory->AddItem(kItemToken);
            break;
        case kAmmoPlastique:
            inventory->AddItem(kItemPlastique);
            break;
        default:
            break;
    }
}

PickupOutcome Objects::Collect(const StageSetup& setup, PlacedObject& object, const WeaponTable& weaponTable,
                               PlayerWeapons* weapons, Inventory* inventory, float* armour, float soloAmmoMultiplier,
                               const Lang* lang, std::vector<std::string>* messages) {
    const auto type = static_cast<PropDefType>(object.type);
    if (object.def == kNoDef && type != PropDefType::Collectable) {
        return PickupOutcome::None;
    }
    // A weapon without a propdef record has no text override of its own.
    const PropDef* record = object.def != kNoDef ? &setup.PropDefs()[object.def] : nullptr;
    const auto objectKey = static_cast<int64_t>(object.def);

    // hudmsgBottomShow(text) for a message that is showing at all.
    const auto show = [messages](std::string text) {
        if (messages != nullptr && !text.empty()) {
            messages->push_back(std::move(text));
        }
    };
    // bondinvGetActivatedTextObject and bondinvGetActivatedTextWeapon: the
    // pickup text a RENAME record gave the object or the weapon id.
    const auto overrideText = [&](const TextOverride* override) -> std::string {
        if (override != nullptr && override->pickupText != 0 && lang != nullptr) {
            return std::string(lang->Get(override->pickupText));
        }
        return {};
    };
    const auto objectText = [&]() {
        return inventory != nullptr ? overrideText(inventory->TextByObject(objectKey)) : std::string();
    };
    const auto ammoMessage = [&](int32_t ammoType, int32_t amount) {
        if (lang != nullptr) {
            show(AmmoCollectedText(*lang, ammoType, amount));
        }
    };
    const auto text = [&](int32_t slot) {
        return lang != nullptr ? std::string(lang->Get(StringId(TextBank::PropObj, slot))) : std::string();
    };

    const auto addAmmo = [&](int32_t ammoType, int32_t amount) {
        if (weapons != nullptr) {
            AddAmmoToInventory(ammoType, amount, weaponTable, weapons, inventory, ammoMessage);
        }
    };

    if (type == PropDefType::Ammo) {
        const PropDef& def = *record;
        for (size_t i = 0; i < PropDef::kAmmoSlotCount; i++) {
            const auto amount =
                static_cast<int32_t>(static_cast<float>(def.ammoSlots[i].quantity) * soloAmmoMultiplier);
            addAmmo(AmmoCrateSlotType(i), amount);
        }
        return Finish(object, true);
    }

    if (type == PropDefType::Armour) {
        if (armour != nullptr) {
            // bondviewAddCurrentPlayerArmor overwrites the player's armour;
            // it does not add to it.
            *armour = object.armourAmount;
        }
        // TODO(https://github.com/liz-baker/frigate/issues/41):
        // ARMOUR_COLLECT_SFX.
        std::string message = objectText();
        if (message.empty()) {
            message = text(PropObjText::PickedUpBodyArmour);
        }
        show(std::move(message));
        return Finish(object, true);
    }

    if (type == PropDefType::Collectable) {
        // TODO(https://github.com/liz-baker/frigate/issues/41):
        // set_sound_effect_for_weapontype_collection.
        if (weapons != nullptr && inventory != nullptr && object.weaponNum == kItemRemoteMine) {
            inventory->AddItem(kItemTrigger);
        }
        if (weapons != nullptr && object.weaponNum == kItemToken) {
            weapons->EquipWeapon(HandRight, kItemToken);
        }

        bool collected = false;
        PickupOutcome outcome;
        if ((object.runtimeFlags & ObjectRuntime::Destroyed) != 0) {
            // A renamed object: bondinvAddWeaponByProp, and the message the
            // rename gave, is kept as an inventory entry of its own.
            if (inventory != nullptr && object.weaponNum < kItemBombCase) {
                AddWeaponByProp(setup, inventory, &object);
            }
            std::string message = objectText();
            if (message.empty() && lang != nullptr) {
                message = WeaponPickupText(*lang, weaponTable, object.weaponNum);
            }
            show(std::move(message));
            collected = true;
            outcome = PickupOutcome::Given;
        } else {
            if (inventory != nullptr && AddWeaponByProp(setup, inventory, &object)) {
                collected = true;
            }
            std::string message =
                inventory != nullptr ? overrideText(inventory->TextByWeapon(object.weaponNum)) : std::string();
            if (!message.empty()) {
                collected = true;
                show(std::move(message));
            } else if (collected && lang != nullptr) {
                show(WeaponPickupText(*lang, weaponTable, object.weaponNum));
            }
            outcome = PickupOutcome::Collected;
        }

        if (weapons != nullptr && (object.flags & ObjectFlag::WeaponNoAmmo) == 0) {
            const int32_t ammoType = weaponTable.AmmoType(object.weaponNum);
            if (ammoType != 0) {
                float pickupQuantity = static_cast<float>(AmmoCollectedFromWeapon(ammoType));
                if (pickupQuantity > 1.0f) {
                    pickupQuantity *= soloAmmoMultiplier;
                }
                const int32_t held = weapons->AmmoHeld(ammoType);
                const int32_t max = weaponTable.MaxAmmo(ammoType);
                if (pickupQuantity > 0.0f && held < max) {
                    weapons->GiveAmmo(ammoType, held + static_cast<int32_t>(pickupQuantity));
                    if (!collected) {
                        ammoMessage(ammoType, static_cast<int32_t>(pickupQuantity));
                    }
                }
            }
        }
        return outcome == PickupOutcome::Collected ? Finish(object, true) : outcome;
    }

    if (type == PropDefType::Magazine) {
        // get_ammo_in_magazine: a flat quantity by type, scaled for solo play.
        int32_t amount = 1;
        switch (object.ammoType) {
            case kAmmo9mm:
            case kAmmo9mm2:
            case kAmmoRifle:
                amount = 10;
                break;
            case kAmmoShotgun:
            case kAmmoMagnum:
                amount = 5;
                break;
            case kAmmoGgun:
                amount = 3;
                break;
            case kAmmoDarts:
                amount = 4;
                break;
            default:
                break;
        }
        if (amount > 1) {
            amount = static_cast<int32_t>(static_cast<float>(amount) * soloAmmoMultiplier);
        }
        addAmmo(object.ammoType, amount);
        return Finish(object, true);
    }

    // A key, and a hat or anything else that falls to the default case: kept
    // as an inventory entry of its own.
    // TODO(https://github.com/liz-baker/frigate/issues/41): KEYCARD_SFX.
    std::string message = objectText();
    if (message.empty()) {
        message = text(type == PropDefType::Key ? PropObjText::PickedUpAKey : PropObjText::PickedUpSomething);
    }
    show(std::move(message));
    return PickupOutcome::Given;
}

Projectile* Objects::ProjectileOf(PlacedObject& object) {
    if ((object.runtimeFlags & ObjectRuntime::Embedded) != 0) {
        return object.embedmentProjectile >= 0 ? &ProjectileAt(object.embedmentProjectile) : nullptr;
    }
    if ((object.runtimeFlags & ObjectRuntime::HasProjectile) != 0 && object.projectile >= 0) {
        return &ProjectileAt(object.projectile);
    }
    return nullptr;
}

void Objects::FreeProjectile(int slot) {
    // projectileFree. The sounds a launching projectile holds are not ported
    // with the audio.
    projectiles_[static_cast<size_t>(slot)].flags |= ProjectileFlag::Free;
}

int Objects::AllocateProjectile() {
    const auto reset = [&](int slot) {
        projectiles_[static_cast<size_t>(slot)] = Projectile{};
        Projectile& projectile = projectiles_[static_cast<size_t>(slot)];
        // projectileReset.
        projectile.flags = 0;
        return slot;
    };

    for (int i = 0; i < kMaxProjectiles; i++) {
        if ((projectiles_[static_cast<size_t>(i)].flags & ProjectileFlag::Free) != 0) {
            return reset(i);
        }
    }

    // Every record is in use: the one made first goes.
    int best = -1;
    for (int i = 0; i < kMaxProjectiles; i++) {
        const Projectile& projectile = projectiles_[static_cast<size_t>(i)];
        if (projectile.prop >= 0 &&
            (best < 0 || projectile.created < projectiles_[static_cast<size_t>(best)].created)) {
            best = i;
        }
    }
    if (best < 0) {
        return -1;
    }
    const int prop = projectiles_[static_cast<size_t>(best)].prop;
    FreeEmbedmentOrProjectile(prop);
    if (PlacedObject* owner = ForProp(prop)) {
        owner->runtimeFlags |= ObjectRuntime::Remove;
    }
    return reset(best);
}

void Objects::GiveProjectile(int prop) {
    PlacedObject* object = ForProp(prop);
    if (object == nullptr) {
        return;
    }
    if ((object->runtimeFlags & ObjectRuntime::Embedded) != 0) {
        object->embedmentProjectile = AllocateProjectile();
    } else if ((object->runtimeFlags & ObjectRuntime::HasProjectile) == 0) {
        object->projectile = AllocateProjectile();
        if (object->projectile >= 0) {
            object->runtimeFlags |= ObjectRuntime::HasProjectile;
        }
    }
}

void Objects::SetSticky(int prop) {
    PlacedObject* object = ForProp(prop);
    if (object == nullptr) {
        return;
    }
    Projectile* projectile = ProjectileOf(*object);
    if (projectile == nullptr) {
        return;
    }
    projectile->flags |= ProjectileFlag::Sticky;
    const int stan = props_[prop].stan;
    if (stan != kNoTile) {
        projectile->rooms[0] = static_cast<uint8_t>(stan_->TileRoom(stan));
        projectile->rooms[1] = kNoRoom;
        return;
    }
    projectile->rooms[0] = kNoRoom;
}

bool Objects::AllocateEmbedment() {
    if (embedmentsUsed_ >= kMaxEmbedments) {
        return false;
    }
    embedmentsUsed_++;
    return true;
}

void Objects::FreeEmbedment() {
    if (embedmentsUsed_ > 0) {
        embedmentsUsed_--;
    }
}

void Objects::FreeEmbedmentOrProjectile(int prop) {
    PlacedObject* object = ForProp(prop);
    if (object == nullptr) {
        return;
    }
    if ((object->runtimeFlags & ObjectRuntime::Embedded) != 0) {
        if (object->hasEmbedment) {
            if (object->embedmentProjectile >= 0) {
                FreeProjectile(object->embedmentProjectile);
            }
            FreeEmbedment();
        }
        object->hasEmbedment = false;
        object->embedmentProjectile = -1;
        object->runtimeFlags &= ~ObjectRuntime::Embedded;
    } else if ((object->runtimeFlags & ObjectRuntime::HasProjectile) != 0) {
        if (object->projectile >= 0) {
            FreeProjectile(object->projectile);
        }
        object->projectile = -1;
        object->runtimeFlags &= ~ObjectRuntime::HasProjectile;
    }
}

void Objects::Free(int prop, bool canRegen) {
    if (canRegen) {
        return;
    }
    PlacedObject* object = ForProp(prop);
    if (object != nullptr) {
        FreeEmbedmentOrProjectile(prop);
        if (props_[prop].parent >= 0) {
            if (detachHook_) {
                detachHook_(prop);
            }
            props_.Detach(prop);
            object->runtimeFlags &= ~ObjectRuntime::HasOwner;
        }
    }
    props_.DeregisterRooms(prop);
    // The objects attached to this one go with it (objFreePermanently).
    for (int child = props_[prop].child; child >= 0;) {
        const int next = props_[child].prev;
        Free(child);
        child = next;
    }
    if (object != nullptr) {
        if (freeDeform_) {
            freeDeform_(*object);
        }
        ReleaseRecord(*object);
        object->instance.reset();
        object->prop = -1;
    }
    props_.Delist(prop);
    props_.Disable(prop);
    props_.Free(prop);
    props_[prop].object = -1;
    props_[prop].type = PropType::Nul;
}

void Objects::CountSpareRecords(const StageSetup& setup) {
    unmadeWeaponRecords_ = 0;
    unmadeHatRecords_ = 0;
    const auto& defs = setup.PropDefs();
    for (size_t def = 0; def < defs.size(); def++) {
        if (ForDef(def) != nullptr) {
            continue;
        }
        if (defs[def].Is(PropDefType::Collectable)) {
            unmadeWeaponRecords_++;
        } else if (defs[def].Is(PropDefType::Hat)) {
            unmadeHatRecords_++;
        }
    }
}

bool Objects::SpareSetupRecord(uint8_t type) const {
    const bool weapon = type == static_cast<uint8_t>(PropDefType::Collectable);
    int spare = weapon ? unmadeWeaponRecords_ : unmadeHatRecords_;
    for (const PlacedObject& other : objects_) {
        if (other.def != kNoDef && other.type == type && other.prop < 0) {
            spare++;
        }
    }
    return spare > (weapon ? borrowedWeaponRecords_ : borrowedHatRecords_);
}

void Objects::EvictObject(int index) {
    PlacedObject& victim = objects_[static_cast<size_t>(index)];
    if (victim.prop >= 0) {
        // objFreePermanently: the marks it carries go with it.
        Free(victim.prop);
    }
}

bool Objects::AcquireRecord(Pool pool, PlacedObject* object) {
    object->poolSlot = -1;
    object->takesSetupRecord = false;
    const auto slotObject = [this](int index) -> const PlacedObject* {
        return index >= 0 && objects_[static_cast<size_t>(index)].prop >= 0 ? &objects_[static_cast<size_t>(index)]
                                                                            : nullptr;
    };
    const auto onScreen = [this](const PlacedObject& held) {
        return (props_[held.prop].flags & PropFlag::OnScreen) != 0;
    };
    const auto idle = [](const PlacedObject& held) {
        return (held.runtimeFlags & ObjectRuntime::HasProjectile) == 0 && (held.state & ObjectState::Respawn) == 0;
    };

    if (pool == Pool::AmmoCrate) {
        // ammocrateAllocate: a free record, else a crate off screen and
        // attached to nothing, else one on screen.
        for (size_t i = 0; i < kAmmoCrateSlots; i++) {
            if (slotObject(ammoCrateSlots_[i]) == nullptr) {
                object->poolSlot = static_cast<int8_t>(i);
                return true;
            }
        }
        for (const bool allowOnScreen : { false, true }) {
            for (size_t i = 0; i < kAmmoCrateSlots; i++) {
                const PlacedObject* held = slotObject(ammoCrateSlots_[i]);
                if (idle(*held) && props_[held->prop].parent < 0 && (allowOnScreen || !onScreen(*held))) {
                    EvictObject(ammoCrateSlots_[i]);
                    object->poolSlot = static_cast<int8_t>(i);
                    return true;
                }
            }
        }
        return false;
    }

    const bool weapons = pool == Pool::Weapon;
    std::array<int, kWeaponSlots>& weaponSlots = weaponSlots_;
    std::array<int, kHatSlots>& hatSlots = hatSlots_;
    const size_t count = weapons ? kWeaponSlots : kHatSlots;
    size_t& next = weapons ? nextWeaponSlot_ : nextHatSlot_;
    const auto slotAt = [&](size_t i) { return weapons ? weaponSlots[i] : hatSlots[i]; };
    const uint8_t type = static_cast<uint8_t>(weapons ? PropDefType::Collectable : PropDefType::Hat);

    // The first record from `next` that nothing is using; failing that, the first
    // that can go while off screen, and the first that can go at all.
    int freeSlot = -1;
    int offScreenSlot = -1;
    int anySlot = -1;
    size_t i = next;
    do {
        const PlacedObject* held = slotObject(slotAt(i));
        if (held == nullptr) {
            freeSlot = static_cast<int>(i);
            break;
        }
        bool spare = false;
        if (weapons) {
            if (idle(*held)) {
                const Prop& heldProp = props_[held->prop];
                const bool mine = held->weaponNum == kItemRemoteMine || held->weaponNum == kItemProximityMine ||
                                  held->weaponNum == kItemTimedMine;
                const bool loose = heldProp.parent < 0;
                const bool onObject = heldProp.parent >= 0 && (props_[heldProp.parent].type == PropType::Obj ||
                                                               props_[heldProp.parent].type == PropType::Door ||
                                                               props_[heldProp.parent].type == PropType::Weapon);
                spare = (held->weaponTimer <= 0 && loose) || (mine && (loose || onObject));
            }
        } else {
            spare = (held->runtimeFlags & ObjectRuntime::HasProjectile) == 0 && props_[held->prop].parent < 0;
        }
        if (spare) {
            if (!onScreen(*held) && offScreenSlot < 0) {
                offScreenSlot = static_cast<int>(i);
            }
            if (anySlot < 0) {
                anySlot = static_cast<int>(i);
            }
        }
        i = (i + 1) % count;
    } while (i != next);

    if (freeSlot >= 0) {
        next = (static_cast<size_t>(freeSlot) + 1) % count;
        object->poolSlot = static_cast<int8_t>(freeSlot);
        return true;
    }

    // setupFindObjForReuse: a propdef record of this kind that nothing is using
    // takes the object without freeing another.
    if (SpareSetupRecord(type)) {
        (weapons ? borrowedWeaponRecords_ : borrowedHatRecords_)++;
        object->takesSetupRecord = true;
        return true;
    }

    // Else a pool record off screen, a propdef object of the kind off screen and
    // attached to nothing, a pool record, then any such propdef object.
    const auto evictSlot = [&](int slot) {
        EvictObject(slotAt(static_cast<size_t>(slot)));
        next = (static_cast<size_t>(slot) + 1) % count;
        object->poolSlot = static_cast<int8_t>(slot);
    };
    const auto evictSetupObject = [&](bool offScreenOnly) {
        for (size_t index = 0; index < objects_.size(); index++) {
            const PlacedObject& held = objects_[index];
            if (held.def == kNoDef || held.type != type || held.prop < 0) {
                continue;
            }
            if (!idle(held) || props_[held.prop].parent >= 0 || (offScreenOnly && onScreen(held))) {
                continue;
            }
            EvictObject(static_cast<int>(index));
            (weapons ? borrowedWeaponRecords_ : borrowedHatRecords_)++;
            object->takesSetupRecord = true;
            return true;
        }
        return false;
    };
    if (offScreenSlot >= 0) {
        evictSlot(offScreenSlot);
        return true;
    }
    if (evictSetupObject(true)) {
        return true;
    }
    if (anySlot >= 0) {
        evictSlot(anySlot);
        return true;
    }
    return evictSetupObject(false);
}

void Objects::OccupyRecord(Pool pool, const PlacedObject& object, int index) {
    if (object.poolSlot < 0) {
        return;
    }
    const auto slot = static_cast<size_t>(object.poolSlot);
    switch (pool) {
        case Pool::Weapon:
            weaponSlots_[slot] = index;
            break;
        case Pool::Hat:
            hatSlots_[slot] = index;
            break;
        case Pool::AmmoCrate:
            ammoCrateSlots_[slot] = index;
            break;
    }
}

void Objects::ReleaseRecord(const PlacedObject& object) {
    if (object.takesSetupRecord) {
        int& borrowed =
            object.type == static_cast<uint8_t>(PropDefType::Hat) ? borrowedHatRecords_ : borrowedWeaponRecords_;
        borrowed = std::max(0, borrowed - 1);
    }
    // The slot clears when it is looked at again: a freed object has no prop.
}

void Objects::FreeOrRespawn(int prop) {
    PlacedObject* object = ForProp(prop);
    if (object != nullptr && (object->state & ObjectState::Respawn) != 0) {
        // 1200 ticks on NTSC (0x4B0).
        constexpr int16_t kRespawnTicks = 0x4B0;
        props_[prop].timeToRegen = kRespawnTicks;
        object->runtimeFlags |= ObjectRuntime::Unnamed0x800;
        object->runtimeFlags &= ~ObjectRuntime::Remove;
        object->state &= static_cast<uint8_t>(~ObjectState::Destroyed);
        object->maxDamage = 0.0f;
        props_.DeregisterRooms(prop);
        props_.Disable(prop);
        return;
    }
    Free(prop);
}

void Objects::TickRegen(int prop, int32_t clockTimer, bool nearPlayer) {
    constexpr int16_t kLastSecond = 60;
    PlacedObject* object = ForProp(prop);
    Prop& record = props_[prop];
    if (object == nullptr || record.timeToRegen <= 0) {
        return;
    }
    const bool underLastSecond = record.timeToRegen < kLastSecond;
    record.timeToRegen = static_cast<int16_t>(record.timeToRegen - clockTimer);
    if (record.timeToRegen < kLastSecond && !underLastSecond && nearPlayer) {
        // A player standing by holds it back for another second.
        record.timeToRegen = static_cast<int16_t>(record.timeToRegen + kLastSecond);
    }

    if (record.timeToRegen <= 0) {
        record.timeToRegen = 0;
        if ((object->state & ObjectState::State10) != 0) {
            object->runtimeFlags |= ObjectRuntime::Regenerating;
        } else {
            object->runtimeFlags &= ~ObjectRuntime::Regenerating;
        }
    } else if (record.timeToRegen < kLastSecond && !underLastSecond) {
        if (object->maxDamage == 0.0f && (object->state & ObjectState::Destroyed) == 0) {
            if ((object->flags & ObjectFlag::InsideAnotherObject) != 0) {
                // Back onto the object it was embedded in.
                props_.DeregisterRooms(prop);
                props_.Delist(prop);
                object->runtimeFlags &= ~ObjectRuntime::Unnamed0x800;
                const int64_t hostDef = static_cast<int64_t>(object->def) + object->hostOffset;
                const PlacedObject* host = hostDef >= 0 ? ForDef(static_cast<size_t>(hostDef)) : nullptr;
                if (host != nullptr && host->prop >= 0) {
                    props_.Reparent(prop, host->prop);
                }
            } else {
                props_.Enable(prop);
                UpdateRooms(object);
                object->runtimeFlags &= ~ObjectRuntime::Unnamed0x800;
            }
        } else {
            if ((object->state & ObjectState::ExtCollisionBlock) != 0) {
                object->flags |= ObjectFlag::ForceCollisions;
            } else {
                object->flags &= ~ObjectFlag::ForceCollisions;
            }
            object->maxDamage = 0.0f;
            object->state &= static_cast<uint8_t>(~ObjectState::Destroyed);
            if (freeDeform_) {
                freeDeform_(*object);
            }
        }
        if (object->type == static_cast<uint8_t>(PropDefType::Armour)) {
            object->armourAmount = object->armourInitial;
        }
        // TODO(https://github.com/liz-baker/frigate/issues/41): OBJ_REGEN_SFX.
    }
}

void Objects::GiveToPlayer(int prop, int playerProp) {
    props_.DeregisterRooms(prop);
    props_.Delist(prop);
    props_.Disable(prop);
    // objDetach: the prop leaves what held it, and the model its holder's node.
    if (props_[prop].parent >= 0) {
        if (detachHook_) {
            detachHook_(prop);
        }
        props_.Detach(prop);
        if (PlacedObject* object = ForProp(prop)) {
            object->runtimeFlags &= ~ObjectRuntime::HasOwner;
            if (object->instance) {
                object->instance->Detach();
            }
        }
    }
    FreeEmbedmentOrProjectile(prop);
    props_.Reparent(prop, playerProp);
}

// The tail of propPickupByPlayer: a freed pickup stays as an inventory entry
// when the object is tagged.
PickupOutcome Objects::Finish(const PlacedObject& object, bool freed) const {
    if (freed && (object.runtimeFlags & ObjectRuntime::Tagged) == 0) {
        return PickupOutcome::Collected;
    }
    return PickupOutcome::Given;
}

// bondinvAddWeaponByProp: a weapon still paired with another (LINK record)
// adds only itself and hands the pairing to the other; once unpaired, a weapon
// with a linked type adds the set.
bool Objects::AddWeaponByProp(const StageSetup& setup, Inventory* inventory, PlacedObject* object) {
    const int32_t weapon = object->weaponNum;
    bool added = inventory->AddItem(weapon);
    const bool leftHanded = (object->flags & ObjectFlag::WeaponLeftHanded) != 0;
    if (object->dualObject >= 0 || object->dualWeapon >= 0) {
        PlacedObject* other = nullptr;
        int32_t partner = object->weaponLinkedType;
        if (object->dualObject >= 0) {
            other = &objects_[static_cast<size_t>(object->dualObject)];
            partner = other->weaponNum;
        } else {
            const auto partnerDef = static_cast<size_t>(object->dualWeapon);
            partner = setup.PropDefs()[partnerDef].weaponNum;
            other = ForDef(partnerDef);
        }
        added = leftHanded ? !inventory->HasDual(partner, weapon) : !inventory->HasDual(weapon, partner);
        if (other != nullptr) {
            other->weaponLinkedType = weapon;
            other->dualWeapon = -1;
            other->dualObject = -1;
        }
        object->dualWeapon = -1;
        object->dualObject = -1;
    } else if (object->weaponLinkedType >= 0) {
        added = leftHanded ? inventory->AddDoubles(object->weaponLinkedType, weapon)
                           : inventory->AddDoubles(weapon, object->weaponLinkedType);
    }
    return added;
}

void Objects::SetDualProps(int leftProp, int rightProp) {
    PlacedObject* left = ForProp(leftProp);
    PlacedObject* right = ForProp(rightProp);
    if (left == nullptr || right == nullptr) {
        return;
    }
    const auto leftIndex = static_cast<int64_t>(props_[leftProp].object);
    const auto rightIndex = static_cast<int64_t>(props_[rightProp].object);
    left->weaponLinkedType = right->weaponNum;
    left->dualObject = rightIndex;
    right->weaponLinkedType = left->weaponNum;
    right->dualObject = leftIndex;
}

bool Objects::Paired(int a, int b) const {
    const PlacedObject* first = ForProp(a);
    const PlacedObject* second = ForProp(b);
    if (first == nullptr || second == nullptr) {
        return false;
    }
    const auto firstIndex = static_cast<int64_t>(props_[a].object);
    const auto secondIndex = static_cast<int64_t>(props_[b].object);
    const auto pointsAt = [](const PlacedObject& from, const PlacedObject& to, int64_t toIndex) {
        return from.dualObject == toIndex ||
               (from.dualWeapon >= 0 && to.def != kNoDef && from.dualWeapon == static_cast<int64_t>(to.def));
    };
    return pointsAt(*first, *second, secondIndex) && pointsAt(*second, *first, firstIndex);
}

void Objects::SetDual(size_t leftDef, int32_t leftWeapon, size_t rightDef, int32_t rightWeapon) {
    if (PlacedObject* left = ForDef(leftDef)) {
        left->weaponLinkedType = rightWeapon;
        left->dualWeapon = static_cast<int64_t>(rightDef);
    }
    if (PlacedObject* right = ForDef(rightDef)) {
        right->weaponLinkedType = leftWeapon;
        right->dualWeapon = static_cast<int64_t>(leftDef);
    }
}

} // namespace Frigate::Level
