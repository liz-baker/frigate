#include <cstdio>
#include <string>

#include "level/Stage.h"
#include "level/StageAi.h"
#include "level/WeaponTable.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

// LGUNE_STRINGS: GUN_STR_DA_KILLCOUNT, _DB_DIEDONCE_LF, _DC_DIED, _DD_TIMES and
// _DE_SUICIDECOUNT.
constexpr int32_t kKillCountText = 218;
constexpr int32_t kDiedOnceText = 219;
constexpr int32_t kDiedText = 220;
constexpr int32_t kTimesText = 221;
constexpr int32_t kSuicideCountText = 222;

// ITEM_IDS_MAX.
constexpr int32_t kItemIdsMax = 89;

// The ticks within which the last kills count as one streak.
constexpr int kStreakTicks = 0x78;

// A "%s: %d\n" message.
std::string CountMessage(std::string_view label, int count) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%.*s: %d\n", static_cast<int>(label.size()), label.data(), count);
    return buffer;
}

constexpr float kTwoPi = 6.2831855f;

} // namespace

void Stage::RecordKillTime() {
    // The tail the kill and the suicide counters share.
    PlayerStats& stats = CurStats();
    const int now = missionTimer_;
    if (stats.killCount >= 2) {
        const int sinceKill = now - Cur().lastKillTime[0];
        if (stats.maxTimeBetweenKills < sinceKill) {
            stats.maxTimeBetweenKills = sinceKill;
        }
        if (sinceKill < stats.minTimeBetweenKills) {
            stats.minTimeBetweenKills = sinceKill;
        }
    }

    std::array<int, 4>& last = Cur().lastKillTime;
    int streak = 1;
    last[3] = last[2];
    last[2] = last[1];
    last[1] = last[0];
    last[0] = now;

    if (last[1] != -1 && (last[0] - last[1]) < kStreakTicks) {
        streak++;
        if (last[2] != -1 && (last[0] - last[2]) < kStreakTicks) {
            streak++;
            if (last[3] != -1 && (last[0] - last[3]) < kStreakTicks) {
                streak++;
            }
        }
    }

    if (stats.mostKilledOneTime < streak) {
        stats.mostKilledOneTime = streak;
    }
}

void Stage::IncrementKills() {
    // increment_num_kills_display_text_in_MP.
    CurStats().killCount++;
    Cur().killsThisLife++;

    if (playerCount_ < 2) {
        return;
    }

    Cur().hud.BottomShow(CountMessage(lang_.Get(StringId(TextBank::Gun, kKillCountText)), CurStats().killCount));
    RecordKillTime();
}

void Stage::IncrementDeaths() {
    // increment_num_deaths.
    Cur().deathCount++;
    if (playerCount_ >= 2) {
        if (Cur().deathCount == 1) {
            Cur().hud.BottomShow(lang_.Get(StringId(TextBank::Gun, kDiedOnceText)));
        } else {
            char buffer[256];
            const std::string_view died = lang_.Get(StringId(TextBank::Gun, kDiedText));
            const std::string_view times = lang_.Get(StringId(TextBank::Gun, kTimesText));
            std::snprintf(buffer, sizeof(buffer), "%.*s %d %.*s\n", static_cast<int>(died.size()), died.data(),
                          Cur().deathCount, static_cast<int>(times.size()), times.data());
            Cur().hud.BottomShow(buffer);
        }
    }
}

void Stage::IncrementSuicides() {
    // increment_num_suicides_display_MP.
    Cur().suicides++;
    if (playerCount_ >= 2) {
        Cur().hud.BottomShow(CountMessage(lang_.Get(StringId(TextBank::Gun, kSuicideCountText)), Cur().suicides));
        RecordKillTime();
    }
}

void Stage::CreditKill(int attacker) {
    // record_damage_kills' multiplayer share of a kill, for the current player
    // (the victim). The decomp reads g_playerPlayerData[-1] for a kill by no
    // player; the death counts and nothing is credited.
    const int victim = current_;
    const bool holdsGoldenGun = multiplayer_->scenario == Game::MpScenario::TheManWithTheGoldenGun &&
                                Cur().intro.inventory.HasItem(kItemGoldenGun);

    if (attacker != victim) {
        DropInventory();
        IncrementDeaths();
    }

    if (attacker >= 0) {
        AsPlayer(attacker, [&] {
            if (attacker == victim) {
                IncrementSuicides();
            } else {
                IncrementKills();
                if (holdsGoldenGun) {
                    CurStats().killedGoldenGunOwner++;
                }
            }
        });
        stats_[static_cast<size_t>(attacker)].killCounts[static_cast<size_t>(victim)]++;
    }
}

void Stage::DropInventory() {
    // drop_inventory: whatever is in the player's hands goes, then every item
    // the player carries that has a model is made and set to fall from the
    // player's prop.
    ChrHost host;
    std::array<int, 2> unattachedHands{ -1, -1 };
    int unattachedHat = -1;
    int chrnum = -1;
    host.prop = Cur().move.PropId();
    host.weaponsHeld = &unattachedHands;
    host.hat = &unattachedHat;
    if (Cur().bondChr >= 0) {
        Chr& chr = chrs_[Cur().bondChr];
        for (const int hand : { HandRight, HandLeft }) {
            aiBridge_->RemoveHeldItem(chr, hand);
        }
        host.model = chr.model;
        host.weaponsHeld = &chr.weaponsHeld;
        host.hat = &chr.hat;
        chrnum = chr.chrnum;
    }

    PropList& props = objects_.Props();
    for (int32_t item = kItemFist; item != kItemIdsMax; item++) {
        const int model = HeldItemModel(item);
        if (model < 0 || !Cur().intro.inventory.HasItem(item)) {
            continue;
        }
        // The item is not held: it is carried, and falls at once.
        if (!objects_.GiveWeapon(host, chrnum, model, item, ObjectFlag::ConcealGun)) {
            continue;
        }
        const int made = props[host.prop].child;
        if (made >= 0) {
            SetDropped(made, DropType::Default);
            DropObject(made);
        }
    }
}

void Stage::BeginLife() {
    // bondviewPlayerBeginLife, past the eye height PlacePlayer sets.
    Cur().killsThisLife = 0;
    Cur().lifeStartTime = missionTimer_;
    Cur().hud.Health().displayTime = 0;

    Cur().intro.inventory.AddItem(kItemFist);

    if (playerCount_ >= 2) {
        if (Cur().weapons.Ready()) {
            Cur().weapons.EquipWeapon(HandLeft, Cur().intro.startingWeaponLeft);
            Cur().weapons.EquipWeapon(HandRight, Cur().intro.startingWeaponRight);
        }
        // solo_char_load gives the player the body of the character chosen for
        // them, if they have none.
        if (Cur().bondChr < 0) {
            LoadBondBody();
        }
    }
}

void Stage::RespawnPlayer() {
    // mp_respawn_handler.
    PlayerState& state = Cur();
    const float perspectiveHeight = CurStats().perspectiveHeight;

    // init_player_BONDdata: everything but the position, which is placed below.
    state.options.controlType = multiplayer_->controlStyle[static_cast<size_t>(current_)];
    state.frozenCamera = false;
    state.cameraPos = {};
    state.cameraPos2 = { 0.0f, 0.0f, 1.0f };
    state.cameraUp = { 0.0f, 1.0f, 0.0f };
    state.cameraFloorPos = {};
    state.cameraTile = kNoTile;
    state.sightPitch = 0.0f;
    state.previousButtons = 0;
    state.autoAimEnabled = true;
    state.autoAim.Reset();
    state.colour.Reset();
    state.health.BeginLife();
    state.watch.BeginLife();
    // startnewbonddie.
    if (state.head) {
        state.head->StartNewDeath();
    }

    BeginLife();

    // gunammooff, gunsightmode, hudmsgsSetOn(-1) and
    // bondviewClearUpperTextDisplayFlag(-1).
    state.hud.BeginLife();

    size_t pad = 0;
    if (!state.intro.startPads.empty()) {
        std::vector<Vec3> pads;
        for (const size_t index : state.intro.startPads) {
            pads.push_back(setup_->Pads()[index].position);
        }
        std::vector<std::optional<Vec3>> positions(static_cast<size_t>(playerCount_));
        for (int other = 0; other < playerCount_; other++) {
            const int prop = players_[static_cast<size_t>(other)].move.PropId();
            if (prop >= 0) {
                positions[static_cast<size_t>(other)] = objects_.Props()[prop].position;
            }
        }
        pad = ChooseStartPad(pads, current_, positions, &state.spawnPadCursor, RandomGetNext);
    }

    const float lookAngle = static_cast<float>(RandomGetNext()) * 2.3283064e-10f * kTwoPi;
    state.spawn = PlaceRespawn(*setup_, state.intro, level_.StanFile(), perspectiveHeight, pad, lookAngle);
    state.move.BeginLife(state.spawn, level_.StanFile());
    if (state.move.PropId() >= 0) {
        Prop& prop = objects_.Props()[state.move.PropId()];
        prop.position = state.spawn.position;
        prop.stan = state.spawn.stanTile;
    }

    // bondinvReinitInv, the ammo held, and the intro's items and ammo again.
    state.intro.inventory.Clear();
    if (state.weapons.Ready()) {
        state.weapons.ClearAmmoHeld();
    }
    for (const IntroItem& item : state.intro.items) {
        if (item.itemLeft >= 0) {
            state.intro.inventory.AddDoubles(item.itemRight, item.itemLeft);
        } else {
            state.intro.inventory.AddItem(item.itemRight);
        }
    }
    if (state.weapons.Ready()) {
        for (const IntroAmmo& ammo : state.intro.ammo) {
            state.weapons.GiveAmmo(ammo.ammoType, ammo.amount);
        }
    }

    StartBodyFade(120.0f, 1.0f);
}

} // namespace Frigate::Level
