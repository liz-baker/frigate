#include "game/menu/CastData.h"

#include <cmath>
#include <cstddef>

namespace Frigate::Game::Menu::Cast {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kDamp = 0.95f;
constexpr float kDampComp = 0.050000012f;
// The camera looks this far below the root, and sits this far above it.
constexpr float kTargetDrop = 10.0f;
constexpr float kEyeRise = 52.5f;
// The unlockable guest stars show 1 time in this many even when locked.
constexpr uint32_t kGuestChance = 0x2710;

constexpr Member kMembers[] = {
    { "BODY_Brosnan_Tuxedo", HeadKind::Named, "HEAD_Male_Brosnan_Tuxedo", 227, 228, 227, true },
    { "BODY_Special_Operations_Uniform", HeadKind::Named, "HEAD_Male_Brosnan_Boiler", 229, 232, 233, false },
    { "BODY_Natalya_Skirt", HeadKind::Fixed, "", 229, 234, 227, false },
    { "BODY_Trevelyan_006", HeadKind::Fixed, "", 229, 235, 236, false },
    { "BODY_Xenia", HeadKind::Fixed, "", 230, 237, 238, false },
    { "BODY_Ourumov", HeadKind::Fixed, "", 230, 239, 240, false },
    { "BODY_Boris", HeadKind::Fixed, "", 230, 241, 227, false },
    { "BODY_Valentin_", HeadKind::Fixed, "", 230, 242, 243, false },
    { "BODY_Siberian_Guard_1_Mishkin", HeadKind::Named, "HEAD_Male_Mishkin", 230, 244, 245, false },
    { "BODY_Russian_Soldier", HeadKind::Random, "", 227, 253, 227, true },
    { "BODY_Russian_Infantry", HeadKind::Random, "", 227, 252, 227, true },
    { "BODY_Scientist_1_Male", HeadKind::Random, "", 227, 263, 227, true },
    { "BODY_Scientist_2_Female", HeadKind::Random, "", 227, 263, 227, true },
    { "BODY_Russian_Commandant", HeadKind::Random, "", 227, 256, 227, true },
    { "BODY_Janus_Marine", HeadKind::Random, "", 227, 254, 227, true },
    { "BODY_Naval_Officer", HeadKind::Random, "", 227, 257, 227, true },
    { "BODY_Helicopter_Pilot", HeadKind::Fixed, "", 227, 262, 227, true },
    { "BODY_St_Petersburg_Guard", HeadKind::Random, "", 227, 251, 227, true },
    { "BODY_Civilian_1_Female", HeadKind::Random, "", 227, 264, 227, true },
    { "BODY_Civilian_2", HeadKind::Random, "", 227, 264, 227, true },
    { "BODY_Civilian_3", HeadKind::Random, "", 227, 264, 227, true },
    { "BODY_Civilian_4", HeadKind::Random, "", 227, 264, 227, true },
    { "BODY_Siberian_Guard_1_Mishkin", HeadKind::Random, "", 227, 258, 227, true },
    { "BODY_Arctic_Commando", HeadKind::Random, "", 227, 259, 227, true },
    { "BODY_Siberian_Guard_2", HeadKind::Random, "", 227, 258, 227, true },
    { "BODY_Siberian_Special_Forces", HeadKind::Fixed, "", 227, 260, 227, true },
    { "BODY_Jungle_Commando", HeadKind::Random, "", 227, 250, 227, true },
    { "BODY_Janus_Special_Forces", HeadKind::Random, "", 227, 255, 227, true },
    { "BODY_Moonraker_Elite_1_Male", HeadKind::Random, "", 227, 261, 227, true },
    { "BODY_Moonraker_Elite_2_Female", HeadKind::Random, "", 227, 261, 227, true },
    { "BODY_Mayday", HeadKind::Fixed, "", 231, 246, 227, false },
    { "BODY_Jaws", HeadKind::Fixed, "", 231, 247, 227, false },
    { "BODY_Oddjob", HeadKind::Fixed, "", 231, 248, 227, false },
    { "BODY_Baron_Samedi", HeadKind::Fixed, "", 231, 249, 227, false },
};

constexpr Animation kAnimations[] = {
    { "spotting_bond", 98.0f, 1.0f, WeaponType::None },
    { "fire_standing_draw_one_handed_weapon_fast", 21.0f, 1.0f, WeaponType::Pistol },
    { "fire_standing_draw_one_handed_weapon_slow", 26.0f, 1.0f, WeaponType::Pistol },
    { "fire_step_right_one_handed_weapon", 0.0f, 1.0f, WeaponType::Pistol },
    { "fire_kneel_forward_one_handed_weapon_fast", 0.0f, 1.0f, WeaponType::Pistol },
    { "running_one_handed_weapon", 0.0f, 0.91000003f, WeaponType::Pistol },
    { "draw_one_handed_weapon_and_stand_up", 31.0f, 1.0f, WeaponType::Pistol },
    { "aim_one_handed_weapon_left_right", 0.0f, 1.0f, WeaponType::Pistol },
    { "cock_one_handed_weapon_and_turn_around", 0.0f, 1.0f, WeaponType::Pistol },
    { "cock_one_handed_weapon_turn_around_and_stand_up", 0.0f, 1.0f, WeaponType::Pistol },
    { "draw_one_handed_weapon_and_turn_around", 0.0f, 1.0f, WeaponType::Pistol },
    { "drop_weapon_and_show_fight_stance", 248.0f, 1.0f, WeaponType::None },
    { "laughing_in_disbelief", 150.0f, 1.0f, WeaponType::None },
    { "fire_hip_forward_one_handed_weapon", 0.0f, 0.89999998f, WeaponType::Pistol },
    { "fire_standing_left_one_handed_weapon_fast", 0.0f, 0.89999998f, WeaponType::Pistol },
    { "fire_kneel_left_one_handed_weapon_fast", 0.0f, 0.89999998f, WeaponType::Pistol },
    { "draw_one_handed_weapon_and_look_around", 51.0f, 1.0f, WeaponType::Pistol },
    { "aim_one_handed_weapon_left", 0.0f, 0.89999998f, WeaponType::Pistol },
    { "aim_one_handed_weapon_right", 0.0f, 0.89999998f, WeaponType::Pistol },
    { "conversation", 37.0f, 1.0f, WeaponType::Rifle },
    { "conversation_listener", 300.0f, 1.0f, WeaponType::Rifle },
    { "conversation_cleaned", 120.0f, 1.0f, WeaponType::Rifle },
};

constexpr std::string_view kMaleHeads[] = {
    "HEAD_Male_Jim",    "HEAD_Male_Chris", "HEAD_Male_Lee",         "HEAD_Male_Graeme", "HEAD_Male_Steve_H",
    "HEAD_Male_Neil",   "HEAD_Male_Robin", "HEAD_Male_Des",         "HEAD_Male_Grant",  "HEAD_Male_Dave_Dr_Doak",
    "HEAD_Male_Karl",   "HEAD_Male_Alan",  "HEAD_Male_Pete",        "HEAD_Male_Martin", "HEAD_Male_Mark",
    "HEAD_Male_Duncan", "HEAD_Male_Shaun", "HEAD_Male_Dwayne",      "HEAD_Male_B",      "HEAD_Male_Steve_Ellis",
    "HEAD_Male_Joel",   "HEAD_Male_Scott", "HEAD_Male_Joe_Altered", "HEAD_Male_Ken",    "HEAD_Male_Joe",
};

constexpr std::string_view kFemaleHeads[] = {
    "HEAD_Female_Sally",
    "HEAD_Female_Marion_Rosika",
    "HEAD_Female_Mandy",
    "HEAD_Female_Vivien",
};

constexpr std::string_view kRifles[] = {
    "models/prop/PchrkalashZ",   "models/prop/Pchrm16Z",           "models/prop/Pchrfnp90Z",
    "models/prop/PchrautoshotZ", "models/prop/PchrgrenadelaunchZ", "models/prop/PchrsniperrifleZ",
};

constexpr std::string_view kPistols[] = {
    "models/prop/PchrwppkZ",  "models/prop/PchrwppksilZ", "models/prop/PchrskorpionZ", "models/prop/PchruziZ",
    "models/prop/PchruziZ",   "models/prop/Pchrtt33Z",    "models/prop/Pchrtt33Z",     "models/prop/PchrrugerZ",
    "models/prop/PchrlaserZ", "models/prop/PchrgoldenZ",
};

constexpr std::string_view kPp7 = "models/prop/PchrwppkZ";

float RandomUnit(const Random& random) {
    return static_cast<float>(random()) * (1.0f / static_cast<float>(UINT32_MAX));
}

Vec3 Scaled(const Vec3& v, float s) {
    return { v[0] * s, v[1] * s, v[2] * s };
}

} // namespace

Unlocks QueryUnlocks(const CompletionSource& source) {
    const auto ask = [](const std::function<bool()>& check) { return check && check(); };
    return { ask(source.aztecOnSecretAgentOr00), ask(source.egyptOn00), ask(source.cradle) };
}

std::span<const Member> Members() {
    return kMembers;
}

std::span<const Animation> Animations() {
    return kAnimations;
}

size_t NextMember(size_t index, bool extendedCast, const Unlocks& unlocks, const Random& random) {
    for (index++; index < std::size(kMembers); index++) {
        const Member& member = kMembers[index];
        if (member.extendedOnly && !extendedCast) {
            continue;
        }
        if ((member.body == "BODY_Moonraker_Elite_1_Male" || member.body == "BODY_Moonraker_Elite_2_Female") &&
            !unlocks.aztecOnSecretAgentOr00) {
            continue;
        }
        if ((member.body == "BODY_Mayday" || member.body == "BODY_Jaws") && !unlocks.aztecOnSecretAgentOr00 &&
            random() % kGuestChance != 0) {
            continue;
        }
        if ((member.body == "BODY_Oddjob" || member.body == "BODY_Baron_Samedi") && !unlocks.egyptOn00 &&
            random() % kGuestChance != 0) {
            continue;
        }
        break;
    }
    return index;
}

BodyAndHead ChooseBodyAndHead(const Member& member, const std::function<bool(std::string_view)>& isMale,
                              const Random& random) {
    BodyAndHead chosen{ std::string(member.body), std::string(member.head) };
    if (member.body == "BODY_Special_Operations_Uniform") {
        switch (random() % 5) {
            case 1:
                chosen = { "BODY_Formal_Wear", "HEAD_Male_Brosnan_Default" };
                break;
            case 2:
                chosen = { "BODY_Jungle_Fatigues", "HEAD_Male_Brosnan_Default" };
                break;
            case 3:
                chosen = { "BODY_Parka", "HEAD_Male_Brosnan_Default" };
                break;
            case 4:
                chosen = { "BODY_Brosnan_Tuxedo", "HEAD_Male_Brosnan_Tuxedo" };
                break;
            default:
                break;
        }
    } else if (member.body == "BODY_Natalya_Skirt") {
        if (random() & 1) {
            chosen.body = "BODY_Natalya_Jungle_Fatigues";
        }
    } else if (member.body == "BODY_Trevelyan_006" && (random() & 1)) {
        chosen.body = "BODY_Trevelyan_Janus";
    }

    if (member.headKind == HeadKind::Random) {
        chosen.head = isMale(chosen.body) ? kMaleHeads[random() % std::size(kMaleHeads)]
                                          : kFemaleHeads[random() % std::size(kFemaleHeads)];
    }
    return chosen;
}

std::string_view ChooseWeapon(WeaponType type, const Unlocks& unlocks, const Random& random) {
    if (type == WeaponType::None) {
        return {};
    }
    if (type == WeaponType::Rifle) {
        return kRifles[random() % std::size(kRifles)];
    }
    const std::string_view weapon = kPistols[random() % std::size(kPistols)];
    if ((weapon == "models/prop/PchrrugerZ" && !unlocks.cradle) ||
        (weapon == "models/prop/PchrlaserZ" && !unlocks.aztecOnSecretAgentOr00) ||
        (weapon == "models/prop/PchrgoldenZ" && !unlocks.egyptOn00)) {
        return kPp7;
    }
    return weapon;
}

CameraPath ChooseCameraPath(const Random& random) {
    CameraPath path;
    path.distStart = RandomUnit(random) * 80.0f + 70.0f;
    path.distEnd = RandomUnit(random) * 80.0f + 70.0f;
    path.angleStart = (RandomUnit(random) - 0.5f) * kTau;
    path.angleEnd = (RandomUnit(random) - 0.5f) * 2.5132742f;
    path.heightStart = RandomUnit(random) * 200.0f + -100.0f;
    path.heightEnd = RandomUnit(random) * 200.0f + -100.0f;
    return path;
}

float Fade(int timer) {
    if (timer < 0 || timer >= kLength) {
        return 0.0f;
    }
    if (timer < kFadeLength) {
        return static_cast<float>(timer) / static_cast<float>(kFadeLength);
    }
    if (timer >= kLength - kFadeLength + 1) {
        return static_cast<float>(kLength - timer) / static_cast<float>(kFadeLength);
    }
    return 1.0f;
}

Orbit OrbitCamera(const CameraPath& path, float menuFraction) {
    const float dist = (path.distEnd - path.distStart) * menuFraction + path.distStart;
    float angle = (path.angleEnd - path.angleStart) * menuFraction + path.angleStart;
    const float height = (path.heightEnd - path.heightStart) * menuFraction + path.heightStart;
    if (angle < 0.0f) {
        angle += kTau;
    }
    Orbit orbit;
    // The camera and its target both slide sideways along the view by a fifth
    // of the distance, which puts the character off centre.
    orbit.pos = { dist * std::sin(angle) + std::cos(angle) * 0.2f * dist, height,
                  dist * std::cos(angle) - std::sin(angle) * 0.2f * dist };
    orbit.target = { std::cos(angle) * 0.2f * dist, 0.0f, -std::sin(angle) * 0.2f * dist };
    return orbit;
}

void Step(RootFollow& follow, const FollowInput& input, Orbit& orbit) {
    if (follow.reset) {
        follow.rootPos[1] = input.rootOffset[1];
    }

    Vec3 vec;
    for (int i = 0; i < 3; i++) {
        vec[i] = (input.rootOffset[i] - follow.rootPos[i]) / input.timerDelta;
    }
    if (follow.reset) {
        follow.rootVelAccumulator = Scaled(vec, 1.0f / kDampComp);
    }
    for (int tick = 0; tick < input.ticks; tick++) {
        for (int i = 0; i < 3; i++) {
            follow.rootVelAccumulator[i] = vec[i] + kDamp * follow.rootVelAccumulator[i];
        }
    }
    const Vec3 rootVel = Scaled(follow.rootVelAccumulator, kDampComp);
    for (int i = 0; i < 3; i++) {
        follow.rootPos[i] += rootVel[i] * input.timerDelta;
    }

    // mtx4TransformVecInPlace(): the vector through the matrix's rotation part,
    // then its translation. The vector is still the root's velocity term.
    const Frigate::Mat4& m = input.firstMatrix;
    const Vec3 moved = { m.m[0][0] * vec[0] + m.m[1][0] * vec[1] + m.m[2][0] * vec[2] + m.m[3][0],
                         m.m[0][1] * vec[0] + m.m[1][1] * vec[1] + m.m[2][1] * vec[2] + m.m[3][1],
                         m.m[0][2] * vec[0] + m.m[1][2] * vec[1] + m.m[2][2] * vec[2] + m.m[3][2] };
    for (int i = 0; i < 3; i++) {
        vec[i] = moved[i] - follow.rootPos[i];
    }
    if (follow.reset) {
        follow.reset = false;
        follow.targetAccumulator = Scaled(vec, 1.0f / kDampComp);
    }
    for (int tick = 0; tick < input.ticks; tick++) {
        for (int i = 0; i < 3; i++) {
            follow.targetAccumulator[i] = vec[i] + kDamp * follow.targetAccumulator[i];
        }
    }
    const Vec3 targetSmoothed = Scaled(follow.targetAccumulator, kDampComp);

    for (int i = 0; i < 3; i++) {
        orbit.target[i] += follow.rootPos[i] + targetSmoothed[i];
        orbit.pos[i] += follow.rootPos[i];
    }
    orbit.target[1] -= kTargetDrop;
    orbit.pos[1] += kEyeRise;
}

} // namespace Frigate::Game::Menu::Cast
