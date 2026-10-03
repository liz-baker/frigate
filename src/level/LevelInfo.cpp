#include "level/LevelInfo.h"

namespace Frigate::Level {

namespace {

constexpr LevelEntry kLevels[] = {
    { LevelId::Bunker1, "bg_sev", "obseg/stan/Tbg_sev_all_p_stanZ", 0.53931433f, 1.0f, 23.148148f },
    { LevelId::Silo, "bg_silo", "obseg/stan/Tbg_silo_all_p_stanZ", 0.47256002f, 1.0f, 29.069f },
    { LevelId::Statue, "bg_stat", "obseg/stan/Tbg_stat_all_p_stanZ", 0.107202865f, 1.0f, 0.0801f },
    { LevelId::Control, "bg_arec", "obseg/stan/Tbg_arec_all_p_stanZ", 0.49886572f, 1.0f, 80.645164f },
    { LevelId::Archives, "bg_arch", "obseg/stan/Tbg_arch_all_p_stanZ", 0.50678575f, 1.0f, 54.347824f },
    { LevelId::Train, "bg_tra", "obseg/stan/Tbg_tra_all_p_stanZ", 0.15019713f, 1.0f, 19.53125f },
    { LevelId::Frigate, "bg_dest", "obseg/stan/Tbg_dest_all_p_stanZ", 0.44757429f, 1.0f, 36.764706f },
    { LevelId::Bunker2, "bg_sevb", "obseg/stan/Tbg_sevb_all_p_stanZ", 0.53931433f, 1.0f, 23.148148f },
    { LevelId::Aztec, "bg_azt", "obseg/stan/Tbg_azt_all_p_stanZ", 0.35300568f, 1.0f, 52.083332f },
    { LevelId::Streets, "bg_pete", "obseg/stan/Tbg_pete_all_p_stanZ", 0.34187999f, 1.0f, 42.372883f },
    { LevelId::Depot, "bg_depo", "obseg/stan/Tbg_depo_all_p_stanZ", 0.21847887f, 1.0f, 17.605633f },
    { LevelId::Complex, "bg_ref", "obseg/stan/Tbg_ref_all_p_stanZ", 0.94285715f, 1.0f, 37.878788f },
    { LevelId::Egypt, "bg_cryp", "obseg/stan/Tbg_cryp_all_p_stanZ", 0.25608f, 1.0f, 23.584906f },
    { LevelId::Dam, "bg_dam", "obseg/stan/Tbg_dam_all_p_stanZ", 0.23363999f, 0.2f, 100.0f },
    { LevelId::Facility, "bg_ark", "obseg/stan/Tbg_ark_all_p_stanZ", 1.20648f, 1.0f, 64.102562f },
    { LevelId::Runway, "bg_run", "obseg/stan/Tbg_run_all_p_stanZ", 0.089571431f, 1.0f, 4.5537338f },
    { LevelId::Surface, "bg_sevx", "obseg/stan/Tbg_sevx_all_p_stanZ", 0.45445713f, 0.2f, 22.603975f },
    { LevelId::Jungle, "bg_jun", "obseg/stan/Tbg_jun_all_p_stanZ", 0.094662853f, 1.0f, 6.6844921f },
    { LevelId::Temple, "bg_dish", "obseg/stan/Tbg_dish_all_p_stanZ", 0.47142857f, 1.0f, 147.05882f },
    { LevelId::Caverns, "bg_cave", "obseg/stan/Tbg_cave_all_p_stanZ", 0.26824287f, 1.0f, 13.44086f },
    { LevelId::Citadel, "bg_cat", "obseg/stan/Tbg_cat_all_p_stanZ", 0.76852286f, 1.0f, 38.461536f },
    { LevelId::Cradle, "bg_crad", "obseg/stan/Tbg_crad_all_p_stanZ", 0.23571429f, 1.0f, 43.103451f },
    { LevelId::Sho, "bg_sho", "obseg/stan/Tbg_sho_all_p_stanZ", 0.528f, 1.0f, 21.18644f },
    { LevelId::Surface2, "bg_sevx", "obseg/stan/Tbg_sevx_all_p_stanZ", 0.45445713f, 0.2f, 22.603975f },
    { LevelId::Eld, "bg_eld", "obseg/stan/Tbg_eld_all_p_stanZ", 0.94285715f, 1.0f, 10.123456f },
    { LevelId::Basement, "bg_ame", "obseg/stan/Tbg_ame_all_p_stanZ", 0.65999997f, 1.0f, 37.878788f },
    { LevelId::Stack, "bg_ame", "obseg/stan/Tbg_ame_all_p_stanZ", 0.65999997f, 1.0f, 37.878788f },
    { LevelId::Lue, "bg_lue", "obseg/stan/Tbg_lue_all_p_stanZ", 0.94285715f, 1.0f, 10.123456f },
    { LevelId::Library, "bg_ame", "obseg/stan/Tbg_ame_all_p_stanZ", 0.65999997f, 1.0f, 37.878788f },
    { LevelId::Rit, "bg_rit", "obseg/stan/Tbg_rit_all_p_stanZ", 0.94285715f, 1.0f, 10.123456f },
    { LevelId::Caves, "bg_oat", "obseg/stan/Tbg_oat_all_p_stanZ", 0.14142857f, 1.0f, 10.123456f },
    { LevelId::Ear, "bg_ear", "obseg/stan/Tbg_ear_all_p_stanZ", 0.94285715f, 1.0f, 10.123456f },
    { LevelId::Lee, "bg_lee", "obseg/stan/Tbg_lee_all_p_stanZ", 0.94285715f, 1.0f, 10.123456f },
    { LevelId::Lip, "bg_lip", "obseg/stan/Tbg_lip_all_p_stanZ", 0.94285715f, 1.0f, 10.123456f },
    { LevelId::Cuba, "bg_len", "obseg/stan/Tbg_len_all_p_stanZ", 0.094662853f, 1.0f, 6.6844921f },
    { LevelId::Wax, "bg_wax", "obseg/stan/Tbg_wax_all_p_stanZ", 0.94285715f, 1.0f, 10.123456f },
    { LevelId::Pam, "bg_pam", "obseg/stan/Tbg_pam_all_p_stanZ", 0.94285715f, 1.0f, 10.123456f },
    { LevelId::Max, "bgx", "obseg/stan/TbgxZ", 0.94285715f, 1.0f, 1.0f },
};

constexpr size_t kLevelCount = sizeof(kLevels) / sizeof(kLevels[0]);

// setup_text_pointers: the setup file of each LEVELID, without the country
// prefix; the ids without a file are null.
constexpr const char* kSetupNames[] = {
    nullptr,           nullptr,        nullptr,           nullptr,      nullptr,      nullptr,      nullptr,
    nullptr,           nullptr,        "setupsevbunkerZ", nullptr,      nullptr,      nullptr,      nullptr,
    nullptr,           nullptr,        nullptr,           nullptr,      nullptr,      nullptr,      "setupsiloZ",
    "setupsevbunkerZ", "setupstatueZ", "setupcontrolZ",   "setuparchZ", "setuptraZ",  "setupdestZ", "setupsevbZ",
    "setupaztZ",       "setuppeteZ",   "setupdepoZ",      "setuprefZ",  "setupcrypZ", "setupdamZ",  "setuparkZ",
    "setuprunZ",       "setupsevxZ",   "setupjunZ",       "setupdishZ", "setupcaveZ", "setupcatZ",  "setupcradZ",
    "setupshoZ",       "setupsevxbZ",  "setupeldZ",       "setupimpZ",  "setupashZ",  "setuplueZ",  "setupameZ",
    "setupritZ",       "setupoatZ",    "setupearZ",       "setupleeZ",  "setuplipZ",  "setuplenZ",  "setupwaxZ",
    "setuppamZ",       nullptr,        nullptr,
};

} // namespace

const char* SetupName(LevelId id) {
    const auto index = static_cast<size_t>(id);
    return index < sizeof(kSetupNames) / sizeof(kSetupNames[0]) ? kSetupNames[index] : nullptr;
}

bool StagePoolAnimFlipFlag(LevelId id) {
    return id == LevelId::Dam;
}

size_t FindLevelIndex(LevelId id) {
    size_t found = 0;
    // The last matching row wins, as in the game's loop.
    for (size_t i = 0; i < kLevelCount; i++) {
        if (kLevels[i].id == id) {
            found = i;
        }
    }
    return found;
}

const LevelEntry& LevelEntryAt(size_t index) {
    return kLevels[index];
}

} // namespace Frigate::Level
