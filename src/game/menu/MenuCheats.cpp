#include "game/menu/MenuCheats.h"

#include "SfxIds.h"
#include "game/FrontState.h"
#include "game/menu/CheatIds.h"
#include "game/menu/MenuAudio.h"
#include "game/save/SaveData.h"
#include "game/save/SaveManager.h"
#include "game/save/SaveProgress.h"
#include "level/CheatMasks.h"

namespace Frigate::Game::Menu {

namespace {

// The cheat bits and saves belong to the four folders only.
constexpr int32_t kFolderCount = 4;
// The time a stage unlock code records (0x5F5E0FF).
constexpr int32_t kUnlockCodeTime = 99999999;

bool ValidFolder(int32_t folder) {
    return folder >= 0 && folder < kFolderCount;
}

} // namespace

void TickMenuCheatInput(FrontState& front, uint16_t held, uint16_t pressed, bool inTitle) {
    if (!inTitle) {
        front.menuCheatInput.Clear();
        return;
    }
    // Every menu code shows text, so a completed one is always switched on
    // rather than toggled.
    for (const int32_t cheat : front.menuCheatInput.Feed(held, pressed, Level::CheatMask::kMenus)) {
        TurnOnMenuCheat(front, cheat);
    }
}

void TurnOnMenuCheat(FrontState& front, int32_t cheat) {
    const int32_t folder = front.progress != nullptr ? front.progress->CurrentFolder() : -1;
    if (cheat == Cheat::kExtraMpChars) {
        front.mpUnlocks.UnlockAllCharacters();
    } else if (cheat >= Cheat::kUnlockPaintball && cheat <= Cheat::kUnlockAllGuns) {
        if (ValidFolder(folder)) {
            front.progress->UnlockCheat(folder, cheat - Cheat::kUnlockPaintball);
            Audio().PlaySfx(AudioDriver::kSfxCameraBeep1);
        }
    } else if (cheat >= Cheat::kUnlockDam && cheat <= Cheat::kUnlockEgypt) {
        // A stage's code records the stage before it as completed on Agent, when
        // exactly one of the stages before the code's own is not completed
        // there. Dam's own code therefore does nothing, and Facility's
        // completes Dam.
        const int32_t max = cheat - Cheat::kUnlockDam;
        if (ValidFolder(folder)) {
            int32_t completed = 0;
            for (int32_t stage = 0; stage < max; stage++) {
                if (front.progress->StageStatusFor(folder, stage, Save::kDifficultyAgent) ==
                    Save::StageStatus::Completed) {
                    completed++;
                }
            }
            if (max - 1 == completed) {
                front.progress->UnlockStage(folder, max - 1, Save::kDifficultyAgent, kUnlockCodeTime);
                Audio().PlaySfx(AudioDriver::kSfxCameraBeep1);
            }
        }
    }
}

} // namespace Frigate::Game::Menu
