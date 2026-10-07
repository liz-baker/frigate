#include <cmath>
#include <cstdio>

#include "level/PlayerHealth.h"

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-5f;
}

void TestDamage() {
    PlayerHealth health;
    health.Reset();
    health.SetArmour(0.5f);
    HealthFrame frame;

    DamageEffects effects = health.RecordDamage(frame, 0.2f, true);
    Check(Near(health.Armour(), 0.3f) && Near(health.Health(), 1.0f), "armour takes a hit it can absorb");
    Check(effects.push && !effects.killed, "which pushes the player");

    // The hit display blocks the next one until it is over.
    effects = health.RecordDamage(frame, 0.2f, true);
    Check(Near(health.Armour(), 0.3f) && !effects.push, "no hit lands while the last one shows");

    PlayerHealth overflow;
    overflow.Reset();
    overflow.SetArmour(0.1f);
    overflow.RecordDamage(frame, 0.3f, true);
    Check(Near(overflow.Armour(), 0.0f) && Near(overflow.Health(), 0.8f), "what armour cannot absorb reaches health");

    PlayerHealth gas;
    gas.Reset();
    gas.SetArmour(0.5f);
    gas.RecordDamage(frame, 0.125f, false);
    Check(Near(gas.Armour(), 0.5f) && Near(gas.Health(), 0.875f), "gas goes through armour");

    PlayerHealth lethal;
    lethal.Reset();
    effects = lethal.RecordDamage(frame, 1.5f, true);
    Check(effects.killed, "damage past all health kills");

    HealthFrame invincible;
    invincible.invincible = true;
    PlayerHealth safe;
    safe.Reset();
    safe.RecordDamage(invincible, 0.5f, true);
    Check(Near(safe.Health(), 1.0f), "an invincible player takes none");

    HealthFrame handicapped;
    handicapped.handicap = 2.0f;
    PlayerHealth weak;
    weak.Reset();
    weak.RecordDamage(handicapped, 0.1f, true);
    Check(Near(weak.Health(), 0.8f), "the handicap scales the damage");
}

void TestDisplay() {
    PlayerHealth health;
    health.Reset();
    HealthFrame frame;
    health.RecordDamage(frame, 0.1f, true);

    DamageTickEffects first = health.Tick(frame);
    Check(first.begin, "the first frame of a hit starts its display");
    Check(health.DamageShowing(), "which is showing");

    bool ended = false;
    for (int i = 0; i < 400 && !ended; i++) {
        ended = health.Tick(frame).end;
    }
    Check(ended && !health.DamageShowing(), "and ends");
}

void TestKill() {
    PlayerHealth health;
    health.Reset();
    Check(!health.IsDead(), "a life starts alive");
    health.Kill();
    Check(health.IsDead() && health.Dead() == 1, "a kill marks the player just killed");
    health.SetDead(2);
    Check(health.Dead() == 2, "and then dead");
}

void TestMultiplayerHit() {
    HealthFrame frame;
    frame.multiplayer = true;
    PlayerHealth health;
    health.Reset();

    DamageEffects effects = health.RecordDamage(frame, 0.2f, true);
    Check(effects.registered && effects.push, "a hit registers and pushes");
    // The display of the hit has not ticked yet (it is at 0): a second hit the
    // same tick, from another player's shot, still lands, without a push.
    effects = health.RecordDamage(frame, 0.2f, true);
    Check(effects.registered && !effects.push && Near(health.Health(), 0.6f),
          "two or more players take a second hit before the display starts");

    health.Tick(frame);
    effects = health.RecordDamage(frame, 0.2f, true);
    Check(!effects.registered && Near(health.Health(), 0.6f), "but none once it is showing");

    HealthFrame single;
    PlayerHealth alone;
    alone.Reset();
    alone.RecordDamage(single, 0.2f, true);
    effects = alone.RecordDamage(single, 0.2f, true);
    Check(!effects.registered, "a single player takes no second hit before the display starts");
}

void TestLicenceToKill() {
    HealthFrame frame;
    frame.multiplayer = true;
    frame.licenceToKill = true;
    frame.handicap = 200.0f;

    PlayerHealth full;
    full.Reset();
    full.SetArmour(1.0f);
    DamageEffects effects = full.RecordDamage(frame, 0.0001f, true);
    Check(effects.killed && Near(full.Armour(), 0.0f), "any hit takes all health and armour in License to Kill");

    PlayerHealth gas;
    gas.Reset();
    gas.SetArmour(1.0f);
    effects = gas.RecordDamage(frame, 0.0001f, false);
    Check(effects.killed, "gas kills too");

    PlayerHealth tank;
    tank.Reset();
    HealthFrame inTank = frame;
    inTank.inTank = true;
    Check(tank.RecordDamage(inTank, 0.0001f, true).killed, "a tank's armour does not soften it");

    PlayerHealth bars;
    bars.Reset();
    bars.SetActualHealth(4.0f);
    bars.SetActualArmour(2.0f);
    bars.SetArmour(0.5f);
    Check(bars.RecordDamage(frame, 0.0001f, true).killed, "bigger health and armour bars die to it as well");

    frame.licenceToKill = false;
    frame.handicap = 1.0f;
    PlayerHealth normal;
    normal.Reset();
    normal.SetArmour(1.0f);
    Check(!normal.RecordDamage(frame, 0.0001f, true).killed, "the same hit in any other scenario does not");

    HealthFrame invincible;
    invincible.licenceToKill = true;
    invincible.invincible = true;
    PlayerHealth safe;
    safe.Reset();
    Check(!safe.RecordDamage(invincible, 0.0001f, true).killed, "an invincible player still takes none");
}

void TestBeginLife() {
    PlayerHealth health;
    health.Reset();
    health.SetActualHealth(2.0f);
    health.SetActualArmour(10.0f);
    HealthFrame frame;
    health.SetArmour(0.5f);
    health.RecordDamage(frame, 5.0f, true);
    health.Kill();
    health.SetDead(2);
    health.SetRedBloodFinished();
    health.SetDeathAnimFinished();

    health.BeginLife();
    Check(!health.IsDead() && !health.RedBloodFinished() && !health.DeathAnimFinished(), "a new life starts alive");
    Check(Near(health.Health(), 1.0f) && Near(health.Armour(), 0.0f), "with full health and no armour");
    Check(!health.DamageShowing(), "and no hit showing");
    Check(Near(health.ActualHealth(), 2.0f) && Near(health.ActualArmour(), 10.0f), "the cheats' bar sizes carry over");
}

} // namespace

int main() {
    TestDamage();
    TestDisplay();
    TestKill();
    TestMultiplayerHit();
    TestLicenceToKill();
    TestBeginLife();
    return gFailures == 0 ? 0 : 1;
}
