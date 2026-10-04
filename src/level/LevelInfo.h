#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

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

// The value `struct player`'s animFlipFlag has when the stage loads. The game
// never initialises it and the stage pool isn't cleared, so it is whatever the
// title stage's texture pool held at that address. Dam, Frigate and Runway
// land in the part texReset fills from the ROM, so their values are derived
// (Runway's is 0). Facility, Bunker1, Silo and Train land in the title's
// display-list and vertex buffers, which can't be derived, so they get 0.
// Derivation: docs/determinism-anim-flip-flag.md.
// TODO(https://github.com/liz-baker/frigate/issues/262): the non-derivable
// stages' values.
bool StagePoolAnimFlipFlag(LevelId id);

// The animFlipFlag attract demo `demoIndex` (ramrom table order) was recorded
// with, for the demos whose recording needs a value other than its stage's.
// These are fitted to the recordings rather than derived, so they apply to
// attract playback only; empty means the stage's StagePoolAnimFlipFlag.
// Evidence per demo: docs/determinism-anim-flip-flag.md.
std::optional<bool> DemoAnimFlipFlagOverride(size_t demoIndex);

} // namespace Frigate::Level
