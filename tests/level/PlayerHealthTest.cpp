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

} // namespace

int main() {
    TestDamage();
    TestDisplay();
    TestKill();
    return gFailures == 0 ? 0 : 1;
}
