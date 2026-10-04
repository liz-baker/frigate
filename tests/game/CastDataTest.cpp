#include "game/menu/CastData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Frigate::Game::Menu::Cast;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

Random Constant(uint32_t value) {
    return [value] { return value; };
}

// Returns each value in turn, then the last one forever.
Random Sequence(std::vector<uint32_t> values) {
    size_t next = 0;
    return [values, next]() mutable { return values[std::min(next++, values.size() - 1)]; };
}

size_t IndexOf(std::string_view body) {
    const auto members = Members();
    for (size_t i = 0; i < members.size(); i++) {
        if (members[i].body == body) {
            return i;
        }
    }
    return members.size();
}

void TestTheShortCastSkipsTheExtendedEntries() {
    // The short cast is entries 1 through 8, then whichever guest stars show.
    Check(NextMember(0, false, {}, Constant(1)) == 1, "the short cast starts at Bond");
    Check(NextMember(8, false, {}, Constant(1)) == Members().size(), "and, with nothing unlocked, ends after Mishkin");
    Check(NextMember(8, false, { true, true, true }, Constant(1)) == IndexOf("BODY_Mayday"),
          "and skips the extended-only entries, Moonraker Elite included, to reach the guest stars");
}

void TestTheExtendedCastShowsTheGroupEntries() {
    Check(NextMember(0, true, {}, Constant(1)) == 1, "the extended cast goes on from Brosnan to Bond");
    Check(NextMember(8, true, {}, Constant(1)) == 9, "and shows the extended-only entries");
}

void TestLockedCharactersAreSkipped() {
    const size_t end = Members().size();
    const size_t moonraker = IndexOf("BODY_Moonraker_Elite_1_Male");
    Check(NextMember(moonraker - 1, true, {}, Constant(1)) == end, "the Moonraker Elite and guest stars are skipped");
    Check(NextMember(moonraker - 1, true, { true, false, false }, Constant(1)) == moonraker,
          "the Moonraker Elite show once Aztec is completed");
    Check(NextMember(moonraker, true, { true, false, false }, Constant(1)) == moonraker + 1,
          "including the female one");
}

void TestGuestStarsShowWithAOneInTenThousandChance() {
    const size_t mayday = IndexOf("BODY_Mayday");
    Check(NextMember(mayday - 1, true, { false, false, false }, Constant(0)) == mayday,
          "a locked guest star shows when the roll is 0");
    Check(NextMember(mayday - 1, true, { false, false, false }, Constant(10000)) == mayday,
          "and on any multiple of 10000");
    Check(NextMember(mayday - 1, true, { true, false, false }, Constant(7)) == mayday, "an unlocked one always shows");
    Check(NextMember(mayday, true, { true, false, false }, Constant(7)) == mayday + 1, "and Jaws follows Mayday");
    const size_t oddjob = IndexOf("BODY_Oddjob");
    Check(NextMember(oddjob - 1, true, { true, false, false }, Constant(7)) == Members().size(),
          "Oddjob and Baron Samedi ask about Egypt, not Aztec");
    Check(NextMember(oddjob - 1, true, { false, true, false }, Constant(7)) == oddjob, "and show once it is completed");
}

void TestNothingIsRolledForAnUnlockedCharacter() {
    int rolls = 0;
    const Random counting = [&rolls] { return static_cast<uint32_t>(++rolls); };
    NextMember(IndexOf("BODY_Mayday") - 1, true, { true, false, false }, counting);
    Check(rolls == 0, "an unlocked guest star draws no random number");
}

CompletionSource Source(bool aztec, bool egypt, bool cradle) {
    return { [aztec] { return aztec; }, [egypt] { return egypt; }, [cradle] { return cradle; } };
}

void TestUnlocksComeFromTheCompletionSource() {
    for (int bits = 0; bits < 8; bits++) {
        const Unlocks unlocks = QueryUnlocks(Source(bits & 1, bits & 2, bits & 4));
        Check(unlocks.aztecOnSecretAgentOr00 == ((bits & 1) != 0) && unlocks.egyptOn00 == ((bits & 2) != 0) &&
                  unlocks.cradle == ((bits & 4) != 0),
              "each unlock is its own check's answer");
    }
    const Unlocks none = QueryUnlocks({});
    Check(!none.aztecOnSecretAgentOr00 && !none.egyptOn00 && !none.cradle, "an unset check reads as not completed");
}

void TestEachGatedEntryFollowsItsOwnCompletion() {
    // The roll is never 0 modulo 10000, so only a completion shows a guest star.
    const struct {
        const char* body;
        bool aztec;
        bool egypt;
    } gated[] = {
        { "BODY_Moonraker_Elite_1_Male", true, false },
        { "BODY_Moonraker_Elite_2_Female", true, false },
        { "BODY_Mayday", true, false },
        { "BODY_Jaws", true, false },
        { "BODY_Oddjob", false, true },
        { "BODY_Baron_Samedi", false, true },
    };
    for (const auto& entry : gated) {
        const size_t index = IndexOf(entry.body);
        for (int bits = 0; bits < 8; bits++) {
            const bool aztec = bits & 1;
            const bool egypt = bits & 2;
            const Unlocks unlocks = QueryUnlocks(Source(aztec, egypt, bits & 4));
            const bool shown = entry.aztec ? aztec : egypt;
            Check((NextMember(index - 1, true, unlocks, Constant(1)) == index) == shown, entry.body);
        }
    }
}

void TestEachSubstitutionFollowsItsOwnCompletion() {
    const struct {
        uint32_t roll;
        const char* weapon;
        int bit;
    } gated[] = {
        { 7, "models/prop/PchrrugerZ", 4 },
        { 8, "models/prop/PchrlaserZ", 1 },
        { 9, "models/prop/PchrgoldenZ", 2 },
    };
    for (const auto& entry : gated) {
        for (int bits = 0; bits < 8; bits++) {
            const Unlocks unlocks = QueryUnlocks(Source(bits & 1, bits & 2, bits & 4));
            const std::string_view expected = (bits & entry.bit) ? entry.weapon : "models/prop/PchrwppkZ";
            Check(ChooseWeapon(WeaponType::Pistol, unlocks, Constant(entry.roll)) == expected, entry.weapon);
        }
    }
}

void TestBondTakesARandomOutfit() {
    const Member& bond = Members()[1];
    const auto male = [](std::string_view) { return true; };

    BodyAndHead chosen = ChooseBodyAndHead(bond, male, Constant(0));
    Check(chosen.body == "BODY_Special_Operations_Uniform" && chosen.head == "HEAD_Male_Brosnan_Boiler",
          "outfit 0 is the table's own");
    chosen = ChooseBodyAndHead(bond, male, Constant(1));
    Check(chosen.body == "BODY_Formal_Wear" && chosen.head == "HEAD_Male_Brosnan_Default", "outfit 1");
    chosen = ChooseBodyAndHead(bond, male, Constant(4));
    Check(chosen.body == "BODY_Brosnan_Tuxedo" && chosen.head == "HEAD_Male_Brosnan_Tuxedo", "outfit 4");
    chosen = ChooseBodyAndHead(bond, male, Constant(8));
    Check(chosen.body == "BODY_Parka", "the outfit is the roll modulo 5, and 8 is 3");
}

void TestNatalyaAndTrevelyanHaveOutfitsToo() {
    const auto male = [](std::string_view) { return true; };
    Check(ChooseBodyAndHead(Members()[IndexOf("BODY_Natalya_Skirt")], male, Constant(1)).body ==
              "BODY_Natalya_Jungle_Fatigues",
          "Natalya can wear her jungle fatigues");
    Check(ChooseBodyAndHead(Members()[IndexOf("BODY_Natalya_Skirt")], male, Constant(2)).body == "BODY_Natalya_Skirt",
          "or her skirt");
    Check(ChooseBodyAndHead(Members()[IndexOf("BODY_Trevelyan_006")], male, Constant(1)).body == "BODY_Trevelyan_Janus",
          "006 can be Janus");
    Check(ChooseBodyAndHead(Members()[IndexOf("BODY_Trevelyan_006")], male, Constant(0)).body == "BODY_Trevelyan_006",
          "or not");
}

void TestRandomHeadsComeFromTheBodysGender() {
    const Member& civilian = Members()[IndexOf("BODY_Civilian_2")];
    BodyAndHead chosen = ChooseBodyAndHead(
        civilian, [](std::string_view) { return true; }, Constant(3));
    Check(chosen.head == "HEAD_Male_Graeme", "a male body's head is the roll modulo 25 into the male list");
    chosen = ChooseBodyAndHead(
        civilian, [](std::string_view) { return false; }, Constant(6));
    Check(chosen.head == "HEAD_Female_Mandy", "a female body's is the roll modulo 4 into the female list");

    const Member& boris = Members()[IndexOf("BODY_Boris")];
    Check(ChooseBodyAndHead(
              boris, [](std::string_view) { return true; }, Constant(3))
              .head.empty(),
          "a body that carries its head is given none");
}

void TestWeaponsAreSubstitutedUntilUnlocked() {
    Check(ChooseWeapon(WeaponType::None, {}, Constant(0)).empty(), "an unarmed animation draws no weapon");
    Check(ChooseWeapon(WeaponType::Rifle, {}, Constant(0)) == "models/prop/PchrkalashZ", "rifle 0 is the AK-47");
    Check(ChooseWeapon(WeaponType::Rifle, {}, Constant(6)) == "models/prop/PchrkalashZ", "the roll is modulo 6");
    Check(ChooseWeapon(WeaponType::Pistol, {}, Constant(3)) == "models/prop/PchruziZ", "pistol 3 is the Uzi");
    Check(ChooseWeapon(WeaponType::Pistol, {}, Constant(7)) == "models/prop/PchrwppkZ",
          "the Cougar Magnum is replaced until Cradle is completed");
    Check(ChooseWeapon(WeaponType::Pistol, { false, false, true }, Constant(7)) == "models/prop/PchrrugerZ",
          "and is shown after");
    Check(ChooseWeapon(WeaponType::Pistol, {}, Constant(8)) == "models/prop/PchrwppkZ", "so is the Laser");
    Check(ChooseWeapon(WeaponType::Pistol, { true, false, false }, Constant(8)) == "models/prop/PchrlaserZ",
          "which Aztec unlocks");
    Check(ChooseWeapon(WeaponType::Pistol, {}, Constant(9)) == "models/prop/PchrwppkZ", "and the Golden Gun");
    Check(ChooseWeapon(WeaponType::Pistol, { false, true, false }, Constant(9)) == "models/prop/PchrgoldenZ",
          "which Egypt unlocks");
}

void TestTheAnimationTable() {
    const auto animations = Animations();
    Check(animations.size() == 22, "the table has 22 animations");
    Check(animations[0].clip == "spotting_bond" && animations[0].weapon == WeaponType::None, "the first is unarmed");
    Check(animations[19].clip == "conversation" && animations[19].weapon == WeaponType::Rifle,
          "the conversations hold rifles");
}

void TestCameraPathRollsInTheDecompsOrder() {
    const CameraPath path = ChooseCameraPath(Sequence({ 0, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 0xFFFFFFFF }));
    Check(Near(path.distStart, 70.0f) && Near(path.distEnd, 150.0f), "distance is 70 to 150");
    Check(Near(path.angleStart, -3.14159265f) && Near(path.angleEnd, 1.2566371f), "the angles");
    Check(Near(path.heightStart, -100.0f) && Near(path.heightEnd, 100.0f), "and height is -100 to 100");
}

void TestFadeInAndOut() {
    Check(Near(Fade(-1), 0.0f) && Near(Fade(0), 0.0f), "the cast starts faded out");
    Check(Near(Fade(15), 0.5f), "half faded in after 15 ticks");
    Check(Near(Fade(30), 1.0f) && Near(Fade(100), 1.0f) && Near(Fade(150), 1.0f), "fully shown from 30 to 150");
    Check(Near(Fade(151), 29.0f / 30.0f), "fading out from 151");
    Check(Near(Fade(179), 1.0f / 30.0f), "to nearly nothing");
    Check(Near(Fade(180), 0.0f) && Near(Fade(400), 0.0f), "and out at 180");
}

void TestOrbitCamera() {
    CameraPath path;
    path.distStart = path.distEnd = 100.0f;
    path.heightStart = path.heightEnd = 5.0f;
    Orbit orbit = OrbitCamera(path, 0.0f);
    Check(Near(orbit.pos[0], 20.0f) && Near(orbit.pos[1], 5.0f) && Near(orbit.pos[2], 100.0f),
          "at angle 0 the camera is 100 back and a fifth of that to the side");
    Check(Near(orbit.target[0], 20.0f) && Near(orbit.target[1], 0.0f) && Near(orbit.target[2], 0.0f),
          "looking straight ahead from there");

    path.angleStart = path.angleEnd = -1.0f;
    orbit = OrbitCamera(path, 0.5f);
    const float angle = -1.0f + 6.2831855f;
    Check(Near(orbit.pos[0], 100.0f * std::sin(angle) + std::cos(angle) * 20.0f), "a negative angle wraps");

    path.angleStart = 0.0f;
    path.angleEnd = 1.0f;
    path.distEnd = 200.0f;
    orbit = OrbitCamera(path, 0.5f);
    Check(Near(orbit.pos[0], 150.0f * std::sin(0.5f) + std::cos(0.5f) * 0.2f * 150.0f), "halfway along the path");
}

Frigate::Mat4 Identity() {
    Frigate::Mat4 m{};
    for (int i = 0; i < 4; i++) {
        m.m[i][i] = 1.0f;
    }
    return m;
}

void TestTheCameraStartsOnTheRoot() {
    const Frigate::Mat4 matrix = Identity();
    RootFollow follow;
    Orbit orbit{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    Step(follow, { { 0.0f, 3.0f, 0.0f }, matrix, 1, 1.0f }, orbit);
    Check(!follow.reset, "the first step ends the reset");
    Check(Near(follow.rootPos[1], 3.0f), "the smoothed root starts at the root's height");
    Check(Near(orbit.pos[1], 3.0f + 52.5f), "the camera sits 52.5 above it");
    Check(Near(orbit.target[1], 3.0f - 3.0f - 10.0f), "and the target 10 below the root");
}

void TestARootThatHasMovedIsFollowedAtOnceOnTheFirstStep() {
    const Frigate::Mat4 matrix = Identity();
    RootFollow follow;
    Orbit orbit{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    Step(follow, { { 10.0f, 0.0f, 0.0f }, matrix, 1, 1.0f }, orbit);
    Check(Near(follow.rootPos[0], 10.0f, 0.01f), "priming the accumulator makes the first step follow exactly");
    Check(Near(orbit.pos[0], 10.0f, 0.01f), "and the camera with it");
}

void TestMoreTicksDecayTheAccumulatorFurther() {
    const Frigate::Mat4 matrix = Identity();
    RootFollow one;
    RootFollow two;
    one.reset = two.reset = false;
    one.rootVelAccumulator = two.rootVelAccumulator = { 100.0f, 0.0f, 0.0f };
    Orbit a{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    Orbit b = a;
    Step(one, { { 0.0f, 0.0f, 0.0f }, matrix, 1, 1.0f }, a);
    Step(two, { { 0.0f, 0.0f, 0.0f }, matrix, 2, 1.0f }, b);
    Check(Near(one.rootVelAccumulator[0], 95.0f), "one tick decays the accumulator by 0.95");
    Check(Near(two.rootVelAccumulator[0], 90.25f), "and two ticks by 0.95 twice");
}

void TestTheTargetIsCarriedThroughTheFirstMatrix() {
    Frigate::Mat4 matrix = Identity();
    matrix.m[3][0] = 100.0f;
    RootFollow follow;
    follow.reset = false;
    Orbit orbit{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    Step(follow, { { 0.0f, 0.0f, 0.0f }, matrix, 1, 1.0f }, orbit);
    Check(Near(follow.targetAccumulator[0], 100.0f), "the matrix's translation enters the target's accumulator");
}

} // namespace

int main() {
    TestTheShortCastSkipsTheExtendedEntries();
    TestTheExtendedCastShowsTheGroupEntries();
    TestLockedCharactersAreSkipped();
    TestGuestStarsShowWithAOneInTenThousandChance();
    TestNothingIsRolledForAnUnlockedCharacter();
    TestUnlocksComeFromTheCompletionSource();
    TestEachGatedEntryFollowsItsOwnCompletion();
    TestEachSubstitutionFollowsItsOwnCompletion();
    TestBondTakesARandomOutfit();
    TestNatalyaAndTrevelyanHaveOutfitsToo();
    TestRandomHeadsComeFromTheBodysGender();
    TestWeaponsAreSubstitutedUntilUnlocked();
    TestTheAnimationTable();
    TestCameraPathRollsInTheDecompsOrder();
    TestFadeInAndOut();
    TestOrbitCamera();
    TestTheCameraStartsOnTheRoot();
    TestARootThatHasMovedIsFollowedAtOnceOnTheFirstStep();
    TestMoreTicksDecayTheAccumulatorFurther();
    TestTheTargetIsCarriedThroughTheFirstMatrix();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All cast data tests passed\n");
    return 0;
}
