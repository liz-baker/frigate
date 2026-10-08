#include <cmath>
#include <cstdio>

#include "level/Beams.h"
#include "level/BulletImpacts.h"
#include "level/BulletSparks.h"
#include "level/Explosions.h"
#include "level/GlassShards.h"
#include "level/ObjectDamage.h"
#include "level/Objects.h"
#include "level/PropList.h"
#include "level/WeaponTable.h"

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// A generator that gives the same number every time.
std::function<uint32_t()> Constant(uint32_t value) {
    return [value]() { return value; };
}

void TestBeamLengthAndCaps() {
    BeamRecord beam;
    StartBeam(&beam, kItemPp7, { 0, 0, 0 }, { 0, 0, 20000 }, Constant(0x80000000u));
    Check(beam.length == 10000.0f, "a bullet's beam is capped at 10000");
    Check(std::fabs(beam.delta[2] - 1.0f) < 1e-6f, "the beam's direction is a unit vector");
    Check(beam.streak == 2000.0f && beam.drawLength == 2000.0f, "a long beam's streak is a fifth of its capped length");
    Check(beam.state == 0, "a beam that shows starts alive");

    BeamRecord watch;
    StartBeam(&watch, kItemWatchLaser, { 0, 0, 0 }, { 0, 100, 0 }, Constant(0));
    Check(watch.tail == 0.0f && watch.state == 0, "the watch laser's beam always shows");
    Check(watch.length == 100.0f, "the watch laser reaches as far as the shot");

    BeamRecord far;
    StartBeam(&far, kItemWatchLaser, { 0, 0, 0 }, { 0, 1000, 0 }, Constant(0));
    Check(far.length == 300.0f, "the watch laser's beam is short");
}

void TestBeamMayNotShow() {
    BeamRecord beam;
    // The highest draw puts the streak's tail at its length: nothing to show.
    StartBeam(&beam, kItemPp7, { 0, 0, 0 }, { 0, 0, 100 }, Constant(0xFFFFFFFFu));
    Check(beam.state < 0, "a beam whose tail is past its end is not shown");
    StartBeam(&beam, kItemPp7, { 0, 0, 0 }, { 0, 0, 100 }, Constant(0));
    Check(beam.state == 0, "the lowest draw starts the tail behind the gun");
}

void TestBeamAdvance() {
    BeamRecord beam;
    StartBeam(&beam, kItemPp7, { 0, 0, 0 }, { 0, 0, 1000 }, Constant(0x80000000u));
    const float tail = beam.tail;
    AdvanceBeam(&beam, 1, 1.0f, Constant(0));
    Check(beam.tail == tail + beam.streak, "a short tick moves the tail by the streak");
    Check(beam.state == 1, "an advancing beam ages");
    for (int i = 0; i < 20 && beam.state >= 0; i++) {
        AdvanceBeam(&beam, 1, 1.0f, Constant(0));
    }
    Check(beam.state < 0, "a beam ends once its tail passes its end");
}

void TestSparks() {
    BulletSparks sparks;
    sparks.Reset();
    Check(sparks.Create({ 1, 2, 3 }, 1, 26.0f, 5, Constant(0)), "a spark is created");
    const BulletSpark& spark = sparks.Slots()[0];
    Check(spark.lifetime == 11 && spark.frameRate == 0.5f && spark.images == SparkImages::Smoke,
          "a smoke spark lives eleven ticks through the smoke frames");
    Check(spark.room == 5 && spark.position[1] == 2.0f, "a spark is in its room at its position");

    sparks.Update(5);
    Check(sparks.Slots()[0].age == 5 && sparks.Slots()[0].lifetime == 11, "a spark ages by the clock");
    sparks.Update(6);
    Check(sparks.Slots()[0].lifetime == 0, "a spark ends when its age reaches its lifetime");

    for (size_t i = 0; i < BulletSparks::kMax; i++) {
        sparks.Create({ 0, 0, 0 }, 4, 1.0f, 1, Constant(0));
    }
    Check(!sparks.Create({ 0, 0, 0 }, 4, 1.0f, 1, Constant(0)), "no spark is created when all are in use");
}

void TestGlassShards() {
    GlassShards shards;
    shards.Allocate(4);
    shards.Create({ 10, 20, 30 }, 0.0f, 10.0f, Constant(0x80000000u));
    Check(shards.Shards()[0].active == 1, "a shard starts flying");
    Check(shards.Shards()[0].corners[0].position[0] >= 10, "a shard's corner is at least the size out");

    shards.Update(1);
    Check(shards.Shards()[0].active == 2, "a shard has flown a tick");
    const float y = shards.Shards()[0].position[1];
    shards.Update(1);
    Check(shards.Shards()[0].position[1] != y, "a shard moves");

    shards.Update(15);
    for (int i = 0; i < 10; i++) {
        shards.Update(15);
    }
    Check(shards.Shards()[0].active == 0, "a shard is gone after 150 ticks");

    shards.Create({ 0, 0, 0 }, 0.0f, 1.0f, Constant(0));
    shards.Clear();
    Check(shards.Shards()[0].active == 0, "clearing ends every shard");
}

void TestImpactOnRoom() {
    BulletImpacts impacts;
    impacts.Reset();
    EffectImages images;
    Check(images.Parse("0xad4\t48\t48\n0xae0\t64\t32\n"), "the effect images parse");
    Check(images.Size(0xAD4).width == 48 && images.Size(0xAE0).height == 32, "an image's size is by its offset");
    impacts.SetImages(&images);

    ImpactPlacement placement;
    placement.position = { 100, 50, 25 };
    placement.normal = { 0, 0, 1 };
    placement.type = 0;
    placement.room = 3;
    placement.levelScale = 0.1f;
    placement.roomPosition = { 5, 5, 5 };
    const uint8_t state = impacts.Create(placement, Constant(0));
    Check(state == 0, "a mark on a room sets no object state");
    const BulletImpact& impact = impacts.Impacts()[0];
    Check(impact.room == 3, "the mark is in its room");
    Check(impact.type == 0, "the mark has its type");
    // Placed at the position in room units, less the room's: (100 * 0.1) - 5.
    int sum = 0;
    for (const EffectVertex& vertex : impact.vertices) {
        sum += vertex.position[0];
    }
    Check(sum == 4 * 5, "a mark is centred on its position in the room's units");

    impacts.ClearByFlag(-1, 0);
}

void TestImpactOnProp() {
    BulletImpacts impacts;
    impacts.Reset();
    Mtxf matrix = IdentityMtxf();
    ImpactPlacement placement;
    placement.position = { 0, 0, 0 };
    placement.normal = { 0, 0, 1 };
    placement.type = 0x11;
    placement.room = 1;
    placement.prop = 7;
    placement.propMatrix = &matrix;
    placement.matrixIndex = 2;
    placement.clearFlag = 1;
    const uint8_t state = impacts.Create(placement, Constant(0));
    Check(state == kStateMarkBlended || state == kStateMarkOpaque, "a mark on a prop is marked on the prop's state");
    Check(impacts.Impacts()[0].prop == 7 && impacts.Impacts()[0].matrixIndex == 2,
          "a mark remembers its prop and matrix");

    impacts.ClearByFlag(7, 1);
    Check(impacts.Impacts()[0].room < 0, "a mark with the clear flag goes with it");
}

void TestObjectDamage() {
    Objects objects;
    Stan stan;
    BulletImpacts impacts;
    GlassShards shards;
    Explosions explosions;
    ObjectDamage damage(objects, stan, impacts, shards, explosions);
    damage.SetRandom(Constant(0x40000000u));
    shards.Allocate(8);

    PlacedObject glass;
    glass.type = static_cast<uint8_t>(PropDefType::Glass);
    glass.damage = 1000.0f;
    glass.prop = 0;
    damage.Apply(glass, 5.0f, { 0, 0, 0 }, kItemPp7, 0);
    Check(glass.maxDamage == 0.0f && (glass.state & ObjectState::Destroyed) != 0,
          "glass that takes its health in damage shatters and is destroyed");
    Check((glass.runtimeFlags & ObjectRuntime::Remove) != 0, "shattered glass is removed");

    PlacedObject invincible;
    invincible.type = static_cast<uint8_t>(PropDefType::Prop);
    invincible.flags = ObjectFlag::Invincible;
    invincible.damage = 1000.0f;
    damage.Apply(invincible, 1.0f, { 0, 0, 0 }, kItemPp7, 0);
    Check(invincible.maxDamage == 0.0f, "an invincible object takes no damage");

    PlacedObject unarmed;
    unarmed.type = static_cast<uint8_t>(PropDefType::Prop);
    unarmed.flags = ObjectFlag::Invincible;
    unarmed.damage = 1000.0f;
    damage.Apply(unarmed, 0.0f, { 0, 0, 0 }, kItemUnarmed, 2);
    Check(((unarmed.runtimeFlags & ObjectRuntime::OwnerMask) >> ObjectRuntime::OwnerShift) == 2,
          "the damage is credited to the player");

    PlacedObject immune;
    immune.type = static_cast<uint8_t>(PropDefType::Prop);
    immune.flags2 = ObjectFlag2::ImmuneToGunfire;
    immune.damage = 1000.0f;
    damage.ApplyUnlessImmune(immune, 1.0f, { 0, 0, 0 }, kItemPp7, 0);
    Check(immune.maxDamage == 0.0f, "an object immune to gunfire takes none");

    PlacedObject armour;
    armour.type = static_cast<uint8_t>(PropDefType::Armour);
    armour.damage = 1000.0f;
    armour.armourAmount = 1.0f;
    armour.armourInitial = 1.0f;
    damage.Apply(armour, 1.0f, { 0, 0, 0 }, kItemPp7, 0);
    Check(std::fabs(armour.armourAmount - 0.75f) < 1e-6f, "shot armour loses the share of its health it has taken");
    damage.Apply(armour, 1.0f, { 0, 0, 0 }, kItemPp7, 0);
    Check(std::fabs(armour.armourAmount - 0.5f) < 1e-6f, "shot armour's loss is taken from its initial amount");
}

void TestVideoShake() {
    VideoShake shake;
    shake.Shake(20.0f);
    Check(shake.Intensity() == 14, "a shake is at most 14 lines");
    Check(shake.Advance(1) == 14 && shake.Advance(1) == -14, "the picture swaps direction each frame");
    shake.Shake(-3.0f);
    Check(shake.Intensity() == 0, "a negative shake is none");
    shake.Shake(5.0f);
    shake.Advance(9);
    Check(shake.Intensity() == 5, "a shake lasts ten retraces");
    shake.Advance(1);
    Check(shake.Intensity() == 0 && shake.Advance(1) == 0, "a shake ends after ten retraces");
}

void TestExplosionScreenShake() {
    Explosions explosions;
    const Vec3 origin = { 0, 0, 0 };
    const Vec3 look = { 0, 0, -1 };
    explosions.ScreenShake(origin, look);
    Check(explosions.Video().Intensity() == 0, "nothing shakes without an explosion");

    // A launched shell: six frames, each shaking by the entries left times the
    // smoke's one, and only on the frames whose entry count lacks bit 1.
    explosions.SetSixEntries();
    const int32_t expected[6] = { 5, 4, 0, 0, 1, 0 };
    bool matches = true;
    for (const int32_t intensity : expected) {
        explosions.ScreenShake(origin, look);
        matches = matches && explosions.Video().Intensity() == intensity;
    }
    Check(matches, "a launched shell shakes the picture for six frames");
}

// What a scorch needs: one image to size it by.
void TestScorchesAreSinglePlayer() {
    EffectImages images;
    images.Parse("0xad4\t48\t48\n");
    const auto scorches = [&images](int players) {
        BulletImpacts impacts;
        impacts.Reset();
        impacts.SetPlayerCount(players);
        impacts.SetImages(&images);
        int draws = 0;
        impacts.CreateScorch({ 10, 0, 10 }, 100.0f, 3, { 0, 0, 0 }, 1.0f, [&draws] {
            draws++;
            return 0x40000000u;
        });
        int made = 0;
        for (const BulletImpacts::Scorch& scorch : impacts.Scorches()) {
            made += scorch.room >= 0 ? 1 : 0;
        }
        return std::make_pair(made, draws);
    };
    Check(scorches(1).first == 1, "a one player game leaves a scorch");
    for (int players = 2; players <= 4; players++) {
        Check(scorches(players).first == 0, "a game of two or more players leaves none");
        Check(scorches(players).second == 2, "though the first two random draws are still taken");
    }
    BulletImpacts impacts;
    impacts.SetPlayerCount(3);
    Check(!impacts.ScorchesEnabled(), "and the scorch buffer draws nothing");
}

// A smoke cloud ends early when another is started in a game of two or more.
void TestSmokeIsCutShortInMultiplayer() {
    for (int players = 1; players <= 4; players++) {
        PropList props;
        props.Reset(1, nullptr, nullptr);
        BulletImpacts impacts;
        Explosions explosions;
        explosions.Attach(&props, nullptr, &impacts, {});
        explosions.Reset(8, players);

        const std::array<uint8_t, 8> rooms = { 0, kNoRoom };
        // A new prop joins the front of the active list.
        explosions.CreateSmoke({ 0, 0, 0 }, kNoTile, 2, rooms, false);
        const int first = props.Active().front();
        explosions.CreateSmoke({ 0, 0, 0 }, kNoTile, 2, rooms, false);
        const int second = props.Active().front();
        const Explosions::Smoke* older = explosions.SmokeOf(first);
        const Explosions::Smoke* newer = explosions.SmokeOf(second);
        Check(older != nullptr && newer != nullptr && first != second, "two smoke clouds are made");
        if (older == nullptr || newer == nullptr) {
            continue;
        }
        Check(newer->duration == 0, "a new cloud starts at its beginning");
        Check(older->duration == (players == 1 ? 0 : SmokeTypeOf(2).duration),
              players == 1 ? "a one player game leaves the old cloud alone"
                           : "starting a cloud runs the old one to its end");

        // The quick puffs of a bullet are not cut short.
        explosions.CreateSmoke({ 0, 0, 0 }, kNoTile, 7, rooms, false);
        const int puff = props.Active().front();
        explosions.CreateSmoke({ 0, 0, 0 }, kNoTile, 2, rooms, false);
        Check(explosions.SmokeOf(puff)->duration == 0, "puffs of smoke types 7 to 9 are left alone");
    }
}

} // namespace

int main() {
    TestScorchesAreSinglePlayer();
    TestSmokeIsCutShortInMultiplayer();
    TestVideoShake();
    TestExplosionScreenShake();
    TestBeamLengthAndCaps();
    TestBeamMayNotShow();
    TestBeamAdvance();
    TestSparks();
    TestGlassShards();
    TestImpactOnRoom();
    TestImpactOnProp();
    TestObjectDamage();
    if (gFailures == 0) {
        std::printf("all shot effect tests passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
