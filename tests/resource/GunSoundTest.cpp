#include "AudioBank.h"
#include "AudioEngine.h"
#include "BankBuilder.h"
#include "game/Sound.h"
#include "level/Casings.h"
#include "level/Explosions.h"
#include "level/HitSounds.h"
#include "level/PlayerWeapons.h"
#include "level/TextureHits.h"
#include "level/WeaponTable.h"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Checks the weapon, hit, casing and explosion sounds against an engine whose
// bank has a looped sound for every sound number, each carrying its number in
// its pan and volume, so which sound played reads back from the state.

using namespace Frigate::Audio;
using namespace Frigate::AudioDriver;
using namespace Frigate::AudioTest;
using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

constexpr int kSoundCount = 300;

// An engine with sound `n` (from 1) a looped tone that reports `n`.
struct Rig {
    AudioBank bank;
    std::unique_ptr<AudioEngine> engine;
    std::vector<int16_t> frame = std::vector<int16_t>(kFrameSamples * 2);

    Rig() {
        std::vector<SoundSpec> specs(kSoundCount);
        for (int i = 0; i < kSoundCount; i++) {
            const int number = i + 1;
            specs[static_cast<size_t>(i)].keyMin = 0;
            specs[static_cast<size_t>(i)].keyMax = 127;
            specs[static_cast<size_t>(i)].velocityMin = 0;
            specs[static_cast<size_t>(i)].velocityMax = 127;
            specs[static_cast<size_t>(i)].looped = true;
            specs[static_cast<size_t>(i)].samplePan = static_cast<uint8_t>(number >> 7);
            specs[static_cast<size_t>(i)].sampleVolume = static_cast<uint8_t>(number & 0x7F);
        }
        const std::vector<uint8_t> payload = BuildToneBank(specs);
        Check(ParseAudioBankPayload(payload.data(), payload.size(), &bank), "the tone bank parses");
        engine = std::make_unique<AudioEngine>(bank, EngineConfig(), nullptr);
        Frigate::Game::Sound::Install(engine.get());
    }
    ~Rig() {
        Frigate::Game::Sound::Uninstall();
    }

    void Render(int frames = 2) {
        for (int i = 0; i < frames; i++) {
            Frigate::Game::Sound::RenderFrame(frame.data());
        }
    }
    int Active() const {
        return engine->Sfx().ActiveStates();
    }
};

// The sound number a state plays.
int NumberOf(const SoundState* state) {
    return state != nullptr && state->sound != nullptr ? (state->sound->samplePan << 7) + state->sound->sampleVolume
                                                       : -1;
}

// One `item` line of the weapon table.
std::string Item(int index, int ammoType, int magazine, uint32_t flags, int autoRate, int soundRate, int sound) {
    char flagText[16];
    std::snprintf(flagText, sizeof flagText, "0x%x", flags);
    return "item\t" + std::to_string(index) + "\tITEM\t1\t" + std::to_string(ammoType) + "\t" +
           std::to_string(magazine) + "\t" + flagText +
           "\t-\t-\t1.0\t11.0\t-20.0\t-33.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t" + std::to_string(autoRate) +
           "\t0\t3\t5\t0\t-1\t2.0\t4.0\t-\t0.0\t10.0\t4.0\t2.0\t4.0\t0.0\t0.0\t0\t1.0\t" + std::to_string(soundRate) +
           "\t" + std::to_string(sound) + "\n";
}

constexpr uint32_t kShown = 0x800;
constexpr uint32_t kHasAmmo = 0x40000;
constexpr uint32_t kClicky = 0x10;

// Item 4 is a pistol whose shot is sound 100, 8 an automatic whose shot is sound
// 101 and sounds every sixth tick.
std::string Table() {
    std::string text;
    for (int i = 0; i < 12; i++) {
        switch (i) {
            case 4:
                text += Item(i, 1, 7, kShown | kHasAmmo | kClicky, -1, 0, 100);
                break;
            case 8:
                text += Item(i, 2, 30, kShown | kHasAmmo, 3, 6, 101);
                break;
            default:
                text += Item(i, 0, 0, i == 0 ? 0x110 : kShown, -1, 0, 0);
                break;
        }
    }
    text += "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0\n";
    text += "ammo\t1\tAMMO_9MM\t800\t0x0\t0.0\n";
    text += "ammo\t2\tAMMO_RIFLE\t400\t0x0\t0.0\n";
    return text;
}

class FakeHooks : public GunHooks {
  public:
    int32_t GlobalTimer() const override {
        return timer;
    }
    bool RaiseSoundsAllowed() const override {
        return raiseSounds;
    }
    int32_t timer = 1;
    bool raiseSounds = true;
};

struct Scene {
    Inventory inventory;
    PlayerWeapons weapons;
    FakeHooks hooks;
    Rig& rig;

    explicit Scene(Rig& r) : rig(r) {
        for (const int item : { 1, 2, 4, 8 }) {
            inventory.AddItem(item);
        }
        weapons.Init(WeaponTable::Parse(Table()), &inventory, [] { return 0u; });
    }

    void Tick(bool trigger) {
        GunTickInput input;
        input.triggerOn = trigger;
        input.random = [] { return 1u; };
        weapons.TickGameplay(input, &hooks);
        hooks.timer++;
    }
    void Tick(int times, bool trigger) {
        for (int i = 0; i < times; i++) {
            Tick(trigger);
        }
    }
    void Arm(int item) {
        weapons.GiveAmmo(weapons.Table().AmmoType(item), 100);
        weapons.EquipWeapon(HandLeft, kItemUnarmed);
        weapons.EquipWeapon(HandRight, item);
        Tick(60, false);
    }
};

void TestTheWeaponTableCarriesTheShotSound() {
    const auto table = WeaponTable::Parse(Table());
    Check(table != nullptr && table->Item(4).sound == 100 && table->Item(4).soundTriggerRate == 0,
          "a weapon's sound and its rate are read");
    Check(table->Item(8).sound == 101 && table->Item(8).soundTriggerRate == 6, "an automatic sounds at a rate");
}

void TestAShotSoundsAndTheNextCutsItOff() {
    Rig rig;
    Scene scene(rig);
    scene.Arm(4);
    rig.Render();
    const int before = rig.Active();
    const HandState& hand = scene.weapons.Hands(HandRight);

    scene.Tick(true);
    rig.Render();
    Check(NumberOf(hand.audioHandle.Get()) == 100, "the pistol's shot plays its sound on the hand's handle");
    Check(rig.Active() == before + 1, "one sound is playing");
    Check(hand.soundLastTime != 0, "the hand remembers when it sounded");

    // The next shot stops the playing sound and takes the second handle when the
    // first still names a sound that has not yet gone.
    scene.Tick(10, false);
    SoundState* first = hand.audioHandle.Get();
    scene.Tick(true);
    rig.Render();
    Check(first == nullptr || Frigate::Game::Sound::GetPlayingState(first) != kSoundStatePlaying ||
              hand.audioHandle.Get() != first,
          "a second shot cuts the first off");
    Check(hand.secondAudioHandle.Get() != nullptr || hand.audioHandle.Get() != nullptr, "and plays again");
}

void TestTheSoundTriggerRateSpacesTheAutomaticsSounds() {
    Rig rig;
    Scene scene(rig);
    scene.Arm(8);
    const HandState& hand = scene.weapons.Hands(HandRight);

    int sounded = 0;
    int shots = 0;
    int32_t last = hand.soundReadyTime;
    for (int i = 0; i < 18; i++) {
        scene.Tick(true);
        shots += hand.firingStatus;
        if (hand.soundReadyTime != last) {
            sounded++;
            last = hand.soundReadyTime;
        }
    }
    Check(shots == 6, "the automatic fires every third tick");
    Check(sounded == 3 || sounded == 4, "but sounds only every sixth tick or so");
    Check(sounded < shots, "fewer sounds than shots");
}

void TestADryFireClicks() {
    Rig rig;
    Scene scene(rig);
    scene.Arm(4);
    // Empty the magazine without the held ammo reloading it.
    scene.weapons.GiveAmmo(1, 0);
    while (scene.weapons.Hands(HandRight).ammoInMagazine > 0) {
        scene.Tick(true);
        scene.Tick(12, false);
    }
    scene.Tick(2, false);
    rig.Render();
    const int before = rig.Active();
    scene.Tick(true);
    rig.Render();
    Check(rig.Active() > before, "a click plays when the magazine is empty");
}

void TestRaisingAnItemSoundsUnlessMuted() {
    Rig rig;
    {
        Scene scene(rig);
        scene.weapons.GiveAmmo(1, 100);
        scene.weapons.EquipWeapon(HandLeft, kItemUnarmed);
        scene.weapons.EquipWeapon(HandRight, 4);
        scene.Tick(60, false);
        rig.Render();
        Check(rig.Active() >= 1, "raising a gun plays its pickup sound");
    }
    rig.Render(10);

    Rig muted;
    Scene scene(muted);
    scene.hooks.raiseSounds = false;
    scene.weapons.GiveAmmo(1, 100);
    scene.weapons.EquipWeapon(HandLeft, kItemUnarmed);
    scene.weapons.EquipWeapon(HandRight, 4);
    scene.Tick(60, false);
    muted.Render();
    Check(muted.Active() == 0, "the intro camera and invincibility mute it");
}

void TestTheMagnetHums() {
    Rig rig;
    Scene scene(rig);
    scene.weapons.StartMagnetAttract();
    scene.weapons.TickMagnet(10, false);
    rig.Render();
    Check(NumberOf(scene.weapons.Hands(HandRight).audioHandle.Get()) == kSfxMagneticHum, "the magnet hums");
    scene.weapons.TickMagnet(300, false);
    rig.Render(10);
    Check(scene.weapons.MagnetAttractTime() < 0 && rig.Active() == 0, "and stops when it ends");

    scene.weapons.StartMagnetAttract();
    scene.weapons.TickMagnet(10, true);
    rig.Render();
    Check(rig.Active() == 0, "a locked control stops it starting");
}

void TestDroppingTheHandsStopsTheirSounds() {
    Rig rig;
    {
        Scene scene(rig);
        scene.weapons.StartMagnetAttract();
        scene.weapons.TickMagnet(10, false);
        rig.Render();
        Check(rig.Active() == 1, "the hum plays");
    }
    rig.Render(10);
    Check(rig.Active() == 0, "and ends with the weapons");
}

void TestHitsShareFourHandles() {
    Rig rig;
    HitSounds hits;
    uint32_t draws = 0;
    const HitSounds::RandomFn random = [&draws] {
        draws++;
        return 3u;
    };
    const Vec3 position = { 0.0f, 0.0f, 0.0f };

    hits.PropHit(kItemPp7, position, false, HitType::Metal, random);
    rig.Render();
    Check(draws == 2 && rig.Active() == 2, "a hit on a prop plays a ricochet and the surface's sound");

    hits.PropHit(kItemPp7, position, false, HitType::Metal, random);
    hits.PropHit(kItemPp7, position, false, HitType::Metal, random);
    rig.Render();
    Check(rig.Active() == 4, "only four play at once");

    draws = 0;
    hits.PropHit(kItemRemoteMine, position, true, HitType::Metal, random);
    hits.PropHit(kItemWatchLaser, position, true, HitType::Metal, random);
    rig.Render();
    Check(draws == 4 && rig.Active() == 4, "a mine or the watch laser draws and stays silent");
}

void TestAHitOnACharacterSoundsFlesh() {
    Rig rig;
    HitSounds hits;
    const HitSounds::RandomFn random = [] { return 0u; };
    const Vec3 position = { 0.0f, 0.0f, 0.0f };
    hits.PropHit(kItemFist, position, true, -1, random);
    rig.Render();
    Check(rig.Active() == 1, "a punch lands with no surface sound");
}

void TestTheSurfacesHaveTheirSounds() {
    Check(SurfaceImpactsFor(HitType::Metal).soundCount == 3 &&
              SurfaceImpactsFor(HitType::Metal).sounds[0] == kSfxHitBulletMetalA,
          "metal has three hits");
    Check(SurfaceImpactsFor(HitType::Water).soundCount == 1 &&
              SurfaceImpactsFor(HitType::Water).sounds[0] == kSfxHitBulletWater,
          "water splashes");
    Check(SurfaceImpactsFor(HitType::GlassXlu).sounds[0] == kSfxHitBulletGlass, "translucent glass breaks");
}

void TestTheExplosionsHaveTheirSounds() {
    Check(ExplosionTypeOf(0).sound == kSfxNothing && ExplosionTypeOf(1).sound == kSfxNothing,
          "the first two are silent");
    Check(ExplosionTypeOf(2).sound == kSfxExplosion1b && ExplosionTypeOf(10).sound == kSfxExplosion5a &&
              ExplosionTypeOf(13).sound == kSfxExplosion4b,
          "the rest name theirs");
}

void TestACasingRollsOnceUnlessAHandFires() {
    Rig rig;
    Casings casings;
    bool firing = false;
    casings.SetHandFiringQuery([&firing] { return firing; });

    CasingEject eject;
    eject.model = "models/gun/GcartridgeZ";
    eject.pistol = true;
    eject.floorY = -50.0f;
    eject.gunMatrix = TranslationMtxf({ 10.0f, 20.0f, 30.0f });
    eject.previousGunMatrix = eject.gunMatrix;
    const auto random = [] { return 0x80000000u; };

    firing = true;
    casings.Eject(eject, random);
    for (int i = 0; i < 40; i++) {
        casings.Update(1, 1.0f);
    }
    rig.Render();
    Check(rig.Active() == 0, "a casing that lands while a hand fires is silent");

    firing = false;
    casings.Eject(eject, random);
    casings.Eject(eject, random);
    for (int i = 0; i < 40; i++) {
        casings.Update(1, 1.0f);
    }
    rig.Render();
    Check(rig.Active() == 1, "otherwise it rolls, and one at a time");
}

} // namespace

int main() {
    TestTheWeaponTableCarriesTheShotSound();
    TestAShotSoundsAndTheNextCutsItOff();
    TestTheSoundTriggerRateSpacesTheAutomaticsSounds();
    TestADryFireClicks();
    TestRaisingAnItemSoundsUnlessMuted();
    TestTheMagnetHums();
    TestDroppingTheHandsStopsTheirSounds();
    TestHitsShareFourHandles();
    TestAHitOnACharacterSoundsFlesh();
    TestTheSurfacesHaveTheirSounds();
    TestTheExplosionsHaveTheirSounds();
    TestACasingRollsOnceUnlessAHandFires();
    return gFailures == 0 ? 0 : 1;
}
