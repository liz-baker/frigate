#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "level/BgHit.h"
#include "level/BgLevel.h"
#include "level/BgVisibility.h"
#include "level/BulletImpacts.h"
#include "level/BulletSparks.h"
#include "level/Chrs.h"
#include "level/Doors.h"
#include "level/Explosions.h"
#include "level/GlassShards.h"
#include "level/LightFixtures.h"
#include "level/ModelHit.h"
#include "level/ObjectDamage.h"
#include "level/Objectives.h"
#include "level/Objects.h"
#include "level/PlayerCamera.h"
#include "level/PlayerWeapons.h"
#include "level/PropLinks.h"

namespace Frigate::Level {

// BulletHit: one thing a shot met.
struct ShotHit {
    int prop = -1;
    // How far along the view axis it was met.
    float dist = 0.0f;
    int hitpart = 0;
    // The node the part is in (a character's model, or a held weapon's or
    // hat's), and for an object the offset of the node the triangle was in.
    NodeRef node;
    // The offset in its model of the node the triangle was in.
    uint32_t triangleNode = 0;
    HitThing hit;
    // The matrix, under the view, of the node the triangle was in, and its
    // index in its model.
    Mtxf matrix = IdentityMtxf();
    int matrixIndex = 0;
    const Model::ModelInstance* model = nullptr;
    bool countsAsPenetration = false;
};

// ShotData.
struct ShotData {
    static constexpr int kMaxHits = 10;
    // Where the shot starts and which way it goes, in view space and in the
    // world (gunpos and dir).
    Vec3 viewOrigin{};
    Vec3 viewDir{};
    Vec3 gunPosition{};
    Vec3 dir{};
    int32_t weapon = 0;
    float maxDistance = 0.0f;
    std::array<ShotHit, kMaxHits> hits;
};

// What a shot reads off the stage and changes in it.
struct ShotScene {
    Stan* stan = nullptr;
    BgLevel* level = nullptr;
    BgVisibility* visibility = nullptr;
    BgHit* bgHit = nullptr;
    PropList* props = nullptr;
    Objects* objects = nullptr;
    Doors* doors = nullptr;
    Chrs* chrs = nullptr;
    const PropLinks* links = nullptr;
    Objectives* objectives = nullptr;
    BulletSparks* sparks = nullptr;
    BulletImpacts* impacts = nullptr;
    Explosions* explosions = nullptr;
    ObjectDamage* damage = nullptr;
    LightFixtures* lights = nullptr;
    GlassShards* shards = nullptr;
    PlayerWeapons* weapons = nullptr;
    const PlayerCamera* camera = nullptr;
    const TextureHits* textures = nullptr;
    ModelFactory* models = nullptr;
    // The on-screen props, the farthest first.
    const std::vector<int>* onScreen = nullptr;
    // The shot counts the end-of-mission statistics show.
    std::array<int, 7>* shotCount = nullptr;
    std::function<uint32_t()> random;
    // The player's prop.
    std::function<int()> playerProp;
    // handles_shot_actors: false when the character does not take the shot.
    std::function<bool(Chr& chr, int hitpart, const Vec3& direction, int32_t weapon, bool fromPlayer)> shotActor;
    // F_80030B24 and F_80030B18.
    std::function<float()> autogunDamageScale;
    std::function<float()> cctvDamageScale;
    // The player's crouch (currentPlayerGetCrouchPos): 0 standing, 1 half,
    // 2 squatting.
    std::function<int()> crouch;
    // bondviewGetPlayerDuckingHeightRelated.
    std::function<float()> duckingHeight;
    // propobjSetDropped(prop, DROPTYPE_DEFAULT).
    std::function<void(int prop)> setDropped;
};

// What projectileFindCollidingProp found: the prop met and, for a model, where.
struct ProjectileContact {
    int prop = -1;
    // bodypartshot: the part of a character hit, HitPart::NullPart for none.
    int hitpart = -1;
    // g_CurrentProjectileModel and dword_CODE_bss_80075B74: the model and the
    // node of it that was hit, null and kNoNode for none.
    const Model::ModelInstance* model = nullptr;
    int32_t node = kNoNode;
    // flt_CODE_bss_80075B78 and flt_CODE_bss_80075B88: the direction the segment
    // was tested along, in the world and in view space.
    Vec3 worldDirection{};
    Vec3 viewDirection{};
};

// The shots the player fires: chraiCheckUseHeldItems and the hit tests of
// chrprop.c, chr.c and propobj.c it runs.
class Shots {
  public:
    void Attach(ShotScene scene) {
        scene_ = std::move(scene);
        posed_.clear();
    }
    // The models made for the props are dropped.
    void Reset() {
        posed_.clear();
    }

    const ShotScene& Scene() const {
        return scene_;
    }

    // projectileFindCollidingProp: the nearest hit among the props in `rooms` (a
    // -1-terminated list) of the types `cdTypes` selects, along the segment
    // from `start` to `end`, ignoring `ignoreProp`. `hitPos` and `hitNormal`
    // are updated, in the world, and `contact` says what was met. A hit
    // only counts when it is nearer than `*bestDistance`... it starts the
    // segment's length, as in the decomp.
    bool FindCollidingProp(int ignoreProp, const Vec3& start, const Vec3& end, int cdTypes, const int* rooms,
                           Vec3* hitPos, Vec3* hitNormal, ProjectileContact* contact);
    // The model of an object, posed under the view this frame (with the models
    // it is embedded in): its render_pos. Null when it has none.
    const Model::ModelInstance* PosedObject(int prop) {
        return PoseChain(prop);
    }
    // chrCreateBloodStain.
    void BloodStain(Model::ModelInstance& model, int hitpart, const NodeRef& root, const Vec3& position) {
        CreateBloodStain(model, hitpart, root, position);
    }
    // bullet_path_from_screen_center: the origin and direction, in view space, of
    // a shot from `hand`.
    void BulletPath(Hand hand, Vec3* origin, Vec3* dir);
    // recall_joy2_hits_edit_detail_edit_flag's draws.
    void DrawHitSounds();

    // chraiCheckUseHeldItems.
    void CheckUseHeldItems();
    // inc_curplayer_hitcount_with_weapon.
    void CountShot(int32_t item, int reg);

  private:
    // chraiCheckUseHeldItem.
    void CheckUseHeldItem(Hand hand);
    // chraiDefaultWeaponFireHandler.
    void FireDefault(Hand hand);
    // chraiFistAttackHandler.
    void FireFist(Hand hand, int32_t item);
    // objectiveTakePictureHandler.
    void TakePicture();
    // chrCreateBloodStain: paints the vertices of the collision list nearest
    // `position` (in the space of the node `root`) on the character's `model`.
    void CreateBloodStain(Model::ModelInstance& model, int hitpart, const NodeRef& root, const Vec3& position);

    // gunSetTracerTarget.
    void SetTracerTarget(const Vec3& target);

    // chrpropRayIntersectsRoomBbox.
    bool RayMeetsRoom(int room, const Vec3& start, const Vec3& dir) const;
    // chrpropFindFirstBgHitInConnectedRooms.
    int FindFirstBgHit(int startRoom, const Vec3& from, const Vec3& to, const Vec3& dir, const Vec3& scaledDir,
                       std::array<uint8_t, 256>& visited, HitThing* hit);
    // chrpropFindClosestBgHitRoom.
    int FindClosestBgHit(const Vec3& from, const Vec3& to, const Vec3& dir, const Vec3& scaledDir,
                         std::array<uint8_t, 256>& visited, HitThing* hit);
    // chrpropFindCloserBgHitInVisibleRooms.
    int FindCloserBgHit(const Vec3& from, const Vec3& to, const Vec3& dir, const Vec3& scaledDir,
                        std::array<uint8_t, 256>& visited, HitThing* best, int bestRoom);
    // bgTestBulletHitBackground for a room that is loaded.
    bool TestBackground(const Vec3& from, const Vec3& to, int room, HitThing* hit) const;

    // chrpropAddBulletHit.
    void AddHit(ShotData* shot, const ShotHit& hit, int blocksFurtherHits);

    // chrTestHit and chrHandleBulletHit.
    void TestChr(int prop, ShotData* shot);
    void HandleChrHit(const ShotData& shot, const ShotHit& hit);
    // chrCreateHitPuffs.
    void CreateHitPuffs(int prop, int hitpart, const Vec3& viewPosition, const Vec3& position);

    // sub_GAME_7F04E9BC, sub_GAME_7F04E720 and objHit.
    void TestObject(int prop, ShotData* shot);
    void TestObjectModel(int prop, const Model::ModelInstance* parent, ShotData* shot);
    void HitObject(const ShotData& shot, const ShotHit& hit);

    Model::ModelInstance* PoseChain(int prop);
    // The model of an object or door prop, posed under the view this frame;
    // null when it has none. `parent` is the model of the prop it is embedded in.
    Model::ModelInstance* PoseProp(int prop, const Model::ModelInstance* parent);

    // The surface type the impact mark of a texture is chosen by (0 for none).
    int ImpactSurface(int texture) const;
    // lightFixtureBreak for the triangle a shot hit in `room`.
    void BreakLight(const HitThing& hit, int room);
    // objBreakCCTVGlass and sub_GAME_7F04DD68: the glass of a camera or of a
    // door breaks.
    void BreakCctvGlass(PlacedObject& object, const Model::ModelInstance* model);
    void BreakDoorGlass(PlacedDoor& door, const Model::ModelInstance* model);
    // The room of a tile.
    int TileRoom(int tile) const;
    // projectileTestObjectCollisionRecursive and what it calls.
    bool TestProjectileObject(int prop, const Model::ModelInstance* parent, const Vec3& worldStart,
                              const Vec3& worldEnd, const Vec3& worldDirection, float maxDistance,
                              const Vec3& viewStart, const Vec3& viewDirection, Vec3* bestPos, Vec3* bestNormal,
                              float* bestDistance, ProjectileContact* contact);
    // projectileLineTestModel.
    bool LineTestModel(int prop, const Model::ModelInstance* parent, const Vec3& viewStart,
                       const Vec3& viewDirection, Vec3* hitPos, Vec3* hitNormal, const Model::ModelInstance** model,
                       int32_t* node);
    // sub_GAME_7F041400: the segment against the prop's footprint.
    bool LineTestFootprint(int prop, const Vec3& start, const Vec3& end, const Vec3& direction, Vec3* hitPos,
                           Vec3* hitNormal, float* hitDistance);
    // sub_GAME_7F041BB8: the segment against a character's model.
    bool LineTestChr(int prop, const Vec3& worldStart, const Vec3& worldDirection, float maxDistance,
                     const Vec3& viewStart, const Vec3& viewDirection, Vec3* hitPos, Vec3* hitNormal,
                     float* hitDistance, ProjectileContact* contact);
    // The impact mark of a hit on a room or a prop.
    void CreateImpact(const Vec3& position, const Vec3& normal, int type, int room, int prop, const Mtxf* matrix,
                      int matrixIndex, int8_t clearFlag);
    float RandomFraction();

    ShotScene scene_;
    // The models created for the props, by prop.
    std::unordered_map<int, std::shared_ptr<ModelHandle>> posed_;
};

} // namespace Frigate::Level
