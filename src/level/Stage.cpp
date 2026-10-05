#include "level/Stage.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>

#include "game/Sound.h"
#include "game/menu/BasicGeometry.h"
#include "gfx/GbiF3dex.h"
#include "level/LevelInfo.h"
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

// viSetupScreensForNumPlayers, for one player: black in whatever the
// player's view leaves uncovered.
Gfx* FillUncoveredScreen(Gfx* dl, const ViewMetrics& vi) {
    using namespace Frigate::F3dex;
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_FILL);
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, vi.x, vi.y);
    gDPSetFillColor(dl++, (GPACK_RGBA5551(0, 0, 0, 1) << 16) | GPACK_RGBA5551(0, 0, 0, 1));
    if (vi.viewTop > 0) {
        gDPFillRectangle(dl++, 0, 0, vi.x - 1, vi.viewTop - 1);
        gDPPipeSync(dl++);
    }
    if (vi.viewTop + vi.viewHeight < vi.y) {
        gDPFillRectangle(dl++, 0, vi.viewTop + vi.viewHeight, vi.x - 1, vi.y - 1);
        gDPPipeSync(dl++);
    }
    return dl;
}

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
    render_.SetTank([this]() { return player_.DrivenTank(); },
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

bool Stage::Load(LevelId stage, int32_t demoSlot, int difficulty) {
    loaded_ = false;
    error_.clear();
    currentStage_ = stage;
    difficulty_ = difficulty;
    demoPlayback_ = demoSlot != 0;
    health_.Reset();
    colour_.Reset();
    autoAim_.Reset();
    forceDisarm_ = 0;
    civilianCasualties_ = 0;
    surroundBondWithExplosions_ = false;
    surroundTicks_ = 0;
    tickExplodePosition_ = 0;
    lifeStartTicks_ = 0;
    alarmTimer_ = 0;
    demo_ = demoSlot != 0;
    ResetAiState();

    // init_player_data_ptrs_construct_viewports draws the byte the players'
    // field_29E0 count up from, ahead of lvlStageLoad. Nothing reads that
    // field, so only the draw is kept.
    RandomGetNext();

    drawEnabled_ = true;
    controlsLocked_ = false;
    copiedGoldenEye_ = false;
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
        if (player_.PropId() >= 0) {
            listeners.push_back(objects_.Props()[player_.PropId()].position);
        }
        return listeners;
    });
    // set_mt_tex_alloc, texReset and modelmgrSetLevelResetting(TRUE) put the
    // texture and model pools into their stage-load allocation mode; textures
    // and models are resource-managed here, so there is no pool to prepare.
    sparks_.Reset();
    // TODO(https://github.com/liz-baker/frigate/issues/89): load_font_tables, which
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

    for (int player = 0; player < kMaxPlayers; player++) {
        PlayerStats& stats = stats_[player];
        stats = PlayerStats{};
        // TODO(https://github.com/liz-baker/frigate/issues/288): the multiplayer
        // branch, which takes handicap and character height from the setup
        // and sets the time and point limits.
    }

    // TODO(https://github.com/liz-baker/frigate/issues/136):
    // mpwatchUnpauseGame, initModelHitEntryFreeList, modelmgrResetSlotCounts and
    // init_ejected_cartridges.
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
    chrs_.SetDkModeCheck([this] { return cheats_.IsActive(Game::Menu::Cheat::kDkMode); });
    aiLists_.Clear();
    if (const auto text = source_.LoadAiGlobals()) {
        if (!aiLists_.ParseGlobals(*text)) {
            error_ = "the global AI lists (ai/globals) are malformed";
            return false;
        }
    }
    chrs_.SetAiLists(&aiLists_);
    objects_.SetModels(source_.Models());
    objects_.SetEnemyRocketsCheck([this] { return cheats_.IsActive(Game::Menu::Cheat::kEnemyRockets); });
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
            float always30 = 0.0f;
            player_.CollisionRadius(&out->radius, &out->height, &always30);
            out->above = always30;
            out->ground = player_.StanHeight();
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

    intro_ = IntroSetup{};
    spawn_ = PlayerSpawn{};
    // init_player_BONDdata_stats's hands come first: their sway draws random
    // numbers ahead of the intro section's.
    weapons_ = PlayerWeapons{};
    weapons_.SetModels(source_.Models());
    if (const auto text = source_.LoadWeaponStats()) {
        auto table = WeaponTable::Parse(*text);
        if (!table) {
            error_ = "the weapon table (weapon/stats) is malformed";
            return false;
        }
        weapons_.Init(std::move(table), &intro_.inventory, RandomGetNext);
        weapons_.SetPropListed([this](const InvItem& item) { return watchBridge_->PropListed(item); });
    }
    if (setup_) {
        // Single-player only: the per-player loop runs once.
        intro_ = LoadIntroSection(*setup_, demoSlot, RandomGetNext);
        // proplvreset2's rename records hand bondinvAddTextOverride their text
        // after the inventory is emptied, which the intro then fills.
        PropLinks::AddTextOverrides(*setup_, &intro_.inventory);
        spawn_ = PlacePlayer(*setup_, intro_, level_.StanFile(), stats_[0].perspectiveHeight);
    }
    monitorLists_.reset();
    if (const auto text = source_.LoadMonitorLists()) {
        monitorLists_ = MonitorLists::Parse(*text);
        if (!monitorLists_) {
            error_ = "the monitor animation lists (monitor/lists) are malformed";
            return false;
        }
    }
    gunDraw_.AttachMonitors(monitorLists_, [this](int offset) { return effectImages_.Size(offset); }, SelectEffectImage);
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
        combatHooks.dkMode = [this] { return cheats_.IsActive(Game::Menu::Cheat::kDkMode); };
        combatHooks.positionJoint = [this](Chr& chr, int index, Mat4* matrix, const Mtxf& viewToWorld,
                                           const Mtxf& worldToScreen) {
            aiActions_->PositionJoint(chr, index, matrix, viewToWorld, worldToScreen);
        };
        render_.SetChrCombat(std::move(combatHooks));
        vehicles_ = std::make_unique<Vehicles>(objects_, level_.StanFile(), paths_, chrs_, *ai_, *aiActions_, *setup_,
                                               source_.Models(), currentStage_);
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
    // sets_a_bunch_of_BONDdata_values_to_default: the head's animation state.
    head_ = source_.CreateHead();
    player_.SetHead(head_.get());
    player_.Init(spawn_, level_.StanFile());
    player_.SetCameraFrozen(false);

    // The HUD, and the watch, which shows the time the intro set
    // (init_watch_at_start_of_stage).
    hud_.Reset();
    if (hudSource_ != nullptr) {
        hud_.SetFonts(hudSource_->LoadFont("font/fontBankGothic"), hudSource_->LoadFont("font/fontZurichBold"));
        if (const auto images = hudSource_->LoadHudImages()) {
            if (!hud_.SetImages(*images) || !effectImages_.Parse(*images)) {
                error_ = "the HUD image table (hud/images) is malformed";
                return false;
            }
        }
    }
    watch_.Attach(&player_, &weapons_, &hud_, source_.Models());
    watch_.AttachMenu(watchBridge_.get(), &options_, &intro_.inventory);
    watch_.SetCountdownVisible([this](int bits, bool hide) {
        countdownHidden_ = hide ? countdownHidden_ | bits : countdownHidden_ & ~bits;
    });
    watch_.Reset(intro_.watchTime);
    speedFrames_ = 1;
    aspect_ = 4.0f / 3.0f;

    // The player's prop, at the start position. It is a viewer prop, with a
    // footprint of its own.
    PropList& props = objects_.Props();
    const int playerProp = props.Allocate();
    if (playerProp >= 0) {
        Prop& prop = props[playerProp];
        prop.type = PropType::Viewer;
        prop.position = spawn_.position;
        prop.stan = spawn_.stanTile;
        props.Activate(playerProp);
        props.Enable(playerProp);
        player_.SetProp(&props, playerProp);
    }
    objects_.SetViewerFootprint([this](PropCollider* out) { return player_.Footprint(out); });
    // playerTick, around the tick of the body Bond has in the swirl.
    {
        PropRender::PlayerBodyHooks hooks;
        hooks.before = [this] {
            chrs_[bondChr_].hidden &= ~ChrHidden::Freeze;
            player_.SetFootprintCounts(false);
        };
        hooks.after = [this] {
            player_.SetFootprintCounts(true);
            // The body's head position the game also copies to the camera's
            // position is rebuilt from the collision position at the start of
            // every move, and the cinematic camera does not read it.
            const Prop& prop = objects_.Props()[player_.PropId()];
            player_.AdoptBodyPosition(prop.position, prop.stan, level_.StanFile());
        };
        render_.SetPlayerBody(std::move(hooks));
    }
    if (weapons_.Ready()) {
        // The intro grants each ammo record as it reads the setup, before the
        // player holds anything.
        for (const IntroAmmo& ammo : intro_.ammo) {
            weapons_.GiveAmmo(ammo.ammoType, ammo.amount);
        }
    }
    // TODO(https://github.com/liz-baker/frigate/issues/136):
    // sets_a_bunch_of_BONDdata_values_to_default.
    // disableOnscreenCheatText.
    cheatInput_.Clear();

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
    const auto file = source_.LoadSetup(std::string("U") + name);
    if (!file) {
        error_ = std::string("no setup file U") + name;
        return false;
    }
    std::string parseError;
    setup_ = StageSetup::Parse(*file, 1.0f / level_.Level().scale, &parseError);
    if (!setup_) {
        error_ = std::string("U") + name + ": " + parseError;
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
        if (IsObjectRecord(def.type) && StageSetup::LoadsObject(def, difficulty, kPlayerCount)) {
            objectDefs_.push_back(i);
        }
    }
    // The characters, objects and doors are created in the setup's order, as
    // proplvreset2's pass over the records does: a guard's start position is
    // tested against what exists before it. The types none of them serve yet
    // are skipped.
    const ChrTiming timing{ clock_.globalTimer, clock_.globalTimerDelta, clock_.clockTimer };
    size_t nextObject = 0;
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
            } else {
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
    if (!setup_ || !weapons_.Ready() || clock_.clockTimer == 0 || health_.IsDead()) {
        return;
    }
    // Nothing is picked up from inside a tank, or while the player is invincible.
    if (player_.InTank() || playerInvincible_) {
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
        const Vec3& position = player_.CollisionPosition();
        if (!objects_.TestPickup(*setup_, *object, position, player_.Tile(), health_.Armour(), intro_.inventory,
                                 weapons_, weapons_.Table(), player_.Verta() * kDegToRad)) {
            continue;
        }
        CollectProp(propId);
    }
}

void Stage::CollectProp(int propId) {
    PlacedObject* object = objects_.ForProp(propId);
    if (object == nullptr || !setup_ || !weapons_.Ready()) {
        return;
    }
    std::vector<std::string> messages;
    // A pickup of body armour replaces the armour (bondviewAddCurrentPlayerArmor).
    constexpr float kNoArmourPickup = -1.0f;
    float armour = kNoArmourPickup;
    const PickupOutcome outcome = objects_.Collect(*setup_, *object, weapons_.Table(), &weapons_, &intro_.inventory,
                                                   &armour, SoloAmmoMultiplier(difficulty_), &lang_, &messages);
    if (armour != kNoArmourPickup) {
        stats_[0].bodyArmorPickups += armour;
        health_.SetArmour(armour);
    }
    for (const std::string& message : messages) {
        hud_.BottomShow(message);
    }
    if (outcome == PickupOutcome::Collected) {
        objects_.FreeOrRespawn(propId);
    } else if (outcome == PickupOutcome::Given) {
        intro_.inventory.AddProp(propId, static_cast<int64_t>(object->def));
        objects_.GiveToPlayer(propId, player_.PropId());
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
        camera_.SetZRange(fog_.ZNear(), fog_.ZFar());
        if (gasDamages_) {
            constexpr int kCoughInterval = 0xE1;
            if (gasLastCough_ < clock_.globalTimer - kCoughInterval) {
                gasLastCough_ = clock_.globalTimer;
                if (gasTimer_ >= 600.0f) {
                    Game::Sound::PlaySfx(AudioDriver::kSfxCough);
                }
                if (gasTimer_ >= 1800.0f) {
                    RecordPlayerDamage(0.125f, 0.0f, 0.0f, false);
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
    weapons_.StopSounds();
    player_.StopTankSounds();
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
    hud_.ResetCredits();
    cameraAfterCinema_ = CameraMode::None;
    cameraTransitionTimer_ = 0.0f;
    introCameraIndex_ = 1;
    cameraFadeActive_ = false;
    frozenCamera_ = false;
    cameraTile_ = kNoTile;
    bondChr_ = -1;
    forcedMove_ = {};
    timerActive_ = true;
    stopTime_ = 0;
    credits_ = 0;
    playerInvincible_ = false;
    cheatInvincible_ = false;
    visibleToGuards_ = true;
    fastBond_ = false;
    bondKilledInAction_ = false;
    levelEndRequested_ = false;
    exit_ = StageExit::None;
    aborted_ = false;
    missionTimer_ = 0;
    previousButtons_ = 0;
    modifiers_ = DifficultyModifiers{};
}

void Stage::SetDifficultyModifiers() {
    // lvlSetMultipliersForDifficulty. The values are the lv.h defaults of each
    // difficulty.
    DifficultyModifiers& m = modifiers_;
    switch (difficulty_) {
        case 0: {
            const float armourDiff = health_.Health() + health_.Armour();
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
                if (record.timeToRegen > 0 && record.type != PropType::Door && player_.PropId() >= 0) {
                    // A respawning object comes back unless a player stands within
                    // 400 units (chrpropIsFarFromPlayers).
                    const Prop& playerProp = objects_.Props()[player_.PropId()];
                    const float dx = playerProp.position[0] - record.position[0];
                    const float dy = playerProp.position[1] - record.position[1];
                    const float dz = playerProp.position[2] - record.position[2];
                    const bool near = std::sqrt((dx * dx) + (dy * dy) + (dz * dz)) < 400.0f;
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
                if (weapons_.Ready()) {
                    for (const Hand hand : { HandRight, HandLeft }) {
                        AdvanceBeam(&weapons_.MutableHand(hand).beam, clock_.clockTimer, clock_.globalTimerDelta,
                                    RandomGetNext);
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
    player.prop = player_.PropId();
    const Prop& record = objects_.Props()[player_.PropId()];
    player.position = record.position;
    player.tile = record.stan;
    // bondviewGetPlayerYawRadians: DegToRad(360.0f - vv_theta).
    player.yaw = ((360.0f - player_.Theta()) * 6.2831855f) / 360.0f;
    player.look = player_.LookDirection();
    return player;
}

bool Stage::UseProp() {
    if (!setup_ || player_.PropId() < 0) {
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
        Interact::UseDoor(scene, player, prop, intro_.inventory, lang_, &message);
        if (!message.empty()) {
            hud_.BottomShow(message);
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
    player_.StopTankSounds();
    player_.DetachTankSounds();
    weapons_.StopSounds();
    music_.Stop();
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
    if (!object.cctv || (object.flags & ObjectFlag::DroneGun) != 0 || player_.PropId() < 0 || object.prop < 0) {
        return;
    }
    CctvState& camera = *object.cctv;
    const Prop& playerProp = objects_.Props()[player_.PropId()];
    const Vec3 toPlayer = { playerProp.position[0] - object.runtimePosition[0],
                            playerProp.position[1] - object.runtimePosition[1],
                            playerProp.position[2] - object.runtimePosition[2] };

    if (CctvInView(camera, toPlayer, (object.flags & ObjectFlag::InMotion) != 0)) {
        const Prop& mine = objects_.Props()[object.prop];
        int tile = mine.stan;
        // bondviewUpdateGuardTankFlagsRelated takes the player out of the line
        // test and back.
        player_.SetCollides(false);
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
        player_.SetCollides(true);
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
    for (int i = 0; i < 3; i++) {
        RandomGetNext();
    }
}

void Stage::MovePlayer(const MoveInput& input) {
    // lvlViewMoveTick starts with cheat_buttons_mp_related.
    TickCheatInput(input);
    // A stage whose start position found no tile has nothing to walk on.
    if (player_.Tile() == kNoTile) {
        return;
    }
    // bondviewMovePlayerUpdateViewport: the view, and what the options show of
    // the HUD, ahead of MoveBond.
    UpdateViewport();
    colour_.Update(clock_.globalTimerDelta);
    if (const int alpha = colour_.TickBodyFade(clock_.globalTimerDelta); alpha >= 0 && bondChr_ >= 0) {
        chrs_[bondChr_].fadeAlpha = static_cast<uint8_t>(alpha);
    }
    // currentPlayerSetYAutoAimEnabled and ...XAutoAimEnabled.
    autoAimEnabled_ = options_.autoAim;
    hud_.SetAmmoVisible(AmmoReason::Option, options_.ammoOnScreen);
    hud_.SetSightVisible(SightReason::Option, options_.sightOnScreen);
    watch_.SetTiming({ speedFrames_, clock_.clockTimer, clock_.globalTimerDelta });
    watch_.SetInput(input);

    // mission_timer runs while the player is in play, and the player moves
    // under MoveBond; every other mode moves him under bondviewFrozenMoveBond.
    const bool playing = cameraMode_ == CameraMode::None || (cameraMode_ == CameraMode::FirstPerson && timerActive_) ||
                         cameraMode_ == CameraMode::FadeToTitle;
    if (playing) {
        missionTimer_ += clock_.clockTimer;
    }
    const bool frozenMove = !playing;

    MoveContext context;
    context.clockTimer = clock_.clockTimer;
    context.globalTimerDelta = clock_.globalTimerDelta;
    // bondviewMovePlayerUpdateViewport starts every tick with
    // viSetFovY(FOV_Y_F); the zoom update inside the tick then moves it to the
    // zoom reached this tick.
    camera_.SetViFovY(kFovY);
    context.fovY = camera_.ViFovY();
    context.setFovY = [this](float fovY) { camera_.SetViFovY(fovY); };
    context.perspectiveHeight = stats_[0].perspectiveHeight;
    context.fastBond = fastBond_;
    context.controlsLocked = controlsLocked_;
    context.options = options_;
    context.random = RandomGetNext;
    context.weaponDisablesCrouch = weapons_.Ready() && weapons_.RightHandHasFlag(WeaponFlag::DisableCrouch);
    context.dead = health_.IsDead();
    context.forceDisarm = forceDisarm_ > 0;
    context.damageShowing = health_.DamageShowing();
    context.autoAimEnabled = autoAimEnabled_;
    context.tank = tankBridge_.get();
    context.forcedMove = forcedMove_;
    context.stopAudioForPause = [this] { StopAudioForPause(); };
    context.sightPitch = sightPitch_;
    context.autoAimTargetX = autoAim_.HasTargetX();
    context.autoAimTargetY = autoAim_.HasTargetY();
    context.autoAimX = autoAim_.X();
    context.autoAimY = autoAim_.Y();
    if (weapons_.Ready()) {
        context.weapons = &weapons_;
        context.gunHooks = gunBridge_.get();
        context.gunView = MakeGunFrame(nullptr).view;
        context.watch = &watch_;
    }

    if (frozenMove) {
        // bondviewFrozenMoveBond.
        TickPlayerDamage();
        TickPlayerExplode();
        context.dead = health_.IsDead();
        context.forceDisarm = forceDisarm_ > 0;
        context.damageShowing = health_.DamageShowing();

        player_.TickFrozen(context, input, level_.StanFile());
        frozenCamera_ = cameraMode_ == CameraMode::Intro || cameraMode_ == CameraMode::FadeSwirl ||
                        cameraMode_ == CameraMode::Swirl || cameraMode_ == CameraMode::PosEnd || DeathCameraShowing();
        if (frozenCamera_) {
            SetFrozenCamera(FrozenCameraTick(input));
        }
        player_.SetCameraFrozen(frozenCamera_);
    } else {
        // MoveBond's steps ahead of the input: the disarm the intro asks for,
        // the display of a hit and the explosions around the player.
        if (forceDisarm_ > 0) {
            forceDisarm_++;
            if (forceDisarm_ >= 4) {
                forceDisarm_ = 0;
                if (weapons_.Ready()) {
                    weapons_.UnlockHandModels();
                    weapons_.UnEquipWeapon(HandLeft, weapons_.WeaponId(HandLeft));
                    weapons_.UnEquipWeapon(HandRight, weapons_.WeaponId(HandRight));
                }
            }
        }
        frozenCamera_ = false;
        player_.SetCameraFrozen(false);
        TickPlayerDamage();
        TickPlayerExplode();
        context.dead = health_.IsDead();
        context.forceDisarm = forceDisarm_ > 0;
        context.damageShowing = health_.DamageShowing();

        player_.Tick(context, input, level_.StanFile());
        // The watch's own tick ends its zoom update with viSetFovY(zoominfovy)
        // too.
        camera_.SetViFovY(watch_.ZoomFovY());
    }
    if (player_.Tile() != kNoTile && setup_) {
        objectives_.CheckRoomEntered(level_.StanFile().TileRoom(player_.Tile()), *setup_, level_.StanFile());
    }
    // gunTickGameplay's share of the floor shade the gun is lit with.
    if (weapons_.Ready() && player_.Tile() != kNoTile) {
        weapons_.TickShading(ShadeFromTile(level_.StanFile().Tile(player_.Tile())));
    }
    // The watch's tick sets whether the controls are locked (which the next
    // frame's clock reads); the crosshair shows while the player is aiming.
    controlsLocked_ = watch_.ControlsLocked();
    hud_.SetSightVisible(SightReason::NotAiming, player_.LastMove().aiming);
    watch_.AdvanceTime();

    // The distance the player covered this tick, for the stats.
    const Vec3& position = player_.CollisionPosition();
    const Vec3& previous = player_.PreviousPosition();
    const float dx = position[0] - previous[0];
    const float dz = position[2] - previous[2];
    stats_[0].distanceTraveled += std::sqrt((dx * dx) + (dz * dz));
    // TODO(https://github.com/liz-baker/frigate/issues/288): the multiplayer
    // scenarios' token and golden gun flags that lvlViewMoveTick sets.
    TickExits(input);
}

void Stage::UpdateViewport() {
    // bondviewGetCurrentPlayerViewportWidth, ...Height and ...Uly for one
    // player on a 320 by 240 screen: the letterboxed screen settings open up
    // as the arm comes up for the watch.
    constexpr int32_t kWidth = 320;
    constexpr int32_t kScreenHeight = 240;
    const float raised = watch_.PauseAnimationPercent();

    int32_t height;
    int32_t top;
    switch (options_.screen) {
        case ScreenSize::Wide:
            height = static_cast<int32_t>(40.0f * raised) + 180;
            top = static_cast<int32_t>(-20.0f * raised) + 30;
            break;
        case ScreenSize::Cinema:
            height = static_cast<int32_t>(84.0f * raised) + 136;
            top = static_cast<int32_t>(-42.0f * raised) + 52;
            break;
        default:
            height = 220;
            top = 10;
            break;
    }

    camera_.SetView({ kWidth, kScreenHeight, 0, top, kWidth, height });
    const float ratio = static_cast<float>(kWidth) / static_cast<float>(height);
    aspect_ = options_.ratio16x9 ? ratio * 0.75f * (16.0f / 9.0f) : ratio;
}

SkyView Stage::MakeSkyView() const {
    SkyView view;
    view.view = camera_.Metrics();
    view.halfWidth = camera_.ScreenWidth() * 0.5f;
    view.halfHeight = camera_.ScreenHeight() * 0.5f;
    view.scaleX = camera_.ScaleX();
    view.scaleY = camera_.ScaleY();
    view.eye = camera_.WorldPosition();
    view.viewToWorld = camera_.ViewToWorld();
    std::memcpy(view.projection.m, camera_.ProjectionF(), sizeof(view.projection.m));
    std::memcpy(view.worldToScreen.m, camera_.WorldToScreen(), sizeof(view.worldToScreen.m));
    view.levelScale = level_.Level().scale;
    view.playerCount = kPlayerCount;
    return view;
}

HudFrame Stage::MakeHudFrame(const PlayerPose& pose) const {
    HudFrame frame;
    frame.vi = camera_.Metrics();
    frame.ratio16x9 = options_.ratio16x9;
    frame.clockTimer = clock_.clockTimer;
    frame.watchState = watch_.State();
    frame.countdownTime = countdownTime_;
    frame.countdownShown = countdownHidden_ == 0;
    frame.introCamera = pose.frozen;
    if (currentStage_ == LevelId::Cuba && credits_ == 1 && setup_) {
        frame.credits = setup_->Credits();
        frame.lang = &lang_;
    }
    frame.visibilityScale = level_.VisibilityScale();
    frame.projection = &camera_.Projection();
    frame.weapons = weapons_.Ready() ? &weapons_ : nullptr;
    frame.colourRed = colour_.Red();
    frame.colourGreen = colour_.Green();
    frame.colourBlue = colour_.Blue();
    frame.colourFrac = colour_.Frac();
    frame.blood = bloodOverlay_ ? &*blood_ : nullptr;
    return frame;
}

GunFrame Stage::MakeGunFrame(const LookAt* lookAt) {
    GunFrame frame;
    frame.clockTimer = clock_.clockTimer;
    frame.globalTimerDelta = clock_.globalTimerDelta;
    frame.duckingHeightOffset = player_.DuckingHeightOffset();
    frame.view = { camera_.ScreenLeft(),   camera_.ScreenTop(), camera_.ScreenWidth(),
                   camera_.ScreenHeight(), camera_.ScaleX(),    camera_.ScaleY() };
    frame.wideScreen = options_.WideScreen();
    // The intro's cuff record sets the Bond type; CUFF_BLUE is the default.
    frame.bondType = intro_.bondType.value_or(0);
    frame.lodScaleZ = camera_.LodScaleZ();
    frame.lookAt = lookAt;
    frame.viewToWorld = camera_.ViewToWorld();
    std::memcpy(frame.worldToView.m, camera_.WorldToScreen(), sizeof(frame.worldToView.m));
    frame.floorHeight = player_.StanHeight();
    frame.casings = &casings_;
    frame.previousPosition = player_.PreviousPosition();
    frame.launcher = &launcher_;
    if (const int tank = player_.DrivenTank(); tank >= 0 && (objects_.Props()[tank].flags & PropFlag::OnScreen) != 0) {
        PlacedObject* object = objects_.ForProp(tank);
        const TankGeometry* geometry = object != nullptr ? objects_.TankGeometryOf(*object) : nullptr;
        if (geometry != nullptr) {
            frame.tankAimed = true;
            frame.tankAim = player_.TankAimDirection();
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

Gfx* Stage::Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                   const PlayerPose& pose) {
    // gSPSegment for the font display list and gSPClipRatio(FRUSTRATIO_2) have
    // no counterpart: text is drawn from resources and Fast3D clips on its own.
    dl = Game::Menu::FastPipelineSetup(dl);
    dl = Game::Menu::ZBufferGeometry(dl);

    // viSetFovY(g_CurrentPlayer->fovy) and viSetAspect: the zoom the watch or
    // the sniper rifle set this tick.
    camera_.SetFovAspect(watch_.ZoomFovY(), aspect_);
    // viClearZBufCurrentPlayer is the renderer's own depth clear.
    dl = camera_.SetupView(dl);
    // bondviewRenderDebugBondView: a cinematic camera is not shaken, and a dead
    // player's view is still.
    if (!pose.frozen) {
        if (!health_.IsDead()) {
            explosions_.ScreenShake(pose.position, pose.look);
        } else {
            explosions_.Video().Shake(0.0f);
        }
    }
    screenShakeOffset_ = explosions_.Video().Advance(clock_.clockTimer);
    camera_.Update(pose, level_);
    if (weapons_.Ready()) {
        // bondviewRenderDebugBondView: the pitch the sight points at, which the
        // tank's gun follows on the next move.
        const Vec3 sight = shots_.SightDirection();
        const float flat = std::sqrt((sight[2] * sight[2]) + (sight[0] * sight[0]));
        sightPitch_ = GameAtan2f(sight[1], flat) + (player_.Verta() * kDegToRad);
        if (sightPitch_ >= 3.1415927f) {
            sightPitch_ -= 6.2831855f;
        }
    }
    dl = FillUncoveredScreen(dl, camera_.Metrics());
    dl = sky_.Render(dl, resourceManager, MakeSkyView(), fog_);
    sky_.UpdateRoomWater(resourceManager);
    // Whatever skyRender leaves the pipeline in, the rooms draw from.
    dl = Game::Menu::FastPipelineSetup(dl);
    dl = Game::Menu::ZBufferGeometry(dl);

    // bgRoomVisibilityRelated
    visibility_.Update(camera_.Visibility(pose), pose.previousPosition, pose.cameraMode);
    // propsTick's per-prop tick (chrTick, the doors' objTick and the embedded
    // objects' matrices) runs inside the on-screen list below.
    chrs_.SetTiming({ clock_.globalTimer, clock_.globalTimerDelta, clock_.clockTimer, clock_.frame });
    PropCamera propCamera;
    propCamera.frustum = camera_.Frustum();
    propCamera.box = camera_.Box();
    propCamera.viewer = camera_.WorldPosition();
    propCamera.worldToScreen = &camera_.WorldToScreen();
    propCamera.viewToWorld = &camera_.ViewToWorld();
    propCamera.lodScaleZ = camera_.LodScaleZ();
    render_.SetViewerFrozen(frozenCamera_);
    render_.UpdateOnScreenProps(propCamera, camera_.ModelPosition(), camera_.ModelviewF());
    // propsTick ends with the alarm, gas and countdown tick and the room lists'
    // defragmentation.
    TickAlarm();
    // loop_set_sound_effect_all_slots.
    aiSounds_->Tick();
    objects_.Props().Defrag();
    if (player_.PropId() >= 0) {
        AutoAimView aim;
        aim.props = &objects_.Props();
        aim.chrs = &chrs_;
        aim.stan = &level_.StanFile();
        aim.camera = &camera_;
        aim.onScreen = &render_.OnScreenProps();
        aim.playerProp = player_.PropId();
        aim.duckingHeight = player_.DuckingHeightRelated();
        aim.crosshairX = weapons_.Aim().crosshairScreenX;
        aim.crosshairY = weapons_.Aim().crosshairScreenY;
        aim.difficulty = modifiers_.difficulty;
        aim.clockTimer = clock_.clockTimer;
        aim.xEnabled = autoAimEnabled_;
        aim.yEnabled = autoAimEnabled_;
        aim.setPlayerCollides = [this](bool collides) { player_.SetCollides(collides); };
        autoAim_.Update(aim);
    }
    if (weapons_.Ready() && player_.PropId() >= 0 && setup_) {
        shots_.CheckUseHeldItems();
    }
    if (player_.UsePressed() && UseProp() && weapons_.Ready()) {
        // With nothing to use, B reloads the item in each hand.
        weapons_.AttemptReload(HandRight);
        weapons_.AttemptReload(HandLeft);
    }
    TickPickups();
    if (weapons_.TakeRemoteMineTrigger()) {
        projectiles_.TriggerRemoteMines();
    }

    // sub_GAME_7F0BD8F0: the open watch hides the level behind its face.
    drawEnabled_ = watch_.LevelDrawn();
    RenderView view = camera_.Render(pose, drawEnabled_);
    view.stage = currentStage_;
    effects_.BeginFrame({ &camera_, &view });
    dl = render_.Render(dl, resourceManager, view);
    shots_.RefreshPoses();
    stats_[0].timeOtherPlayersOnScreen += chrs_.TakeDrawnCount();
    dl = RenderEffects(dl);

    // maybe_mp_interface: a cinematic camera shows no guns; otherwise they are
    // drawn last, over the level, and then the watch and the HUD.
    const bool intro = pose.frozen;
    if (weapons_.Ready() && !intro) {
        gunDraw_.BeginFrame();
        watch_.BeginFrame();
        const GunFrame gunFrame = MakeGunFrame(view.lookAt);
        gunDraw_.Update(weapons_, gunFrame);
        if (source_.Models() != nullptr) {
            dl = gunDraw_.RenderCasings(dl, resourceManager, casings_, camera_.WorldToScreen(), weapons_.TileColour(),
                                        *source_.Models());
        }
        dl = gunDraw_.Render(dl, resourceManager, weapons_, gunFrame);
        WatchView watchView;
        watchView.worldToScreen = &camera_.WorldToScreen();
        watchView.tileColour = weapons_.TileColour();
        watchView.bondType = gunFrame.bondType;
        watchView.menu.hud = MakeHudFrame(pose);
        watchView.menu.speedFrames = speedFrames_;
        watchView.menu.tileColour = weapons_.TileColour();
        watchView.menu.lookAt = view.lookAt;
        dl = watch_.Render(dl, resourceManager, watchView);
    }
    if (!intro) {
        TickObjectiveText();
        TickPlayerDeath(resourceManager);
    }
    const HudFrame hudFrame = MakeHudFrame(pose);
    dl = intro ? hud_.RenderIntroCamera(dl, hudFrame) : hud_.Render(dl, hudFrame);
    if (hud_.CreditsFinished() && credits_ == 1) {
        credits_ = 2;
    }

    using namespace Frigate::F3dex;
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, camera_.Metrics().x, camera_.Metrics().y);
    // boss.c's tail of the frame: the line mode cheat visualises the coverage
    // value the edges' anti-aliasing uses.
    if (lineMode_) {
        gDPPipeSync(dl++);
        gDPSetCycleType(dl++, G_CYC_1CYCLE);
        gDPSetBlendColor(dl++, 0xFF, 0xFF, 0xFF, 0xFF);
        gDPSetPrimDepth(dl++, 0xFFFF, 0xFFFF);
        gDPSetDepthSource(dl++, G_ZS_PRIM);
        gDPSetRenderMode(dl++, G_RM_VISCVG, G_RM_VISCVG2);
        gDPFillRectangle(dl++, 0, 0, camera_.Metrics().x - 1, camera_.Metrics().y - 1);
    }
    return dl;
}

} // namespace Frigate::Level
