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
        if (const PlacedObject* object = objects_.ForProp(prop)) {
            *position = object->runtimePosition;
            return true;
        }
        if (const PlacedDoor* door = doors_.ForProp(prop)) {
            *position = door->centre;
            return true;
        }
        return false;
    };
    explosionHooks.objectDamageable = [this](int prop) {
        if (const PlacedObject* object = objects_.ForProp(prop)) {
            return (object->runtimeFlags & ObjectRuntime::Regenerating) == 0 && (object->flags2 & 0x200400) == 0;
        }
        const PlacedDoor* door = doors_.ForProp(prop);
        return door != nullptr && (door->runtimeFlags & ObjectRuntime::Regenerating) == 0 &&
               (door->flags2 & 0x200400) == 0;
    };
    explosionHooks.detonateObject = [this](int prop, float damage, const Vec3& position, int item, int owner) {
        damage_.Detonate(prop, damage, position, item, owner);
    };
    explosionHooks.damageChr = [this](int prop, const Vec3& position, float damage) {
        const int chr = objects_.Props()[prop].object;
        if (aiActions_ && chr >= 0) {
            aiActions_->ExplosionDamage(chrs_[chr], position, damage, true);
        }
    };
    explosionHooks.damagePlayer = [this](int, float damage, float dx, float dz, int) {
        // g_SpExplosionDamageMult.
        damage *= modifiers_.explosionDamage;
        if (player_.InTank()) {
            damage *= 2.0f;
        }
        RecordPlayerDamage(damage, dx, dz, true);
    };
    explosions_.Attach(&props, &level_.StanFile(), &impacts_, std::move(explosionHooks));

    ObjectDamageHooks damageHooks;
    damageHooks.setDropped = [this](int prop) { SetDropped(prop, DropType::Default); };
    damageHooks.doorRuntimeFlags = [this](int prop) -> uint32_t* {
        PlacedDoor* door = doors_.ForProp(prop);
        return door != nullptr ? &door->runtimeFlags : nullptr;
    };
    damageHooks.releaseGas = [this](const Vec3& position) { TriggerGas(position); };
    damageHooks.blackenMonitor = [this](PlacedObject& object) { BlackenMonitor(object); };
    damageHooks.deform = [this](PlacedObject& object, int type) { deform_.Deform(object, type); };
    damageHooks.destroySupported = [this](int prop, int player) { projectiles_.DestroySupported(prop, player); };
    damageHooks.fall = [this](PlacedObject& object, int player) { projectiles_.Fall(object, player); };
    damage_.SetHooks(std::move(damageHooks));
    damage_.SetRandom(RandomGetNext);

    PropEffects effects;
    effects.tick = [this](int prop) { explosions_.SetDepth(prop, FromGu(camera_.WorldToScreen())); };
    effects.render = [this](Gfx* dl, int prop, const Prop& record, bool withAlpha) {
        if (record.type == PropType::Smoke) {
            return effects_.SmokeProp(dl, prop, record, withAlpha);
        }
        if (record.type == PropType::Explosion) {
            return effects_.ExplosionProp(dl, prop, record, withAlpha);
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
            level_.SetPortalDisabled(static_cast<size_t>(object->portal), false);
        }
    };
    effects.setPortal = [this](int portal, bool disabled) {
        level_.SetPortalDisabled(static_cast<size_t>(portal), disabled);
    };
    effects.playerPosition = [this]() { return objects_.Props()[player_.PropId()].position; };
    effects.deformed = [this](const PlacedObject& object) { return deform_.VerticesOf(object); };
    effects.imageSize = [this](int offset) { return effectImages_.Size(offset); };
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
    scene.links = &links_;
    scene.objectives = &objectives_;
    scene.sparks = &sparks_;
    scene.impacts = &impacts_;
    scene.explosions = &explosions_;
    scene.damage = &damage_;
    scene.projectiles = &projectiles_;
    scene.lights = &lightFixtures_;
    scene.shards = &shards_;
    scene.weapons = &weapons_;
    casings_.SetHandFiringQuery([this] {
        return weapons_.Hands(HandRight).actionState == GunState::Fire ||
               weapons_.Hands(HandLeft).actionState == GunState::Fire;
    });
    scene.camera = &camera_;
    scene.textures = &textureHits_;
    scene.models = source_.Models();
    scene.onScreen = &render_.OnScreenProps();
    scene.shotCount = &stats_[0].shotCount;
    scene.random = RandomGetNext;
    scene.playerProp = [this]() { return player_.PropId(); };
    scene.paintball = [this]() { return cheats_.IsActive(Game::Menu::Cheat::kPaintball); };
    scene.shotActor = [this](Chr& chr, int part, const Vec3& direction, int32_t weapon, bool fromPlayer) {
        return aiActions_ && aiActions_->HandleShot(chr, part, direction, weapon, fromPlayer);
    };
    scene.autogunDamageScale = [this]() { return modifiers_.unnamed0x80030B24; };
    scene.cctvDamageScale = [this]() { return modifiers_.unnamed0x80030B18; };
    scene.crouch = [this]() { return player_.CrouchPosition(); };
    scene.duckingHeight = [this]() { return player_.DuckingHeightRelated(); };
    scene.setDropped = [this](int prop) { SetDropped(prop, DropType::Default); };
    scene.fireTankShell = [this](Hand hand) { launcher_.FireTankShell(weapons_, hand, MakeGunFrame(nullptr)); };
    scene.deformed = [this](const PlacedObject& object) { return deform_.VerticesOf(object); };
    shots_.Attach(std::move(scene));

    ProjectileHooks projectileHooks;
    projectileHooks.damageChr = [this](int prop, const Vec3& position, float damage) {
        const int chr = objects_.Props()[prop].object;
        if (aiActions_ && chr >= 0) {
            aiActions_->ExplosionDamage(chrs_[chr], position, damage, true);
        }
    };
    projectileHooks.setMoving = [this](int prop, bool unset) { SetPropCollides(prop, unset); };
    projectileHooks.dropObject = [this](int prop) { DropObject(prop); };
    projectileHooks.stage = [this]() { return currentStage_; };
    projectileHooks.surroundBond = [this](int ticks) {
        // SurroundWithExplosions, then countdownTimerSetVisible(2, FALSE).
        surroundBondWithExplosions_ = true;
        surroundTicks_ = ticks + clock_.globalTimer;
        tickExplodePosition_ = 0;
        countdownHidden_ |= 2;
    };
    projectileHooks.checkDeposit = [this](int weapon, int room) {
        if (setup_) {
            objectives_.CheckDeposit(weapon, room, *setup_, level_.StanFile());
        }
    };
    projectileHooks.player = []() { return 0; };
    projectileHooks.controlsLocked = [this]() { return controlsLocked_; };
    projectileHooks.backgroundGuards = [this](const std::function<void(const Vec3&)>& visit) {
        // check_guard_detonate_proxmine.
        if (!aiActions_) {
            return;
        }
        for (Chr& chr : chrs_.Slots()) {
            if (chr.model && (chr.hidden & ChrHidden::BackgroundAi) != 0) {
                visit(aiActions_->PatrolPercentOrPosition(chr));
            }
        }
    };
    projectiles_.Attach(&shots_, std::move(projectileHooks));
    ObjectDeformHooks deformHooks;
    deformHooks.freeObject = [this](int prop) {
        impacts_.ClearByFlag(prop, 0);
        impacts_.ClearByFlag(prop, 1);
        objects_.Free(prop);
    };
    deform_.Attach(source_.Models(), std::move(deformHooks), RandomGetNext);
    render_.SetProjectiles(&projectiles_);
    launcher_.Attach(&shots_, &projectiles_);
    weapons_.SetRocketHooks([this](Hand hand) { launcher_.CreateRocket(weapons_, hand); },
                            [this](Hand hand) { launcher_.FreeRocket(weapons_, hand); });
}

void Stage::SetPropCollides(int prop, bool collides) {
    // sub_GAME_7F03D058.
    const Prop& record = objects_.Props()[prop];
    switch (record.type) {
        case PropType::Chr:
            if (record.object >= 0) {
                ChrAi::SetMoving(chrs_[record.object], collides);
            }
            break;
        case PropType::Viewer:
            player_.SetCollides(collides);
            break;
        case PropType::Obj:
        case PropType::Weapon:
            // sub_GAME_7F04F218: the object's footprint is left out of the tests
            // while PROPSTATE_20 is set.
            if (PlacedObject* object = objects_.ForProp(prop)) {
                if (collides) {
                    object->state &= static_cast<uint8_t>(~ObjectState::State20);
                } else {
                    object->state |= ObjectState::State20;
                }
            }
            break;
        default:
            break;
    }
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
    projectiles_.Reset();
    deform_.Reset(currentStage_, kPlayerCount);
}

Gfx* Stage::RenderEffects(Gfx* dl) {
    // weaponRenderTracers: the bullet beams of the characters and the autoguns,
    // newest prop first.
    PropList& props = objects_.Props();
    for (int prop = props.ActiveTail(); prop >= 0; prop = props.ActiveBefore(prop)) {
        const Prop& record = props[prop];
        if (record.type == PropType::Chr && record.object >= 0) {
            for (const BeamRecord& beam : chrs_[record.object].beams) {
                dl = effects_.Beam(dl, beam, 1, RandomGetNext);
            }
        } else if (record.type == PropType::Obj && record.object >= 0) {
            if (const PlacedObject* gun = objects_.ForProp(prop); gun != nullptr && gun->autogun) {
                dl = effects_.Beam(dl, gun->autogun->beam, 1, RandomGetNext);
            }
        }
    }
    dl = effects_.Sparks(dl);
    dl = effects_.Shards(dl);
    dl = effects_.Particles(dl);
    if (cheats_.IsActive(Game::Menu::Cheat::kInfiniteAmmo) && weapons_.Ready()) {
        weapons_.SetMaxAmmo();
    }
    return dl;
}

} // namespace Frigate::Level
