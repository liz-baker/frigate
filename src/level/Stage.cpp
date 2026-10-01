#include "level/Stage.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>

#include "game/menu/BasicGeometry.h"
#include "gfx/GbiF3dex.h"
#include "level/LevelInfo.h"
#include "level/StageAi.h"
#include "level/StageGun.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

// 60 Hz: a clock tick's length in seconds is 1/60 (US).
constexpr float kTicksPerSecond = 60.0f;
constexpr float kDegToRad = 0.017453292f;

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
      render_(level_, visibility_, objects_, doors_, chrs_, fog_), hudSource_(hudSource) {
    level_.SetObserver(&roomLoad_);
    aiBridge_ = std::make_unique<AiBridge>(*this);
    gunBridge_ = std::make_unique<GunBridge>(*this);
}

Stage::~Stage() = default;

bool Stage::Load(LevelId stage, int32_t demoSlot, int difficulty) {
    loaded_ = false;
    error_.clear();
    currentStage_ = stage;
    difficulty_ = difficulty;
    armour_ = 0.0f;
    alarmTimer_ = 0;
    ResetAiState();

    // init_player_data_ptrs_construct_viewports draws the byte the players'
    // field_29E0 count up from, ahead of lvlStageLoad. Nothing reads that
    // field, so only the draw is kept.
    RandomGetNext();

    drawEnabled_ = true;
    controlsLocked_ = false;
    clock_ = StageClock{};
    // The game also clears the idle-input clocks (D_80048388 through D_80048390).
    // TODO(https://github.com/liz-baker/frigate/issues/136): the idle check that
    // stops the stage timer after 30 seconds of no input.

    // TODO(https://github.com/liz-baker/frigate/issues/41): the volume resets
    // (sndSetScalerApplyVolumeAllSfxSlot, musicTrackNApplySeqpVol) and
    // sub_GAME_7F0C11FC, the stage's music.
    // set_mt_tex_alloc, texReset and modelmgrSetLevelResetting(TRUE) put the
    // texture and model pools into their stage-load allocation mode; textures
    // and models are resource-managed here, so there is no pool to prepare.
    // TODO(https://github.com/liz-baker/frigate/issues/136): bullet_sparks_reset_all.
    // TODO(https://github.com/liz-baker/frigate/issues/89): load_font_tables, which
    // resets the text style globals.

    // TODO(https://github.com/liz-baker/frigate/issues/136): the cheat turn-on
    // for enemy-rocket cheats (g_NewCheatUnlocked and friends).

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
    // TODO(https://github.com/liz-baker/frigate/issues/136): init_watch_at_start_of_stage.

    for (int player = 0; player < kMaxPlayers; player++) {
        PlayerStats& stats = stats_[player];
        stats = PlayerStats{};
        // TODO(https://github.com/liz-baker/frigate/issues/136): the multiplayer
        // branch, which takes handicap and character height from the setup
        // and sets the time and point limits.
    }

    // TODO(https://github.com/liz-baker/frigate/issues/136):
    // something_with_stage_objectives, mpwatchUnpauseGame,
    // initModelHitEntryFreeList, modelmgrResetSlotCounts, initCheatTextBuffer,
    // disableOnscreenCheatText, alloc_shattered_window_pieces and
    // init_ejected_cartridges.
    // TODO(https://github.com/liz-baker/frigate/issues/126): sub_GAME_7F09B820,
    // the dynamic vertex store's sizes.
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
    doors_.SetClearCheck([this](int prop) { return objects_.FootprintClear(prop); });
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
    aiLists_.Clear();
    if (const auto text = source_.LoadAiGlobals()) {
        if (!aiLists_.ParseGlobals(*text)) {
            error_ = "the global AI lists (ai/globals) are malformed";
            return false;
        }
    }
    chrs_.SetAiLists(&aiLists_);
    objects_.SetModels(source_.Models());
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
    // TODO(https://github.com/liz-baker/frigate/issues/41): reinit_between_menus'
    // sound reset and init_sound_effects_registers.
    // TODO(https://github.com/liz-baker/frigate/issues/127): alloc_explosion_smoke_casing_scorch_impact_buffers.
    // reinit_gunheld_totaltime: PlayerWeapons::Init clears the held times.
    // TODO(https://github.com/liz-baker/frigate/issues/146): init_player_BONDdata_stats.
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
    }
    if (setup_) {
        // Single-player only: the per-player loop runs once.
        intro_ = LoadIntroSection(*setup_, demoSlot, RandomGetNext);
        // proplvreset2's rename records hand bondinvAddTextOverride their text
        // after the inventory is emptied, which the intro then fills.
        PropLinks::AddTextOverrides(*setup_, &intro_.inventory);
        spawn_ = PlacePlayer(*setup_, intro_, level_.StanFile(), stats_[0].perspectiveHeight);
    }
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
        aiActions_ = std::make_unique<ChrActions>(scene, *aiBridge_, source_.Models(), std::move(animations));
        ai_ = std::make_unique<ChrAi>(scene, *aiBridge_, *aiActions_);
        aiActions_->Attach(ai_.get());
        render_.SetChrActionTick([this](Chr& chr) { ai_->ActionTick(chr); });
        vehicles_ = std::make_unique<Vehicles>(objects_, level_.StanFile(), paths_, chrs_, *ai_, source_.Models());
        render_.SetObjectTick([this](PlacedObject& object) { vehicles_->Tick(object); });
    }
    // sets_a_bunch_of_BONDdata_values_to_default: the head's animation state.
    head_ = source_.CreateHead();
    player_.SetHead(head_.get());
    player_.Init(spawn_, level_.StanFile());

    // The HUD, and the watch, which shows the time the intro set
    // (init_watch_at_start_of_stage).
    hud_.Reset();
    if (hudSource_ != nullptr) {
        hud_.SetFonts(hudSource_->LoadFont("font/fontBankGothic"), hudSource_->LoadFont("font/fontZurichBold"));
        if (const auto images = hudSource_->LoadHudImages()) {
            if (!hud_.SetImages(*images)) {
                error_ = "the HUD image table (hud/images) is malformed";
                return false;
            }
        }
    }
    watch_.Attach(&player_, &weapons_, &hud_, source_.Models());
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
    // TODO(https://github.com/liz-baker/frigate/issues/140): the footprint goes
    // while the intro camera shows.
    objects_.SetViewerFootprint([this](PropCollider* out) { return player_.Footprint(out); });
    if (weapons_.Ready()) {
        // The intro grants each ammo record as it reads the setup, before the
        // player holds anything.
        for (const IntroAmmo& ammo : intro_.ammo) {
            weapons_.GiveAmmo(ammo.ammoType, ammo.amount);
        }
    }
    // TODO(https://github.com/liz-baker/frigate/issues/136):
    // sets_a_bunch_of_BONDdata_values_to_default and disableOnscreenCheatText.

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
    if (!setup_ || !weapons_.Ready() || clock_.clockTimer == 0) {
        return;
    }
    // TODO(https://github.com/liz-baker/frigate/issues/148): nothing is picked
    // up from inside a tank.
    // TODO(https://github.com/liz-baker/frigate/issues/224): the invincible
    // player's skipped pass, and timetoregen, which a respawning weapon waits out.
    // TODO(https://github.com/liz-baker/frigate/issues/146): a dead player
    // picks nothing up.
    PropList& props = objects_.Props();
    // The newest props first; a pickup takes itself out of the list.
    const std::vector<int> active = props.Active();
    for (auto it = active.rbegin(); it != active.rend(); ++it) {
        const int propId = *it;
        const PropType type = props[propId].type;
        if (type != PropType::Obj && type != PropType::Weapon) {
            continue;
        }
        const PlacedObject* object = objects_.ForProp(propId);
        if (object == nullptr) {
            continue;
        }
        const Vec3& position = player_.CollisionPosition();
        if (!objects_.TestPickup(*setup_, *object, position, player_.Tile(), armour_, intro_.inventory, weapons_,
                                 weapons_.Table(), player_.Verta() * kDegToRad)) {
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
    const PickupOutcome outcome = objects_.Collect(*setup_, *object, weapons_.Table(), &weapons_, &intro_.inventory,
                                                   &armour_, SoloAmmoMultiplier(difficulty_), &lang_, &messages);
    for (const std::string& message : messages) {
        hud_.BottomShow(message);
    }
    if (outcome == PickupOutcome::Collected) {
        objects_.Free(propId);
    } else if (outcome == PickupOutcome::Given) {
        intro_.inventory.AddProp(propId, static_cast<int64_t>(object->def));
        objects_.GiveToPlayer(propId, player_.PropId());
    }
}

void Stage::TickAlarm() {
    // handle_alarm_gas_timer_calldamage. The alarm runs out after
    // CHROBJ_GAS_TIMER ticks.
    // TODO(https://github.com/liz-baker/frigate/issues/41): ALARM3_SFX, looped
    // while it runs.
    // TODO(https://github.com/liz-baker/frigate/issues/136): check_guard_detonate_proxmine,
    // the proximity mines a guard walks over.
    constexpr int kAlarmTicks = 0x708;
    if (alarmTimer_ > 0) {
        alarmTimer_ += clock_.clockTimer;
        if (kAlarmTicks < alarmTimer_) {
            alarmTimer_ = 0;
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
                // TODO(https://github.com/liz-baker/frigate/issues/41): COUGH_SFX
                // from 600 ticks of gas and GAS_HISS_SFX while it leaks.
                // TODO(https://github.com/liz-baker/frigate/issues/146): the
                // 0.125 damage record_damage_kills deals from 1800 ticks of gas.
            }
        }
    }

    // if_enabled_reset_clock.
    if (countdownRunning_) {
        countdownTime_ -= clock_.globalTimerDelta;
    }
}

void Stage::ResetAiState() {
    // reinit_between_menus: the alarm, toxic gas and countdown.
    gasActivate_ = false;
    gasTimer_ = 0.0f;
    gasLastCough_ = 0;
    countdownTime_ = 0.0f;
    countdownRunning_ = false;
    countdownHidden_ = 1;
    mpTicks_ = 0;
    cameraMode_ = CameraMode::None;
    timerActive_ = true;
    stopTime_ = 0;
    credits_ = 0;
    playerInvincible_ = false;
    bondKilledInAction_ = false;
    levelEndRequested_ = false;
    modifiers_ = DifficultyModifiers{};
}

void Stage::SetDifficultyModifiers() {
    // lvlSetMultipliersForDifficulty. The values are the lv.h defaults of each
    // difficulty.
    DifficultyModifiers& m = modifiers_;
    switch (difficulty_) {
        case 0: {
            // TODO(https://github.com/liz-baker/frigate/issues/146): the player's
            // health and armour (currentPlayerGetHealth and ...GetArmor), which
            // the damage scale follows; a full-health player takes none of it.
            const float armourDiff = 1.0f + armour_;
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
    // TODO(https://github.com/liz-baker/frigate/issues/212): gunAdvanceBeamTimer
    // for each character's, autogun's and player's bullet tracers.
    // TODO(https://github.com/liz-baker/frigate/issues/224): the respawn of an
    // object whose timetoregen runs out.
    // TODO(https://github.com/liz-baker/frigate/issues/214): explosionTick and
    // explosionSmokeTick.
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
    // bondviewGetPlayerYawRadians.
    player.yaw = (360.0f - player_.Theta()) * kDegToRad;
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
        // TODO(https://github.com/liz-baker/frigate/issues/41): ALARM_SWITCH_SFX.
        alarmTimer_ = alarmTimer_ > 0 ? 0 : 1;
    }
    object->runtimeFlags |= ObjectRuntime::Activated;
    // sub_GAME_7F03E6A0 runs before the pickup frees the prop.
    links_.TriggerSwitches(prop, &doors_);
    if ((object->flags & ObjectFlag::CollectByInteractOnly) != 0) {
        CollectProp(prop);
    }
}

void Stage::Unload() {
    // cleanup_rooms is the only part of lvlUnloadStageTextData the stage owns
    // so far; the rest cleans up systems that are not ported yet.
    // TODO(https://github.com/liz-baker/frigate/issues/136): the guard, object, explosion,
    // window-piece, alarm, objective and sound cleanups.
    level_.StanFile().SetProps(nullptr);
    level_.Cleanup();
    loaded_ = false;
}

void Stage::Tick() {
    chrs_.SetTiming({ clock_.globalTimer, clock_.globalTimerDelta, clock_.clockTimer });
    // lvlManageMpGame's clocks.
    mpTicks_ += clock_.clockTimer;
    powerTicks_ += clock_.clockTimer;
    clock_.stageTicks += clock_.clockTimer;
    clock_.stageTimeSec = static_cast<float>(clock_.stageTicks) / kTicksPerSecond;

    // updateRoomStatusFlags
    render_.Tick();
    sky_.Tick(clock_.clockTimer);
    // TODO(https://github.com/liz-baker/frigate/issues/213): bullet_sparks_update_all
    // and update_broken_windows.
    // TODO(https://github.com/liz-baker/frigate/issues/214):
    // explosionUpdateFlyingParticles.
    casings_.Update(clock_.clockTimer, clock_.globalTimerDelta);
    SetDifficultyModifiers();
    TickProps();
    // chrpropTick ends with the room lists' defragmentation.
    objects_.Props().Defrag();

    // shuffle_player_ids runs once a frame after lvlManageMpGame: three
    // draws, whatever the number of players.
    for (int i = 0; i < 3; i++) {
        RandomGetNext();
    }
}

void Stage::EnterSwirl() {
    // TODO(https://github.com/liz-baker/frigate/issues/140): the swirl's
    // black fade-in.
    fog_.LoadLevelEnvironment(currentStage_, /*cinema=*/false, kPlayerCount, level_.VisibilityScale());
    camera_.SetZRange(fog_.ZNear(), fog_.ZFar());
    cameraMode_ = CameraMode::Swirl;
}

void Stage::EnterFirstPerson() {
    cameraMode_ = CameraMode::FirstPerson;
    // TODO(https://github.com/liz-baker/frigate/issues/140): the camera fade
    // (Cuba's black fade and camera_fade_active's fade-in).
    if (weapons_.Ready() && watch_.State() == WatchState::Closed) {
        weapons_.EquipWeapon(HandLeft, intro_.startingWeaponLeft);
        weapons_.EquipWeapon(HandRight, intro_.startingWeaponRight);
    }
}

void Stage::MovePlayer(const MoveInput& input) {
    // A stage whose start position found no tile has nothing to walk on.
    if (player_.Tile() == kNoTile) {
        return;
    }
    // bondviewMovePlayerUpdateViewport: the view, and what the options show of
    // the HUD, ahead of MoveBond.
    UpdateViewport();
    // TODO(https://github.com/liz-baker/frigate/issues/226): the colour screen
    // properties, the chr fade and the auto-aim enable.
    hud_.SetAmmoVisible(AmmoReason::Option, options_.ammoOnScreen);
    hud_.SetSightVisible(SightReason::Option, options_.sightOnScreen);
    watch_.SetTiming({ speedFrames_, clock_.clockTimer, clock_.globalTimerDelta });

    MoveContext context;
    context.clockTimer = clock_.clockTimer;
    context.globalTimerDelta = clock_.globalTimerDelta;
    context.perspectiveHeight = stats_[0].perspectiveHeight;
    context.controlsLocked = controlsLocked_;
    context.options = options_;
    context.random = RandomGetNext;
    context.weaponDisablesCrouch = weapons_.Ready() && weapons_.RightHandHasFlag(WeaponFlag::DisableCrouch);
    if (weapons_.Ready()) {
        context.weapons = &weapons_;
        context.gunHooks = gunBridge_.get();
        context.gunView = MakeGunFrame(nullptr).view;
        context.watch = &watch_;
    }

    player_.Tick(context, input, level_.StanFile());
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
    // TODO(https://github.com/liz-baker/frigate/issues/136): the multiplayer
    // scenarios' token and golden gun flags that lvlViewMoveTick sets.
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
    frame.introCamera = pose.cameraMode == CameraMode::Intro;
    frame.visibilityScale = level_.VisibilityScale();
    frame.projection = &camera_.Projection();
    frame.weapons = weapons_.Ready() ? &weapons_ : nullptr;
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
    frame.floorHeight = player_.StanHeight();
    frame.casings = &casings_;
    return frame;
}

Gfx* Stage::Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const PlayerPose& pose) {
    // gSPSegment for the font display list and gSPClipRatio(FRUSTRATIO_2) have
    // no counterpart: text is drawn from resources and Fast3D clips on its own.
    dl = Game::Menu::FastPipelineSetup(dl);
    dl = Game::Menu::ZBufferGeometry(dl);

    // viSetFovY(g_CurrentPlayer->fovy) and viSetAspect: the zoom the watch or
    // the sniper rifle set this tick.
    camera_.SetFovAspect(watch_.ZoomFovY(), aspect_);
    // viClearZBufCurrentPlayer is the renderer's own depth clear.
    dl = camera_.SetupView(dl);
    camera_.Update(pose, level_);
    dl = FillUncoveredScreen(dl, camera_.Metrics());
    dl = sky_.Render(dl, resourceManager, MakeSkyView(), fog_);
    // Whatever skyRender leaves the pipeline in, the rooms draw from.
    dl = Game::Menu::FastPipelineSetup(dl);
    dl = Game::Menu::ZBufferGeometry(dl);

    // bgRoomVisibilityRelated
    visibility_.Update(camera_.Visibility(pose), pose.previousPosition, pose.cameraMode);
    // propsTick's per-prop tick (chrTick, the doors' objTick and the embedded
    // objects' matrices) runs inside the on-screen list below.
    chrs_.SetTiming({ clock_.globalTimer, clock_.globalTimerDelta, clock_.clockTimer });
    PropCamera propCamera;
    propCamera.frustum = camera_.Frustum();
    propCamera.box = camera_.Box();
    propCamera.viewer = camera_.WorldPosition();
    propCamera.worldToScreen = &camera_.WorldToScreen();
    propCamera.lodScaleZ = camera_.LodScaleZ();
    render_.UpdateOnScreenProps(propCamera, camera_.ModelPosition(), camera_.ModelviewF());
    // propsTick ends with the alarm, gas and countdown tick.
    TickAlarm();
    // TODO(https://github.com/liz-baker/frigate/issues/166): chrpropUpdateAutoaimTarget,
    // chraiCheckUseHeldItems.
    if (player_.UsePressed() && UseProp() && weapons_.Ready()) {
        // With nothing to use, B reloads the item in each hand.
        weapons_.AttemptReload(HandRight);
        weapons_.AttemptReload(HandLeft);
    }
    TickPickups();

    RenderView view = camera_.Render(pose, drawEnabled_);
    view.stage = currentStage_;
    dl = render_.Render(dl, resourceManager, view);
    stats_[0].timeOtherPlayersOnScreen += chrs_.TakeDrawnCount();
    // TODO(https://github.com/liz-baker/frigate/issues/136): weaponRenderTracers, bullet
    // sparks, glassRenderShards and explosionRenderFlyingParticles.

    // maybe_mp_interface: the intro camera shows no guns; otherwise they are
    // drawn last, over the level, and then the watch and the HUD.
    const bool intro = pose.cameraMode == CameraMode::Intro;
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
        dl = watch_.Render(dl, resourceManager, watchView);
    }
    const HudFrame hudFrame = MakeHudFrame(pose);
    dl = intro ? hud_.RenderIntroCamera(dl, hudFrame) : hud_.Render(dl, hudFrame);

    using namespace Frigate::F3dex;
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, camera_.Metrics().x, camera_.Metrics().y);
    return dl;
}

} // namespace Frigate::Level
