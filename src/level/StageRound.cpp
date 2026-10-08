#include "level/Stage.h"

#include <algorithm>

#include "audio/MusicPlayer.h"
#include "audio/SfxIds.h"
#include "game/Sound.h"
#include "game/TextRender.h"
#include "level/MpWatchMenuDraw.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

// MPMENU_STR_00_LEMMINGAWARD onwards: the award names, then the four rank lines
// (MPMENU_STR_11_RANK1ST).
constexpr int32_t kRankTextBase = 0x11;

// 0x258 ticks (ten seconds) before the limit the alarm sounds, and 0xE10 (a
// minute) before it the message shows.
constexpr int kAlarmTicksBeforeEnd = 0x258;
constexpr int kMessageTicksBeforeEnd = 0xE10;

// g_gameOverFlag's countdown when the scores wait for it.
constexpr int kGameOverDelay = 300;
constexpr int kChevronGlowTicks = 20;
constexpr int kGameOverMessageTicks = 120;
constexpr int kHealthDisplayTicks = 60;

// joyGetStickXInRange(player, -2, 1): the stick's x from -2 (full left) to 1
// (full right), 3 steps over the stick's -60 to 60.
int StickXInRange(int stickX) {
    constexpr int kOffset = 60;
    constexpr int kMax = 120;
    const int clamped = std::clamp(stickX + kOffset, 0, kMax);
    return ((clamped * 3) / kMax) - 2;
}

bool PressingRight(const MoveInput& input, const MpMenuState& menu) {
    return (input.pressed & (Buttons::RightPad | Buttons::RightC)) != 0 ||
           (StickXInRange(input.stickX) >= 1 && menu.joyWasCentre);
}

bool PressingLeft(const MoveInput& input, const MpMenuState& menu) {
    return (input.pressed & (Buttons::LeftPad | Buttons::LeftC)) != 0 ||
           (StickXInRange(input.stickX) < -1 && menu.joyWasCentre);
}

} // namespace

ScoreBoard Stage::MakeScoreBoard() const {
    ScoreBoard board;
    board.scenario = multiplayer_ ? multiplayer_->scenario : Game::MpScenario::Normal;
    board.players = playerCount_;
    for (size_t player = 0; player < kMpPlayers; player++) {
        const PlayerStats& stats = stats_[player];
        board.killCounts[player] = stats.killCounts;
        board.teamOrToken[player] = stats.haveTokenOrGoldenGun;
        board.killedGoldenGunOwner[player] = stats.killedGoldenGunOwner;
        board.orderOutInYolt[player] = stats.orderOutInYolt;
        board.flagCounter[player] = stats.flagCounter;
    }
    return board;
}

int32_t Stage::PointsFor(int player) const {
    return Level::PointsFor(MakeScoreBoard(), player);
}

int Stage::RankOf(int player) const {
    return Level::RankOf(MakeScoreBoard(), player);
}

std::string Stage::RankText(int player) const {
    return std::string(lang_.Get(StringId(TextBank::MpMenu, kRankTextBase + RankOf(player))));
}

std::string_view Stage::AwardText(int player, int which) const {
    const MpMenuState& menu = players_[static_cast<size_t>(player)].menu;
    const int award = which == 0 ? menu.firstAward : menu.secondAward;
    return award < 0 ? std::string_view() : lang_.Get(StringId(TextBank::MpMenu, award));
}

bool Stage::PlayerDying(int player) const {
    const PlayerState& state = players_[static_cast<size_t>(player)];
    return state.health.IsDead() &&
           (!state.health.RedBloodFinished() || !state.health.DeathAnimFinished() || !state.colour.IsComplete());
}

MpWatchInputs Stage::MakeMenuInputs() const {
    const PlayerState& state = Cur();
    MpWatchInputs inputs;
    inputs.playerCount = playerCount_;
    inputs.player = current_;
    inputs.view = state.camera.Metrics();
    inputs.menu = state.menu;
    inputs.gameOverFlag = gameOver_;
    inputs.stopPlay = stopPlay_;
    inputs.paused = paused_;
    inputs.pausedByPlayer = current_ == whoPaused_;
    inputs.chevronGlow = chevronGlow_;
    inputs.altGameOverMessage = altGameOverMessage_;
    inputs.board = MakeScoreBoard();
    inputs.dead = state.health.IsDead();
    inputs.deathAnimFinished = state.health.DeathAnimFinished();
    inputs.redBloodFinished = state.health.RedBloodFinished();
    inputs.string = [this](int32_t slot) { return lang_.Get(StringId(TextBank::MpMenu, slot)); };
    inputs.rank = RankText(current_);
    inputs.firstAward = std::string(AwardText(current_, 0));
    inputs.secondAward = std::string(AwardText(current_, 1));
    // frontGetPlayersFavoriteWeaponInHand(player, 0): the right hand's.
    if (favouriteWeapons_ != nullptr && state.weapons.Ready()) {
        const int32_t item = favouriteWeapons_->Weapon(current_, Game::FavouriteWeapons::kRight);
        inputs.weaponOfChoice = std::string(lang_.Get(static_cast<int32_t>(state.weapons.Table().Watch(item).longName)));
    }
    return inputs;
}

Gfx* Stage::RenderMultiplayerMenu(Gfx* dl) {
    MpWatchPlan& drawn = menuDrawn_[static_cast<size_t>(current_)];
    drawn = {};
    const Font* font = Cur().hud.GothicFont();
    if (playerCount_ == 1 || font == nullptr) {
        return dl;
    }
    drawn = PlanMpWatch(MakeMenuInputs(), [font](std::string_view text) {
        const Game::Text::Extent extent = Game::Text::Measure(*font, text);
        return MpTextExtent{ extent.width, extent.height };
    });
    return DrawMpWatchPlan(dl, drawn, *font, Cur().camera.Metrics(), Cur().camera.Player());
}

void Stage::PlayMenuBeep() {
    Game::Sound::PlaySfx(AudioDriver::kSfxCameraBeep1);
}

void Stage::ManageMultiplayerGame(int ticksBefore) {
    // The dead players' blood runs out and the music comes back.
    if (music_.State() == Game::MissionState::Dead) {
        int dead = 0;
        int bloodFinished = 0;
        for (int player = 0; player < playerCount_; player++) {
            const PlayerState& state = players_[static_cast<size_t>(player)];
            if (state.health.IsDead()) {
                dead++;
                if (state.health.RedBloodFinished()) {
                    bloodFinished++;
                }
            }
        }
        if (dead > 0 && dead == bloodFinished) {
            music_.SetState(Game::MissionState::Theme);
        }
    }

    if (mpTime_ > 0) {
        const int now = ticksBefore;
        const int next = ticksBefore + clock_.clockTimer;

        if (now < mpTime_ - kMessageTicksBeforeEnd && next >= mpTime_ - kMessageTicksBeforeEnd) {
            for (int player = 0; player < playerCount_; player++) {
                AsPlayer(player, [&] { Cur().hud.BottomShow("One minute left"); });
            }
        }

        // The alarm sounds for the last ten seconds, and plays again each time
        // it ends.
        if (next >= mpTime_ - kAlarmTicksBeforeEnd && mpAlarmSound_ == nullptr && !controlsLocked_) {
            Game::Sound::PlaySfx(AudioDriver::kSfxAlarm1, &mpAlarmSound_);
        }
        if (controlsLocked_ && mpAlarmSound_ != nullptr && Game::Sound::GetPlayingState(mpAlarmSound_) != 0) {
            Game::Sound::Deactivate(mpAlarmSound_);
        }

        if (now < mpTime_ && next >= mpTime_) {
            CalculateAwards(false);
        }
    }

    // A point limit ends the game once the players who dying have finished.
    if (mpPoint_ > 0 && clock_.clockTimer != 0) {
        int dying = 0;
        int overLimit = 0;
        for (int player = 0; player < playerCount_; player++) {
            if (PlayerDying(player)) {
                dying++;
            }
            if (PointsFor(player) >= mpPoint_) {
                overLimit++;
            }
        }
        if (overLimit > 0) {
            if (dying == 0) {
                CalculateAwards(false);
            } else {
                // The players stop answering their pads while the dying finish.
                SetStopPlay();
            }
        }
    }

    TickYolt();
}

void Stage::CalculateAwards(bool gameOverDelay) {
    const int players = playerCount_;
    const int32_t duration = missionTimer_;

    Game::Sound::DeactivateAllSfxByFlag1();
    music_.Stop();

    // The end jingle plays on the first music slot at the music volume.
    Game::Sound::MusicTrackApplySeqpVol(0, Game::Sound::GetMTrack2Vol());
    Game::Sound::SetMusicTrackFade(0, AudioDriver::kMusicFadeUnset);
    Game::Sound::MusicTrackPlay(0, AudioDriver::kMusicIntroSwoosh);

    // pauseAndLockControls.
    controlsLocked_ = true;
    paused_ = true;

    gameOver_ = gameOverDelay ? kGameOverDelay : 1;
    altGameOverMessage_ = true;
    // The decomp seeds this with the chevron's timer length, not the message's.
    altGameOverMessageTimer_ = kChevronGlowTicks;
    chevronGlow_ = false;
    chevronGlowTimer_ = 0;

    std::array<AwardInput, kMpPlayers> inputs{};
    for (int player = 0; player < players; player++) {
        const size_t at = static_cast<size_t>(player);
        PlayerState& state = players_[at];
        const PlayerStats& stats = stats_[at];

        state.menu.on = true;
        state.menu.mode = MpMenuMode::Scores;
        state.menu.firstAward = -1;
        state.menu.secondAward = -1;

        if (state.weapons.Ready()) {
            const std::array<int32_t, 2> choice = state.weapons.WeaponOfChoice();
            if (favouriteWeapons_ != nullptr) {
                favouriteWeapons_->Store(player, choice[Game::FavouriteWeapons::kRight],
                                         choice[Game::FavouriteWeapons::kLeft]);
            }
        }

        AwardInput& input = inputs[at];
        input.shotsTotal = stats.shotCount[0];
        input.shotsHead = stats.shotCount[1];
        input.damageToBackside = stats.damageToBackside;
        input.timeOtherPlayersOnScreen = stats.timeOtherPlayersOnScreen;
        input.distanceTraveled = stats.distanceTraveled;
        input.bodyArmourPickups = stats.bodyArmorPickups;
        input.longestInning = stats.longestInning;
        input.shortestInning = stats.shortestInning;
        input.mostKilledOneTime = stats.mostKilledOneTime;
    }

    std::array<std::array<int32_t, kMpPlayers>, kMpPlayers> kills{};
    for (size_t killer = 0; killer < kMpPlayers; killer++) {
        kills[killer] = stats_[killer].killCounts;
    }

    std::array<AwardMetrics, kMpPlayers> metrics = BuildMetrics(players, inputs, kills, duration);
    ChooseEligibleAwards(players, metrics, inputs, RandomGetNext);
    const std::array<AwardPick, kMpPlayers> picks = PickAwards(players, metrics, RandomGetNext);
    for (int player = 0; player < players; player++) {
        const size_t at = static_cast<size_t>(player);
        players_[at].menu.firstAward = picks[at].first;
        players_[at].menu.secondAward = picks[at].second;
    }
}

void Stage::TickMultiplayerMenu(const MoveInput& input) {
    const int player = current_;
    MpMenuState& menu = Cur().menu;

    // The player who moves first drives g_gameOverFlag down, which is both a
    // flag and a timer.
    if (PositionInShuffled(player) == 0 && gameOver_ >= 2) {
        gameOver_ -= speedFrames_;
        if (gameOver_ <= 0) {
            gameOver_ = 1;
        }
    }

    if (playerCount_ == 1) {
        return;
    }

    // A player who dies with the menu up while the game goes on loses it.
    if (Cur().health.IsDead() && gameOver_ == 0) {
        menu.on = false;
        Cur().hud.Health().displayTime = 0;
        return;
    }

    if (gameOver_ >= 2) {
        return;
    }

    if (PositionInShuffled(player) == 0) {
        chevronGlowTimer_ += speedFrames_;
        altGameOverMessageTimer_ += speedFrames_;
        if (chevronGlowTimer_ >= kChevronGlowTicks) {
            chevronGlowTimer_ -= kChevronGlowTicks;
            chevronGlow_ = !chevronGlow_;
        }
        // Flips between "GAME OVER" and "START TO EXIT".
        if (altGameOverMessageTimer_ >= kGameOverMessageTicks) {
            altGameOverMessageTimer_ -= kGameOverMessageTicks;
            altGameOverMessage_ = !altGameOverMessage_;
        }
    }

    PlayerStats& stats = CurStats();
    if (stats.mostKilledOneLife < Cur().killsThisLife) {
        stats.mostKilledOneLife = Cur().killsThisLife;
    }
    if (stats.longestInning < missionTimer_ - Cur().lifeStartTime) {
        stats.longestInning = missionTimer_ - Cur().lifeStartTime;
    }

    const bool joyCentred = StickXInRange(input.stickX) == 0 || StickXInRange(input.stickX) == -1;

    if (menu.on) {
        const bool right = PressingRight(input, menu);
        const bool left = PressingLeft(input, menu);
        const bool confirm = (input.pressed & (Buttons::A | Buttons::Start)) != 0;
        const bool aPressed = (input.pressed & Buttons::A) != 0;
        const bool bPressed = (input.pressed & Buttons::B) != 0;
        const bool game = gameOver_ != 0;

        if (right && MenuCanGoRight(menu.mode, game)) {
            PlayMenuBeep();
            menu.mode = static_cast<MpMenuMode>(static_cast<int>(menu.mode) + 1);
        } else if (left && MenuCanGoLeft(menu.mode, game)) {
            PlayMenuBeep();
            menu.mode = static_cast<MpMenuMode>(static_cast<int>(menu.mode) - 1);
        } else if (right && menu.mode == MpMenuMode::ExitConfirm) {
            PlayMenuBeep();
            menu.quitConfirm = true;
        } else if (left && menu.mode == MpMenuMode::ExitConfirm) {
            PlayMenuBeep();
            menu.quitConfirm = false;
        } else if (aPressed && menu.mode == MpMenuMode::Pause) {
            PlayMenuBeep();
            if (!paused_) {
                paused_ = true;
                whoPaused_ = current_;
                controlsLocked_ = true;
            } else if (current_ == whoPaused_) {
                paused_ = false;
                controlsLocked_ = false;
            }
        } else if (menu.mode == MpMenuMode::Finished) {
            if (bPressed) {
                PlayMenuBeep();
                menu.on = true;
                menu.mode = MpMenuMode::Scores;
            }
        } else if ((confirm && ((menu.mode != MpMenuMode::Exit && menu.mode != MpMenuMode::ExitConfirm) ||
                                (menu.mode == MpMenuMode::ExitConfirm && !menu.quitConfirm))) ||
                   bPressed) {
            PlayMenuBeep();
            if (game) {
                // Once every player has dismissed the scores, the stage ends.
                menu.mode = MpMenuMode::Finished;
                int finished = 0;
                for (int other = 0; other < playerCount_; other++) {
                    if (players_[static_cast<size_t>(other)].menu.mode == MpMenuMode::Finished) {
                        finished++;
                    }
                }
                if (finished == playerCount_) {
                    RunTitleStage();
                }
            } else {
                menu.on = false;
                Cur().hud.Health().displayTime = kHealthDisplayTicks;
                if (current_ == whoPaused_) {
                    paused_ = false;
                    controlsLocked_ = false;
                }
            }
        } else if (confirm && menu.mode == MpMenuMode::Exit) {
            PlayMenuBeep();
            menu.mode = MpMenuMode::ExitConfirm;
            menu.quitConfirm = false;
        } else if (confirm) {
            if (menu.mode == MpMenuMode::ExitConfirm && menu.quitConfirm) {
                PlayMenuBeep();
                menu.on = false;
                Cur().hud.Health().displayTime = 0;
                CalculateAwards(false);
            }
        }

        menu.joyWasCentre = joyCentred;
        return;
    }

    if ((input.pressed & Buttons::Start) != 0) {
        PlayMenuBeep();
        menu.on = true;
        menu.mode = MpMenuMode::Scores;
        menu.joyWasCentre = true;
        Cur().health.SyncApparent();
    }
}

} // namespace Frigate::Level
