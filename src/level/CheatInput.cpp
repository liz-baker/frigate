#include "level/CheatInput.h"

#include "game/Input.h"
#include "game/menu/CheatIds.h"
#include "level/CheatMasks.h"

#include <cstddef>

namespace Frigate::Level {

namespace {

namespace Cheat = Game::Menu::Cheat;
namespace B = Game::Input::Button;

// requiredInputCount of every cheat with a button code.
constexpr int kCodeLength = 10;

// The codes of g_CheatInfo. Feed's mask keeps the menus' codes out of a stage
// and the stage's codes out of the menus.
constexpr uint16_t kInvincibilityCode[kCodeLength] = {
    B::kL | B::kPadDown, B::kR | B::kCRight,   B::kR | B::kCUp,     B::kL | B::kPadRight, B::kL | B::kCDown,
    B::kR | B::kCUp,     B::kL | B::kPadRight, B::kR | B::kPadDown, B::kL | B::kPadLeft,  B::kL | B::kR | B::kCRight,
};
constexpr uint16_t kAllGunsCode[kCodeLength] = {
    B::kL | B::kR | B::kPadDown, B::kL | B::kCLeft, B::kL | B::kCRight, B::kL | B::kR | B::kCLeft,
    B::kL | B::kPadDown,         B::kL | B::kCDown, B::kR | B::kCLeft,  B::kL | B::kR | B::kCRight,
    B::kR | B::kPadUp,           B::kL | B::kCLeft,
};
constexpr uint16_t kMaxAmmoCode[kCodeLength] = {
    B::kL | B::kR | B::kCRight, B::kR | B::kPadUp,           B::kR | B::kPadDown, B::kR | B::kPadDown,
    B::kL | B::kR | B::kCRight, B::kL | B::kR | B::kPadLeft, B::kR | B::kPadDown, B::kR | B::kPadUp,
    B::kL | B::kR | B::kCRight, B::kR | B::kPadLeft,
};
constexpr uint16_t kDebugUnk5Code[kCodeLength] = {
    B::kL | B::kPadUp,           B::kR | B::kCRight,           B::kR | B::kCDown,   B::kL | B::kR | B::kCLeft,
    B::kL | B::kR | B::kPadLeft, B::kL | B::kR | B::kPadRight, B::kR | B::kPadDown, B::kR | B::kCUp,
    B::kR | B::kCRight,          B::kL | B::kR | B::kPadDown,
};
constexpr uint16_t kDeactivateInvincibilityCode[kCodeLength] = {
    B::kR | B::kPadRight, B::kL | B::kPadLeft, B::kL | B::kPadLeft,        B::kR | B::kPadUp,   B::kL | B::kPadDown,
    B::kR | B::kCDown,    B::kL | B::kCLeft,   B::kL | B::kR | B::kCRight, B::kL | B::kPadDown, B::kL | B::kCUp,
};
constexpr uint16_t kBondPhaseCode[kCodeLength] = {
    B::kL | B::kCUp,      B::kL | B::kR | B::kCLeft, B::kR | B::kPadUp,  B::kL | B::kCRight, B::kR | B::kCLeft,
    B::kL | B::kPadRight, B::kL | B::kR | B::kCLeft, B::kL | B::kCRight, B::kL | B::kPadUp,  B::kL | B::kR | B::kCDown,
};
constexpr uint16_t kLineModeCode[kCodeLength] = {
    B::kR | B::kCDown, B::kL | B::kR | B::kPadDown, B::kL | B::kPadRight, B::kR | B::kCUp,   B::kL | B::kR | B::kCRight,
    B::kR | B::kPadUp, B::kL | B::kPadDown,         B::kL | B::kPadRight, B::kR | B::kCLeft, B::kR | B::kCUp,
};
constexpr uint16_t kInvisibilityCode[kCodeLength] = {
    B::kR | B::kCLeft,         B::kL | B::kR | B::kCUp, B::kL | B::kR | B::kPadLeft,
    B::kL | B::kR | B::kPadUp, B::kR | B::kPadUp,       B::kL | B::kCLeft,
    B::kR | B::kCUp,           B::kL | B::kCDown,       B::kL | B::kR | B::kPadLeft,
    B::kR | B::kPadRight,
};

// The menus' codes: the extra multiplayer characters, then the cheat unlocks
// (typed on the cheat menu) and the stage unlocks (typed on mission select).
constexpr uint16_t kExtraMpCharsCode[kCodeLength] = {
    B::kL | B::kR | B::kCLeft,
    B::kL | B::kCUp,
    B::kL | B::kR | B::kPadLeft,
    B::kL | B::kPadRight,
    B::kR | B::kPadDown,
    B::kL | B::kR | B::kCLeft,
    B::kL | B::kCUp,
    B::kL | B::kR | B::kPadRight,
    B::kL | B::kR | B::kCDown,
    B::kL | B::kPadDown,
};
constexpr uint16_t kUnlockPaintballCode[kCodeLength] = {
    B::kL | B::kPadUp, B::kCUp,           B::kR | B::kPadRight,      B::kL | B::kR | B::kCLeft, B::kL | B::kPadUp,
    B::kR | B::kCDown, B::kL | B::kCDown, B::kL | B::kR | B::kCDown, B::kL | B::kR | B::kPadUp, B::kL | B::kCDown,
};
constexpr uint16_t kUnlockInvincibleCode[kCodeLength] = {
    B::kR | B::kPadLeft,
    B::kL | B::kPadDown,
    B::kPadLeft,
    B::kPadUp,
    B::kPadDown,
    B::kR | B::kCLeft,
    B::kL | B::kCLeft,
    B::kL | B::kR | B::kPadLeft,
    B::kL | B::kR | B::kPadRight,
    B::kL | B::kCLeft,
};
constexpr uint16_t kUnlockDkModeCode[kCodeLength] = {
    B::kL | B::kR | B::kPadUp,
    B::kCRight,
    B::kR | B::kPadLeft,
    B::kR | B::kPadUp,
    B::kPadUp,
    B::kR | B::kPadRight,
    B::kPadUp,
    B::kL | B::kR | B::kCDown,
    B::kL | B::kR | B::kPadDown,
    B::kL | B::kR | B::kCLeft,
};
constexpr uint16_t kUnlock2xGlCode[kCodeLength] = {
    B::kR | B::kPadDown,
    B::kR | B::kPadUp,
    B::kPadRight,
    B::kL | B::kR | B::kCDown,
    B::kL | B::kPadRight,
    B::kR | B::kPadLeft,
    B::kPadLeft,
    B::kPadDown,
    B::kPadUp,
    B::kR | B::kCDown,
};
constexpr uint16_t kUnlock2xRlCode[kCodeLength] = {
    B::kR | B::kPadRight, B::kL | B::kPadUp, B::kPadDown,       B::kPadDown,         B::kR | B::kCDown,
    B::kL | B::kPadLeft,  B::kL | B::kCLeft, B::kR | B::kPadUp, B::kR | B::kPadDown, B::kR | B::kCLeft,
};
constexpr uint16_t kUnlockTurboCode[kCodeLength] = {
    B::kL | B::kPadDown, B::kL | B::kCDown,    B::kL | B::kR | B::kPadUp, B::kR | B::kCDown,
    B::kPadLeft,         B::kR | B::kPadDown,  B::kL | B::kCDown,         B::kPadUp,
    B::kR | B::kPadDown, B::kL | B::kPadRight,
};
constexpr uint16_t kUnlockNoRadarCode[kCodeLength] = {
    B::kR | B::kPadUp,   B::kCDown,
    B::kCLeft,           B::kCUp,
    B::kL | B::kPadDown, B::kR | B::kPadUp,
    B::kCLeft,           B::kPadRight,
    B::kR | B::kPadLeft, B::kR | B::kPadRight,
};
constexpr uint16_t kUnlockTinyCode[kCodeLength] = {
    B::kL | B::kR | B::kPadDown, B::kR | B::kPadDown, B::kL | B::kCDown, B::kPadLeft,       B::kR | B::kCLeft,
    B::kL | B::kR | B::kCDown,   B::kPadRight,        B::kPadDown,       B::kR | B::kCDown, B::kR | B::kPadRight,
};
constexpr uint16_t kUnlock2xTknifeCode[kCodeLength] = {
    B::kR | B::kCLeft,
    B::kL | B::kPadLeft,
    B::kPadUp,
    B::kL | B::kR | B::kPadRight,
    B::kPadRight,
    B::kL | B::kR | B::kCLeft,
    B::kL | B::kR | B::kCLeft,
    B::kR | B::kPadDown,
    B::kR | B::kPadLeft,
    B::kR | B::kCLeft,
};
constexpr uint16_t kUnlockFastCode[kCodeLength] = {
    B::kL | B::kCDown, B::kL | B::kCLeft,
    B::kCDown,         B::kCRight,
    B::kCLeft,         B::kL | B::kR | B::kPadRight,
    B::kCRight,        B::kL | B::kR | B::kPadUp,
    B::kR | B::kCLeft, B::kL | B::kPadLeft,
};
constexpr uint16_t kUnlockInvisCode[kCodeLength] = {
    B::kL | B::kR | B::kCLeft, B::kL | B::kR | B::kCDown,   B::kL | B::kCLeft,    B::kR | B::kCLeft,
    B::kR | B::kPadRight,      B::kL | B::kR | B::kPadLeft, B::kL | B::kPadRight, B::kPadLeft,
    B::kL | B::kR | B::kCLeft, B::kL | B::kPadDown,
};
constexpr uint16_t kUnlockEnemyRlCode[kCodeLength] = {
    B::kL | B::kR | B::kCDown, B::kCLeft, B::kR | B::kCDown, B::kCDown,         B::kCDown, B::kL | B::kR | B::kCDown,
    B::kL | B::kR | B::kPadUp, B::kCDown, B::kR | B::kPadUp, B::kL | B::kPadUp,
};
constexpr uint16_t kUnlockSlowCode[kCodeLength] = {
    B::kL | B::kR | B::kPadLeft,
    B::kL | B::kR | B::kPadLeft,
    B::kL | B::kR | B::kPadDown,
    B::kL | B::kR | B::kPadLeft,
    B::kCRight,
    B::kL | B::kR | B::kPadDown,
    B::kL | B::kR | B::kPadDown,
    B::kL | B::kPadDown,
    B::kCLeft,
    B::kCUp,
};
constexpr uint16_t kUnlockSilverPpkCode[kCodeLength] = {
    B::kL | B::kPadLeft,          B::kL | B::kR | B::kPadUp,   B::kL | B::kPadRight,        B::kL | B::kR | B::kPadUp,
    B::kL | B::kR | B::kCLeft,    B::kL | B::kR | B::kPadLeft, B::kL | B::kR | B::kPadDown, B::kCDown,
    B::kL | B::kR | B::kPadRight, B::kL | B::kR | B::kPadLeft,
};
constexpr uint16_t kUnlock2xHknifeCode[kCodeLength] = {
    B::kR | B::kCDown,    B::kL | B::kPadRight,         B::kR | B::kCLeft,
    B::kR | B::kPadRight, B::kL | B::kR | B::kPadRight, B::kL | B::kR | B::kPadUp,
    B::kL | B::kPadDown,  B::kR | B::kPadLeft,          B::kL | B::kPadRight,
    B::kL | B::kCLeft,
};
constexpr uint16_t kUnlockInfAmmoCode[kCodeLength] = {
    B::kL | B::kCLeft,
    B::kL | B::kR | B::kPadRight,
    B::kCRight,
    B::kCLeft,
    B::kR | B::kPadLeft,
    B::kL | B::kCDown,
    B::kL | B::kR | B::kPadLeft,
    B::kL | B::kR | B::kCDown,
    B::kL | B::kPadUp,
    B::kCRight,
};
constexpr uint16_t kUnlock2xFnp0Code[kCodeLength] = {
    B::kPadUp,
    B::kPadRight,
    B::kL | B::kPadLeft,
    B::kR | B::kPadDown,
    B::kL | B::kPadUp,
    B::kL | B::kCLeft,
    B::kL | B::kPadLeft,
    B::kCRight,
    B::kCUp,
    B::kL | B::kR | B::kPadDown,
};
constexpr uint16_t kUnlockGoldPpkCode[kCodeLength] = {
    B::kL | B::kR | B::kPadRight,
    B::kL | B::kR | B::kPadDown,
    B::kL | B::kPadUp,
    B::kL | B::kR | B::kPadDown,
    B::kCUp,
    B::kR | B::kPadUp,
    B::kL | B::kR | B::kPadRight,
    B::kL | B::kPadLeft,
    B::kPadDown,
    B::kL | B::kCDown,
};
constexpr uint16_t kUnlock2xLaserCode[kCodeLength] = {
    B::kL | B::kPadRight,
    B::kL | B::kR | B::kCLeft,
    B::kL | B::kPadDown,
    B::kR | B::kPadLeft,
    B::kR | B::kPadDown,
    B::kL | B::kPadRight,
    B::kCUp,
    B::kPadRight,
    B::kR | B::kPadRight,
    B::kL | B::kR | B::kPadUp,
};
constexpr uint16_t kUnlockAllGunsCode[kCodeLength] = {
    B::kPadDown,         B::kPadLeft,       B::kCUp,   B::kPadRight, B::kL | B::kPadDown,
    B::kL | B::kPadLeft, B::kL | B::kPadUp, B::kCLeft, B::kPadLeft,  B::kCDown,
};
constexpr uint16_t kUnlockDamCode[kCodeLength] = {
    B::kL | B::kR | B::kCUp, B::kR | B::kPadLeft,  B::kR | B::kCDown,
    B::kL | B::kCDown,       B::kR | B::kPadUp,    B::kL | B::kPadDown,
    B::kR | B::kCRight,      B::kL | B::kPadRight, B::kL | B::kR | B::kPadRight,
    B::kR | B::kCDown,
};
constexpr uint16_t kUnlockFacilityCode[kCodeLength] = {
    B::kL | B::kR | B::kCUp, B::kR | B::kCLeft,  B::kL | B::kPadLeft,  B::kR | B::kCUp,         B::kL | B::kPadLeft,
    B::kR | B::kCDown,       B::kL | B::kCRight, B::kR | B::kPadRight, B::kL | B::kR | B::kCUp, B::kL | B::kPadRight,
};
constexpr uint16_t kUnlockRunwayCode[kCodeLength] = {
    B::kL | B::kR | B::kPadLeft, B::kR | B::kPadLeft, B::kL | B::kCUp,      B::kL | B::kPadLeft, B::kR | B::kCUp,
    B::kR | B::kCDown,           B::kR | B::kCRight,  B::kR | B::kPadRight, B::kL | B::kPadDown, B::kR | B::kCLeft,
};
constexpr uint16_t kUnlockSurfaceCode[kCodeLength] = {
    B::kR | B::kCLeft, B::kL | B::kR | B::kCUp, B::kL | B::kPadLeft,  B::kR | B::kPadUp,  B::kR | B::kPadLeft,
    B::kL | B::kPadUp, B::kR | B::kCDown,       B::kL | B::kPadRight, B::kL | B::kCRight, B::kL | B::kR | B::kPadDown,
};
constexpr uint16_t kUnlockBunkerCode[kCodeLength] = {
    B::kL | B::kCDown,           B::kR | B::kPadRight, B::kL | B::kCRight,        B::kR | B::kCLeft,  B::kL | B::kCDown,
    B::kL | B::kR | B::kPadLeft, B::kL | B::kCRight,   B::kL | B::kR | B::kPadUp, B::kR | B::kCRight, B::kL | B::kPadUp,
};
constexpr uint16_t kUnlockSiloCode[kCodeLength] = {
    B::kL | B::kPadUp,          B::kR | B::kCDown, B::kL | B::kPadLeft,  B::kR | B::kPadDown,  B::kL | B::kCLeft,
    B::kL | B::kR | B::kCRight, B::kL | B::kCUp,   B::kR | B::kPadRight, B::kR | B::kPadRight, B::kR | B::kCRight,
};
constexpr uint16_t kUnlockFrigateCode[kCodeLength] = {
    B::kR | B::kCUp,           B::kL | B::kPadDown,       B::kR | B::kCRight, B::kL | B::kPadLeft,
    B::kL | B::kR | B::kPadUp, B::kL | B::kR | B::kCDown, B::kR | B::kCRight, B::kR | B::kPadUp,
    B::kL | B::kR | B::kCDown, B::kR | B::kPadUp,
};
constexpr uint16_t kUnlockSurface2Code[kCodeLength] = {
    B::kL | B::kCDown,           B::kL | B::kR | B::kCRight, B::kR | B::kCRight,      B::kR | B::kCUp,
    B::kR | B::kCLeft,           B::kL | B::kPadRight,       B::kL | B::kR | B::kCUp, B::kL | B::kCUp,
    B::kL | B::kR | B::kPadDown, B::kL | B::kCRight,
};
constexpr uint16_t kUnlockBunker2Code[kCodeLength] = {
    B::kL | B::kPadDown,          B::kR | B::kPadDown, B::kL | B::kR | B::kCUp, B::kL | B::kPadLeft,
    B::kL | B::kR | B::kPadRight, B::kL | B::kCLeft,   B::kR | B::kPadRight,    B::kL | B::kCUp,
    B::kL | B::kPadLeft,          B::kL | B::kCDown,
};
constexpr uint16_t kUnlockStatueCode[kCodeLength] = {
    B::kL | B::kR | B::kCDown, B::kL | B::kR | B::kCDown, B::kL | B::kPadRight,        B::kL | B::kR | B::kPadLeft,
    B::kR | B::kPadLeft,       B::kR | B::kCRight,        B::kL | B::kR | B::kPadLeft, B::kR | B::kCUp,
    B::kR | B::kCDown,         B::kR | B::kPadRight,
};
constexpr uint16_t kUnlockArchivesCode[kCodeLength] = {
    B::kR | B::kPadLeft,        B::kL | B::kR | B::kPadUp, B::kL | B::kR | B::kCDown,    B::kR | B::kPadLeft,
    B::kL | B::kR | B::kCRight, B::kL | B::kPadLeft,       B::kL | B::kR | B::kPadRight, B::kL | B::kR | B::kCDown,
    B::kL | B::kPadUp,          B::kR | B::kCDown,
};
constexpr uint16_t kUnlockStreetsCode[kCodeLength] = {
    B::kL | B::kR | B::kCLeft, B::kL | B::kCRight,  B::kL | B::kPadUp, B::kL | B::kR | B::kCDown, B::kR | B::kCRight,
    B::kR | B::kCDown,         B::kR | B::kPadLeft, B::kR | B::kCDown, B::kR | B::kCUp,           B::kL | B::kPadDown,
};
constexpr uint16_t kUnlockDepotCode[kCodeLength] = {
    B::kL | B::kPadDown, B::kL | B::kPadDown, B::kR | B::kCDown, B::kL | B::kCRight, B::kL | B::kR | B::kPadRight,
    B::kR | B::kCLeft,   B::kL | B::kPadDown, B::kL | B::kCLeft, B::kL | B::kCRight, B::kL | B::kPadUp,
};
constexpr uint16_t kUnlockTrainCode[kCodeLength] = {
    B::kR | B::kPadLeft,  B::kR | B::kCDown, B::kR | B::kCRight,  B::kL | B::kR | B::kPadLeft,
    B::kL | B::kPadRight, B::kR | B::kCDown, B::kL | B::kPadLeft, B::kL | B::kR | B::kCLeft,
    B::kL | B::kPadUp,    B::kL | B::kCUp,
};
constexpr uint16_t kUnlockJungleCode[kCodeLength] = {
    B::kR | B::kCDown,   B::kR | B::kPadLeft,         B::kL | B::kR | B::kPadUp, B::kR | B::kPadRight,
    B::kR | B::kPadDown, B::kR | B::kPadDown,         B::kR | B::kPadUp,         B::kR | B::kCLeft,
    B::kR | B::kCUp,     B::kL | B::kR | B::kPadLeft,
};
constexpr uint16_t kUnlockControlCode[kCodeLength] = {
    B::kL | B::kCDown,   B::kR | B::kPadDown, B::kL | B::kPadRight, B::kR | B::kCRight,  B::kR | B::kCDown,
    B::kR | B::kPadLeft, B::kR | B::kPadLeft, B::kR | B::kCUp,      B::kR | B::kPadLeft, B::kL | B::kR | B::kCUp,
};
constexpr uint16_t kUnlockCavernsCode[kCodeLength] = {
    B::kL | B::kPadDown, B::kR | B::kCDown, B::kL | B::kR | B::kPadUp, B::kL | B::kPadRight, B::kR | B::kCUp,
    B::kR | B::kCLeft,   B::kR | B::kPadUp, B::kL | B::kCLeft,         B::kL | B::kPadUp,    B::kR | B::kCLeft,
};
constexpr uint16_t kUnlockCradleCode[kCodeLength] = {
    B::kL | B::kR | B::kCUp, B::kL | B::kPadLeft,  B::kR | B::kPadDown, B::kL | B::kPadDown, B::kL | B::kCUp,
    B::kL | B::kPadDown,     B::kR | B::kPadRight, B::kR | B::kCUp,     B::kL | B::kCLeft,   B::kR | B::kPadRight,
};
constexpr uint16_t kUnlockAztekCode[kCodeLength] = {
    B::kR | B::kCDown,          B::kR | B::kPadLeft,     B::kL | B::kPadRight,
    B::kL | B::kR | B::kCRight, B::kL | B::kPadDown,     B::kL | B::kR | B::kCUp,
    B::kL | B::kPadLeft,        B::kL | B::kR | B::kCUp, B::kL | B::kR | B::kPadRight,
    B::kL | B::kCLeft,
};
constexpr uint16_t kUnlockEgyptCode[kCodeLength] = {
    B::kL | B::kR | B::kPadUp,  B::kL | B::kR | B::kCRight, B::kL | B::kPadDown, B::kR | B::kPadRight,
    B::kR | B::kPadUp,          B::kL | B::kPadDown,        B::kL | B::kPadLeft, B::kL | B::kCRight,
    B::kL | B::kR | B::kCRight, B::kL | B::kPadLeft,
};

struct CheatCode {
    int32_t cheat;
    const uint16_t* buttons;
};

// In g_CheatInfo's order, which decides the first match.
constexpr CheatCode kCodes[] = {
    { Cheat::kExtraMpChars, kExtraMpCharsCode },
    { Cheat::kInvincibility, kInvincibilityCode },
    { Cheat::kAllGuns, kAllGunsCode },
    { Cheat::kMaxAmmo, kMaxAmmoCode },
    { Cheat::kDebugUnk5, kDebugUnk5Code },
    { Cheat::kDeactivateInvincibility, kDeactivateInvincibilityCode },
    { Cheat::kLineMode, kLineModeCode },
    { Cheat::kInvisibility, kInvisibilityCode },
    { Cheat::kBondPhase, kBondPhaseCode },
    { Cheat::kUnlockPaintball, kUnlockPaintballCode },
    { Cheat::kUnlockInvincible, kUnlockInvincibleCode },
    { Cheat::kUnlockDkMode, kUnlockDkModeCode },
    { Cheat::kUnlock2xGl, kUnlock2xGlCode },
    { Cheat::kUnlock2xRl, kUnlock2xRlCode },
    { Cheat::kUnlockTurbo, kUnlockTurboCode },
    { Cheat::kUnlockNoRadar, kUnlockNoRadarCode },
    { Cheat::kUnlockTiny, kUnlockTinyCode },
    { Cheat::kUnlock2xTknife, kUnlock2xTknifeCode },
    { Cheat::kUnlockFast, kUnlockFastCode },
    { Cheat::kUnlockInvis, kUnlockInvisCode },
    { Cheat::kUnlockEnemyRl, kUnlockEnemyRlCode },
    { Cheat::kUnlockSlow, kUnlockSlowCode },
    { Cheat::kUnlockSilverPpk, kUnlockSilverPpkCode },
    { Cheat::kUnlock2xHknife, kUnlock2xHknifeCode },
    { Cheat::kUnlockInfAmmo, kUnlockInfAmmoCode },
    { Cheat::kUnlock2xFnp0, kUnlock2xFnp0Code },
    { Cheat::kUnlockGoldPpk, kUnlockGoldPpkCode },
    { Cheat::kUnlock2xLaser, kUnlock2xLaserCode },
    { Cheat::kUnlockAllGuns, kUnlockAllGunsCode },
    { Cheat::kUnlockDam, kUnlockDamCode },
    { Cheat::kUnlockFacility, kUnlockFacilityCode },
    { Cheat::kUnlockRunway, kUnlockRunwayCode },
    { Cheat::kUnlockSurface, kUnlockSurfaceCode },
    { Cheat::kUnlockBunker, kUnlockBunkerCode },
    { Cheat::kUnlockSilo, kUnlockSiloCode },
    { Cheat::kUnlockFrigate, kUnlockFrigateCode },
    { Cheat::kUnlockSurface2, kUnlockSurface2Code },
    { Cheat::kUnlockBunker2, kUnlockBunker2Code },
    { Cheat::kUnlockStatue, kUnlockStatueCode },
    { Cheat::kUnlockArchives, kUnlockArchivesCode },
    { Cheat::kUnlockStreets, kUnlockStreetsCode },
    { Cheat::kUnlockDepot, kUnlockDepotCode },
    { Cheat::kUnlockTrain, kUnlockTrainCode },
    { Cheat::kUnlockJungle, kUnlockJungleCode },
    { Cheat::kUnlockControl, kUnlockControlCode },
    { Cheat::kUnlockCaverns, kUnlockCavernsCode },
    { Cheat::kUnlockCradle, kUnlockCradleCode },
    { Cheat::kUnlockAztek, kUnlockAztekCode },
    { Cheat::kUnlockEgypt, kUnlockEgyptCode },
};

// cheatButtonCountBitsSet.
int CountBitsSet(uint16_t bits) {
    int count = 0;
    for (; bits != 0; bits >>= 1) {
        if ((bits & 1) != 0) {
            count++;
        }
    }
    return count;
}

} // namespace

const uint16_t* CheatCodeFor(int32_t cheat) {
    for (const CheatCode& code : kCodes) {
        if (code.cheat == cheat) {
            return code.buttons;
        }
    }
    return nullptr;
}

std::vector<int32_t> CheatInput::Feed(uint16_t held, uint16_t pressed, uint32_t applicableMask) {
    const uint16_t triggers = held & (B::kL | B::kR);
    held &= ~triggers;
    pressed &= ~triggers;

    if (pressed != 0) {
        // Only one button besides the triggers may be down, and it must be the
        // one that just went down.
        if (CountBitsSet(pressed) == 1 && CountBitsSet(held) == 1) {
            buffer_[static_cast<std::size_t>(index_)] = pressed | triggers;
            index_ = (index_ + 1) % kBufferSize;
            if (count_ < kBufferSize) {
                count_++;
            }
        } else {
            count_ = 0;
        }
    }

    std::vector<int32_t> matched;
    for (const CheatCode& code : kCodes) {
        if (count_ < kCodeLength || (CheatMaskField(code.cheat) & applicableMask) == 0) {
            continue;
        }
        int at = ((index_ - kCodeLength) + kBufferSize) % kBufferSize;
        int step = 0;
        for (; step < kCodeLength; step++) {
            if (buffer_[static_cast<std::size_t>(at)] != code.buttons[step]) {
                break;
            }
            at = (at + 1) % kBufferSize;
        }
        if (step == kCodeLength) {
            count_ = 0;
            matched.push_back(code.cheat);
        }
    }
    return matched;
}

} // namespace Frigate::Level
