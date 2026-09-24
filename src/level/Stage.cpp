#include "level/Stage.h"

#include "game/menu/BasicGeometry.h"
#include "gfx/GbiF3dex.h"

namespace Frigate::Level {

namespace {

// 60 Hz: a clock tick's length in seconds is 1/60 (US).
constexpr float kTicksPerSecond = 60.0f;

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

Stage::Stage(BgSource& source) : level_(source), visibility_(level_), render_(level_, visibility_) {
}

bool Stage::Load(LevelId stage) {
    loaded_ = false;
    error_.clear();
    currentStage_ = stage;

    drawEnabled_ = true;
    controlsLocked_ = false;
    clock_ = StageClock{};
    // The game also clears the idle-input and multiplayer clocks (D_80048388
    // through D_80048394, g_CurrentMultiPlayerSec); nothing in a single-player
    // stage reads them.
    // TODO(https://github.com/liz-baker/frigate/issues/136): with multiplayer.

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
    // initializeRoomData, which load_bg_file calls once the rooms are sized.
    if (level_.Header().firstWord == 0) {
        render_.Reset();
    }

    // TODO(https://github.com/liz-baker/frigate/issues/135): skySetStageNum.
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
    // TODO(https://github.com/liz-baker/frigate/issues/134): sub_GAME_7F09B820,
    // init_load_objpos_table, init_path_table_links, proplvreset2 and the
    // setup file they read.
    // TODO(https://github.com/liz-baker/frigate/issues/41): reinit_between_menus'
    // sound reset and init_sound_effects_registers.
    // TODO(https://github.com/liz-baker/frigate/issues/109): init_guards and bodiesReset.
    // TODO(https://github.com/liz-baker/frigate/issues/127): alloc_explosion_smoke_casing_scorch_impact_buffers.
    // TODO(https://github.com/liz-baker/frigate/issues/108): the per-player init
    // (reinit_gunheld_totaltime, init_player_BONDdata_stats, init_player_BONDdata,
    // sets_a_bunch_of_BONDdata_values_to_default).
    // TODO(https://github.com/liz-baker/frigate/issues/134): bondviewLoadSetupIntroSection and
    // bondviewPlayerBeginLife, which place Bond at the stage's spawn.

    // zbufDeallocate and viSetVideoMode(MD_NORMAL) leave the stage-load
    // allocation mode and pick the video mode; the renderer owns the depth
    // buffer and the frame is always 320x240. D_80048368, which the game sets
    // to 1.0 here, has no reader.
    loaded_ = true;
    return true;
}

void Stage::Unload() {
    // cleanup_rooms is the only part of lvlUnloadStageTextData the stage owns
    // so far; the rest cleans up systems that are not ported yet.
    // TODO(https://github.com/liz-baker/frigate/issues/136): the guard, object, explosion,
    // window-piece, alarm, objective and sound cleanups.
    level_.Cleanup();
    loaded_ = false;
}

void Stage::Tick() {
    clock_.stageTicks += clock_.clockTimer;
    clock_.stageTimeSec = static_cast<float>(clock_.stageTicks) / kTicksPerSecond;

    // updateRoomStatusFlags
    render_.Tick();
    // TODO(https://github.com/liz-baker/frigate/issues/135): skyTick.
    // TODO(https://github.com/liz-baker/frigate/issues/136): bullet_sparks_update_all,
    // update_bullet_casings, update_broken_windows and
    // explosionUpdateFlyingParticles.
    // TODO(https://github.com/liz-baker/frigate/issues/109): lvlSetMultipliersForDifficulty
    // and chrpropTick.
}

Gfx* Stage::Render(Gfx* dl, const PlayerPose& pose) {
    // gSPSegment for the font display list and gSPClipRatio(FRUSTRATIO_2) have
    // no counterpart: text is drawn from resources and Fast3D clips on its own.
    dl = Game::Menu::FastPipelineSetup(dl);
    dl = Game::Menu::ZBufferGeometry(dl);

    // viClearZBufCurrentPlayer is the renderer's own depth clear.
    dl = camera_.SetupView(dl);
    camera_.Update(pose, level_);
    dl = FillUncoveredScreen(dl, camera_.Metrics());
    // Whatever skyRender leaves the pipeline in, the rooms draw from.
    // TODO(https://github.com/liz-baker/frigate/issues/135): skyRender.
    dl = Game::Menu::FastPipelineSetup(dl);
    dl = Game::Menu::ZBufferGeometry(dl);

    // bgRoomVisibilityRelated
    visibility_.Update(camera_.Visibility(pose), pose.previousPosition, pose.cameraMode);
    // TODO(https://github.com/liz-baker/frigate/issues/126): propsTick and
    // the prop passes' per-frame updates.
    // TODO(https://github.com/liz-baker/frigate/issues/109): chraiUpdateOnscreenPropCount,
    // chrpropUpdateAutoaimTarget, chraiCheckUseHeldItems.

    dl = render_.Render(dl, camera_.Render(pose, drawEnabled_));
    // TODO(https://github.com/liz-baker/frigate/issues/136): weaponRenderTracers, bullet
    // sparks, glassRenderShards and explosionRenderFlyingParticles.

    using namespace Frigate::F3dex;
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, camera_.Metrics().x, camera_.Metrics().y);
    return dl;
}

} // namespace Frigate::Level
