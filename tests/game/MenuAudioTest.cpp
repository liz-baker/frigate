#include <cstdio>

#include "RecordingMenuAudio.h"
#include "game/menu/MenuAudio.h"

using namespace Frigate::Game::Menu;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// The SFX_ID values of the GoldenEye decomp's enum, counting from NOTHING_SFX.
constexpr int kOptionClick2 = 18;
constexpr int kPaperTurn = 77;
constexpr int kCopyFile = 79;
constexpr int kGunM60AmmGun3 = 118;
constexpr int kDoorMetalClose = 197;
constexpr int kDoorMetalClose2 = 199;
constexpr int kDoorLock = 222;
constexpr int kRarelogo = 258;

void TestFolderCues() {
    Check(SfxFor(FolderSfx::DoorMetalClose) == kDoorMetalClose, "DOOR_METAL_CLOSE_SFX");
    Check(SfxFor(FolderSfx::DoorMetalClose2) == kDoorMetalClose2, "DOOR_METAL_CLOSE2_SFX");
    Check(SfxFor(FolderSfx::PaperTurn) == kPaperTurn, "PAPER_TURN_SFX");
}

void TestFileSelectCues() {
    Check(SfxFor(FileSelectSfx::OptionClick2) == kOptionClick2, "OPTION_CLICK2_SFX");
    Check(SfxFor(FileSelectSfx::GunM60AmmGun3) == kGunM60AmmGun3, "GUN_M60AMMGUN_3_SFX");
    Check(SfxFor(FileSelectSfx::PaperTurn) == kPaperTurn, "PAPER_TURN_SFX");
    Check(SfxFor(FileSelectSfx::CopyFile) == kCopyFile, "COPY_FILE_SFX");
    Check(SfxFor(FileSelectSfx::DoorLock) == kDoorLock, "DOOR_LOCK_SFX");
    Check(Frigate::AudioDriver::kSfxRarelogo == kRarelogo, "RARELOGO_SFX");
}

void TestPlayCuesSendsEachInOrder() {
    RecordingMenuAudio recorder;
    SetMenuAudio(&recorder);
    PlayCues(std::vector<FolderSfx>{ FolderSfx::PaperTurn, FolderSfx::DoorMetalClose });
    PlayCues(std::vector<FileSelectSfx>{ FileSelectSfx::DoorLock });
    PlayCues(std::vector<FolderSfx>{});
    SetMenuAudio(nullptr);
    Check(recorder.sfx.size() == 3, "three cues played");
    Check(recorder.sfx.size() == 3 && recorder.sfx[0] == kPaperTurn && recorder.sfx[1] == kDoorMetalClose &&
              recorder.sfx[2] == kDoorLock,
          "in the order raised");
}

} // namespace

int main() {
    TestFolderCues();
    TestFileSelectCues();
    TestPlayCuesSendsEachInOrder();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    return 0;
}
