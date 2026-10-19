#include "level/Stage.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>

#include "game/Sound.h"
#include "game/menu/BasicGeometry.h"
#include "gfx/GbiF3dex.h"
#include "gfx/Rect2D.h"
#include "level/MpWatchMenuDraw.h"
#include "level/LevelInfo.h"
#include "level/MpWeaponSets.h"
#include "level/PlayerPadInput.h"
#include "level/ScreenSetup.h"
#include "level/StageAi.h"
#include "level/StageGun.h"
#include "level/StageTank.h"
#include "level/StageWatch.h"
#include "model/Random.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

// 60 Hz: a clock tick's length in seconds is 1/60 (US).
constexpr float kTicksPerSecond = 60.0f;
constexpr float kDegToRad = 0.017453292f;
// FOV_Y_F.
constexpr float kFovY = 60.0f;

} // namespace

Stage::Stage(BgSource& source, HudSource* hudSource)
    : source_(source), level_(source), visibility_(level_), bgHit_(level_), roomLoad_(fog_, lightFixtures_, bgHit_),
      render_(level_, visibility_, objects_, doors_, chrs_, fog_),
      damage_(objects_, level_.StanFile(), impacts_, shards_, explosions_), deform_(objects_, impacts_),
      effects_(level_, visibility_, sparks_, shards_, explosions_, impacts_, effectImages_), hudSource_(hudSource) {
    level_.SetObserver(&roomLoad_);
    aiBridge_ = std::make_unique<AiBridge>(*this);
    gunBridge_ = std::make_unique<GunBridge>(*this);
    tankBridge_ = std::make_unique<TankBridge>(*this);
    watchBridge_ = std::make_unique<WatchBridge>(*this);
    Game::AiSoundChannels::Host soundHost;
    soundHost.clockTimer = [this] { return clock_.clockTimer; };
    soundHost.controlsLocked = [this] { return controlsLocked_; };
    soundHost.propPosition = [this](int prop) -> std::optional<Game::AiSoundChannels::Position> {
        const PlacedObject* object = objects_.ForProp(prop);
        if (object == nullptr) {
            return std::nullopt;
        }
        return object->runtimePosition;
    };
    aiSounds_ = std::make_unique<Game::AiSoundChannels>(std::move(soundHost));
    AttachEffects();
    gunDraw_.SetBeamDraw(&effects_);
    render_.SetTank([this]() { return Cur().move.DrivenTank(); },
                    [this](PlacedObject& tank) {
                        // The footprint of the top the player stands on follows the turret.
                        if (const TankGeometry* geometry = objects_.TankGeometryOf(tank)) {
                            Mtxf placement = tank.mtx;
                            for (size_t axis = 0; axis < 3; axis++) {
                                placement.m[3][axis] = tank.runtimePosition[axis];
                            }
                            UpdateTankRect(*geometry, &tank.tank, placement);
                        }
                    });
}

Stage::~Stage() {
    Game::Sound::DetachHandle(&alarmSound_);
    Game::Sound::DetachHandle(&gasSound_);
    Game::Sound::ClearListenerSource(this);
}

void Stage::SetCurrentPlayer(int player) {
    current_ = player;
    PlayerState& state = Cur();
    shots_.BindPlayer(&state.weapons, &state.camera, &CurStats().shotCount);
}

int Stage::PlayerOfProp(int prop) const {
    for (int player = 0; player < playerCount_; player++) {
        if (players_[static_cast<size_t>(player)].move.PropId() == prop) {
            return player;
        }
    }
    return -1;
}

bool Stage::Load(LevelId stage, int32_t demoSlot, int difficulty, bool demo) {
    loaded_ = false;
    error_.clear();
    currentStage_ = stage;
    difficulty_ = difficulty;
    demoPlayback_ = demoSlot != 0;
    playerCount_ = multiplayer_ ? multiplayer_->playerCount : 1;
    current_ = 0;
    for (PlayerState& player : players_) {
        player.health.Reset();
        player.colour.Reset();
        player.autoAim.Reset();
        player.lifeStartTime = 0;
        player.killsThisLife = 0;
        player.deathCount = 0;
        player.suicides = 0;
        player.lastKillTime = { -1, -1, -1, -1 };
        player.aspect = 4.0f / 3.0f;
    }
    forceDisarm_ = 0;
    civilianCasualties_ = 0;
    surroundBondWithExplosions_ = false;
    surroundTicks_ = 0;
    tickExplodePosition_ = 0;
    alarmTimer_ = 0;
    demo_ = demo;
    // g_MpSoundStateRelated = 0.
    Game::Sound::DetachHandle(&mpAlarmSound_);
    mpAlarmSound_ = nullptr;
    ResetAiState();

    // init_player_data_ptrs_construct_viewports draws one byte ahead of
    // lvlStageLoad, and each player's field_29E0 counts up from it.
    const int spawnPadSeed = static_cast<int>(RandomGetNext() & 0xFF);
    for (int player = 0; player < playerCount_; player++) {
        players_[static_cast<size_t>(player)].spawnPadCursor = spawnPadSeed + player;
    }

    drawEnabled_ = true;
    controlsLocked_ = false;
    for (PlayerState& player : players_) {
        player.copiedGoldenEye = false;
    }
    clock_ = StageClock{};
    idleTicks_ = 0;
    idle_ = false;
    idleTotal_ = 0;
    buttonsPressed_ = 0;

    Game::Sound::SetScalerApplyVolumeAllSfxSlot(1.0f);
    for (int slot = 0; slot < AudioDriver::kMusicSlotCount; slot++) {
        Game::Sound::MusicTrackApplySeqpVol(slot, AudioDriver::kMusicVolumeMax);
    }
    music_.ResetSlots();
    // The players' positions, which the sounds' volumes are measured to.
    Game::Sound::SetListenerSource(this, [this] {
        std::vector<std::array<float, 3>> listeners;
        for (int player = 0; player < playerCount_; player++) {
            const int prop = players_[static_cast<size_t>(player)].move.PropId();
            if (prop >= 0) {
                listeners.push_back(objects_.Props()[prop].position);
            }
        }
        return listeners;
    });
    // set_mt_tex_alloc, texReset and modelmgrSetLevelResetting(TRUE) put the
    // texture and model pools into their stage-load allocation mode; textures
    // and models are resource-managed here, so there is no pool to prepare.
    sparks_.Reset();
    // TODO(https://github.com/liz-baker/frigate/issues/21): load_font_tables, which
    // resets the text style globals.

    // g_NewCheatUnlocked is cleared by LiveRun::Start. The enemy rockets are the
    // one cheat switched on before the setup's weapons are made; D_80048394 was
    // just cleared.
    if (clock_.clockTimer > 0) {
        TurnOnActivatedCheats(true);
    }

    if (!level_.Load(stage)) {
        error_ = level_.Error();
        return false;
    }
    textureHits_ = TextureHits();
    if (const auto text = source_.LoadTextureHits()) {
        textureHits_ = TextureHits::Parse(*text);
    }
    bgHit_.SetTextureHits(&textureHits_);
    // initializeRoomData, which load_bg_file calls once the rooms are sized.
    if (level_.Header().firstWord == 0) {
        render_.Reset();
    }

    sky_.SetStageNum(stage);
    // sub_GAME_7F0C11FC: it runs before the players' data is set up, and a stage
    // with no theme draws a random number here.
    music_.StartStage(static_cast<int>(stage));

    mpTime_ = 0;
    mpPoint_ = 0;
    for (size_t player = 0; player < kMaxPlayers; player++) {
        PlayerStats& stats = stats_[player];
        if (playerCount_ == 1) {
            stats.autoaim = 0;
            stats.sight = 0;
            stats.handicap = 1.0f;
            stats.perspectiveHeight = 1.0f;
        } else {
            const Game::MultiplayerSettings& mp = *multiplayer_;
            // The decomp sets every player's handicap and height on each pass
            // of this loop; once is the same result.
            if (player == 0) {
                for (int other = 0; other < kMaxPlayers; other++) {
                    PlayerStats& otherStats = stats_[static_cast<size_t>(other)];
                    // SCENARIO_LTK: every hit kills.
                    otherStats.handicap = mp.scenario == Game::MpScenario::LicenceToKill ? 200.0f : mp.Handicap(other);
                    otherStats.perspectiveHeight = mp.CharacterHeight(other);
                }
                mpTime_ = mp.TimeLimit();
                mpPoint_ = mp.PointLimit();
                // copy_aim_settings_to_playerdata.
                for (PlayerStats& aimed : stats_) {
                    aimed.autoaim = mp.Aim().autoaim;
                    aimed.sight = mp.Aim().sight;
                }
            }
        }
        stats.timeOtherPlayersOnScreen = 0;
        stats.damageToBackside = 0;
        stats.minTimeBetweenKills = INT32_MAX;
        stats.maxTimeBetweenKills = 0;
        stats.mostKilledOneLife = 0;
        stats.mostKilledOneTime = 0;
        stats.longestInning = 0;
        stats.shortestInning = INT32_MAX;
        stats.orderOutInYolt = 0;
        stats.flagCounter = 0;
        stats.distanceTraveled = 0.0f;
        stats.bodyArmorPickups = 0.0f;
        stats.killCounts = {};
        // The team screen sets each player's team in the field the token and
        // golden gun scenarios use, and a stage load leaves it alone.
        stats.haveTokenOrGoldenGun = multiplayer_ ? multiplayer_->TeamFlag(static_cast<int>(player)) : 0;
    }

    // mpwatchUnpauseGame.
    stopPlay_ = false;
    gameOver_ = 0;
    paused_ = false;

    // initModelHitEntryFreeList and modelmgrResetSlotCounts reset the original's
    // model pools; models are resource-managed here, so there is nothing to reset.
    // init_load_objpos_table: the prop pool and the room lookup buffers.
    std::shared_ptr<const PropModels> propModels;
    if (const auto text = source_.LoadPropModels()) {
        propModels = PropModels::Parse(*text);
        if (!propModels) {
            error_ = "the object model table (prop/models) is malformed";
            return false;
        }
    }
    objects_.Reset(level_.MaxNumRooms(), &level_.StanFile(), &level_, propModels);
    doors_.Reset(&level_.StanFile(), &level_, std::move(propModels), &objects_.Props());
    objects_.SetDoorCollision([this](int prop, int cdTypes) { return doors_.Passes(prop, cdTypes); },
                              [this](int prop) { return doors_.Collision(prop); });
    links_.Reset();
    doors_.SetActivatedHook([this](int prop) { links_.TriggerSwitches(prop, &doors_); });
    doors_.SetPadlockCheck([this](size_t doorDef) { return links_.IsLocked(doorDef); });
    doors_.SetControlsLockedCheck([this] { return controlsLocked_; });
    doors_.SetClearCheck([this](int prop) { return objects_.FootprintClear(prop); });
    objects_.SetDetachHook([this](int prop) { ReleaseHeld(prop); });
    doors_.SetFallHook([this](PlacedDoor& door) { projectiles_.StartDoorFall(door); });
    objects_.SetFreeDeformHook([this](PlacedObject& object) { deform_.Release(object); });
    objects_.SetSafeCheck([this](size_t itemDef) { return links_.CanPickupFromSafe(itemDef); });
    level_.StanFile().SetProps(&objects_);
    std::shared_ptr<const Game::ChrRecords> chrRecords;
    if (const auto text = source_.LoadChrRecords()) {
        chrRecords = Game::ChrRecords::Parse(*text);
        if (!chrRecords) {
            error_ = "the body and head table (chr/records) is malformed";
            return false;
        }
    }
    std::shared_ptr<const Game::ChrHats> chrHats;
    if (const auto text = source_.LoadChrHats()) {
        chrHats = Game::ChrHats::Parse(*text);
        if (!chrHats) {
            error_ = "the hat placements (chr/hats) are malformed";
            return false;
        }
    }
    // init_guards and bodiesReset.
    chrs_.Reset(&level_.StanFile(), &objects_.Props(), std::move(chrRecords), std::move(chrHats), source_.Models());
    chrs_.SetHealthMod(Game::Get007HealthMod(sliders007_, difficulty_));
    chrs_.SetDkModeCheck([this] { return CheatActive(Game::Menu::Cheat::kDkMode); });
    aiLists_.Clear();
    if (const auto text = source_.LoadAiGlobals()) {
        if (!aiLists_.ParseGlobals(*text)) {
            error_ = "the global AI lists (ai/globals) are malformed";
            return false;
        }
    }
    chrs_.SetAiLists(&aiLists_);
    objects_.SetModels(source_.Models());
    objects_.SetEnemyRocketsCheck([this] { return CheatActive(Game::Menu::Cheat::kEnemyRockets); });
    objects_.SetMultiplayerRespawn(playerCount_ >= 2,
                                   multiplayer_ && multiplayer_->scenario == Game::MpScenario::TheLivingDaylights,
                                   multiplayer_ && multiplayer_->scenario == Game::MpScenario::TheManWithTheGoldenGun);
    objects_.SetChrCollision(
        [this](int prop, int cdTypes, PropCollider* out) { return chrs_.Collider(prop, cdTypes, out); });
    objects_.SetChrLookup([this](int chrnum) {
        ChrHost host;
        // A character must have a prop and a model (a spawn without a model
        // source has none to hold things on, but takes them).
        if (Chr* chr = chrs_.FindByLiteralId(chrnum)) {
            host.prop = chr->prop;
            host.model = chr->model;
            host.weaponsHeld = &chr->weaponsHeld;
            host.hat = &chr->hat;
        }
        return host;
    });
    objects_.SetBodyBounds([this](int prop, BodyBounds* out) {
        const Prop& record = objects_.Props()[prop];
        if (record.type == PropType::Viewer) {
            const int player = PlayerOfProp(prop);
            if (player < 0) {
                return false;
            }
            PlayerMove& move = players_[static_cast<size_t>(player)].move;
            float always30 = 0.0f;
            move.CollisionRadius(&out->radius, &out->height, &always30);
            out->above = always30;
            out->ground = move.StanHeight();
            return true;
        }
        if (record.type != PropType::Chr || record.object < 0) {
            return false;
        }
        Chr& chr = chrs_[record.object];
        if ((chr.chrflags & ChrFlag::Hidden) != 0) {
            return false;
        }
        float always20 = 0.0f;
        Chrs::WidthHeight(chr, &out->radius, &out->height, &always20);
        out->above = always20;
        out->ground = chr.ground;
        out->hidden = &chr.hidden;
        return true;
    });
    chrs_.BodiesReset(RandomGetNext);
    if (!LoadSetup(stage, difficulty)) {
        return false;
    }
    // init_sound_effects_registers. (reinit_between_menus' alarm and gas sound
    // state is ResetAiState's.)
    aiSounds_->Reset();
    ResetEffects();
    // initCheatTextBuffer, once the setup's props exist: it clears the enemy
    // rockets mark the objects were made under.
    cheats_.Reset();
    // reinit_gunheld_totaltime: PlayerWeapons::Init clears the held times.
    // init_path_table_links.
    if (setup_) {
        paths_.Reset(&*setup_);
        if (const int errors = paths_.InitLinks(); errors > 0) {
            for (const std::string& line : paths_.Errors()) {
                spdlog::error("level: {}", line);
            }
            spdlog::error("level: the setup's waypoint graph has {} errors", errors);
        }
    } else {
        paths_.Reset(nullptr);
    }
    casings_.Clear();

    // The per-player half of lvlStageLoad's loop that reads the stage's setup:
    // each player's hands, then the intro section and start position.
    std::vector<std::optional<Vec3>> startPositions(static_cast<size_t>(playerCount_));
    for (int player = 0; player < playerCount_; player++) {
        SetCurrentPlayer(player);
        PlayerState& state = Cur();
        state.intro = IntroSetup{};
        state.spawn = PlayerSpawn{};
        // init_player_BONDdata_stats's hands come first: their sway draws random
        // numbers ahead of the intro section's.
        state.weapons = PlayerWeapons{};
        CurStats().shotCount = {};
        CurStats().killCount = 0;
        CurStats().killedGoldenGunOwner = 0;
        state.weapons.SetModels(source_.Models());
        if (const auto text = source_.LoadWeaponStats()) {
            auto table = WeaponTable::Parse(*text);
            if (!table) {
                error_ = "the weapon table (weapon/stats) is malformed";
                return false;
            }
            state.weapons.Init(std::move(table), &state.intro.inventory, RandomGetNext);
            state.weapons.SetPlayerCount(playerCount_);
            state.weapons.SetCharacterHooks(
                { [this, player](Hand hand) { AsPlayer(player, [&] { GiveBodyWeapon(hand); }); },
                  [this, player](Hand hand) { AsPlayer(player, [&] { ReleaseBodyWeapon(hand); }); } });
            state.weapons.SetPropListed([this](const InvItem& item) { return watchBridge_->PropListed(item); });
        }
        if (setup_) {
            state.intro = LoadIntroSection(*setup_, demoSlot, RandomGetNext);
            // proplvreset2's rename records hand bondinvAddTextOverride their text
            // after the inventory is emptied, which the intro then fills; the
            // current player when it runs is the first.
            if (player == 0) {
                PropLinks::AddTextOverrides(*setup_, &state.intro.inventory);
            }
            size_t pad = 0;
            if (playerCount_ >= 2 && !state.intro.startPads.empty()) {
                std::vector<Vec3> pads;
                for (const size_t index : state.intro.startPads) {
                    pads.push_back(setup_->Pads()[index].position);
                }
                pad = ChooseStartPad(pads, player, startPositions, &state.spawnPadCursor, RandomGetNext);
            }
            state.spawn = PlacePlayer(*setup_, state.intro, level_.StanFile(),
                                      stats_[static_cast<size_t>(player)].perspectiveHeight, pad);
            startPositions[static_cast<size_t>(player)] = state.spawn.position;
        }
    }
    SetCurrentPlayer(0);
    monitorLists_.reset();
    if (const auto text = source_.LoadMonitorLists()) {
        monitorLists_ = MonitorLists::Parse(*text);
        if (!monitorLists_) {
            error_ = "the monitor animation lists (monitor/lists) are malformed";
            return false;
        }
    }
    gunDraw_.AttachMonitors(
        monitorLists_, [this](int offset) { return effectImages_.Size(offset); }, SelectEffectImage);
    render_.SetMonitorTick([this](PlacedObject& object, uint32_t screens) { TickMonitor(object, screens); });
    // The AI interpreter, which the characters' ticks run.
    {
        AiScene scene;
        scene.setup = setup_ ? &*setup_ : nullptr;
        scene.stan = &level_.StanFile();
        scene.props = &objects_.Props();
        scene.chrs = &chrs_;
        scene.objects = &objects_;
        scene.doors = &doors_;
        scene.links = &links_;
        scene.paths = &paths_;
        scene.lists = &aiLists_;
        std::optional<AnimationTable> animations;
        if (const auto text = source_.LoadAnimationTable()) {
            animations = AnimationTable::Parse(*text);
            if (!animations) {
                error_ = "the animation tables (animations/table) are malformed";
                return false;
            }
        }
        std::shared_ptr<const ChrCombatTables> combat;
        if (const auto text = source_.LoadChrCombat()) {
            combat = ChrCombatTables::Parse(*text);
            if (!combat) {
                error_ = "the characters' combat tables (chr/combat) are malformed";
                return false;
            }
        }
        aiActions_ =
            std::make_unique<ChrActions>(scene, *aiBridge_, source_.Models(), std::move(animations), std::move(combat));
        ai_ = std::make_unique<ChrAi>(scene, *aiBridge_, *aiActions_);
        aiActions_->Attach(ai_.get());
        render_.SetChrActionTick([this](Chr& chr) { ai_->ActionTick(chr); });
        ChrDraw::CombatHooks combatHooks;
        combatHooks.updateAim = [this](Chr& chr) { aiActions_->UpdateAimProperties(chr); };
        combatHooks.triggerFire = [this](Chr& chr) { aiActions_->TriggerFireWeapon(chr); };
        combatHooks.dropObject = [this](int prop) { DropObject(prop); };
        combatHooks.stopFiring = [this](Chr& chr, int hand) { aiActions_->StopFiringHand(chr, hand); };
        combatHooks.dkMode = [this] { return CheatActive(Game::Menu::Cheat::kDkMode); };
        combatHooks.positionJoint = [this](Chr& chr, int index, Mat4* matrix, const Mtxf& viewToWorld,
                                           const Mtxf& worldToScreen) {
            aiActions_->PositionJoint(chr, index, matrix, viewToWorld, worldToScreen);
        };
        render_.SetChrCombat(std::move(combatHooks));
        vehicles_ = std::make_unique<Vehicles>(objects_, level_.StanFile(), paths_, chrs_, *ai_, *aiActions_, *setup_,
                                               currentStage_);
        vehicles_->SetControlsLockedCheck([this] { return controlsLocked_; });
        vehicles_->SetMineDetonator([this](const Vec3& position) { projectiles_.DetonateProximityMines(position); });
        render_.SetObjectTick([this](PlacedObject& object) {
            if (object.type == static_cast<uint8_t>(PropDefType::Cctv)) {
                TickCctv(object);
            } else if (object.type == static_cast<uint8_t>(PropDefType::Autogun)) {
                TickAutogun(object);
            } else {
                vehicles_->Tick(object);
            }
        });
        render_.SetObjectPose([this](PlacedObject& object, bool onScreen) {
            if (object.autogun) {
                FireAutogun(object);
            } else {
                vehicles_->Pose(object, onScreen);
            }
        });
    }
    speedFrames_ = 1;
    PropList& props = objects_.Props();
    // The rest of the per-player loop: the player's own state, HUD and watch,
    // and the viewer prop at the start position.
    for (int player = 0; player < playerCount_; player++) {
        SetCurrentPlayer(player);
        PlayerState& state = Cur();
        // init_player_BONDdata: a game of two or more players takes the control
        // style the front-end set for the player.
        if (multiplayer_ && playerCount_ >= 2) {
            state.options.controlType = multiplayer_->controlStyle[static_cast<size_t>(player)];
        }
        // sets_a_bunch_of_BONDdata_values_to_default: the head's animation state.
        state.head = source_.CreateHead();
        state.move.SetHead(state.head.get());
        state.move.Init(state.spawn, level_.StanFile());
        state.move.SetCameraFrozen(false);

        // The HUD, and the watch, which shows the time the intro set
        // (init_watch_at_start_of_stage).
        state.hud.Reset();
        state.hud.SetMultiplayer(playerCount_ >= 2);
        if (hudSource_ != nullptr) {
            state.hud.SetFonts(hudSource_->LoadFont("font/fontBankGothic"),
                               hudSource_->LoadFont("font/fontZurichBold"));
            if (const auto images = hudSource_->LoadHudImages()) {
                if (!state.hud.SetImages(*images) || !effectImages_.Parse(*images)) {
                    error_ = "the HUD image table (hud/images) is malformed";
                    return false;
                }
            }
        }
        state.watch.Attach(&state.move, &state.weapons, &state.hud, source_.Models());
        state.watch.AttachMenu(watchBridge_.get(), &state.options, &state.intro.inventory);
        state.watch.SetCountdownVisible([this](int bits, bool hide) {
            countdownHidden_ = hide ? countdownHidden_ | bits : countdownHidden_ & ~bits;
        });
        state.watch.Reset(state.intro.watchTime);

        // The player's prop, at the start position. It is a viewer prop, with a
        // footprint of its own.
        const int playerProp = props.Allocate();
        if (playerProp >= 0) {
            Prop& prop = props[playerProp];
            prop.type = PropType::Viewer;
            prop.position = state.spawn.position;
            prop.stan = state.spawn.stanTile;
            props.Activate(playerProp);
            props.Enable(playerProp);
            state.move.SetProp(&props, playerProp);
        }
        if (state.weapons.Ready()) {
            // The intro grants each ammo record as it reads the setup, before the
            // player holds anything.
            for (const IntroAmmo& ammo : state.intro.ammo) {
                state.weapons.GiveAmmo(ammo.ammoType, ammo.amount);
            }
        }
        if (playerCount_ == 1) {
            continue;
        }
        // bondviewPlayerBeginLife's share for two or more players: the starting
        // weapons are held from the first tick.
        BeginLife();
        // bondviewSetCameraMode(CAMERAMODE_MP): the stage opens on the
        // multiplayer swirl.
        SetCameraMode(CameraMode::MultiPlayer);
    }
    SetCurrentPlayer(0);
    objects_.SetViewerFootprint([this](int prop, PropCollider* out) {
        const int player = PlayerOfProp(prop);
        // bondviewGetPropHeightRelatedValues: the body of a dead player
        // takes no part in a multiplayer game's collisions.
        if (player < 0 || (playerCount_ >= 2 && players_[static_cast<size_t>(player)].health.IsDead())) {
            return false;
        }
        return players_[static_cast<size_t>(player)].move.Footprint(out);
    });
    InstallPlayerBodyHooks();
    // disableOnscreenCheatText (sets_a_bunch_of_BONDdata_values_to_default is
    // GaitHead::Init, above).
    for (PlayerState& player : players_) {
        player.cheatInput.Clear();
    }
    playerOrder_.Reset();

    // zbufDeallocate and viSetVideoMode(MD_NORMAL) leave the stage-load
    // allocation mode and pick the video mode; the renderer owns the depth
    // buffer and the frame is always 320x240. D_80048368, which the game sets
    // to 1.0 here, has no reader.
    loaded_ = true;
    return true;
}

bool Stage::LoadSetup(LevelId stage, int difficulty) {
    setup_.reset();
    padTiles_ = {};
    propCounts_ = {};
    objectDefs_.clear();
    const char* name = SetupName(stage);
    if (name == nullptr) {
        return true;
    }
    // proplvreset2 loads the setup file and rebases its offsets. Pad
    // positions are scaled by get_room_data_float2, 1 / level scale.
    // The multiplayer stages have no entry of their own in the table: two or
    // more players take the stage's setup with "mp_" after the "U".
    const std::string fileName = std::string(playerCount_ >= 2 ? "Ump_" : "U") + name;
    const auto file = source_.LoadSetup(fileName);
    if (!file) {
        error_ = "no setup file " + fileName;
        return false;
    }
    std::string parseError;
    setup_ = StageSetup::Parse(*file, 1.0f / level_.Level().scale, &parseError);
    if (!setup_) {
        error_ = fileName + ": " + parseError;
        return false;
    }
    padTiles_ = setup_->ResolveStanTiles(level_.StanFile());
    aiLists_.SetLocal(setup_->AiListSection());
    // The counts that size the guard, model and item tables; the model and
    // item tables (modelmgrAllocateModelSlots, alloc_additional_item_slots)
    // have no user yet.
    propCounts_ = setup_->Counts();
    chrs_.AllocateSlots(propCounts_.guards);
    for (size_t i = 0; i < setup_->PropDefs().size(); i++) {
        const PropDef& def = setup_->PropDefs()[i];
        if (IsObjectRecord(def.type) && StageSetup::LoadsObject(def, difficulty, PlayerCount())) {
            objectDefs_.push_back(i);
        }
    }
    // The characters, objects and doors are created in the setup's order, as
    // proplvreset2's pass over the records does: a guard's start position is
    // tested against what exists before it. The types none of them serve yet
    // are skipped.
    const ChrTiming timing{ clock_.globalTimer, clock_.globalTimerDelta, clock_.clockTimer };
    size_t nextObject = 0;
    MpWeaponStocking stocking(multiplayer_ ? multiplayer_->weaponSet : 0,
                              multiplayer_ && multiplayer_->scenario == Game::MpScenario::TheLivingDaylights);
    for (size_t def = 0; def < setup_->PropDefs().size(); def++) {
        const PropDef& record = setup_->PropDefs()[def];
        if (record.Is(PropDefType::Guard)) {
            chrs_.Spawn(*setup_, def, timing, RandomGetNext);
        } else if (record.Is(PropDefType::GuardAttribute)) {
            chrs_.ApplyAttribute(record);
        } else if (nextObject < objectDefs_.size() && objectDefs_[nextObject] == def) {
            nextObject++;
            if (record.Is(PropDefType::Door)) {
                doors_.Create(*setup_, def);
                continue;
            }
            // A weapon no character holds takes the weapon set's place in a
            // multiplayer game, and so does the ammunition of the crate after it.
            // The models weaponLoadProjectileModels and modelLoad bring in for
            // them come from the archive when the object or a thrown weapon is
            // made, so the set needs no loading step.
            bool make = true;
            if (playerCount_ >= 2) {
                if (record.Is(PropDefType::Collectable) && (record.flags & ObjectFlag::AssignedToChr) == 0) {
                    make = stocking.StockWeapon(&setup_->MutablePropDef(def));
                } else if (record.Is(PropDefType::Ammo)) {
                    make = stocking.StockAmmoCrate(&setup_->MutablePropDef(def));
                }
            }
            if (make) {
                objects_.Create(*setup_, def);
            }
        }
    }
    objects_.LinkEmbedded(*setup_);
    doors_.LinkDoors(*setup_);
    links_.Build(*setup_, &objects_, &doors_);
    objectives_.Reset();
    objectives_.Build(*setup_);
    // A vehicle or aircraft runs the AI list its record names.
    for (size_t def = 0; def < setup_->PropDefs().size(); def++) {
        const PropDef& record = setup_->PropDefs()[def];
        if (!record.Is(PropDefType::Vehicle) && !record.Is(PropDefType::Aircraft)) {
            continue;
        }
        if (PlacedObject* object = objects_.ForDef(def)) {
            object->ai.list = aiLists_.FindById(record.objectAiList);
            object->ai.pad = record.pad;
        }
    }
    // alloc_false_GUARDdata_to_exec_global_action.
    chrs_.CreateBackgroundChrs();
    LoadText(stage);
    return true;
}

void Stage::LoadText(LevelId stage) {
    // langInit's banks, and langLoadToAddr's for the stage.
    lang_ = Lang{};
    std::vector<int32_t> banks = { TextBank::Gun,       TextBank::Title,   TextBank::MpMenu, TextBank::PropObj,
                                   TextBank::MpWeapons, TextBank::Options, TextBank::Misc };
    if (const int32_t own = Lang::StageBank(stage); own > 0) {
        banks.push_back(own);
    }
    for (const int32_t bank : banks) {
        if (auto strings = source_.LoadTextBank(Lang::BankResource(bank))) {
            lang_.SetBank(bank, std::move(*strings));
        }
    }
}

void Stage::TickPickups() {
    if (!setup_ || !Cur().weapons.Ready() || clock_.clockTimer == 0 || Cur().health.IsDead()) {
        return;
    }
    // Nothing is picked up from inside a tank, or while the player is invincible.
    if (Cur().move.InTank() || playerInvincible_) {
        return;
    }
    PropList& props = objects_.Props();
    // The newest props first; a pickup takes itself out of the list.
    const std::vector<int> active = props.Active();
    for (auto it = active.rbegin(); it != active.rend(); ++it) {
        const int propId = *it;
        const PropType type = props[propId].type;
        if ((type != PropType::Obj && type != PropType::Weapon) || props[propId].timeToRegen > 0) {
            continue;
        }
        const PlacedObject* object = objects_.ForProp(propId);
        if (object == nullptr) {
            continue;
        }
        const Vec3& position = Cur().move.CollisionPosition();
        if (!objects_.TestPickup(*setup_, *object, position, Cur().move.Tile(), Cur().health.Armour(),
                                 Cur().intro.inventory, Cur().weapons, Cur().weapons.Table(),
                                 Cur().move.Verta() * kDegToRad, ScenarioRefusesArmour())) {
            continue;
        }
        CollectProp(propId);
    }
}

void Stage::CollectProp(int propId) {
    PlacedObject* object = objects_.ForProp(propId);
    if (object == nullptr || !setup_ || !Cur().weapons.Ready()) {
        return;
    }
    std::vector<std::string> messages;
    // A pickup of body armour replaces the armour (bondviewAddCurrentPlayerArmor).
    constexpr float kNoArmourPickup = -1.0f;
    float armour = kNoArmourPickup;
    const PickupOutcome outcome =
        objects_.Collect(*setup_, *object, Cur().weapons.Table(), &Cur().weapons, &Cur().intro.inventory, &armour,
                         playerCount_ == 1 ? SoloAmmoMultiplier(difficulty_) : 1.0f, &lang_, &messages, playerCount_);
    if (armour != kNoArmourPickup) {
        CurStats().bodyArmorPickups += armour;
        Cur().health.SetArmour(armour);
    }
    for (const std::string& message : messages) {
        Cur().hud.BottomShow(message);
    }
    if (outcome == PickupOutcome::Collected) {
        objects_.FreeOrRespawn(propId);
    } else if (outcome == PickupOutcome::Given) {
        Cur().intro.inventory.AddProp(propId, static_cast<int64_t>(object->def));
        objects_.GiveToPlayer(propId, Cur().move.PropId());
    }
}

void Stage::TickAlarm() {
    // handle_alarm_gas_timer_calldamage. The alarm runs out after
    // CHROBJ_GAS_TIMER ticks.
    constexpr int kAlarmTicks = 0x708;
    if (alarmTimer_ > 0) {
        if (alarmSound_ == nullptr && !controlsLocked_) {
            Game::Sound::PlaySfx(AudioDriver::kSfxAlarm3, &alarmSound_);
        }
        alarmTimer_ += clock_.clockTimer;
        if (kAlarmTicks < alarmTimer_) {
            DeactivateAlarm();
        }
    }

    // handle_gas_damage.
    if (gasActivate_) {
        gasTimer_ += clock_.globalTimerDelta;
        if (gasFullOpacityTime_ <= gasTimer_) {
            gasTimer_ = gasFullOpacityTime_;
            gasActivate_ = false;
        }
    }
    if (gasTimer_ > 0.0f && !playerInvincible_) {
        fog_.SwitchToSolosky2(gasTimer_ / gasFullOpacityTime_);
        Cur().camera.SetZRange(fog_.ZNear(), fog_.ZFar());
        if (gasDamages_) {
            constexpr int kCoughInterval = 0xE1;
            if (gasLastCough_ < clock_.globalTimer - kCoughInterval) {
                gasLastCough_ = clock_.globalTimer;
                if (gasTimer_ >= 600.0f) {
                    Game::Sound::PlaySfx(AudioDriver::kSfxCough);
                }
                if (gasTimer_ >= 1800.0f) {
                    RecordPlayerDamage(0.125f, 0.0f, 0.0f, -1, false);
                }
            }
            if (gasLeakTimer_ < gasFullOpacityTime_) {
                gasLeakTimer_ += clock_.globalTimerDelta;
                if (gasSound_ == nullptr && !controlsLocked_) {
                    Game::Sound::PlaySfx(AudioDriver::kSfxGasHiss, &gasSound_);
                }
                if (gasSound_ != nullptr) {
                    Game::Sound::PostVolumeAtPositionDefault(gasSound_, gasLeakSource_);
                }
            } else if (gasSound_ != nullptr && Game::Sound::GetPlayingState(gasSound_) != 0) {
                Game::Sound::Deactivate(gasSound_);
            }
        }
    }

    // if_enabled_reset_clock.
    if (countdownRunning_) {
        countdownTime_ -= clock_.globalTimerDelta;
    }
}

void Stage::ActivateAlarm() {
    if (alarmTimer_ < 1) {
        alarmTimer_ = 1;
    }
}

void Stage::DeactivateAlarm() {
    alarmTimer_ = 0;
    StopAlarmSound();
}

void Stage::StopAlarmSound() {
    if (alarmSound_ != nullptr && Game::Sound::GetPlayingState(alarmSound_) != 0) {
        Game::Sound::Deactivate(alarmSound_);
    }
}

void Stage::StopGasSound() {
    if (gasSound_ != nullptr && Game::Sound::GetPlayingState(gasSound_) != 0) {
        Game::Sound::Deactivate(gasSound_);
    }
}

void Stage::TriggerGas(const Vec3& source) {
    gasActivate_ = true;
    gasLeakTimer_ = 0.0f;
    gasLeakSource_ = source;
    if (currentStage_ == LevelId::Egypt) {
        gasFullOpacityTime_ = 120.0f;
        gasDamages_ = false;
        return;
    }
    gasFullOpacityTime_ = 3600.0f;
    gasDamages_ = true;
}

void Stage::StopAudioForPause() {
    StopAlarmSound();
    StopGasSound();
    Cur().weapons.StopSounds();
    Cur().move.StopTankSounds();
    objects_.StopEngineSounds();
}

void Stage::ResetAiState() {
    // reinit_between_menus: the alarm, toxic gas and countdown.
    Game::Sound::DetachHandle(&alarmSound_);
    Game::Sound::DetachHandle(&gasSound_);
    alarmSound_ = nullptr;
    gasSound_ = nullptr;
    gasLeakTimer_ = 0.0f;
    gasActivate_ = false;
    gasTimer_ = 0.0f;
    gasLastCough_ = 0;
    countdownTime_ = 0.0f;
    countdownRunning_ = false;
    countdownHidden_ = 1;
    mpTicks_ = 0;
    cameraMode_ = CameraMode::None;
    deathCamera_ = DeathCameraState{};
    cutscenePad_ = nullptr;
    cutscene_ = nullptr;
    cameraAfterCinema_ = CameraMode::None;
    cameraTransitionTimer_ = 0.0f;
    introCameraIndex_ = 1;
    cameraFadeActive_ = false;
    // bondviewInit's start: Cuba allocates the camera feed's frame buffer and
    // blacks the screen out until the feed is up.
    cameraBufferToggle_ = false;
    cameraFrameCounter1_ = 0;
    cameraFrameCounter2_ = 0;
    secondFrameBuffer_ = false;
    hiResBuffer_ = currentStage_ == LevelId::Cuba;
    if (hiResBuffer_) {
        cameraFrameCounter1_ = 1;
    }
    for (PlayerState& player : players_) {
        player.hud.ResetCredits();
        player.frozenCamera = false;
        player.cameraTile = kNoTile;
        player.bondChr = -1;
        player.previousButtons = 0;
        player.cheatInvincible = false;
    }
    forcedMove_ = {};
    timerActive_ = true;
    stopTime_ = 0;
    credits_ = 0;
    playerInvincible_ = false;
    visibleToGuards_ = true;
    fastBond_ = false;
    bondKilledInAction_ = false;
    levelEndRequested_ = false;
    exit_ = StageExit::None;
    aborted_ = false;
    missionTimer_ = 0;
    modifiers_ = DifficultyModifiers{};
}

void Stage::SetDifficultyModifiers() {
    // lvlSetMultipliersForDifficulty. The values are the lv.h defaults of each
    // difficulty.
    DifficultyModifiers& m = modifiers_;
    switch (difficulty_) {
        case 0: {
            const float armourDiff = Cur().health.Health() + Cur().health.Armour();
            float damageMultiplier = 1.0f;
            if (armourDiff <= 0.125f) {
                damageMultiplier = 0.5f;
            } else if (armourDiff <= 0.6f) {
                damageMultiplier = (((armourDiff - 0.125f) * 0.5f) / 0.47500002f) + 0.5f;
            }
            m.cctvAlarmScale = 2.0f;
            m.unnamed0x80030B18 = 2.0f;
            m.autogunPendingDamageTick = 0.5f * damageMultiplier;
            m.autogunDamageScalar = 0.5f * damageMultiplier;
            m.unnamed0x80030B24 = 2.0f;
            m.aiAccuracy = 0.6f;
            m.aiDamage = 0.5f * damageMultiplier;
            m.aiHealth = 2.0f;
            m.explosionDamage = 0.25f * damageMultiplier;
            m.difficulty = 1.5f;
            m.soloAmmo = 2.0f;
            m.aiReactionSpeed = 0.2f;
            break;
        }
        case 1:
            m.cctvAlarmScale = 1.0f;
            m.unnamed0x80030B18 = 1.0f;
            m.autogunPendingDamageTick = 0.75f;
            m.autogunDamageScalar = 0.75f;
            m.unnamed0x80030B24 = 1.0f;
            m.aiAccuracy = 0.75f;
            m.aiDamage = 0.75f;
            m.aiHealth = 1.0f;
            m.explosionDamage = 0.75f;
            m.difficulty = 0.75f;
            m.soloAmmo = 1.5f;
            m.aiReactionSpeed = 0.5f;
            break;
        case 2:
            m.cctvAlarmScale = 1.0f;
            m.unnamed0x80030B18 = 1.0f;
            m.autogunPendingDamageTick = 1.0f;
            m.autogunDamageScalar = 1.0f;
            m.unnamed0x80030B24 = 1.0f;
            m.aiAccuracy = 1.0f;
            m.aiDamage = 1.0f;
            m.aiHealth = 1.0f;
            m.explosionDamage = 1.0f;
            m.difficulty = 0.2f;
            m.soloAmmo = 1.0f;
            m.aiReactionSpeed = 1.0f;
            break;
        case 3:
            m.cctvAlarmScale = 1.0f;
            m.unnamed0x80030B18 = 1.0f;
            m.autogunPendingDamageTick = 1.0f;
            m.autogunDamageScalar = 1.0f;
            m.unnamed0x80030B24 = 1.0f;
            m.aiAccuracy = 1.0f;
            m.aiDamage = 1.0f;
            m.aiHealth = 1.0f;
            m.explosionDamage = 1.0f;
            m.difficulty = 1.0f;
            m.soloAmmo = 1.0f;
            m.aiReactionSpeed = 1.0f;
            break;
        default:
            break;
    }
}

void Stage::TickProps() {
    // chrpropTick: chrlvAllChrTick ticks the stage's background AI.
    if (ai_) {
        ai_->AllChrTick();
    }
    PropList& props = objects_.Props();
    // The newest props first, carrying on from the prop before the one ticked
    // (a prop an explosion adds joins the start of the list).
    for (int prop = props.ActiveTail(); prop >= 0;) {
        const int before = props.ActiveBefore(prop);
        const Prop& record = props[prop];
        bool release = false;
        switch (record.type) {
            case PropType::Chr:
                // The characters' bullet tracers.
                if (record.object >= 0) {
                    Chr& chr = chrs_[record.object];
                    for (BeamRecord& beam : chr.beams) {
                        AdvanceBeam(&beam, clock_.clockTimer, clock_.globalTimerDelta, RandomGetNext);
                    }
                }
                break;
            case PropType::Obj:
            case PropType::Weapon:
            case PropType::Door:
                if (record.timeToRegen > 0 && record.type != PropType::Door && Cur().move.PropId() >= 0) {
                    // A respawning object comes back unless a player stands within
                    // 400 units (chrpropIsFarFromPlayers).
                    bool near = false;
                    for (int player = 0; player < playerCount_ && !near; player++) {
                        const int playerPropId = players_[static_cast<size_t>(player)].move.PropId();
                        if (playerPropId < 0) {
                            continue;
                        }
                        const Prop& playerProp = objects_.Props()[playerPropId];
                        const float dx = playerProp.position[0] - record.position[0];
                        const float dy = playerProp.position[1] - record.position[1];
                        const float dz = playerProp.position[2] - record.position[2];
                        near = std::sqrt((dx * dx) + (dy * dy) + (dz * dz)) < 400.0f;
                    }
                    objects_.TickRegen(prop, clock_.clockTimer, near);
                }
                // The autogun's bullet tracer.
                if (record.object >= 0 && record.type == PropType::Obj) {
                    if (PlacedObject* gun = objects_.ForProp(prop); gun != nullptr && gun->autogun) {
                        AdvanceBeam(&gun->autogun->beam, clock_.clockTimer, clock_.globalTimerDelta, RandomGetNext);
                    }
                }
                break;
            case PropType::Explosion:
                release = explosions_.TickExplosion(prop, clock_.clockTimer, RandomGetNext);
                break;
            case PropType::Smoke:
                release = explosions_.TickSmoke(prop, clock_.clockTimer, RandomGetNext);
                break;
            case PropType::Viewer:
                if (const int player = PlayerOfProp(prop); player >= 0) {
                    PlayerWeapons& weapons = players_[static_cast<size_t>(player)].weapons;
                    if (weapons.Ready()) {
                        for (const Hand hand : { HandRight, HandLeft }) {
                            AdvanceBeam(&weapons.MutableHand(hand).beam, clock_.clockTimer, clock_.globalTimerDelta,
                                        RandomGetNext);
                        }
                    }
                    // The bullet tracers of a multiplayer character.
                    if (record.object >= 0 && playerCount_ >= 2) {
                        for (BeamRecord& beam : chrs_[record.object].beams) {
                            AdvanceBeam(&beam, clock_.clockTimer, clock_.globalTimerDelta, RandomGetNext);
                        }
                    }
                }
                break;
            default:
                break;
        }

        const int next = props.IsActive(prop) ? props.ActiveBefore(prop) : before;
        if (release) {
            // propExecuteTickOperation(TICKOP_FREE).
            explosions_.FreeProp(prop);
        }
        prop = next;
    }
}

InteractScene Stage::MakeInteractScene() {
    InteractScene scene;
    scene.setup = &*setup_;
    scene.stan = &level_.StanFile();
    scene.objects = &objects_;
    scene.doors = &doors_;
    return scene;
}

InteractPlayer Stage::MakeInteractPlayer() const {
    InteractPlayer player;
    player.prop = Cur().move.PropId();
    const Prop& record = objects_.Props()[Cur().move.PropId()];
    player.position = record.position;
    player.tile = record.stan;
    // bondviewGetPlayerYawRadians: DegToRad(360.0f - vv_theta).
    player.yaw = ((360.0f - Cur().move.Theta()) * 6.2831855f) / 360.0f;
    player.look = Cur().move.LookDirection();
    return player;
}

bool Stage::UseProp() {
    if (!setup_ || Cur().move.PropId() < 0) {
        return true;
    }
    const InteractScene scene = MakeInteractScene();
    const InteractPlayer player = MakeInteractPlayer();
    // g_LastOnScreenProp - 1 down to g_OnScreenPropList.
    const std::vector<int>& farFirst = render_.OnScreenProps();
    const std::vector<int> nearFirst(farFirst.rbegin(), farFirst.rend());
    const int prop = Interact::Find(scene, player, nearFirst);
    if (prop < 0) {
        return true;
    }
    const PropType type = objects_.Props()[prop].type;
    if (type == PropType::Obj || type == PropType::Weapon) {
        UseObject(prop);
    } else if (type == PropType::Door) {
        std::string message;
        Interact::UseDoor(scene, player, prop, Cur().intro.inventory, lang_, &message);
        if (!message.empty()) {
            Cur().hud.BottomShow(message);
        }
    }
    return false;
}

void Stage::UseObject(int prop) {
    PlacedObject* object = objects_.ForProp(prop);
    if (object == nullptr) {
        return;
    }
    if (object->type == static_cast<uint8_t>(PropDefType::Alarm)) {
        Game::Sound::PlaySfx(AudioDriver::kSfxAlarmSwitch);
        if (alarmTimer_ > 0) {
            DeactivateAlarm();
        } else {
            ActivateAlarm();
        }
    }
    object->runtimeFlags |= ObjectRuntime::Activated;
    // sub_GAME_7F03E6A0 runs before the pickup frees the prop.
    links_.TriggerSwitches(prop, &doors_);
    if ((object->flags & ObjectFlag::CollectByInteractOnly) != 0) {
        CollectProp(prop);
    }
}

void Stage::Unload() {
    // lvlUnloadStageTextData: the objectives' cleanup and cleanup_rooms. The
    // guards, objects and alarms are cleared with the stage the next load makes.
    // cleanupObjectSounds and set_missionstate_zero.
    aiSounds_->Cleanup();
    // cleanupObjects' objFree of every door and object, cleanupAlarms,
    // cleanupSFXRelated and cleanupplayersoundrelated.
    doors_.Cleanup();
    objects_.ReleaseSounds();
    DeactivateAlarm();
    StopGasSound();
    Game::Sound::DetachHandle(&alarmSound_);
    Game::Sound::DetachHandle(&gasSound_);
    alarmSound_ = nullptr;
    gasSound_ = nullptr;
    for (PlayerState& player : players_) {
        player.move.StopTankSounds();
        player.move.DetachTankSounds();
        player.weapons.StopSounds();
    }
    music_.Stop();
    // lvlUnloadStageTextData: the alarm stops, and set_favorite_weapon_for_every_player
    // keeps each player's weapon of choice for the next screen.
    if (mpAlarmSound_ != nullptr && Game::Sound::GetPlayingState(mpAlarmSound_) != 0) {
        Game::Sound::Deactivate(mpAlarmSound_);
    }
    Game::Sound::DetachHandle(&mpAlarmSound_);
    mpAlarmSound_ = nullptr;
    if (favouriteWeapons_ != nullptr) {
        favouriteWeapons_->StoreForEveryPlayer(playerCount_, [this](int player) {
            const PlayerState& state = players_[static_cast<size_t>(player)];
            return state.weapons.Ready() ? state.weapons.WeaponOfChoice() : std::array<int32_t, 2>{};
        });
    }
    DisableAllCheats();
    Game::Sound::ClearListenerSource(this);
    objectives_.Cleanup();
    explosions_.Clear();
    shards_.Clear();
    level_.StanFile().SetProps(nullptr);
    level_.Cleanup();
    loaded_ = false;
}

std::vector<MonitorScreen>* Stage::MonitorScreensOf(PlacedObject& object) {
    if (!monitorLists_) {
        return nullptr;
    }
    if (object.monitor.screens.empty()) {
        const size_t count = object.type == static_cast<uint8_t>(PropDefType::Monitor) ? 1 : 4;
        for (size_t slot = 0; slot < count; slot++) {
            object.monitor.screens.push_back(MonitorScreen::Make(*monitorLists_, object.monitor.numbers[slot]));
        }
    }
    return &object.monitor.screens;
}

void Stage::TickMonitor(PlacedObject& object, uint32_t screens) {
    std::vector<MonitorScreen>* all = MonitorScreensOf(object);
    if (all == nullptr) {
        return;
    }
    for (size_t slot = 0; slot < all->size(); slot++) {
        if ((screens & (1u << slot)) != 0) {
            (*all)[slot].Advance(*monitorLists_, clock_.clockTimer, clock_.globalTimerDelta, RandomGetNext);
        }
    }
}

void Stage::BlackenMonitor(PlacedObject& object) {
    std::vector<MonitorScreen>* all = MonitorScreensOf(object);
    if (all == nullptr) {
        return;
    }
    for (MonitorScreen& screen : *all) {
        screen.StartList(monitorLists_->ListNamed("monAnim33BlackSolid"));
    }
}

void Stage::TickCctv(PlacedObject& object) {
    if (!object.cctv || (object.flags & ObjectFlag::DroneGun) != 0 || Cur().move.PropId() < 0 || object.prop < 0) {
        return;
    }
    CctvState& camera = *object.cctv;
    const Prop& playerProp = objects_.Props()[Cur().move.PropId()];
    const Vec3 toPlayer = { playerProp.position[0] - object.runtimePosition[0],
                            playerProp.position[1] - object.runtimePosition[1],
                            playerProp.position[2] - object.runtimePosition[2] };

    if (CctvInView(camera, toPlayer, (object.flags & ObjectFlag::InMotion) != 0)) {
        const Prop& mine = objects_.Props()[object.prop];
        int tile = mine.stan;
        // bondviewUpdateGuardTankFlagsRelated takes the player out of the line
        // test and back.
        Cur().move.SetCollides(false);
        const bool clear =
            level_.StanFile().TestLineUnobstructed(&tile, mine.position[0], mine.position[2], playerProp.position[0],
                                                   playerProp.position[2], 0x1B, 100.0f, 100.0f, 0.0f, 1.0f) != 0;
        if (clear) {
            camera.timer += clock_.clockTimer;
            // CCTV_ALARM_FRAMES (300 on NTSC) scaled by the difficulty; alarmActivate.
            if (camera.timer >= static_cast<int32_t>(300.0f * modifiers_.cctvAlarmScale)) {
                ActivateAlarm();
                camera.timer = 0;
            }
        }
        Cur().move.SetCollides(true);
    }

    SwingCctv(&camera, clock_.globalTimerDelta);
}

void Stage::Tick() {
    chrs_.SetTiming({ clock_.globalTimer, clock_.globalTimerDelta, clock_.clockTimer, clock_.frame });
    // lvlManageMpGame: the first tick the clock runs switches the other cheats
    // on, before the ticks it counts include it.
    if (clock_.clockTimer > 0 && mpTicks_ == 0) {
        TurnOnActivatedCheats(false);
    }
    if (playerCount_ >= 2) {
        ManageMultiplayerGame(mpTicks_);
    }
    // lvlManageMpGame's clocks.
    mpTicks_ += clock_.clockTimer;
    powerTicks_ += clock_.clockTimer;
    // The stage time stops counting once no button has gone down for 30 seconds.
    constexpr int kIdleTicks = 0x708;
    if (buttonsPressed_ != 0) {
        idle_ = false;
        idleTicks_ = 0;
    } else {
        idleTicks_ += clock_.clockTimer;
        if (idleTicks_ >= kIdleTicks) {
            idle_ = true;
        }
    }
    if (idle_) {
        idleTotal_ += clock_.clockTimer;
    } else {
        clock_.stageTicks += clock_.clockTimer;
        clock_.stageTimeSec = static_cast<float>(clock_.stageTicks) / kTicksPerSecond;
    }

    // updateRoomStatusFlags
    deform_.Tick();
    render_.Tick();
    sky_.Tick(clock_.clockTimer);
    sparks_.Update(clock_.clockTimer);
    casings_.Update(clock_.clockTimer, clock_.globalTimerDelta);
    shards_.Update(clock_.clockTimer);
    explosions_.UpdateParticles(clock_.clockTimer, RandomGetNext);
    SetDifficultyModifiers();
    TickProps();
    music_.TickSlots(clock_.clockTimer);

    // shuffle_player_ids runs once a frame after lvlManageMpGame: three
    // draws, whatever the number of players.
    playerOrder_.Shuffle(RandomGetNext);
}

int Stage::PositionInShuffled(int player) const {
    return playerOrder_.PositionOf(player, playerCount_);
}

int Stage::NthPlayerShuffled(int n) const {
    return playerOrder_.Nth(n, playerCount_);
}

void Stage::TickLive(const Platform::Input::State& pads, int controllers) {
    SetClockTimer(1);
    SetButtonsPressed(BuildPlayerMoveInput(pads, 0, playerCount_, controllers).pressed);
    Tick();
    MovePlayers([&](int player) { return BuildPlayerMoveInput(pads, player, playerCount_, controllers); });
}

void Stage::MovePlayers(const std::function<MoveInput(int player)>& inputFor) {
    for (int i = 0; i < playerCount_; i++) {
        const int player = NthPlayerShuffled(i);
        SetCurrentPlayer(player);
        MovePlayer(inputFor(player));
    }
    SetCurrentPlayer(0);
}

void Stage::MovePlayer(const MoveInput& input) {
    if (seedPreviousButtons_) {
        seedPreviousButtons_ = false;
        Cur().previousButtons = input.buttons;
    }
    // lvlViewMoveTick starts with cheat_buttons_mp_related.
    TickCheatInput(input);
    // A stage whose start position found no tile has nothing to walk on.
    if (Cur().move.Tile() == kNoTile) {
        return;
    }
    // bondviewMovePlayerUpdateViewport: the view, and what the options show of
    // the HUD, ahead of MoveBond.
    UpdateViewport();
    Cur().colour.Update(clock_.globalTimerDelta);
    if (const int alpha = Cur().colour.TickBodyFade(clock_.globalTimerDelta); alpha >= 0 && Cur().bondChr >= 0) {
        chrs_[Cur().bondChr].fadeAlpha = static_cast<uint8_t>(alpha);
    }
    // currentPlayerSetYAutoAimEnabled and ...XAutoAimEnabled.
    // currentPlayerGetYAutoAimEnabled and gunSetSightVisible: one player's
    // options decide both, a game of two or more takes the aim setting.
    Cur().autoAimEnabled = playerCount_ == 1 ? Cur().options.autoAim : CurStats().autoaim != 0;
    Cur().hud.SetAmmoVisible(AmmoReason::Option, Cur().options.ammoOnScreen);
    Cur().hud.SetSightVisible(SightReason::Option,
                              playerCount_ == 1 ? Cur().options.sightOnScreen : CurStats().sight != 0);
    Cur().watch.SetTiming({ speedFrames_, clock_.clockTimer, clock_.globalTimerDelta });
    Cur().watch.SetInput(input);

    // mission_timer runs while the player is in play, and the player moves
    // under MoveBond; every other mode moves him under bondviewFrozenMoveBond.
    const bool playing = cameraMode_ == CameraMode::None || (cameraMode_ == CameraMode::FirstPerson && timerActive_) ||
                         cameraMode_ == CameraMode::FadeToTitle;
    if (playing && current_ == 0) {
        missionTimer_ += clock_.clockTimer;
    }
    const bool frozenMove = !playing;

    MoveContext context;
    context.clockTimer = clock_.clockTimer;
    context.globalTimerDelta = clock_.globalTimerDelta;
    // bondviewMovePlayerUpdateViewport starts every tick with
    // viSetFovY(FOV_Y_F); the zoom update inside the tick then moves it to the
    // zoom reached this tick.
    Cur().camera.SetViFovY(kFovY);
    context.fovY = Cur().camera.ViFovY();
    context.setFovY = [this](float fovY) { Cur().camera.SetViFovY(fovY); };
    context.perspectiveHeight = CurStats().perspectiveHeight;
    context.fastBond = fastBond_;
    context.controlsLocked = controlsLocked_;
    context.multiplayer = playerCount_ >= 2;
    context.padStopped = !PadMovesPlayer();
    context.menuOn = Cur().menu.on;
    context.paused = paused_;
    // disablePlayerActionsWhenPausedOrInMpMenu is false with the game stopped
    // or the player's menu open.
    context.actionsDisabled = playerCount_ >= 2 && (stopPlay_ || Cur().menu.on);
    context.options = Cur().options;
    context.random = RandomGetNext;
    context.weaponDisablesCrouch = Cur().weapons.Ready() && Cur().weapons.RightHandHasFlag(WeaponFlag::DisableCrouch);
    context.dead = Cur().health.IsDead();
    context.forceDisarm = forceDisarm_ > 0;
    context.damageShowing = playerCount_ == 1 && Cur().health.DamageShowing();
    context.autoAimEnabled = Cur().autoAimEnabled;
    context.tank = tankBridge_.get();
    context.forcedMove = forcedMove_;
    context.stopAudioForPause = [this] { StopAudioForPause(); };
    context.sightPitch = Cur().sightPitch;
    context.autoAimTargetX = Cur().autoAim.HasTargetX();
    context.autoAimTargetY = Cur().autoAim.HasTargetY();
    context.autoAimX = Cur().autoAim.X();
    context.autoAimY = Cur().autoAim.Y();
    if (Cur().weapons.Ready()) {
        context.weapons = &Cur().weapons;
        context.gunHooks = gunBridge_.get();
        context.gunView = MakeGunFrame(nullptr).view;
        context.watch = &Cur().watch;
    }
    context.canCycleWeapons = !(Scenario() == Game::MpScenario::TheLivingDaylights && AliveWithFlag());

    if (frozenMove) {
        // bondviewFrozenMoveBond.
        TickPlayerDamage();
        TickPlayerExplode();
        context.dead = Cur().health.IsDead();
        context.forceDisarm = forceDisarm_ > 0;
        context.damageShowing = playerCount_ == 1 && Cur().health.DamageShowing();

        Cur().move.TickFrozen(context, input, level_.StanFile());
        Cur().frozenCamera = cameraMode_ == CameraMode::Intro || cameraMode_ == CameraMode::FadeSwirl ||
                             cameraMode_ == CameraMode::Swirl || cameraMode_ == CameraMode::MultiPlayer ||
                             cameraMode_ == CameraMode::PosEnd || DeathCameraShowing();
        if (Cur().frozenCamera) {
            SetFrozenCamera(FrozenCameraTick(input));
        }
        Cur().move.SetCameraFrozen(Cur().frozenCamera);
    } else {
        // MoveBond's steps ahead of the input: the disarm the intro asks for,
        // the display of a hit and the explosions around the player.
        if (forceDisarm_ > 0) {
            forceDisarm_++;
            if (forceDisarm_ >= 4) {
                forceDisarm_ = 0;
                if (Cur().weapons.Ready()) {
                    Cur().weapons.UnlockHandModels();
                    Cur().weapons.UnEquipWeapon(HandLeft, Cur().weapons.WeaponId(HandLeft));
                    Cur().weapons.UnEquipWeapon(HandRight, Cur().weapons.WeaponId(HandRight));
                }
            }
        }
        Cur().frozenCamera = false;
        Cur().move.SetCameraFrozen(false);
        TickPlayerDamage();
        TickPlayerExplode();
        context.dead = Cur().health.IsDead();
        context.forceDisarm = forceDisarm_ > 0;
        context.damageShowing = playerCount_ == 1 && Cur().health.DamageShowing();

        Cur().move.Tick(context, input, level_.StanFile());
        // The watch's own tick ends its zoom update with viSetFovY(zoominfovy)
        // too.
        Cur().camera.SetViFovY(Cur().watch.ZoomFovY());
    }
    if (Cur().move.Tile() != kNoTile && setup_) {
        objectives_.CheckRoomEntered(level_.StanFile().TileRoom(Cur().move.Tile()), *setup_, level_.StanFile());
    }
    // gunTickGameplay's share of the floor shade the gun is lit with.
    if (Cur().weapons.Ready() && Cur().move.Tile() != kNoTile) {
        Cur().weapons.TickShading(ShadeFromTile(level_.StanFile().Tile(Cur().move.Tile())));
    }
    // The watch's tick sets whether the controls are locked (which the next
    // frame's clock reads); the crosshair shows while the player is aiming.
    // The watch never opens in a game of two or more, whose lock is
    // pauseAndLockControls.
    if (playerCount_ == 1) {
        controlsLocked_ = Cur().watch.ControlsLocked();
    }
    Cur().hud.SetSightVisible(SightReason::NotAiming, Cur().move.LastMove().aiming);
    Cur().watch.AdvanceTime();

    // mpwatchMenuTick.
    TickMultiplayerMenu(input);

    // The distance the player covered this tick, for the stats.
    const Vec3& position = Cur().move.CollisionPosition();
    const Vec3& previous = Cur().move.PreviousPosition();
    const float dx = position[0] - previous[0];
    const float dz = position[2] - previous[2];
    CurStats().distanceTraveled += std::sqrt((dx * dx) + (dz * dz));
    TickScenarioItem();
    TickExits(input);
}

bool Stage::Ratio16x9Stretch() const {
    // The option widens the projection into a 4:3 frame that a widescreen TV
    // then stretches back. A display that is already wider shows the widened
    // world itself, so the stretch would be applied twice.
    return Cur().options.ratio16x9 && displayWiden_ == 1.0f;
}

void Stage::UpdateViewport() {
    // bondviewGetCurrentPlayerViewportWidth, ...Height, ...Uly and
    // get_curplayer_viewport_ulx: the player's share of the screen. For one
    // player the letterboxed screen settings open up as the arm comes up for
    // the watch.
    if (cameraFrameCounter1_ != 0) {
        if (cameraFrameCounter1_ >= 4 && hiResBuffer_ && secondFrameBuffer_) {
            cameraBufferToggle_ = true;
            cameraFrameCounter1_ = 0;
        } else {
            cameraFrameCounter1_++;
        }
    } else if (cameraFrameCounter2_ != 0) {
        if (cameraFrameCounter2_ >= 4 && !secondFrameBuffer_) {
            cameraBufferToggle_ = false;
            cameraFrameCounter2_ = 0;
        } else {
            cameraFrameCounter2_++;
        }
    }
    secondFrameBuffer_ = !secondFrameBuffer_;
    const ViewMetrics vi = PlayerViewMetrics(playerCount_, current_, Cur().options.screen,
                                             Cur().watch.PauseAnimationPercent(), cameraBufferToggle_);
    Cur().camera.SetView(vi);
    const float ratio = static_cast<float>(vi.viewWidth) / static_cast<float>(vi.viewHeight);
    Cur().aspect = Ratio16x9Stretch() ? ratio * 0.75f * (16.0f / 9.0f) : ratio;
}

SkyView Stage::MakeSkyView() const {
    SkyView view;
    view.view = Cur().camera.Metrics();
    view.wide = Cur().camera.WideMetrics(displayWiden_);
    view.player = Cur().camera.Player(displayWiden_);
    view.halfWidth = Cur().camera.ScreenWidth() * 0.5f;
    view.halfHeight = Cur().camera.ScreenHeight() * 0.5f;
    view.scaleX = Cur().camera.ScaleX();
    view.scaleY = Cur().camera.ScaleY();
    view.eye = Cur().camera.WorldPosition();
    view.viewToWorld = Cur().camera.ViewToWorld();
    std::memcpy(view.projection.m, Cur().camera.ProjectionF(), sizeof(view.projection.m));
    std::memcpy(view.worldToScreen.m, Cur().camera.WorldToScreen(), sizeof(view.worldToScreen.m));
    view.levelScale = level_.Level().scale;
    view.playerCount = PlayerCount();
    return view;
}

HudFrame Stage::MakeHudFrame(const PlayerPose& pose) const {
    HudFrame frame;
    frame.vi = Cur().camera.Metrics();
    frame.ratio16x9 = Ratio16x9Stretch();
    frame.widen = displayWiden_;
    frame.clockTimer = clock_.clockTimer;
    frame.watchState = Cur().watch.State();
    frame.countdownTime = countdownTime_;
    frame.countdownShown = countdownHidden_ == 0;
    frame.introCamera = pose.frozen;
    if (currentStage_ == LevelId::Cuba && credits_ == 1 && setup_) {
        frame.credits = setup_->Credits();
        frame.lang = &lang_;
    }
    frame.visibilityScale = level_.VisibilityScale();
    frame.projection = &Cur().camera.Projection();
    frame.weapons = Cur().weapons.Ready() ? &Cur().weapons : nullptr;
    frame.colourRed = Cur().colour.Red();
    frame.colourGreen = Cur().colour.Green();
    frame.colourBlue = Cur().colour.Blue();
    frame.colourFrac = Cur().colour.Frac();
    frame.blood = Cur().bloodOverlay ? &*Cur().blood : nullptr;
    frame.mpMenuOn = Cur().menu.on;
    frame.gameOver = gameOver_ != 0;
    frame.playerCount = playerCount_;
    frame.playerNumber = current_;
    frame.cameraBuffer = cameraBufferToggle_;
    frame.cameraBlackout = cameraFrameCounter1_ != 0 || cameraFrameCounter2_ != 0;
    return frame;
}

RadarFrame Stage::MakeRadarFrame() const {
    using Game::MpScenario;
    RadarFrame frame;
    frame.playerCount = playerCount_;
    frame.player = current_;
    frame.theta = Cur().move.Theta();
    frame.menuOn = Cur().menu.on;
    frame.disabled = CheatActive(Game::Menu::Cheat::kNoRadarMp);
    switch (Scenario()) {
        case MpScenario::TwoVsTwo:
        case MpScenario::ThreeVsOne:
        case MpScenario::TwoVsOne:
        case MpScenario::TheLivingDaylights:
        case MpScenario::TheManWithTheGoldenGun:
            frame.teamColours = true;
            break;
        default:
            break;
    }
    for (int player = 0; player < playerCount_; player++) {
        const PlayerState& state = players_[static_cast<size_t>(player)];
        RadarPlayer& out = frame.players[static_cast<size_t>(player)];
        if (state.move.PropId() >= 0) {
            const Prop& prop = objects_.Props()[state.move.PropId()];
            out.x = prop.position[0];
            out.z = prop.position[2];
        }
        out.dead = state.health.IsDead();
        out.haveTokenOrGoldenGun = stats_[static_cast<size_t>(player)].haveTokenOrGoldenGun;
    }
    return frame;
}

GunFrame Stage::MakeGunFrame(const LookAt* lookAt) {
    GunFrame frame;
    frame.clockTimer = clock_.clockTimer;
    frame.globalTimerDelta = clock_.globalTimerDelta;
    frame.duckingHeightOffset = Cur().move.DuckingHeightOffset();
    frame.view = { Cur().camera.ScreenLeft(),   Cur().camera.ScreenTop(), Cur().camera.ScreenWidth(),
                   Cur().camera.ScreenHeight(), Cur().camera.ScaleX(),    Cur().camera.ScaleY() };
    frame.wideScreen = Cur().options.WideScreen();
    // The intro's cuff record sets the Bond type; CUFF_BLUE is the default.
    frame.bondType = Cur().intro.bondType.value_or(0);
    frame.lodScaleZ = Cur().camera.LodScaleZ();
    frame.lookAt = lookAt;
    frame.viewToWorld = Cur().camera.ViewToWorld();
    std::memcpy(frame.worldToView.m, Cur().camera.WorldToScreen(), sizeof(frame.worldToView.m));
    frame.floorHeight = Cur().move.StanHeight();
    frame.playerCount = playerCount_;
    frame.casings = &casings_;
    frame.previousPosition = Cur().move.PreviousPosition();
    frame.launcher = &launcher_;
    if (playerCount_ >= 2 && Cur().bondChr >= 0) {
        frame.characterBeams = &chrs_[Cur().bondChr].beams;
        frame.characterMuzzle = &Cur().characterMuzzle;
    }
    if (const int tank = Cur().move.DrivenTank();
        tank >= 0 && (objects_.Props()[tank].flags & PropFlag::OnScreen) != 0) {
        PlacedObject* object = objects_.ForProp(tank);
        const TankGeometry* geometry = object != nullptr ? objects_.TankGeometryOf(*object) : nullptr;
        if (geometry != nullptr) {
            frame.tankAimed = true;
            frame.tankAim = Cur().move.TankAimDirection();
            Mtxf placement = object->mtx;
            for (size_t axis = 0; axis < 3; axis++) {
                placement.m[3][axis] = object->runtimePosition[axis];
            }
            const std::array<Mtxf, 5> pose = TankPose(*geometry, object->tank, placement);
            frame.tankMuzzle = { pose[4].m[3][0], pose[4].m[3][1], pose[4].m[3][2] };
        }
    }
    return frame;
}

Gfx* Stage::Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    // lvlRender's loop: each player's pass in the order the shuffle left, each
    // in the view of their own. The current player is player 0 again when it
    // returns.
    for (int i = 0; i < playerCount_; i++) {
        SetCurrentPlayer(NthPlayerShuffled(i));
        dl = RenderPass(dl, resourceManager, CameraPose(), i == 0);
    }
    SetCurrentPlayer(0);
    return FinishFrame(dl);
}

Gfx* Stage::Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const PlayerPose& pose) {
    return FinishFrame(RenderPass(dl, resourceManager, pose, true));
}

Gfx* Stage::FinishFrame(Gfx* dl) {
    // The video interface runs once a frame, whatever the number of views.
    screenShakeOffset_ = explosions_.Video().Advance(clock_.clockTimer);

    using namespace Frigate::F3dex;
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, kScreenWidth, kScreenHeight);
    // boss.c's tail of the frame: the line mode cheat visualises the coverage
    // value the edges' anti-aliasing uses.
    if (lineMode_) {
        gDPPipeSync(dl++);
        gDPSetCycleType(dl++, G_CYC_1CYCLE);
        gDPSetBlendColor(dl++, 0xFF, 0xFF, 0xFF, 0xFF);
        gDPSetPrimDepth(dl++, 0xFFFF, 0xFFFF);
        gDPSetDepthSource(dl++, G_ZS_PRIM);
        gDPSetRenderMode(dl++, G_RM_VISCVG, G_RM_VISCVG2);
        gDPFillRectangle(dl++, 0, 0, kScreenWidth - 1, kScreenHeight - 1);
    }
    return dl;
}

Gfx* Stage::RenderPass(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const PlayerPose& pose,
                       bool firstPass) {
    // gSPSegment for the font display list and gSPClipRatio(FRUSTRATIO_2) have
    // no counterpart: text is drawn from resources and Fast3D clips on its own.
    // Every pass starts from the pipeline setup the frame does, not from
    // whatever the previous player's HUD left.
    dl = Game::Menu::FastPipelineSetup(dl);
    dl = Game::Menu::ZBufferGeometry(dl);

    // viSetViewSize and viSetViewPosition are the camera's own view, set when
    // the player moved. viSetFovY(g_CurrentPlayer->fovy) and viSetAspect: the
    // zoom the watch or the sniper rifle set this tick.
    Cur().camera.SetWiden(displayWiden_);
    Cur().camera.SetFovAspect(Cur().watch.ZoomFovY(), Cur().aspect);
    // The view's 2D belongs in the middle of the view the display stretches it
    // to (gfx/Rect2D.h).
    const ViewMetrics& screen = Cur().camera.Metrics();
    const Display::Shift2DScope shift2D(
        Display::ViewShift2D(screen.viewLeft, screen.viewWidth, screen.x, displayWiden_));
    dl = ClearPlayerZBuffer(dl, playerCount_, current_, hiResBuffer_, cameraBufferToggle_);
    dl = Cur().camera.SetupView(dl);
    dl = SelectFramebuffer(dl, cameraBufferToggle_);
    // bondviewRenderDebugBondView: a cinematic camera is not shaken, and a dead
    // player's view is still.
    if (!pose.frozen) {
        if (!Cur().health.IsDead()) {
            explosions_.ScreenShake(pose.position, pose.look);
        } else {
            explosions_.Video().Shake(0.0f);
        }
    }
    Cur().camera.Update(pose, level_);
    if (Cur().weapons.Ready()) {
        // bondviewRenderDebugBondView: the pitch the sight points at, which the
        // tank's gun follows on the next move.
        const Vec3 sight = shots_.SightDirection();
        const float flat = std::sqrt((sight[2] * sight[2]) + (sight[0] * sight[0]));
        Cur().sightPitch = GameAtan2f(sight[1], flat) + (Cur().move.Verta() * kDegToRad);
        if (Cur().sightPitch >= 3.1415927f) {
            Cur().sightPitch -= 6.2831855f;
        }
        // field_2A0C: the heading the sight points at. A player's sight is
        // worked out in their own pass, which the bodies other players see
        // them by read.
        Cur().sightYaw = GameAtan2f(-sight[0], -sight[2]);
        if (Cur().sightYaw >= 3.1415927f) {
            Cur().sightYaw -= 6.2831855f;
        }
    }
    const int position = PositionInShuffled(current_);
    dl = FillUncoveredScreen(dl, Cur().camera.Metrics(), playerCount_, position);
    SkyView skyView = MakeSkyView();
    skyView.newFrame = firstPass;
    dl = sky_.Render(dl, resourceManager, skyView, fog_);
    sky_.UpdateRoomWater(resourceManager);
    // Whatever skyRender leaves the pipeline in, the rooms draw from.
    dl = Game::Menu::FastPipelineSetup(dl);
    dl = Game::Menu::ZBufferGeometry(dl);

    // bgRoomVisibilityRelated
    visibility_.Update(Cur().camera.Visibility(pose), pose.previousPosition, pose.cameraMode, position == 0);
    // propsTick's per-prop tick (chrTick, the doors' objTick and the embedded
    // objects' matrices) runs inside the on-screen list below.
    chrs_.SetTiming({ clock_.globalTimer, clock_.globalTimerDelta, clock_.clockTimer, clock_.frame });
    PropCamera propCamera;
    propCamera.frustum = Cur().camera.Frustum();
    propCamera.box = Cur().camera.Box();
    propCamera.viewer = Cur().camera.WorldPosition();
    propCamera.worldToScreen = &Cur().camera.WorldToScreen();
    propCamera.viewToWorld = &Cur().camera.ViewToWorld();
    propCamera.lodScaleZ = Cur().camera.LodScaleZ();
    propCamera.playerCount = playerCount_;
    propCamera.position = position;
    propCamera.playerProp = Cur().move.PropId();
    render_.UpdateOnScreenProps(propCamera, Cur().camera.ModelPosition(), Cur().camera.ModelviewF(), firstPass);
    // propsTick ends with the alarm, gas and countdown tick, the sounds on the
    // AI's channels and the room lists' defragmentation, once a frame: in the
    // pass of the player drawn first.
    if (position == 0) {
        TickAlarm();
        // loop_set_sound_effect_all_slots.
        aiSounds_->Tick();
        objects_.Props().Defrag();
    }
    if (Cur().move.PropId() >= 0) {
        AutoAimView aim;
        aim.props = &objects_.Props();
        aim.chrs = &chrs_;
        aim.stan = &level_.StanFile();
        aim.camera = &Cur().camera;
        aim.onScreen = &render_.OnScreenProps();
        aim.playerProp = Cur().move.PropId();
        aim.duckingHeight = Cur().move.DuckingHeightRelated();
        aim.crosshairX = Cur().weapons.Aim().crosshairScreenX;
        aim.crosshairY = Cur().weapons.Aim().crosshairScreenY;
        aim.difficulty = playerCount_ == 1 ? modifiers_.difficulty : 1.0f;
        aim.clockTimer = clock_.clockTimer;
        aim.xEnabled = Cur().autoAimEnabled;
        aim.yEnabled = Cur().autoAimEnabled;
        aim.setPlayerCollides = [this](bool collides) { Cur().move.SetCollides(collides); };
        Cur().autoAim.Update(aim);
    }
    if (Cur().weapons.Ready() && Cur().move.PropId() >= 0 && setup_) {
        shots_.CheckUseHeldItems();
    }
    if (Cur().move.UsePressed() && UseProp() && Cur().move.UseFallsBackToReload() && Cur().weapons.Ready()) {
        // With nothing to use, B reloads the item in each hand.
        Cur().weapons.AttemptReload(HandRight);
        Cur().weapons.AttemptReload(HandLeft);
    }
    if (Cur().move.ReloadPressed() && Cur().weapons.Ready()) {
        // Modern reload, independent of what is in reach.
        Cur().weapons.AttemptIdleReload(HandRight);
        Cur().weapons.AttemptIdleReload(HandLeft);
    }
    TickPickups();
    if (Cur().weapons.TakeRemoteMineTrigger()) {
        projectiles_.TriggerRemoteMines();
    }

    // sub_GAME_7F0BD8F0: the open watch hides the level behind its face.
    drawEnabled_ = Cur().watch.LevelDrawn();
    RenderView view = Cur().camera.Render(pose, drawEnabled_);
    view.stage = currentStage_;
    effects_.BeginFrame({ &Cur().camera, &view }, firstPass);
    dl = render_.Render(dl, resourceManager, view);
    shots_.RefreshPoses();
    CurStats().timeOtherPlayersOnScreen += chrs_.TakeDrawnCount();
    dl = RenderEffects(dl);

    // maybe_mp_interface: a cinematic camera shows no guns; otherwise they are
    // drawn last, over the level, and then the watch and the HUD.
    const bool intro = pose.frozen;
    if (Cur().weapons.Ready() && !intro) {
        gunDraw_.BeginFrame(firstPass);
        Cur().watch.BeginFrame();
        const GunFrame gunFrame = MakeGunFrame(view.lookAt);
        gunDraw_.Update(Cur().weapons, gunFrame);
        if (source_.Models() != nullptr) {
            dl = gunDraw_.RenderCasings(dl, resourceManager, casings_, Cur().camera.WorldToScreen(),
                                        Cur().weapons.TileColour(), *source_.Models());
        }
        dl = gunDraw_.Render(dl, resourceManager, Cur().weapons, gunFrame);
        WatchView watchView;
        watchView.worldToScreen = &Cur().camera.WorldToScreen();
        watchView.tileColour = Cur().weapons.TileColour();
        watchView.bondType = gunFrame.bondType;
        watchView.menu.hud = MakeHudFrame(pose);
        watchView.menu.speedFrames = speedFrames_;
        watchView.menu.tileColour = Cur().weapons.TileColour();
        watchView.menu.lookAt = view.lookAt;
        dl = Cur().watch.Render(dl, resourceManager, watchView);
    }
    if (!intro) {
        TickObjectiveText();
        TickPlayerDeath(resourceManager);
    }
    HudFrame hudFrame = MakeHudFrame(pose);
    RadarFrame radarFrame;
    if (playerCount_ >= 2) {
        radarFrame = MakeRadarFrame();
        hudFrame.radar = &radarFrame;
    }
    dl = intro ? Cur().hud.RenderIntroCamera(dl, hudFrame) : Cur().hud.Render(dl, hudFrame);
    if (Cur().hud.CreditsFinished() && credits_ == 1) {
        credits_ = 2;
    }

    // lvlRender draws the multiplayer menu after maybe_mp_interface.
    return RenderMultiplayerMenu(dl);
}

} // namespace Frigate::Level
