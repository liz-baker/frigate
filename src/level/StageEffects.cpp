#include <cstring>

#include "level/Stage.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

// The stages whose debris and glass buffers are half the size.
bool IsHalfBuffer(LevelId stage) {
    return stage == LevelId::Streets || stage == LevelId::Depot;
}

Mtxf FromGu(const Gu::MatrixF& matrix) {
    Mtxf out;
    std::memcpy(out.m, matrix, sizeof(out.m));
    return out;
}

} // namespace

void Stage::AttachEffects() {
    PropList& props = objects_.Props();

    ExplosionHooks explosionHooks;
    explosionHooks.objectPosition = [this](int prop, Vec3* position) {
        const PlacedObject* object = objects_.ForProp(prop);
        if (object == nullptr) {
            return false;
        }
        *position = object->runtimePosition;
        return true;
    };
    explosionHooks.objectDamageable = [this](int prop) {
        const PlacedObject* object = objects_.ForProp(prop);
        return object != nullptr && (object->runtimeFlags & ObjectRuntime::Regenerating) == 0 &&
               (object->flags2 & 0x200400) == 0;
    };
    explosionHooks.detonateObject = [this](int prop, float damage, const Vec3& position, int item, int owner) {
        damage_.Detonate(prop, damage, position, item, owner);
    };
    // TODO(https://github.com/liz-baker/frigate/issues/218): chrlvExplosionDamage, the
    // characters an explosion reaches.
    // TODO(https://github.com/liz-baker/frigate/issues/146): record_damage_kills, the
    // damage an explosion does to the player.
    explosions_.Attach(&props, &level_.StanFile(), &impacts_, std::move(explosionHooks));

    ObjectDamageHooks damageHooks;
    damageHooks.setDropped = [this](int prop) { SetDropped(prop, DropType::Default); };
    damageHooks.releaseGas = [this](const Vec3&) {
        // init_trigger_toxic_gas_effect.
        gasActivate_ = true;
    };
    // TODO(https://github.com/liz-baker/frigate/issues/215): monAnim33BlackSolid, the
    // screen of a destroyed monitor.
    damageHooks.blackenMonitor = [](PlacedObject&) {};
    damage_.SetHooks(std::move(damageHooks));
    damage_.SetRandom(RandomGetNext);

    PropEffects effects;
    effects.tick = [this](int prop) { explosions_.SetDepth(prop, FromGu(camera_.WorldToScreen())); };
    effects.render = [this](Gfx* dl, int prop, const Prop& record, bool withAlpha) {
        if (record.type == PropType::Smoke) {
            return effects_.SmokeProp(dl, prop, record, withAlpha);
        }
        return dl;
    };
    effects.marks = [this](Gfx* dl, int prop, const Mtx::FixedMtx* matrices, size_t count, bool withAlpha,
                           bool* drawn) { return effects_.PropMarks(dl, prop, matrices, count, withAlpha, drawn); };
    effects.freeObject = [this](int prop) {
        impacts_.ClearByFlag(prop, 0);
        impacts_.ClearByFlag(prop, 1);
        if (const PlacedObject* object = objects_.ForProp(prop);
            object != nullptr && object->type == static_cast<uint8_t>(PropDefType::TintedGlass) &&
            object->portal >= 0) {
            level_.SetPortalDisabled(static_cast<size_t>(object->portal), true);
        }
    };
    effects.setPortal = [this](int portal, bool disabled) {
        level_.SetPortalDisabled(static_cast<size_t>(portal), disabled);
    };
    effects.playerPosition = [this]() { return objects_.Props()[player_.PropId()].position; };
    render_.SetPropEffects(std::move(effects));
    render_.SetRoomMarks([this](Gfx* dl) { return effects_.RoomMarks(dl); });

    ShotScene scene;
    scene.stan = &level_.StanFile();
    scene.level = &level_;
    scene.visibility = &visibility_;
    scene.bgHit = &bgHit_;
    scene.props = &props;
    scene.objects = &objects_;
    scene.doors = &doors_;
    scene.chrs = &chrs_;
    scene.sparks = &sparks_;
    scene.impacts = &impacts_;
    scene.explosions = &explosions_;
    scene.damage = &damage_;
    scene.lights = &lightFixtures_;
    scene.shards = &shards_;
    scene.weapons = &weapons_;
    scene.camera = &camera_;
    scene.textures = &textureHits_;
    scene.models = source_.Models();
    scene.onScreen = &render_.OnScreenProps();
    scene.shotCount = &stats_[0].shotCount;
    scene.random = RandomGetNext;
    scene.playerProp = [this]() { return player_.PropId(); };
    scene.shotActor = [this](Chr& chr, int part, const Vec3& direction, int32_t weapon, bool fromPlayer) {
        return aiActions_ && aiActions_->HandleShot(chr, part, direction, weapon, fromPlayer);
    };
    scene.autogunDamageScale = [this]() { return modifiers_.unnamed0x80030B24; };
    scene.cctvDamageScale = [this]() { return modifiers_.unnamed0x80030B18; };
    scene.crouch = [this]() { return player_.CrouchPosition(); };
    scene.duckingHeight = [this]() { return player_.DuckingHeightRelated(); };
    scene.setDropped = [this](int prop) { SetDropped(prop, DropType::Default); };
    shots_.Attach(std::move(scene));
}

void Stage::ResetEffects() {
    impacts_.Reset();
    impacts_.SetImages(&effectImages_);
    // alloc_shattered_window_pieces and the debris buffer: 200 split between the
    // players, halved on the Streets and the Depot.
    int capacity = 200 / kPlayerCount;
    if (IsHalfBuffer(currentStage_)) {
        capacity >>= 1;
    }
    explosions_.Reset(capacity);
    explosions_.SetRoomInfo([this](int room) { return level_.RoomEntry(room).position; }, level_.Level().scale);
    shards_.Allocate(capacity);
    shots_.Reset();
}

Gfx* Stage::RenderEffects(Gfx* dl) {
    // weaponRenderTracers: the bullet beams of the characters, newest prop first.
    // TODO(https://github.com/liz-baker/frigate/issues/187): a drone gun's beam.
    PropList& props = objects_.Props();
    for (int prop = props.ActiveTail(); prop >= 0; prop = props.ActiveBefore(prop)) {
        const Prop& record = props[prop];
        if (record.type == PropType::Chr && record.object >= 0) {
            for (const BeamRecord& beam : chrs_[record.object].beams) {
                dl = effects_.Beam(dl, beam, 1, RandomGetNext);
            }
        }
    }
    dl = effects_.Sparks(dl);
    dl = effects_.Shards(dl);
    dl = effects_.Particles(dl);
    return dl;
}

} // namespace Frigate::Level
