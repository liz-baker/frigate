#include "level/Lang.h"

namespace Frigate::Level {

namespace {

// LnameX_lookuptable's English column, by bank index.
constexpr std::array<const char*, TextBank::Count> kBankNames = {
    nullptr,  "LameE",  "LarchE", "LarkE",   "LashE",    "LaztE",     "LcatE",       "LcaveE",    "LarecE",
    "LcradE", "LcrypE", "LdamE",  "LdepoE",  "LdestE",   "LdishE",    "LearE",       "LeldE",     "LimpE",
    "LjunE",  "LleeE",  "LlenE",  "LlipE",   "LlueE",    "LoatE",     "LpamE",       "LpeteE",    "LrefE",
    "LritE",  "LrunE",  "LsevbE", "LsevE",   "LsevxE",   "LsevxbE",   "LshoE",       "LsiloE",    "LstatE",
    "LtraE",  "LwaxE",  "LgunE",  "LtitleE", "LmpmenuE", "LpropobjE", "LmpweaponsE", "LoptionsE", "LmiscE",
};

// The banks named by langGetLangBankIndexFromStagenum, in TEXTBANK_LEVEL_INDEX
// order.
constexpr int32_t kBankArchives = 2;
constexpr int32_t kBankFacility = 3;
constexpr int32_t kBankControl = 8;
constexpr int32_t kBankCradle = 9;
constexpr int32_t kBankEgypt = 10;
constexpr int32_t kBankDam = 11;
constexpr int32_t kBankDepot = 12;
constexpr int32_t kBankFrigate = 13;
constexpr int32_t kBankTemple = 14;
constexpr int32_t kBankBasement = 17;
constexpr int32_t kBankJungle = 18;
constexpr int32_t kBankCuba = 20;
constexpr int32_t kBankCaves = 23;
constexpr int32_t kBankStreets = 25;
constexpr int32_t kBankComplex = 26;
constexpr int32_t kBankRunway = 28;
constexpr int32_t kBankBunker2 = 29;
constexpr int32_t kBankBunker1 = 30;
constexpr int32_t kBankSurface = 31;
constexpr int32_t kBankSurface2 = 32;
constexpr int32_t kBankSilo = 34;
constexpr int32_t kBankStatue = 35;
constexpr int32_t kBankTrain = 36;
constexpr int32_t kBankAztec = 5;
constexpr int32_t kBankCaverns = 7;
constexpr int32_t kBankLibrary = 1;
constexpr int32_t kBankStack = 4;

} // namespace

int32_t Lang::StageBank(LevelId stage) {
    switch (stage) {
        case LevelId::Dam:
            return kBankDam;
        case LevelId::Facility:
            return kBankFacility;
        case LevelId::Runway:
            return kBankRunway;
        case LevelId::Surface:
            return kBankSurface;
        case LevelId::Bunker1:
            return kBankBunker1;
        case LevelId::Silo:
            return kBankSilo;
        case LevelId::Frigate:
            return kBankFrigate;
        case LevelId::Surface2:
            return kBankSurface2;
        case LevelId::Bunker2:
            return kBankBunker2;
        case LevelId::Statue:
            return kBankStatue;
        case LevelId::Archives:
            return kBankArchives;
        case LevelId::Streets:
            return kBankStreets;
        case LevelId::Depot:
            return kBankDepot;
        case LevelId::Train:
            return kBankTrain;
        case LevelId::Jungle:
            return kBankJungle;
        case LevelId::Control:
            return kBankControl;
        case LevelId::Caverns:
            return kBankCaverns;
        case LevelId::Cradle:
            return kBankCradle;
        case LevelId::Aztec:
            return kBankAztec;
        case LevelId::Egypt:
            return kBankEgypt;
        case LevelId::Temple:
            return kBankTemple;
        case LevelId::Complex:
            return kBankComplex;
        case LevelId::Library:
            return kBankLibrary;
        case LevelId::Basement:
            return kBankBasement;
        case LevelId::Stack:
            return kBankStack;
        case LevelId::Caves:
            return kBankCaves;
        case LevelId::Cuba:
            return kBankCuba;
        default:
            return -1;
    }
}

std::string Lang::BankResource(int32_t bank) {
    if (bank <= 0 || bank >= TextBank::Count) {
        return {};
    }
    return std::string("obseg/text/") + kBankNames[static_cast<size_t>(bank)];
}

void Lang::SetBank(int32_t bank, std::vector<std::string> strings) {
    if (bank > 0 && bank < TextBank::Count) {
        banks_[static_cast<size_t>(bank)] = std::move(strings);
    }
}

std::string_view Lang::Get(int32_t id) const {
    const int32_t bank = id >> 10;
    const auto slot = static_cast<size_t>(id & 0x3FF);
    if (bank < 0 || bank >= TextBank::Count) {
        return {};
    }
    const std::vector<std::string>& strings = banks_[static_cast<size_t>(bank)];
    return slot < strings.size() ? std::string_view(strings[slot]) : std::string_view();
}

} // namespace Frigate::Level
