#pragma once

#include <array>
#include <cstdint>
#include <functional>

// The music a stage plays (mp_music.c and music_0D2720.c): a state machine that
// puts the stage's theme, its tense "x" track and the pause jingle on the three
// music slots, and the AI's timed music slots that switch between them.
namespace Frigate::Game {

// MISSION_STATE_ID.
enum class MissionState : int {
    // Nothing plays.
    Stopped = 0,
    // The stage's theme.
    Theme = 1,
    // The tense track over the faded theme.
    Tense = 2,
    // The watch menu's music over the faded stage music.
    Watch = 3,
    // The theme with its background sound on slot 3.
    ThemeBackground = 4,
    // The tense track with the background sound.
    TenseBackground = 5,
    // The death music (the multiplayer's).
    Dead = 6,
};

class MissionMusic {
  public:
    // randomGetNext, for the stage that picks a random theme.
    using Random = std::function<uint32_t()>;
    explicit MissionMusic(Random random) : mRandom(std::move(random)) {
    }

    // sub_GAME_7F0C11FC: a stage starts. Stops the music and plays the stage's
    // theme (`stage` is its LEVELID).
    void StartStage(int stage);
    // set_missionstate.
    void SetState(MissionState state);
    MissionState State() const {
        return mState;
    }
    // set_missionstate_zero.
    void Stop() {
        SetState(MissionState::Stopped);
    }
    // sub_GAME_7F0C1310 and sub_GAME_7F0C1340: the watch menu opens over
    // whatever plays, and closes again.
    void EnterWatch();
    void LeaveWatch();

    // musicPlaySlot: the AI asks for music slot `slot` (0 to 3) to play,
    // after `stoppedSeconds` of quiet for `totalSeconds`, counted in ticks.
    void PlaySlot(int slot, int stoppedSeconds, int totalSeconds);
    // musicStopSlot: slot `slot`, or all of them for a negative slot.
    void StopSlot(int slot);
    // sub_GAME_7F0C1364: a stage load clears the AI's slots.
    void ResetSlots();
    // reset_all_music_slots: counts the slots down by `clockTimer` ticks and
    // moves between the theme and the tense track as they start and run out.
    void TickSlots(int clockTimer);

    // The stage's tracks (music_setup_entries): getmusictrack_or_randomtrack
    // (the table's theme, or a random one for a stage with none),
    // musicGetBgTrackForStage and musicGetXTrackForStage (negative: none).
    int ThemeFor(int stage);
    static int BackgroundTrackFor(int stage);
    static int TenseTrackFor(int stage);

  private:
    // Handles a change from `from` to `to`; false for one the table does not
    // list, which falls to the next state's handler as the original's does.
    bool Handle(MissionState from, MissionState to);
    // sub_GAME_7F0C1288 and sub_GAME_7F0C12CC.
    void RaiseTense();
    void LowerTense();

    Random mRandom;
    MissionState mState = MissionState::Stopped;
    int mStageMusicId = 0;
    MissionState mSavedState = MissionState::Stopped;
    std::array<bool, 4> mSlotActive{};
    std::array<int, 4> mSlotStopped{};
    std::array<int, 4> mSlotTotal{};
};

} // namespace Frigate::Game
