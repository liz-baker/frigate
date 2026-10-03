#include "level/Shots.h"

#include "game/Sound.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "level/ChrActions.h"
#include "level/DoorGeometry.h"
#include "level/HatType.h"
#include "level/HitParts.h"
#include "level/LevelModels.h"
#include "level/Projectiles.h"
#include "level/PropRender.h"

namespace Frigate::Level {

namespace {

constexpr float kFraction = 2.3283064e-10f;
constexpr float kU32Max = 4294967296.0f;
constexpr float kU16Max = 65536.0f;
// g_GunScreenAspectRatio.
constexpr float kGunScreenAspectRatio = 4.0f / 3.0f;
// How far in front of a surface a spark or tracer end is put.
constexpr float kSparkBackOff = 26.0f;
// The reach of a punch, and of the sniper rifle's butt.
constexpr float kFistReach = 50.0f;
constexpr float kSniperFistReach = 100.0f;
constexpr int kShotgunBullets = 5;
// The farthest the watch laser reaches.
constexpr float kLaserReach = 300.0f;
constexpr float kDegToRad = 0.017453292f;

// The instance behind a level model handle.
Model::ModelInstance& Instance(ModelHandle& handle) {
    return *static_cast<LevelModel&>(handle).Instance();
}

float SquareSum(const Vec3& v) {
    return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]);
}

void ToBounds(const ModelBounds& source, float (&out)[6]) {
    out[0] = source.xmin;
    out[1] = source.xmax;
    out[2] = source.ymin;
    out[3] = source.ymax;
    out[4] = source.zmin;
    out[5] = source.zmax;
}

// g_HitReactionTable's puffs: the spark kind and size each part leaves
// where it is hit, and the kind and size it leaves behind it.
struct HitPuff {
    int part;
    int count;
    float size;
    int backCount;
    float backSize;
};
constexpr std::array<HitPuff, 18> kHitPuffs = { {
    { 0, 0, 0.0f, 0, 0.0f },
    { 1, 1, 17.0f, 3, 34.0f },
    { 2, 1, 17.0f, 3, 39.0f },
    { 3, 1, 21.0f, 3, 43.0f },
    { 4, 1, 17.0f, 3, 34.0f },
    { 5, 1, 17.0f, 3, 39.0f },
    { 6, 1, 21.0f, 3, 43.0f },
    { 7, 1, 21.0f, 3, 52.0f },
    { 8, 1, 21.0f, 3, 43.0f },
    { 9, 1, 17.0f, 3, 34.0f },
    { 0xA, 1, 17.0f, 3, 43.0f },
    { 0xB, 1, 21.0f, 3, 52.0f },
    { 0xC, 1, 17.0f, 3, 34.0f },
    { 0xD, 1, 17.0f, 3, 43.0f },
    { 0xE, 1, 21.0f, 3, 52.0f },
    { 0xF, 1, 26.0f, 3, 60.0f },
    { 0x64, 1, 26.0f, 0, 0.0f },
    { 0x6E, 1, 21.0f, 0, 0.0f },
} };

} // namespace

float Shots::RandomFraction() {
    return static_cast<float>(scene_.random()) * kFraction;
}

int Shots::TileRoom(int tile) const {
    return scene_.stan->TileRoom(tile);
}

void Shots::CountShot(int32_t item, int reg) {
    if (scene_.weapons->Table().HasFlag(item, WeaponFlag::PlayerStatHit) && scene_.shotCount != nullptr) {
        (*scene_.shotCount)[static_cast<size_t>(reg)]++;
    }
}

void Shots::SetTracerTarget(const Vec3& target) {
    scene_.weapons->SetTracerTarget(target);
}

int Shots::SoundSurface(int texture) const {
    if (texture < 0) {
        return -1;
    }
    const TextureHit* hit = scene_.textures != nullptr ? scene_.textures->Find(texture) : nullptr;
    return hit != nullptr ? hit->sound : 0;
}

void Shots::PropHitSound(int32_t item, const Vec3& position, bool fleshy, int texture) {
    hitSounds_.PropHit(item, position, fleshy, SoundSurface(texture), scene_.random);
}

void Shots::WorldHitSound(int32_t item, const Vec3& position, int texture) {
    hitSounds_.WorldHit(item, position, SoundSurface(texture), scene_.random);
}

void Shots::CheckUseHeldItems() {
    // objTick poses the on-screen objects for this frame's camera before the
    // shots are traced against them.
    RefreshPoses();
    CheckUseHeldItem(HandRight);
    CheckUseHeldItem(HandLeft);
}

void Shots::CheckUseHeldItem(Hand hand) {
    PlayerWeapons& weapons = *scene_.weapons;
    if (!weapons.Firing(hand)) {
        return;
    }
    const int32_t item = weapons.WeaponId(hand);
    if (item == kItemTrigger) {
        weapons.TriggerRemoteMines();
    } else if (item == kItemGrenadeLauncher || item == kItemRocketLauncher || item == kItemGrenade ||
               item == kItemThrowKnife || item == kItemRemoteMine || item == kItemProximityMine ||
               item == kItemTimedMine || item == kItemFlarePistol || item == kItemPitonGun || item == kItemBombCase ||
               item == kItemBug || item == kItemMicroCamera || item == kItemGoldenEyeKey || item == kItemToken ||
               item == kItemPlastique) {
        // What these throw or launch is made as the hand's model is posed.
    } else if (item == kItemTankShells) {
        if (scene_.fireTankShell) {
            scene_.fireTankShell(hand);
        }
    } else if (item == kItemFist || item == kItemKnife) {
        FireFist(hand, item);
    } else if (item == kItemShotgun || item == kItemAutoShotgun) {
        CountShot(item, ShotRegister::Total);
        for (int i = 0; i < kShotgunBullets; i++) {
            FireDefault(hand);
        }
    } else if (item == kItemCamera) {
        TakePicture();
    } else if (item == kItemWatchMagnetAttract) {
        scene_.weapons->StartMagnetAttract();
    } else {
        CountShot(item, ShotRegister::Total);
        FireDefault(hand);
    }
}

void Shots::BulletPath(Hand hand, Vec3* origin, Vec3* dir) {
    PlayerWeapons& weapons = *scene_.weapons;
    const PlayerCamera& camera = *scene_.camera;
    const WeaponTable& table = weapons.Table();

    float inaccuracy = table.Item(weapons.WeaponId(hand)).inaccuracy;
    if (table.HasFlag(weapons.ItemInHandOrWatchMenu(hand), WeaponFlag::FirstShotAccuracy) &&
        weapons.Hands(hand).volley == 1) {
        // Single shots are four times more accurate.
        inaccuracy *= 0.25f;
    }

    const float scaledSpread = (120.0f * inaccuracy) / camera.FovY();
    const GunAimState& aim = weapons.Aim();

    float first = RandomFraction() - 0.5f;
    float second = RandomFraction();
    float randomFactor = first * second;
    const float x = aim.crosshairScreenX + randomFactor * scaledSpread * camera.ScreenWidth() * kGunScreenAspectRatio /
                                               (camera.Aspect() * 320.0f);

    first = RandomFraction() - 0.5f;
    second = RandomFraction();
    randomFactor = first * second;
    const float y = aim.crosshairScreenY + randomFactor * scaledSpread * camera.ScreenHeight() / 240.0f;

    *origin = { 0.0f, 0.0f, 0.0f };
    *dir = camera.ScreenToView(x, y, 1.0f);
}

Vec3 Shots::SightDirection() const {
    const GunAimState& aim = scene_.weapons->Aim();
    return scene_.camera->ScreenToView(aim.crosshairScreenX, aim.crosshairScreenY, 1.0f);
}

bool Shots::RayMeetsRoom(int room, const Vec3& start, const Vec3& dir) const {
    if (!scene_.bgHit->HasBounds(room)) {
        return false;
    }
    const RoomInfo& info = scene_.level->Room(room);
    int32_t min[3];
    int32_t max[3];
    for (size_t axis = 0; axis < 3; axis++) {
        min[axis] = static_cast<int32_t>(info.minBounds[axis]);
        max[axis] = static_cast<int32_t>(info.maxBounds[axis]);
    }
    return RayIntersectsBox(start, dir, min, max);
}

bool Shots::TestBackground(const Vec3& from, const Vec3& to, int room, HitThing* hit) const {
    return scene_.bgHit->TestBullet(from, to, room, hit, scene_.level->Room(room).geometry);
}

int Shots::FindFirstBgHit(int startRoom, const Vec3& from, const Vec3& to, const Vec3& dir, const Vec3& scaledDir,
                          std::array<uint8_t, 256>& visited, HitThing* hit) {
    std::vector<int> rooms = { startRoom };
    for (size_t index = 0; index < rooms.size(); index++) {
        const int room = rooms[index];
        if (visited[static_cast<size_t>(room)] == 0) {
            visited[static_cast<size_t>(room)] = 1;
            if (RayMeetsRoom(room, scaledDir, dir) && TestBackground(from, to, room, hit)) {
                return room;
            }
        }
        for (const int neighbour : scene_.level->ConnectedRooms(room, 100)) {
            if (std::find(rooms.begin(), rooms.end(), neighbour) == rooms.end()) {
                rooms.push_back(neighbour);
            }
        }
    }
    return 0;
}

int Shots::FindClosestBgHit(const Vec3& from, const Vec3& to, const Vec3& dir, const Vec3& scaledDir,
                            std::array<uint8_t, 256>& visited, HitThing* best) {
    float bestDistance = kU32Max;
    int bestRoom = 0;
    const float scale = 1.0f / scene_.level->Level().scale;

    for (int room = 1; room < scene_.level->MaxNumRooms(); room++) {
        if (visited[static_cast<size_t>(room)] != 0) {
            continue;
        }
        visited[static_cast<size_t>(room)] = 1;
        if (!RayMeetsRoom(room, scaledDir, dir)) {
            continue;
        }
        HitThing hit;
        if (!TestBackground(from, to, room, &hit)) {
            continue;
        }
        const float dx = (hit.hitpos[0] * scale) - from[0];
        const float dy = ((hit.hitpos[1] * scale) - from[1]) * 1.0f;
        float distance = (hit.hitpos[2] * scale) - from[2];
        distance = ((dx * dx) + (dy * dy)) + (distance * distance);
        float adjusted = distance;
        if (hit.textureHash != 0 && LightFixtures::IsLightTexture(hit.textureHash)) {
            adjusted = distance - 4.0f;
        }
        if (adjusted < bestDistance) {
            *best = hit;
            bestDistance = adjusted;
            bestRoom = room;
        }
    }
    return bestRoom;
}

int Shots::FindCloserBgHit(const Vec3& from, const Vec3& to, const Vec3& dir, const Vec3& scaledDir,
                           std::array<uint8_t, 256>& visited, HitThing* best, int bestRoom) {
    const float scale = 1.0f / scene_.level->Level().scale;
    const std::vector<uint8_t>& rooms = scene_.visibility->CommandRooms();
    const size_t count = std::min<size_t>(rooms.size(), 100);

    for (size_t i = 0; i < count; i++) {
        const int room = rooms[i];
        if (visited[static_cast<size_t>(room)] != 0) {
            continue;
        }
        visited[static_cast<size_t>(room)] = 1;
        if (!RayMeetsRoom(room, scaledDir, dir)) {
            continue;
        }
        HitThing hit;
        if (!TestBackground(from, to, room, &hit)) {
            continue;
        }
        for (size_t axis = 0; axis < 3; axis++) {
            hit.hitpos[axis] *= scale;
        }

        // Closer along every axis than the hit so far.
        const auto between = [&](size_t axis) {
            return ((from[axis] <= best->hitpos[axis] && from[axis] <= hit.hitpos[axis] &&
                     hit.hitpos[axis] < best->hitpos[axis]) ||
                    (best->hitpos[axis] <= from[axis] && hit.hitpos[axis] <= from[axis] &&
                     best->hitpos[axis] < hit.hitpos[axis]));
        };
        if (bestRoom <= 0 || (between(0) && between(1) && between(2))) {
            bestRoom = room;
            *best = hit;
        }
    }
    return bestRoom;
}

void Shots::AddHit(ShotData* shot, const ShotHit& hit, int blocksFurtherHits) {
    const int shootThrough = scene_.weapons->Table().Item(shot->weapon).objectsShootThrough;
    const float dist = hit.dist;
    auto& hits = shot->hits;

    // If the hit is on something bullets may pass through, it counts against the
    // weapon's limit.
    if (hit.countsAsPenetration) {
        float furthest = 0.0f;
        float previousFurthest = furthest;
        int furthestIndex = 0;
        int penetrated = 0;

        for (int i = 0; i < ShotData::kMaxHits; i++) {
            if (hits[static_cast<size_t>(i)].prop >= 0 && hits[static_cast<size_t>(i)].countsAsPenetration) {
                penetrated++;
                if (furthest < hits[static_cast<size_t>(i)].dist) {
                    previousFurthest = furthest;
                    furthest = hits[static_cast<size_t>(i)].dist;
                    furthestIndex = i;
                }
            }
        }

        if (penetrated >= shootThrough) {
            // The bullet has reached the most objects it can pass through: this hit
            // replaces the furthest.
            hits[static_cast<size_t>(furthestIndex)].prop = -1;
            shot->maxDistance = previousFurthest;
            if (previousFurthest < dist) {
                shot->maxDistance = dist;
            }
            for (int i = 0; i < ShotData::kMaxHits; i++) {
                ShotHit& other = hits[static_cast<size_t>(i)];
                if (other.prop >= 0 && !other.countsAsPenetration && previousFurthest < other.dist) {
                    other.prop = -1;
                }
            }
        } else if (penetrated + 1 == shootThrough) {
            // The last object the bullet can pass through.
            if (dist < shot->maxDistance) {
                shot->maxDistance = dist;
            }
        }
    }

    // Bulletproof glass stops every bullet but the Cougar Magnum's and the
    // Silver PP7's.
    if (blocksFurtherHits != 0 && shot->weapon != kItemRuger && shot->weapon != kItemSilverPp7) {
        for (ShotHit& other : hits) {
            if (other.prop >= 0 && dist < other.dist) {
                other.prop = -1;
            }
        }
        shot->maxDistance = dist;
    }

    for (ShotHit& slot : hits) {
        if (slot.prop < 0) {
            slot = hit;
            break;
        }
    }
}

void Shots::FireDefault(Hand hand) {
    PlayerWeapons& weapons = *scene_.weapons;
    const PlayerCamera& camera = *scene_.camera;
    Stan& stan = *scene_.stan;
    PropList& props = *scene_.props;

    bool hitStan = false;
    bool gotBgHit = false;
    int bestRoom = 0;
    int hitTile = kNoTile;
    const int playerPropId = scene_.playerProp();
    const Prop& playerProp = props[playerPropId];
    int fromTile = playerProp.stan;
    int numHits = 0;

    ShotData shot;
    BulletPath(hand, &shot.viewOrigin, &shot.viewDir);
    shot.weapon = weapons.WeaponId(hand);
    shot.maxDistance = kU32Max;
    shot.gunPosition = TransformVector(camera.ViewToWorld(), shot.viewOrigin);
    shot.dir = RotateVector(camera.ViewToWorld(), shot.viewDir);

    const Vec3 dest = { (shot.dir[0] * kU16Max) + shot.gunPosition[0], (shot.dir[1] * kU16Max) + shot.gunPosition[1],
                        (shot.dir[2] * kU16Max) + shot.gunPosition[2] };

    Vec3 stanHit{};
    Vec3 visibleHitPosition{};
    Vec3 bestHitPosition{};
    HitThing bgHit;
    int bestTexture = -1;
    float negZ = 0.0f;

    if (stan.WalkTiles(&fromTile, playerProp.position[0], playerProp.position[2], shot.gunPosition[0],
                       shot.gunPosition[2])) {
        const float distScale = scene_.level->Level().scale * scene_.level->VisibilityScale();
        const Vec3 playerPosition = camera.WorldPosition();

        stan.ResetHits();

        if (!stan.WalkTiles(&fromTile, shot.gunPosition[0], shot.gunPosition[2], dest[0], dest[2])) {
            Vec3 edgeA{};
            Vec3 edgeB{};
            stan.CollisionEdge(&edgeA, &edgeB);
            stanHit = ChrActions::LineLineIntersection(edgeA, edgeB, shot.gunPosition, shot.dir);
            hitStan = true;
        } else {
            stanHit = dest;
        }

        const Vec3 hitDir = { stanHit[0] - playerPosition[0], stanHit[1] - playerPosition[1],
                              stanHit[2] - playerPosition[2] };
        const Vec3 scaledDir = { playerPosition[0] * distScale, playerPosition[1] * distScale,
                                 playerPosition[2] * distScale };
        hitTile = fromTile;
        const int startRoom = TileRoom(fromTile);

        std::array<uint8_t, 256> visited{};

        if (TestBackground(playerPosition, stanHit, startRoom, &bgHit)) {
            bestRoom = startRoom;
        }
        visited[static_cast<size_t>(startRoom)] = 1;

        if (bestRoom <= 0) {
            if (!scene_.level->Portals().empty()) {
                bestRoom = FindFirstBgHit(TileRoom(playerProp.stan), playerPosition, stanHit, hitDir, scaledDir,
                                          visited, &bgHit);
            } else {
                bestRoom = FindClosestBgHit(playerPosition, stanHit, hitDir, scaledDir, visited, &bgHit);
            }
        }

        if (bestRoom > 0) {
            const float scale = 1.0f / scene_.level->Level().scale;
            for (size_t axis = 0; axis < 3; axis++) {
                bgHit.hitpos[axis] *= scale;
            }
        }

        bestRoom = FindCloserBgHit(playerPosition, stanHit, hitDir, scaledDir, visited, &bgHit, bestRoom);

        if (bestRoom > 0) {
            gotBgHit = true;
            bestTexture = bgHit.texture;
            bestHitPosition = bgHit.hitpos;
            visibleHitPosition = bgHit.hitpos;
        } else {
            bestRoom = startRoom;
            bestTexture = -1;
            bestHitPosition = dest;
        }

        if (hitStan || gotBgHit) {
            Mtxf worldToScreen;
            std::memcpy(worldToScreen.m, camera.WorldToScreen(), sizeof(worldToScreen.m));
            bestHitPosition = TransformVector(worldToScreen, bestHitPosition);
            negZ = -bestHitPosition[2];
            shot.maxDistance = negZ;
        }
    }

    if (shot.weapon == kItemWatchLaser && shot.maxDistance > kLaserReach) {
        shot.maxDistance = kLaserReach;
    }

    // The props on screen, the nearest first.
    for (auto it = scene_.onScreen->rbegin(); it != scene_.onScreen->rend(); ++it) {
        const Prop& prop = props[*it];
        if (prop.type == PropType::Chr) {
            TestChr(*it, &shot);
        } else if (prop.type == PropType::Obj || prop.type == PropType::Weapon || prop.type == PropType::Door) {
            TestObject(*it, &shot);
        }
    }

    const int shootThrough = weapons.Table().Item(shot.weapon).objectsShootThrough;
    for (ShotHit& hit : shot.hits) {
        if (hit.prop < 0) {
            continue;
        }
        const PropType type = props[hit.prop].type;
        if (type == PropType::Chr || type == PropType::Viewer) {
            HandleChrHit(shot, hit);
        } else if (type == PropType::Obj || type == PropType::Weapon || type == PropType::Door) {
            HitObject(shot, hit);
        }

        if (hit.countsAsPenetration) {
            numHits++;
            if (numHits >= shootThrough) {
                gotBgHit = false;
                hitStan = false;
            }
        }
    }

    if (!gotBgHit && !hitStan) {
        return;
    }

    bool createSpark = true;
    if (shot.weapon == kItemWatchLaser && negZ > kLaserReach) {
        createSpark = false;
    }

    Vec3 stanEnd{};
    Vec3* finalPosition = nullptr;
    if (gotBgHit) {
        const SurfaceImpacts& impacts = SurfaceImpactsFor(ImpactSurface(bgHit.texture));
        if (createSpark) {
            if (impacts.count > 0 && shot.weapon != kItemWatchLaser) {
                const int pick = static_cast<int>(scene_.random() % static_cast<uint32_t>(impacts.count));
                CreateImpact(visibleHitPosition, bgHit.normal, impacts.types[static_cast<size_t>(pick)], bestRoom, -1,
                             nullptr, -1, 0);
            }
            if (bgHit.textureHash != 0 && LightFixtures::IsLightTexture(bgHit.textureHash)) {
                BreakLight(bgHit, bestRoom);
            }
        }
        finalPosition = &visibleHitPosition;
    } else if (hitStan) {
        stanEnd = { (shot.dir[0] * kU16Max) + shot.gunPosition[0], (shot.dir[1] * kU16Max) + shot.gunPosition[1],
                    (shot.dir[2] * kU16Max) + shot.gunPosition[2] };
        finalPosition = &stanEnd;
    }

    if (finalPosition != nullptr) {
        if (createSpark) {
            WorldHitSound(shot.weapon, *finalPosition, bestTexture);
            const int surface = ImpactSurface(bestTexture);
            if (surface != HitType::Water && surface != HitType::Snow) {
                std::array<uint8_t, 8> rooms{};
                rooms.fill(kNoRoom);
                rooms[0] = static_cast<uint8_t>(bestRoom);
                scene_.explosions->Create(-1, *finalPosition, hitTile, 1, 0, 0, rooms, false, scene_.random);
            }
        }

        (*finalPosition)[0] -= kSparkBackOff * shot.dir[0];
        (*finalPosition)[1] -= kSparkBackOff * shot.dir[1];
        (*finalPosition)[2] -= kSparkBackOff * shot.dir[2];

        SetTracerTarget(*finalPosition);

        if (createSpark) {
            scene_.sparks->Create(*finalPosition, 1, kSparkBackOff, bestRoom, scene_.random);
        }
    }
}

void Shots::TakePicture() {
    if (scene_.objectives == nullptr || scene_.links == nullptr) {
        return;
    }
    const PlayerCamera& camera = *scene_.camera;
    const PropList& props = *scene_.props;
    const float left = camera.ScreenLeft();
    const float top = camera.ScreenTop();
    const float width = camera.ScreenWidth();
    const float height = camera.ScreenHeight();

    // transform3Dto2DCoords.
    const auto projectX = [&](float x, float z) {
        return (left + (width * 0.5f)) - (x * (1.0f / z) * camera.RecipScaleX());
    };
    const auto projectY = [&](float y, float z) {
        return (y * (1.0f / z) * camera.RecipScaleY()) + (top + (height * 0.5f));
    };

    for (const Objectives::Picture& picture : scene_.objectives->PendingPictures()) {
        const LinkTarget target = scene_.links->FindByTag(picture.tag);
        if (!target.Exists() || target.Prop() < 0) {
            continue;
        }
        const Prop& prop = props[target.Prop()];
        // objIsHealthy; a door has no destroyed state yet.
        if ((prop.flags & PropFlag::OnScreen) == 0 || !(prop.zDepth >= 0.0f) ||
            (target.object != nullptr && !Objects::IsHealthy(*target.object))) {
            continue;
        }

        // objGetOnscreenRenderBounds: the model's origin in the view, in front
        // of the camera, and its extents across and up.
        const Model::ModelInstance* model = PoseProp(target.Prop(), nullptr);
        if (model == nullptr) {
            continue;
        }
        NodeRef root;
        root.model = model;
        root.node = 0;
        const Mtxf* rootMatrix = NodeMatrix(root);
        if (rootMatrix == nullptr || !(rootMatrix->m[3][2] < 0.0f)) {
            continue;
        }
        const float centreZ = rootMatrix->m[3][2];
        float maxX = 0.0f;
        float minX = 0.0f;
        float maxY = 0.0f;
        float minY = 0.0f;
        AxisExtents(*model, 0, &maxX, &minX);
        AxisExtents(*model, 1, &maxY, &minY);

        // projectRectCornersTo2D.
        const float right = projectX(minX, centreZ);
        const float rectLeft = projectX(maxX, centreZ);
        const float down = projectY(maxY, centreZ);
        const float up = projectY(minY, centreZ);

        if (left < right && right < left + width && left < rectLeft && rectLeft < left + width && top < down &&
            down < top + height && top < up && up < top + height) {
            scene_.objectives->MarkPictureTaken(picture.record);
        }
    }
}

void Shots::FireFist(Hand hand, int32_t item) {
    PlayerWeapons& weapons = *scene_.weapons;
    PropList& props = *scene_.props;
    Stan& stan = *scene_.stan;
    const PlayerCamera& camera = *scene_.camera;

    bool hit = false;
    const int playerPropId = scene_.playerProp();
    const Prop& playerProp = props[playerPropId];
    const float ducking = scene_.duckingHeight();

    for (auto it = scene_.onScreen->rbegin(); it != scene_.onScreen->rend(); ++it) {
        const Prop& prop = props[*it];
        if (!(prop.zDepth < 500.0f)) {
            continue;
        }
        // Only characters are punched (another player is not part of a
        // single-player stage).
        if (prop.type != PropType::Chr || prop.object < 0) {
            continue;
        }

        float reach = kFistReach;
        Chr& chr = (*scene_.chrs)[prop.object];
        if (!chr.model) {
            continue;
        }
        if (weapons.FistModel() == kItemSniperRifle) {
            reach = kSniperFistReach;
        }

        const Model::ModelInstance& model = Instance(*chr.model);
        float max0 = 0.0f;
        float min0 = 0.0f;
        AxisExtents(model, 0, &max0, &min0);
        if (!(0.0f <= max0) || !(min0 <= 0.0f)) {
            continue;
        }
        float max1 = 0.0f;
        float min1 = 0.0f;
        AxisExtents(model, 1, &max1, &min1);
        if (!(0.0f <= max1) || !(min1 <= 0.0f)) {
            continue;
        }
        float max2 = 0.0f;
        float min2 = 0.0f;
        AxisExtents(model, 2, &max2, &min2);
        if (!(min2 <= 0.0f) || !((-reach) <= max2)) {
            continue;
        }

        int tile = playerProp.stan;
        if (!stan.TestLineUnobstructed(&tile, playerProp.position[0], playerProp.position[2], prop.position[0],
                                       prop.position[2], CdType::Objs | CdType::Doors | CdType::PathBlocker, ducking,
                                       ducking, 0.0f, 1.0f)) {
            continue;
        }
        if (tile != prop.stan) {
            continue;
        }

        int hitpart = HitPart::Chest;
        const int crouch = scene_.crouch();
        if (crouch == kCrouchHalf) {
            hitpart = HitPart::General;
        } else if (crouch == kCrouchSquat) {
            hitpart = HitPart::GeneralHalf;
        }

        Vec3 origin{};
        Vec3 vector{};
        BulletPath(hand, &origin, &vector);
        vector = RotateVector(camera.ViewToWorld(), vector);

        if (scene_.shotActor(chr, hitpart, vector, item, true)) {
            PropHitSound(item, prop.position, true, -1);
            hit = true;
        }
    }

    if (!hit && item == kItemFist) {
        Game::Sound::PlaySfx(AudioDriver::kSfxPunchingAir);
    }
}

int Shots::ImpactSurface(int texture) const {
    if (texture < 0 || scene_.textures == nullptr) {
        return 0;
    }
    const TextureHit* hit = scene_.textures->Find(texture);
    return hit != nullptr ? hit->impact : 0;
}

void Shots::BreakLight(const HitThing& hit, int room) {
    const RoomInfo& info = scene_.level->Room(room);
    LightFixtures::BreakContext context;
    context.vertices = static_cast<Vtx*>(info.geometry.vertices.data);
    context.vertexCount = info.geometry.vertices.count;
    context.commands = static_cast<const Gfx*>(info.geometry.primaryList.data);
    context.commandCount = info.geometry.primaryList.count;
    context.levelScale = scene_.level->Level().scale;
    context.recipLevelScale = 1.0f / context.levelScale;
    const Vec3& position = scene_.level->RoomEntry(room).position;
    context.roomOrigin = { position[0] * context.recipLevelScale, position[1] * context.recipLevelScale,
                           position[2] * context.recipLevelScale };
    context.createShard = [this](const Vec3& at) { scene_.shards->Create(at, 0.0f, 10.0f, scene_.random); };
    scene_.lights->Break(static_cast<const Gfx*>(hit.triCommand), static_cast<uint32_t>(hit.triType), room, context);
}

void Shots::CreateImpact(const Vec3& position, const Vec3& normal, int type, int room, int prop, const Mtxf* matrix,
                         int matrixIndex, int8_t clearFlag) {
    ImpactPlacement placement;
    placement.position = position;
    placement.normal = normal;
    placement.type = type;
    placement.room = room;
    placement.prop = prop;
    placement.propMatrix = matrix;
    placement.matrixIndex = matrixIndex;
    placement.clearFlag = clearFlag;
    // TODO(https://github.com/liz-baker/frigate/issues/136): CHEAT_PAINTBALL.
    placement.paintball = false;
    placement.levelScale = scene_.level->Level().scale;
    placement.roomPosition = scene_.level->RoomEntry(room).position;
    const uint8_t mark = scene_.impacts->Create(placement, scene_.random);
    if (prop >= 0 && mark != 0) {
        if (PlacedObject* object = scene_.objects->ForProp(prop)) {
            object->state |= mark;
        } else if (PlacedDoor* door = scene_.doors->ForProp(prop)) {
            door->marks |= mark;
        }
    }
}

void Shots::CreateHitPuffs(int prop, int hitpart, const Vec3& viewPosition, const Vec3& position) {
    const HitPuff* entry = &kHitPuffs[0];
    for (const HitPuff& candidate : kHitPuffs) {
        if (candidate.part == hitpart) {
            entry = &candidate;
            break;
        }
    }

    const int stan = (*scene_.props)[prop].stan;
    const int room = stan != kNoTile ? TileRoom(stan) : 0;

    if (entry->backCount != 0) {
        // About half the time, a second puff behind the hit.
        if ((scene_.random() & 4) == 0) {
            const float scale =
                (42.0f / std::sqrt(viewPosition[2] * viewPosition[2] +
                                   (viewPosition[0] * viewPosition[0] + viewPosition[1] * viewPosition[1]))) +
                1.0f;
            Vec3 behind = { viewPosition[0] * scale, viewPosition[1] * scale, viewPosition[2] * scale };
            behind = TransformVector(scene_.camera->ViewToWorld(), behind);
            scene_.sparks->Create(behind, entry->backCount, entry->backSize, room, scene_.random);
        }
    }

    if (entry->count != 0) {
        scene_.sparks->Create(position, entry->count, entry->size, room, scene_.random);
    }
}

void Shots::TestChr(int propId, ShotData* shot) {
    PropList& props = *scene_.props;
    Prop& prop = props[propId];
    Chr& chr = (*scene_.chrs)[prop.object];
    if (chr.actionType == ChrAction::Dead || !chr.model) {
        return;
    }

    const Model::ModelInstance& model = Instance(*chr.model);
    float modelSize = InstanceSize(model);
    if ((prop.flags & PropFlag::OnScreen) == 0) {
        return;
    }
    if (!((prop.zDepth - modelSize) < shot->maxDistance)) {
        return;
    }

    // The held weapons count towards the character's size.
    float heldSize = 0.0f;
    for (const int held : chr.weaponsHeld) {
        if (held < 0) {
            continue;
        }
        const PlacedObject* weapon = scene_.objects->ForProp(held);
        if (weapon == nullptr || !weapon->instance) {
            continue;
        }
        const float size = model.playback.scale * InstanceSize(Instance(*weapon->instance));
        if (heldSize < size) {
            heldSize = size;
        }
    }
    modelSize += heldSize;

    int hitpart = HitPart::NullPart;
    bool hitBounds = false;
    NodeRef root;
    root.model = &model;
    root.node = 0;
    const Mtxf* rootMatrix = NodeMatrix(root);
    if (rootMatrix != nullptr &&
        RayIntersectsSphere(shot->viewOrigin, shot->viewDir,
                            { rootMatrix->m[3][0], rootMatrix->m[3][1], rootMatrix->m[3][2] }, modelSize)) {
        hitBounds = true;
        hitpart = HitPart::LeftFoot;
    }

    NodeRef hitNode;
    HitThing hitThing;
    int matrixIndex = 0;
    int32_t triangleNode = kNoNode;
    if (hitpart != HitPart::NullPart) {
        std::vector<const Model::ModelInstance*> models;
        for (const auto& handle : chr.drawList) {
            models.push_back(&Instance(*handle));
        }
        JointHitSearch search(models);
        hitpart = search.First(shot->viewOrigin, shot->viewDir, &hitNode);
        while (hitpart == HitPart::Gun || hitpart == HitPart::Hat) {
            if (scene_.models != nullptr && scene_.models->Geometry() != nullptr &&
                FindTriangleHit(*hitNode.model, hitNode.node, shot->viewOrigin, shot->viewDir,
                                *scene_.models->Geometry(), scene_.textures, &hitThing, &matrixIndex, &triangleNode)) {
                break;
            }
            hitpart = search.Next(shot->viewOrigin, shot->viewDir, &hitNode);
        }
    }

    if (hitpart > HitPart::NullPart) {
        if (const Mtxf* nodeMatrix = NodeMatrix(hitNode)) {
            const Vec3 diff = { nodeMatrix->m[3][0] - shot->viewOrigin[0], nodeMatrix->m[3][1] - shot->viewOrigin[1],
                                nodeMatrix->m[3][2] - shot->viewOrigin[2] };
            const float length = std::sqrt((diff[2] * diff[2]) + ((diff[0] * diff[0]) + (diff[1] * diff[1])));
            const float dist = -((shot->viewDir[2] * length) + shot->viewOrigin[2]);

            if (dist < shot->maxDistance) {
                ShotHit hit;
                hit.prop = propId;
                hit.dist = dist;
                hit.hitpart = hitpart;
                hit.node = hitNode;
                hit.hit = hitThing;
                hit.matrixIndex = matrixIndex;
                hit.model = hitNode.model;
                hit.countsAsPenetration = true;
                if (triangleNode != kNoNode) {
                    hit.triangleNode = hitNode.model->Nodes()[static_cast<size_t>(triangleNode)].offset;
                }
                const auto& matrices = hitNode.model->Walk().matrices;
                if (matrixIndex >= 0 && static_cast<size_t>(matrixIndex) < matrices.size()) {
                    std::memcpy(hit.matrix.m, matrices[static_cast<size_t>(matrixIndex)].m, sizeof(hit.matrix.m));
                } else {
                    hit.matrix = *nodeMatrix;
                }
                AddHit(shot, hit, 0);
            }
        }
    }

    if (hitBounds && prop.zDepth <= shot->maxDistance) {
        chr.chrflags |= ChrFlag::NearMiss;
        chr.numCloseArghs++;
    }
}

void Shots::HandleChrHit(const ShotData& shot, const ShotHit& hit) {
    PropList& props = *scene_.props;
    Prop& prop = props[hit.prop];
    Chr& chr = (*scene_.chrs)[prop.object];

    // The hit's position in view space, for the effects.
    const Vec3 hitPosition = { shot.viewOrigin[0] - ((hit.dist * shot.viewDir[0]) / shot.viewDir[2]),
                               shot.viewOrigin[1] - ((hit.dist * shot.viewDir[1]) / shot.viewDir[2]),
                               shot.viewOrigin[2] - hit.dist };

    const float scale =
        1.0f - (42.0f / std::sqrt((hitPosition[0] * hitPosition[0]) + (hitPosition[1] * hitPosition[1]) +
                                  (hitPosition[2] * hitPosition[2])));

    // A position nearer the camera, so the tracer and the puffs do not land
    // inside the character's model.
    Vec3 nearPosition = { hitPosition[0] * scale, hitPosition[1] * scale, hitPosition[2] * scale };
    nearPosition = TransformVector(scene_.camera->ViewToWorld(), nearPosition);

    SetTracerTarget(nearPosition);
    PropHitSound(shot.weapon, prop.position, true, -1);
    CreateHitPuffs(hit.prop, hit.hitpart, hitPosition, nearPosition);

    if (!scene_.shotActor(chr, hit.hitpart, shot.dir, shot.weapon, true)) {
        return;
    }

    if (hit.hitpart == HitPart::Gun) {
        for (const int held : chr.weaponsHeld) {
            if (held < 0) {
                continue;
            }
            PlacedObject* weapon = scene_.objects->ForProp(held);
            if (weapon == nullptr || !weapon->instance || &Instance(*weapon->instance) != hit.model) {
                continue;
            }
            const int32_t number = weapon->weaponNum;
            if (number == kItemGrenade || number == kItemGrenadeRound || number == kItemRocketRound ||
                number == kItemTimedMine || number == kItemBombCase || number == kItemRemoteMine ||
                number == kItemProximityMine) {
                // A shot explosive in a hand goes off.
                scene_.setDropped(held);
                chr.hidden |= ChrHidden::DropHeldItems;
                scene_.damage->Apply(*weapon, scene_.weapons->Table().Item(shot.weapon).destructionAmount, hitPosition,
                                     shot.weapon, 0);
            } else {
                // A bullet hole on the held weapon.
                const SurfaceImpacts& impacts = SurfaceImpactsFor(ImpactSurface(hit.hit.texture));
                if (impacts.count > 0) {
                    const int pick = static_cast<int>(scene_.random() % static_cast<uint32_t>(impacts.count));
                    CreateImpact(hit.hit.hitpos, hit.hit.normal, impacts.types[static_cast<size_t>(pick)], 1, held,
                                 &hit.matrix, hit.matrixIndex, 0);
                }
            }
        }
        return;
    }

    if (hit.hitpart == HitPart::Hat) {
        // A bullet hole on the hat or helmet.
        const SurfaceImpacts& impacts = SurfaceImpactsFor(ImpactSurface(hit.hit.texture));
        if (impacts.count > 0 && chr.hat >= 0) {
            const int pick = static_cast<int>(scene_.random() % static_cast<uint32_t>(impacts.count));
            CreateImpact(hit.hit.hitpos, hit.hit.normal, impacts.types[static_cast<size_t>(pick)], 1, chr.hat,
                         &hit.matrix, hit.matrixIndex, 0);
        }
        return;
    }

    // The stain's position in the hit joint's own space.
    const Mtxf* nodeMatrix = NodeMatrix(hit.node);
    if (nodeMatrix != nullptr && chr.model) {
        Vec3 joint = hitPosition;
        for (int axis = 0; axis < 3; axis++) {
            joint[axis] += (joint[axis] - nodeMatrix->m[3][axis]) * 0.5f;
        }
        const float size = JointSize(hit.node);
        for (int axis = 0; axis < 3; axis++) {
            joint[axis] -= size * 0.5f * shot.viewDir[axis];
        }
        joint = TransformVector(InverseRotationAndTranslation(*nodeMatrix), joint);
        CreateBloodStain(Instance(*chr.model), hit.hitpart, hit.node, joint);
    }
}

void Shots::CreateBloodStain(Model::ModelInstance& model, int hitpart, const NodeRef& root, const Vec3& position) {
    constexpr uint8_t kCollisionList = 0x18;
    constexpr uint8_t kBoundingBox = 0x0A;
    constexpr uint8_t kOp17 = 0x17;

    const bool head = root.attachment >= 0;
    const ModelTree* tree = head ? model.HeadTree() : &model.Tree();
    if (tree == nullptr) {
        return;
    }
    const std::vector<ModelNode>& nodes = tree->Nodes;

    const int32_t target[3] = { static_cast<int32_t>(position[0]), static_cast<int32_t>(position[1]),
                                static_cast<int32_t>(position[2]) };
    int32_t bestNode = kNoNode;
    int32_t bestIndex = 0;
    int32_t bestDist = 0x7fffffff;

    int32_t node = root.node;
    while (node != kNoNode) {
        const ModelNode& current = nodes[static_cast<size_t>(node)];
        if (current.type == kCollisionList && current.collision >= 0) {
            const ModelCollision& collision = tree->Collisions[static_cast<size_t>(current.collision)];
            for (size_t n = 0; n < collision.collisionVertices.size(); n++) {
                const auto& vertex = collision.collisionVertices[n];
                const int32_t dx = target[0] - vertex.position[0];
                const int32_t dy = target[1] - vertex.position[1];
                const int32_t dz = target[2] - vertex.position[2];
                // The squares are summed unsigned and then read as signed.
                const auto dist = static_cast<int32_t>((static_cast<uint32_t>(dx) * static_cast<uint32_t>(dx)) +
                                                       (static_cast<uint32_t>(dy) * static_cast<uint32_t>(dy)) +
                                                       (static_cast<uint32_t>(dz) * static_cast<uint32_t>(dz)));
                if (dist < bestDist) {
                    bestDist = dist;
                    bestNode = node;
                    bestIndex = static_cast<int32_t>(n);
                }
            }
        }

        if (current.child != kNoNode &&
            (node == root.node || (current.type != kBoundingBox && current.type != kOp17))) {
            node = current.child;
        } else {
            while (node != kNoNode) {
                if (node == root.node) {
                    node = kNoNode;
                    break;
                }
                if (nodes[static_cast<size_t>(node)].next != kNoNode) {
                    node = nodes[static_cast<size_t>(node)].next;
                    break;
                }
                node = nodes[static_cast<size_t>(node)].parent;
            }
        }
    }
    if (bestNode == kNoNode) {
        return;
    }

    int paint = static_cast<int>(scene_.random() % 50u) + 20;
    if (hitpart == HitPart::Head) {
        paint += 100;
    }
    if (hitpart == HitPart::Chest) {
        paint += 50;
    }
    // TODO(https://github.com/liz-baker/frigate/issues/136): the dynamic vertex
    // store's capacity, which a stain beyond it does not paint past.

    const auto paintPoint = [&](int32_t at, int32_t vertex) {
        const ModelNode& painted = nodes[static_cast<size_t>(at)];
        const ModelCollision& collision = tree->Collisions[static_cast<size_t>(painted.collision)];
        int32_t index = collision.collisionVertices[static_cast<size_t>(vertex)].index;
        while (index >= 0) {
            model.PaintVertex({ head, painted.offset, index, static_cast<uint8_t>(paint) });
            index = collision.pointUsage[static_cast<size_t>(index)];
        }
    };
    const ModelNode& best = nodes[static_cast<size_t>(bestNode)];
    const ModelCollision::Vertex& vertex =
        tree->Collisions[static_cast<size_t>(best.collision)].collisionVertices[static_cast<size_t>(bestIndex)];
    paintPoint(bestNode, bestIndex);
    if (vertex.relatedNode != kNoNode) {
        paintPoint(vertex.relatedNode, vertex.relatedIndex);
    }
}

void Shots::RefreshPoses() {
    if (scene_.onScreen == nullptr) {
        return;
    }
    for (const int propId : *scene_.onScreen) {
        const PropType type = (*scene_.props)[propId].type;
        if (type == PropType::Obj || type == PropType::Weapon || type == PropType::Door) {
            PoseTree(propId, nullptr);
        }
    }
}

void Shots::PoseTree(int propId, const Model::ModelInstance* parent) {
    PropList& props = *scene_.props;
    const Prop& prop = props[propId];
    const PlacedObject* object = prop.type == PropType::Door ? nullptr : scene_.objects->ForProp(propId);
    const PlacedDoor* door = prop.type == PropType::Door ? scene_.doors->ForProp(propId) : nullptr;
    if (object == nullptr && door == nullptr) {
        return;
    }
    const uint32_t runtime = object != nullptr ? object->runtimeFlags : door->runtimeFlags;
    if ((runtime & ObjectRuntime::Regenerating) != 0 || (prop.flags & PropFlag::OnScreen) == 0) {
        return;
    }
    const Model::ModelInstance* instance = PoseProp(propId, parent, true);
    if (instance == nullptr) {
        return;
    }
    for (int child = prop.child; child >= 0; child = props[child].prev) {
        PoseTree(child, instance);
    }
}

Model::ModelInstance* Shots::PoseProp(int propId, const Model::ModelInstance* parent, bool render) {
    const Prop& prop = (*scene_.props)[propId];
    PlacedObject* object = prop.type == PropType::Door ? nullptr : scene_.objects->ForProp(propId);
    PlacedDoor* door = prop.type == PropType::Door ? scene_.doors->ForProp(propId) : nullptr;
    if (object == nullptr && door == nullptr) {
        return nullptr;
    }
    const PropModel* model =
        object != nullptr ? scene_.objects->Model(object->model) : scene_.doors->Model(door->model);
    if (model == nullptr || scene_.models == nullptr) {
        return nullptr;
    }

    auto found = posed_.find(propId);
    if (found == posed_.end()) {
        auto handle = scene_.models->CreateProp("models/prop/P" + model->name + "Z");
        if (!handle) {
            return nullptr;
        }
        // An object's first matrix is its own transform, so the stored origin never
        // applies.
        Instance(*handle).SetRootOrigin({ 0.0f, 0.0f, 0.0f });
        found = posed_.emplace(propId, std::move(handle)).first;
    }
    Model::ModelInstance& instance = Instance(*found->second);
    if (!render && posedThisRender_.count(propId) != 0) {
        return &instance;
    }
    posedThisRender_.insert(propId);

    if (object != nullptr && scene_.deformed) {
        const Model::NodeVertices* deformed = scene_.deformed(*object);
        if (deformed != nullptr) {
            instance.SetNodeVertices(deformed->node, deformed->vertices.data(), deformed->vertices.size());
        } else {
            instance.SetNodeVertices(0, nullptr, 0);
        }
    }

    const PlayerCamera& camera = *scene_.camera;
    // The switches a broken window hid.
    const std::vector<uint8_t>& hidden = object != nullptr ? object->hiddenSwitches : door->hiddenSwitches;
    for (size_t slot = 0; slot < instance.SwitchCount(); slot++) {
        instance.SetSwitchVisible(slot, std::find(hidden.begin(), hidden.end(), slot) == hidden.end());
    }
    instance.lod.zScale = camera.LodScaleZ();
    Mat4 base{};
    if (object != nullptr) {
        instance.playback.scale = object->scale;
        Mtxf relative = object->mtx;
        if ((object->runtimeFlags & ObjectRuntime::Embedded) != 0 && parent != nullptr) {
            const int index = object->embedNode >= 0 ? parent->MatrixIndexOfNode(object->embedNode)
                                                     : parent->MatrixIndexOfSwitch(object->embedSlot);
            Mtxf nodeMatrix = IdentityMtxf();
            if (index >= 0) {
                std::memcpy(nodeMatrix.m, parent->Walk().matrices[static_cast<size_t>(index)].m, sizeof(nodeMatrix.m));
            }
            const Mtxf combined = Multiply(nodeMatrix, object->embedment);
            std::memcpy(base.m, combined.m, sizeof(base.m));
        } else {
            for (int axis = 0; axis < 3; axis++) {
                relative.m[3][axis] = object->runtimePosition[axis] - camera.ModelPosition()[axis];
            }
            base = PropRender::CombineWithView(relative, camera.ModelviewF());
        }
        const CctvGeometry* swing = object->cctv ? scene_.objects->CctvGeometryOf(*object) : nullptr;
        if (swing != nullptr && instance.Tree().MatrixCount > 1) {
            std::vector<Mat4> matrices(instance.Tree().MatrixCount, base);
            matrices[1] = PropRender::CctvPose(*object, *swing, camera.ModelPosition(), camera.ModelviewF());
            instance.PoseWithMatrices(matrices);
        } else {
            instance.Pose(base);
        }
    } else {
        Mtxf relative = scene_.doors->Transform(*door);
        for (int axis = 0; axis < 3; axis++) {
            relative.m[3][axis] -= camera.ModelPosition()[axis];
        }
        base = PropRender::CombineWithView(relative, camera.ModelviewF());
        Model::ModelDrawOverrides overrides;
        FillDoorMatrices(*model, *door, base, &overrides);
        if (overrides.matrices) {
            instance.PoseWithMatrices(overrides.matrices(instance.Tree()));
        } else {
            instance.Pose(base);
        }
    }
    return &instance;
}

void Shots::TestObject(int propId, ShotData* shot) {
    const Prop& prop = (*scene_.props)[propId];
    const PlacedObject* object = prop.type == PropType::Door ? nullptr : scene_.objects->ForProp(propId);
    const PlacedDoor* door = prop.type == PropType::Door ? scene_.doors->ForProp(propId) : nullptr;
    if (object == nullptr && door == nullptr) {
        return;
    }
    const uint32_t runtime = object != nullptr ? object->runtimeFlags : door->runtimeFlags;
    const uint32_t flags2 = object != nullptr ? object->flags2 : door->flags2;
    if ((prop.flags & PropFlag::OnScreen) == 0 || (runtime & ObjectRuntime::Regenerating) != 0 ||
        (flags2 & ObjectFlag2::ShootThrough) != 0) {
        return;
    }

    const ModelBounds* bounds = nullptr;
    if (object != nullptr) {
        bounds = scene_.objects->BoundsOf(*object);
    } else {
        bounds = &door->modelBounds;
    }
    Model::ModelInstance* instance = PoseProp(propId, nullptr);
    if (bounds == nullptr || instance == nullptr) {
        return;
    }
    NodeRef root;
    root.model = instance;
    root.node = 0;
    const Mtxf* rootMatrix = NodeMatrix(root);
    if (rootMatrix == nullptr) {
        return;
    }
    float box[6];
    ToBounds(*bounds, box);
    const float nearest = -(rootMatrix->m[3][2] + SumMatrix(box, *rootMatrix, 2, false));
    if (nearest <= shot->maxDistance) {
        TestObjectModel(propId, nullptr, shot);
    }
}

void Shots::TestObjectModel(int propId, const Model::ModelInstance* parent, ShotData* shot) {
    PropList& props = *scene_.props;
    const Prop& prop = props[propId];
    PlacedObject* object = prop.type == PropType::Door ? nullptr : scene_.objects->ForProp(propId);
    PlacedDoor* door = prop.type == PropType::Door ? scene_.doors->ForProp(propId) : nullptr;
    if (object == nullptr && door == nullptr) {
        return;
    }
    const uint32_t runtime = object != nullptr ? object->runtimeFlags : door->runtimeFlags;
    if ((runtime & ObjectRuntime::Regenerating) != 0 || (prop.flags & PropFlag::OnScreen) == 0) {
        return;
    }

    Model::ModelInstance* instance = PoseProp(propId, parent);
    if (instance == nullptr) {
        return;
    }

    for (int child = prop.child; child >= 0;) {
        const int next = props[child].prev;
        TestObjectModel(child, instance, shot);
        child = next;
    }

    const PropModel* model =
        object != nullptr ? scene_.objects->Model(object->model) : scene_.doors->Model(door->model);
    int32_t node = kNoNode;
    int hitpart = HitPart::NullPart;
    HitThing hit;
    int matrixIndex = 0;
    int32_t hitNode = kNoNode;
    ModelGeometrySource* geometry = scene_.models != nullptr ? scene_.models->Geometry() : nullptr;
    if (geometry == nullptr) {
        return;
    }

    const bool clipped =
        door != nullptr && ((door->doorFlags & DoorFlag::ClipToBbox) != 0 || door->doorType == DoorMotionType::Eye ||
                            door->doorType == DoorMotionType::Iris);
    if (clipped) {
        NodeRef root;
        root.model = instance;
        root.node = 0;
        const Mtxf* rootMatrix = NodeMatrix(root);
        float box[6];
        ToBounds(door->bbox, box);
        hitpart =
            rootMatrix != nullptr && RayIntersectsTransformedBox(box, *rootMatrix, shot->viewOrigin, shot->viewDir)
                ? 1
                : HitPart::NullPart;
        node = 0;
        if (hitpart > HitPart::NullPart && !FindTriangleHit(*instance, node, shot->viewOrigin, shot->viewDir, *geometry,
                                                            scene_.textures, &hit, &matrixIndex, &hitNode)) {
            hitpart = HitPart::NullPart;
        }
    } else {
        do {
            hitpart = FindProjectileHitCandidate(*instance, shot->viewOrigin, shot->viewDir, &node);
            if (hitpart > HitPart::NullPart &&
                FindTriangleHit(*instance, node, shot->viewOrigin, shot->viewDir, *geometry, scene_.textures, &hit,
                                &matrixIndex, &hitNode)) {
                break;
            }
        } while (hitpart > HitPart::NullPart);
    }

    if (hitpart > HitPart::NullPart) {
        const auto& matrices = instance->Walk().matrices;
        if (matrixIndex < 0 || static_cast<size_t>(matrixIndex) >= matrices.size()) {
            return;
        }
        Mtxf matrix;
        std::memcpy(matrix.m, matrices[static_cast<size_t>(matrixIndex)].m, sizeof(matrix.m));
        const Vec3 position = TransformVector(matrix, hit.hitpos);

        if ((-position[2]) <= shot->maxDistance) {
            bool penetrates = true;
            const uint32_t flags = object != nullptr ? object->flags : door->flags;
            const uint32_t flags2 = object != nullptr ? object->flags2 : door->flags2;
            const std::string skeleton = model != nullptr ? model->skeleton : std::string();
            uint32_t hitOffset = 0;
            if (hitNode != kNoNode) {
                hitOffset = instance->Nodes()[static_cast<size_t>(hitNode)].offset;
            }
            if ((flags & ObjectFlag::Invincible) == 0) {
                if (object != nullptr && (object->type == static_cast<uint8_t>(PropDefType::Glass) ||
                                          object->type == static_cast<uint8_t>(PropDefType::TintedGlass))) {
                    penetrates = false;
                } else if (skeleton == "door") {
                    const ModelNode* glass = instance->SwitchNode(3);
                    if (glass != nullptr && hitOffset == glass->offset) {
                        penetrates = false;
                    }
                }
            }

            ShotHit shotHit;
            shotHit.prop = propId;
            shotHit.dist = -position[2];
            shotHit.hitpart = hitpart;
            shotHit.node.model = instance;
            shotHit.node.node = node;
            shotHit.triangleNode = hitOffset;
            shotHit.hit = hit;
            shotHit.matrix = matrix;
            shotHit.matrixIndex = matrixIndex;
            shotHit.model = instance;
            shotHit.countsAsPenetration = penetrates;
            AddHit(shot, shotHit, (flags2 & ObjectFlag2::Bulletproof) != 0 ? 1 : 0);
        }
    }
}

void Shots::HitObject(const ShotData& shot, const ShotHit& hit) {
    PropList& props = *scene_.props;
    int rootProp = hit.prop;
    while (props[rootProp].parent >= 0) {
        rootProp = props[rootProp].parent;
    }

    PlacedObject* object = props[hit.prop].type == PropType::Door ? nullptr : scene_.objects->ForProp(hit.prop);
    PlacedDoor* door = props[hit.prop].type == PropType::Door ? scene_.doors->ForProp(hit.prop) : nullptr;
    if (object == nullptr && door == nullptr) {
        return;
    }
    const PropModel* model =
        object != nullptr ? scene_.objects->Model(object->model) : scene_.doors->Model(door->model);
    const std::string skeleton = model != nullptr ? model->skeleton : std::string();

    Vec3 position = { shot.viewOrigin[0] - ((hit.dist * shot.viewDir[0]) / shot.viewDir[2]),
                      shot.viewOrigin[1] - ((hit.dist * shot.viewDir[1]) / shot.viewDir[2]),
                      shot.viewOrigin[2] - hit.dist };
    position[0] -= kSparkBackOff * shot.viewDir[0];
    position[1] -= kSparkBackOff * shot.viewDir[1];
    position[2] -= kSparkBackOff * shot.viewDir[2];
    position = TransformVector(scene_.camera->ViewToWorld(), position);

    if (hit.countsAsPenetration) {
        SetTracerTarget(position);
    }

    const int rootStan = props[rootProp].stan;
    scene_.sparks->Create(position, 1, kSparkBackOff, rootStan != kNoTile ? TileRoom(rootStan) : 0, scene_.random);

    if (object != nullptr && Objects::DestroyedLevel(*object) == 0 && Objects::IsMortal(*object) &&
        hit.countsAsPenetration) {
        CountShot(shot.weapon, ShotRegister::Object);
    }

    if (!hit.countsAsPenetration) {
        hitSounds_.GlassHit(props[hit.prop].position);
    } else {
        PropHitSound(shot.weapon, props[hit.prop].position,
                     props[hit.prop].type == PropType::Chr || props[hit.prop].type == PropType::Viewer,
                     hit.hit.texture);
    }

    const Model::ModelInstance* instance = hit.model;
    int triangleSwitch = -1;
    if (instance != nullptr) {
        for (size_t slot = 0; slot < instance->SwitchCount(); slot++) {
            const ModelNode* node = instance->SwitchNode(slot);
            if (node != nullptr && node->offset == hit.triangleNode) {
                triangleSwitch = static_cast<int>(slot);
                break;
            }
        }
    }

    if (shot.weapon != kItemWatchLaser) {
        if (!hit.countsAsPenetration) {
            const int8_t clearFlag = skeleton == "door" ? 1 : 0;
            const int type = static_cast<int>(scene_.random() % 3U) + 0x11;
            CreateImpact(hit.hit.hitpos, hit.hit.normal, type, 1, hit.prop, &hit.matrix, hit.matrixIndex, clearFlag);
        } else {
            const SurfaceImpacts& impacts = SurfaceImpactsFor(ImpactSurface(hit.hit.texture));
            if (impacts.count > 0) {
                const int pick = static_cast<int>(scene_.random() % static_cast<uint32_t>(impacts.count));
                int8_t clearFlag = 0;
                if ((skeleton == "door" && triangleSwitch == 3) || (skeleton == "cctv" && triangleSwitch == 1)) {
                    clearFlag = 1;
                }
                CreateImpact(hit.hit.hitpos, hit.hit.normal, impacts.types[static_cast<size_t>(pick)], 1, hit.prop,
                             &hit.matrix, hit.matrixIndex, clearFlag);
            }
        }
    }

    if (object != nullptr) {
        float damage = scene_.weapons->Table().Item(shot.weapon).destructionAmount;
        if (object->type == static_cast<uint8_t>(PropDefType::Autogun)) {
            // Shots at a drone gun do extra damage on the easier difficulties.
            damage *= scene_.autogunDamageScale();
        } else if (object->type == static_cast<uint8_t>(PropDefType::Cctv)) {
            if (skeleton == "cctv" && triangleSwitch == 1) {
                // A shot at a camera's glass does a hundred times the damage.
                damage *= 100.0f;
                BreakCctvGlass(*object, instance);
            }
            damage *= scene_.cctvDamageScale();
        }
        scene_.damage->ApplyUnlessImmune(*object, damage, position, shot.weapon, 0);
    }

    if (door != nullptr && skeleton == "door" && !hit.countsAsPenetration) {
        door->glassHits++;
        if (door->glassHits >= 3) {
            BreakDoorGlass(*door, instance);
        }
    }

    // What rests on the object falls, and some objects jump when shot.
    if (scene_.projectiles != nullptr) {
        scene_.projectiles->DropRecursively(hit.prop);
    }
    if (object != nullptr && scene_.projectiles != nullptr) {
        bool bounce = false;
        if (Objects::IsCollectable(object->type)) {
            bounce = (object->flags & ObjectFlag::NoBounceIfShot) == 0;
        } else if ((object->flags & ObjectFlag::BounceIfShot) != 0) {
            bounce = true;
        }
        if ((object->flags2 & ObjectFlag2::Lightweight) != 0 && !Objects::IsHealthy(*object)) {
            bounce = true;
        }
        if ((object->flags2 & ObjectFlag2::LinkedToSafe) != 0) {
            bounce = false;
        }
        if (bounce) {
            scene_.projectiles->Bounce(*object, shot.viewDir);
        }
    }
}

void Shots::BreakCctvGlass(PlacedObject& object, const Model::ModelInstance* model) {
    // The glass shatters where it hangs, if the camera is in view.
    if (((*scene_.props)[object.prop].flags & PropFlag::OnScreen) != 0 && model != nullptr) {
        const ModelNode* bounds = model->SwitchNode(2);
        const int index = model->MatrixIndexOfSwitch(1);
        if (bounds != nullptr && bounds->hasBounds && index >= 0) {
            Mtxf nodeMatrix;
            std::memcpy(nodeMatrix.m, model->Walk().matrices[static_cast<size_t>(index)].m, sizeof(nodeMatrix.m));
            const Mtxf world = MultiplyHomogeneous(scene_.camera->ViewToWorld(), nodeMatrix);
            scene_.shards->ShatterWindow(
                { world.m[3][0], world.m[3][1], world.m[3][2] }, { world.m[0][0], world.m[0][1], world.m[0][2] },
                { world.m[1][0], world.m[1][1], world.m[1][2] }, { world.m[2][0], world.m[2][1], world.m[2][2] },
                bounds->bounds[0], bounds->bounds[1], bounds->bounds[2], bounds->bounds[3], scene_.random);
        }
    }

    scene_.impacts->ClearByFlag(object.prop, 1);
    // The glass that is whole is hidden.
    constexpr uint8_t kGlassSwitch = 3;
    if (std::find(object.hiddenSwitches.begin(), object.hiddenSwitches.end(), kGlassSwitch) ==
        object.hiddenSwitches.end()) {
        object.hiddenSwitches.push_back(kGlassSwitch);
    }
}

void Shots::BreakDoorGlass(PlacedDoor& door, const Model::ModelInstance* model) {
    if (model != nullptr) {
        if (const ModelNode* bounds = model->SwitchNode(2); bounds != nullptr && bounds->hasBounds) {
            const Mtxf world = scene_.doors->Transform(door);
            scene_.shards->ShatterWindow(
                { world.m[3][0], world.m[3][1], world.m[3][2] }, { world.m[0][0], world.m[0][1], world.m[0][2] },
                { world.m[1][0], world.m[1][1], world.m[1][2] }, { world.m[2][0], world.m[2][1], world.m[2][2] },
                bounds->bounds[0], bounds->bounds[1], bounds->bounds[2], bounds->bounds[3], scene_.random);
        }
    }

    scene_.impacts->ClearByFlag(door.prop, 1);
    constexpr uint8_t kWindowSwitch = 1;
    if (std::find(door.hiddenSwitches.begin(), door.hiddenSwitches.end(), kWindowSwitch) == door.hiddenSwitches.end()) {
        door.hiddenSwitches.push_back(kWindowSwitch);
    }
}

} // namespace Frigate::Level
