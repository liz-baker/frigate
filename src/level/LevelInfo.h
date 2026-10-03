#pragma once

#include <cstddef>
#include <cstdint>

namespace Frigate::Level {

// The values the game uses for LEVELID (bondconstants.h).
enum class LevelId : int32_t {
    Bunker1 = 9,
    Silo = 20,
    Statue = 22,
    Control,
    Archives,
    Train,
    Frigate,
    Bunker2,
    Aztec,
    Streets,
    Depot,
    Complex,
    Egypt,
    Dam,
    Facility,
    Runway,
    Surface,
    Jungle,
    Temple,
    Caverns,
    Citadel,
    Cradle,
    Sho,
    Surface2,
    Eld,
    Basement,
    Stack,
    Lue,
    Library,
    Rit,
    Caves,
    Ear,
    Lee,
    Lip,
    Cuba,
    Wax,
    Pam,
    Max,
    Title = 90,
};

// One row of the game's per-stage table (levelinfotable).
struct LevelEntry {
    LevelId id;
    // Directory of the stage's packed rooms and header: `bg/<bgName>/...`.
    const char* bgName;
    // Resource path of the stage's stan (collision) file.
    const char* stanName;
    float scale;
    float visibility;
    float unknownFloat;
};

// The table row for `id`. An id with no row gets row 0, as in the game. The
// row's index is what the game keys its per-stage special cases on.
size_t FindLevelIndex(LevelId id);

const LevelEntry& LevelEntryAt(size_t index);

// The stage's setup file as packed (`obseg/setup/U<name>`), without the
// country prefix; null when the stage has none.
// Layout: docs/formats/goldeneye-stage-setup.md.
const char* SetupName(LevelId id);

// What the stage pool holds where `struct player` keeps animFlipFlag (offset
// 0x5BC) when the stage loads. The game never initialises that byte and
// allocates the player struct from the pool without clearing it, so
// bheadAdjustAnimation's first read of it as the gait animation's mirror flag
// sees whatever an earlier stage left at that address, which depends on where
// the stage's player struct lands. The flag decides which way the
// sprint animation's root motion drifts sideways: on Dam it decides whether
// the first demo's shot lands on the padlock. Dam's is the one value derived so
// far, from replaying Dam_1's recorded random bytes (every packet matches with
// it set, packet 355 diverges without).
// TODO(https://github.com/liz-baker/frigate/issues/262): the other stages'
// values, which the other demos' recordings can tell apart.
bool StagePoolAnimFlipFlag(LevelId id);

} // namespace Frigate::Level
