#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "LevelFixture.h"
#include "StanFixture.h"
#include "level/AiCommands.h"
#include "level/AiLists.h"
#include "level/BgLevel.h"
#include "level/AnimationTable.h"
#include "level/ApplySpeed.h"
#include "level/ChrActions.h"
#include "level/ChrAi.h"
#include "level/Chrs.h"
#include "level/Doors.h"
#include "level/Objects.h"
#include "level/PropLinks.h"
#include "level/StageSetup.h"
#include "level/Waypoints.h"
#include "model/Random.h"

using namespace Frigate::Level;
using namespace LevelFixture;
using Frigate::RandomGetNext;
using Frigate::RandomSetState;
using Frigate::RandomState;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) < tolerance;
}

// A host whose every answer is a fixed stand-in, recording the calls the tests look at.
class FakeHost final : public AiHost {
  public:
    AiPlayer Player() const override {
        return player;
    }
    int Difficulty() const override {
        return difficulty;
    }
    int StageNumber() const override {
        return 33;
    }
    float StageSeconds() const override {
        return stageSeconds;
    }
    float PowerOnSeconds() const override {
        return 0.0f;
    }
    int PlayerCount() const override {
        return 1;
    }
    float FarFogIntensitySquared() const override {
        return 1e12f;
    }
    bool RoomRendered(int) const override {
        return rendered;
    }
    bool RoomsSharePortal(int, int) const override {
        return false;
    }
    bool PositionOffScreen(const Vec3&, int) const override {
        return true;
    }
    bool PlayerAimsAtSphere(const Vec3&, float) const override {
        return false;
    }
    void AlarmActivate() override {
        alarm = true;
    }
    void AlarmDeactivate() override {
        alarm = false;
    }
    bool AlarmActive() const override {
        return alarm;
    }
    bool GasLeaking() const override {
        return false;
    }
    void GasLeak() override {
    }
    int CiviliansKilled() const override {
        return 0;
    }
    int ObjectiveCount() const override {
        return 2;
    }
    bool ObjectiveComplete(int objective) const override {
        return objective == 1;
    }
    bool AllObjectivesComplete() const override {
        return false;
    }
    int PlayerWeapon(int) const override {
        return 8;
    }
    int PlayerAmmo(int) const override {
        return 0;
    }
    void PlayerEquip(int, int) override {
    }
    void PlayerUnequip(int, int) override {
    }
    void PlayerRemoveItemInHand(int) override {
    }
    bool PlayerHasProp(int) const override {
        return false;
    }
    bool WeaponHasFlag(int, uint32_t) const override {
        return false;
    }
    const WeaponTable* Weapons() const override {
        return nullptr;
    }
    float AiAccuracy() const override {
        return 1.0f;
    }
    float AiDamage() const override {
        return 1.0f;
    }
    float AiHealth() const override {
        return 1.0f;
    }
    float AiReactionSpeed() const override {
        return 1.0f;
    }
    float PlayerDuckingHeight() const override {
        return 0.0f;
    }
    const Mtxf& ViewToWorld() const override {
        return viewToWorld;
    }
    const Mtxf& PreviousViewToWorld() const override {
        return viewToWorld;
    }
    bool PlayerDamageShowing() const override {
        return false;
    }
    void DamagePlayer(float, float) override {
    }
    void CountPlayerShot(int, int) override {
    }
    void PropHitSound(int, const Vec3&, bool) override {
    }
    void WorldHitSound(int, const Vec3&) override {
    }
    void CreateSpark(const Vec3&, int, float, int) override {
    }
    void ShootObject(PlacedObject&, float, const Vec3&, int) override {
    }
    void LaunchRound(Chr&, int, const Vec3&, int, const Vec3&, float, float) override {
    }
    void CountCivilianCasualty() override {
    }
    Mtxf viewToWorld = IdentityMtxf();
    void ShowBottomText(int text) override {
        bottomText = text;
    }
    void ShowTopText(int) override {
    }
    void BondControl(bool, uint8_t) override {
    }
    void CountdownShow(bool) override {
    }
    void CountdownSet(float ticks) override {
        countdown = ticks;
    }
    void CountdownRunning(bool running) override {
        countdownRunning = running;
    }
    bool CountdownIsRunning() const override {
        return countdownRunning;
    }
    float CountdownValue() const override {
        return countdown;
    }
    int CameraMode() const override {
        return 4;
    }
    void CameraReturnToBond() override {
    }
    void CameraLookAtBondFromPad(int) override {
    }
    void CameraSwitch(const PropDef&, int, int) override {
    }
    void CameraOrbitPad(int, int, int, int, int, int) override {
    }
    void ScreenFade(bool) override {
    }
    bool ScreenFadeCompleted() const override {
        return true;
    }
    void BondForceMove(float, float) override {
    }
    void SwitchSky() override {
    }
    void ExplodeAroundBond() override {
    }
    int FolderActor() const override {
        return 0;
    }
    void EndLevel() override {
        ended = true;
    }
    int StopTime() const override {
        return stopTime;
    }
    void SetStopTime(int state) override {
        stopTime = state;
    }
    int Credits() const override {
        return 0;
    }
    void SetCredits(int) override {
    }
    bool PlayerInvincible() const override {
        return invincible;
    }
    void SetPlayerInvincible(bool value) override {
        invincible = value;
    }
    void SetBondKilledInAction() override {
    }
    void SfxPlay(int, int) override {
    }
    void SfxStop(int) override {
    }
    void SfxSetVolume(int, int, int) override {
    }
    void SfxFadeVolume(int, float, int) override {
    }
    void SfxEmitFromProp(int, int, int) override {
    }
    void SfxEmitFromPad(int, int, int) override {
    }
    int SfxChannelVolume(int) const override {
        return 0;
    }
    void MusicPlaySlot(int, int, int) override {
    }
    void MusicStopSlot(int) override {
    }
    void UseObject(int) override {
    }
    void DestroyObject(PlacedObject&) override {
    }
    void DropProp(int, int) override {
    }
    void DropConcealedItems(Chr&) override {
    }
    void CollectProp(int) override {
    }
    void EquipObject(int, Chr&) override {
    }
    void MoveObjectToPad(PlacedObject&, int) override {
    }
    bool KeyDropped(int) const override {
        return false;
    }
    void SetMonitorImage(PlacedObject&, int, int) override {
    }
    void LaunchObject(PlacedObject&) override {
    }
    bool GiveWeapon(Chr&, int, int, uint32_t) override {
        return false;
    }
    bool GiveHat(Chr&, int, uint32_t) override {
        return false;
    }
    bool DropNewItem(Chr&, int, int) override {
        return false;
    }
    void CloneChr(Chr&, Chr&) override {
    }
    void RemoveHeldItem(Chr&, int) override {
    }
    int HeldGunItem(const Chr&, int) const override {
        return -1;
    }

    AiPlayer player;
    int difficulty = 0;
    float stageSeconds = 0.0f;
    bool rendered = true;
    bool alarm = false;
    bool ended = false;
    bool countdownRunning = false;
    float countdown = 0.0f;
    int bottomText = -1;
    int stopTime = 0;
    bool invincible = false;
};

// Records what the AI asks of a character.
class RecordingActions final : public AiActions {
  public:
    bool Sidestep(Chr&) override {
        sidesteps++;
        return sidestepResult;
    }
    void Stop(Chr&) override {
        stops++;
    }
    bool PlotCourse(Chr&, const Vec3&, int, Speed speed) override {
        plotted.push_back(speed);
        return true;
    }
    void StartIdleAnimation(Chr&, float duration, bool unarmed, bool mirror) override {
        idle.push_back({ duration, unarmed, mirror });
    }

    struct Idle {
        float duration;
        bool unarmed;
        bool mirror;
    };
    int sidesteps = 0;
    bool sidestepResult = false;
    int stops = 0;
    std::vector<Speed> plotted;
    std::vector<Idle> idle;
};

constexpr float kFloorSize = 2000.0f;

// An AI list from its bytes.
AiList MakeList(int32_t id, std::vector<uint8_t> bytes) {
    AiList list;
    list.id = id;
    list.bytes = std::move(bytes);
    return list;
}

// A setup file with four pads, four waypoints in two groups, a patrol path and
// two AI lists.
std::vector<uint8_t> BuildSetup(float scale, const std::vector<std::vector<uint8_t>>& lists) {
    std::vector<uint8_t> file(0x28, 0);
    const size_t padsAt = file.size();
    file.resize(padsAt + 5 * 0x2C, 0);
    const size_t namesAt = file.size();
    file.resize(namesAt + 8, 0);
    std::memcpy(&file[namesAt], "p5a1", 5);
    const size_t introAt = file.size();
    file.resize(introAt + 4, 0);
    PutWords(file, introAt, { 9 });
    const size_t propsAt = file.size();
    file.resize(propsAt + 4, 0);
    PutWords(file, propsAt, { 0x30 });

    for (size_t i = 0; i < 4; i++) {
        PutPad(file, padsAt + i * 0x2C, { (100 + 300 * static_cast<float>(i)) * scale, 50 * scale, 700 * scale },
               { 0, 0, 1 }, static_cast<uint32_t>(namesAt));
    }

    // Neighbour arrays, each ended by -1.
    const auto array = [&file](std::initializer_list<int32_t> values) {
        const size_t at = file.size();
        file.resize(at + (values.size() + 1) * 4, 0);
        size_t cursor = at;
        for (const int32_t value : values) {
            PutWords(file, cursor, { value });
            cursor += 4;
        }
        PutWords(file, cursor, { -1 });
        return at;
    };
    const size_t n0 = array({ 1 });
    const size_t n1 = array({ 0, 2 });
    const size_t n2 = array({ 1, 3 });
    const size_t n3 = array({ 2 });
    const size_t g0 = array({ 0, 1 });
    const size_t g1 = array({ 2, 3 });
    const size_t gn0 = array({ 1 });
    const size_t gn1 = array({ 0 });
    const size_t path = array({ 0, 1, 2 });

    const size_t waypointsAt = file.size();
    file.resize(waypointsAt + 5 * 16, 0);
    const size_t neighbours[4] = { n0, n1, n2, n3 };
    for (int32_t i = 0; i < 4; i++) {
        PutWords(file, waypointsAt + static_cast<size_t>(i) * 16, { i, static_cast<int32_t>(neighbours[i]), i / 2, 0 });
    }
    PutWords(file, waypointsAt + 4 * 16, { -1 });

    const size_t groupsAt = file.size();
    file.resize(groupsAt + 3 * 12, 0);
    PutWords(file, groupsAt, { static_cast<int32_t>(gn0), static_cast<int32_t>(g0), 0 });
    PutWords(file, groupsAt + 12, { static_cast<int32_t>(gn1), static_cast<int32_t>(g1), 0 });

    const size_t pathsAt = file.size();
    file.resize(pathsAt + 2 * 8, 0);
    PutWords(file, pathsAt, { static_cast<int32_t>(path), (5 << 24) | (1 << 16) });

    std::vector<size_t> listAt;
    for (const auto& bytes : lists) {
        listAt.push_back(file.size());
        file.insert(file.end(), bytes.begin(), bytes.end());
        while (file.size() % 4 != 0) {
            file.push_back(0);
        }
    }
    const size_t listsAt = file.size();
    file.resize(listsAt + (lists.size() + 1) * 8, 0);
    for (size_t i = 0; i < lists.size(); i++) {
        PutWords(file, listsAt + i * 8, { static_cast<int32_t>(listAt[i]), static_cast<int32_t>(1025 + i) });
    }

    PutWords(file, 0,
             { static_cast<int32_t>(waypointsAt), static_cast<int32_t>(groupsAt), static_cast<int32_t>(introAt),
               static_cast<int32_t>(propsAt), static_cast<int32_t>(pathsAt), static_cast<int32_t>(listsAt),
               static_cast<int32_t>(padsAt), 0, 0, 0 });
    return file;
}

// The pieces a script runs against: a floor, a setup, one character, the player.
struct World {
    explicit World(std::vector<std::vector<uint8_t>> listBytes = {}) {
        source.header = BuildHeader([] {
            HeaderSpec spec;
            spec.rooms = { { false, false, { 0, 0, 0 } }, { false, false, { 0, 0, 0 } } };
            return spec;
        }());
        source.stan = StanFixture::MakeFloor(static_cast<int16_t>(kFloorSize), 0, 0, 1);
        level = std::make_unique<BgLevel>(source);
        level->Load(LevelId::Dam);
        const float scale = level->Level().scale;
        std::string error;
        auto parsed = StageSetup::Parse(BuildSetup(scale, listBytes), 1.0f / scale, &error);
        setup = std::move(*parsed);
        stan = &level->StanFile();
        setup.ResolveStanTiles(*stan);

        objects.Reset(level->MaxNumRooms(), stan, level.get(), nullptr);
        doors.Reset(stan, level.get(), nullptr, &objects.Props());
        stan->SetProps(&objects);
        chrs.Reset(stan, &objects.Props(), Frigate::Game::ChrRecords::Parse(kRecords), nullptr, nullptr);
        chrs.AllocateSlots(3);
        chrs.SetAiLists(&lists);
        links.Reset();
        lists.SetLocal(setup.AiListSection());
        paths.Reset(&setup);

        // The player stands at the third pad's place.
        PropList& props = objects.Props();
        playerProp = props.Allocate();
        props[playerProp].type = PropType::Viewer;
        props[playerProp].position = setup.Pads()[2].position;
        props[playerProp].stan = setup.Pads()[2].stanTile;
        props.Activate(playerProp);
        host.player.prop = playerProp;
        host.player.yaw = 0.0f;

        scene.setup = &setup;
        scene.stan = stan;
        scene.props = &props;
        scene.chrs = &chrs;
        scene.objects = &objects;
        scene.doors = &doors;
        scene.links = &links;
        scene.paths = &paths;
        scene.lists = &lists;
        ai = std::make_unique<ChrAi>(scene, host, actions);
        chrs.SetTiming({ 100, 1.0f, 1 });
    }

    // Spawns a guard at pad `pad` with the global list `listId`.
    Chr& Spawn(int pad, int chrnum = 30) {
        // A guard propdef record at the end of the setup is not parsed by the
        // test's setup, so the slot is made directly.
        int slot = -1;
        for (size_t i = 0; i < chrs.Slots().size(); i++) {
            if (!chrs.Slots()[i].inUse) {
                slot = static_cast<int>(i);
                break;
            }
        }
        PropList& props = objects.Props();
        const int prop = props.Allocate();
        Chr& chr = chrs[slot];
        chr = Chr{};
        chr.inUse = true;
        chr.chrnum = static_cast<int16_t>(chrnum);
        chr.prop = prop;
        props[prop].type = PropType::Chr;
        props[prop].object = slot;
        props[prop].position = setup.Pads()[static_cast<size_t>(pad)].position;
        props[prop].stan = setup.Pads()[static_cast<size_t>(pad)].stanTile;
        props.Activate(prop);
        props.Enable(prop);
        return chr;
    }

    static constexpr const char* kRecords = "0\tBODY_Jungle_Commando\tCa\t1\t0\t1.0\t1.0\t100.0\n";

    FakeSource source;
    std::unique_ptr<BgLevel> level;
    StageSetup setup;
    Stan* stan = nullptr;
    Objects objects;
    Doors doors;
    PropLinks links;
    Chrs chrs;
    Waypoints paths;
    AiLists lists;
    FakeHost host;
    RecordingActions actions;
    AiScene scene;
    std::unique_ptr<ChrAi> ai;
    int playerProp = -1;
};

std::vector<uint8_t> Bytes(std::initializer_list<int> values) {
    std::vector<uint8_t> bytes;
    for (const int value : values) {
        bytes.push_back(static_cast<uint8_t>(value));
    }
    return bytes;
}

void TestLists() {
    AiLists lists;
    Check(lists.ParseGlobals("0\t0204\n7\t0304\n"), "global lists parse");
    Check(!lists.ParseGlobals("0\t020\n"), "an odd number of hex digits is malformed");
    Check(lists.ParseGlobals("0\t0204\n7\t0304\n"), "and the lists are replaced by a good parse");
    lists.SetLocal({ MakeList(1025, Bytes({ AI_EndList })), MakeList(7, Bytes({ AI_Yield, AI_EndList })) });
    Check(lists.FindById(0) != nullptr && lists.FindById(0)->bytes.size() == 2, "a global id finds a global list");
    Check(lists.FindById(7) != nullptr && lists.FindById(7)->bytes[0] == AI_Yield,
          "an id of 1024 or less never finds a setup list");
    Check(lists.FindById(1025) != nullptr && lists.FindById(1025)->bytes.size() == 1,
          "a larger id finds the setup's list");
    Check(lists.FindById(2000) == nullptr, "and an unknown one finds none");

    // chraiitemsize: a fixed size, an unknown opcode is one byte, and AI_PRINT runs to its NUL.
    const std::vector<uint8_t> list = Bytes({ AI_Label, 1, AI_PRINT, 'h', 'i', 0, AI_SetNewRandom, 250, AI_EndList });
    Check(AiItemSize(list.data(), list.size(), 0) == AiLabelRecord::kSize, "a label is two bytes");
    Check(AiItemSize(list.data(), list.size(), 2) == 4, "a print runs to its terminator");
    Check(AiItemSize(list.data(), list.size(), 7) == 1, "an unknown opcode is one byte");

    // chraiGoToLabel.
    const std::vector<uint8_t> jumps =
        Bytes({ AI_SetNewRandom, AI_Label, 3, AI_SetNewRandom, AI_Label, 4, AI_SetNewRandom, AI_EndList });
    Check(AiGoToLabel(jumps.data(), jumps.size(), 0, 4) == 4, "a jump lands on its label");
    Check(AiGoToLabel(jumps.data(), jumps.size(), 0, 9) == 0, "a label the list lacks sends it to the start");
    Check(AiGoToLabel(jumps.data(), jumps.size(), 5, 3) == 0, "a label behind the start is not found");

    const AiCommand command(jumps.data(), jumps.size(), 1);
    Check(command.U8(AiLabelRecord::ID) == 3, "a command reads its bytes");
    const std::vector<uint8_t> wide = Bytes({ 0, 0x12, 0x34, 0x56, 0x78, 0xFF, 0xFE });
    const AiCommand read(wide.data(), wide.size(), 0);
    Check(read.U16(1) == 0x1234 && read.U32(1) == 0x12345678u && read.U24(1) == 0x123456u && read.S16(5) == -2,
          "multi-byte fields are big-endian");
}

void TestSetupSections() {
    World world({ Bytes({ AI_Yield, AI_EndList }), Bytes({ AI_SetNewRandom, AI_EndList }) });
    Check(world.setup.PathWaypoints().size() == 4 && world.setup.WaypointGroups().size() == 2 &&
              world.setup.PatrolPaths().size() == 1 && world.setup.AiListSection().size() == 2,
          "the setup's graph and lists are read");
    Check(world.setup.PathWaypoints()[1].neighbours == std::vector<int32_t>({ 0, 2 }) &&
              world.setup.PathWaypoints()[0].groupNum == 0,
          "a waypoint has its neighbours and group");
    const PatrolPath& path = world.setup.PatrolPaths()[0];
    Check(path.id == 5 && path.isLoop && path.len == 3 && path.waypoints == std::vector<int32_t>({ 0, 1, 2 }),
          "a patrol path has its id, loop flag and length");
    Check(world.setup.AiListSection()[0].id == 1025 && world.setup.AiListSection()[0].bytes.size() == 2 &&
              world.setup.AiListSection()[1].id == 1026,
          "each AI list runs to its end command");
}

void TestWaypoints() {
    World world;
    Waypoints& paths = world.paths;
    const int errors = paths.InitLinks();
    for (const std::string& line : paths.Errors()) {
        std::fprintf(stderr, "  %s\n", line.c_str());
    }
    Check(errors == 0, "a consistent graph has no errors");
    Check(paths.All()[0].groupNum == 0 && paths.All()[3].groupNum == 1, "the groups number their waypoints");
    Check(paths.FindPath(5) == 0 && paths.FindPath(6) == -1, "a patrol path is found by its id");

    // waypointFindRoute across both groups.
    std::array<int, 8> route{};
    const int written = paths.FindRoute(0, 3, route.data(), static_cast<int>(route.size()));
    Check(written == 5 && route[0] == 0 && route[1] == 1 && route[2] == 2 && route[3] == 3 && route[4] == -1,
          "a route runs from one waypoint to the other through the link between the groups");
    std::array<int, 3> shortRoute{};
    const int shortWritten = paths.FindRoute(0, 3, shortRoute.data(), 3);
    Check(shortWritten == 3 && shortRoute[0] == 0 && shortRoute[2] == -1, "and is cut short by a short buffer");

    // The next step toward a waypoint in the same group, and in another.
    RandomSetState(0x123456789ull);
    // The search is the decomp's as written; a step is a waypoint or none.
    const int step = paths.FindNextStepToward(0, 3);
    Check(step >= -1 && step < 4, "a next-step query answers with a waypoint or none");

    // Nearest waypoints.
    Check(paths.FindNearestToPosition(world.setup.Pads()[2].position) == 2, "the nearest waypoint is at the pad");
    Check(paths.RefineNearest(world.setup.Pads()[2].position, 1) == 2, "a neighbour closer than the start wins");
    Check(paths.NearestTo(*world.stan, world.setup.Pads()[3].position, world.setup.Pads()[3].stanTile) >= 0,
          "a search from a tile finds a waypoint on it");
}

void TestInterpreter() {
    // Label 1 at offset 8; the list sets morale to 7, or to 9 after the jump.
    const std::vector<uint8_t> script = Bytes({ AI_SetNewRandom, AI_IFRandomLessThan, 255, 1, AI_SetMyMorale, 7,
                                                AI_Yield, AI_Label, 1, AI_SetMyMorale, 9, AI_Yield, AI_EndList });
    World world({ script });
    Chr& chr = world.Spawn(0);
    chr.aiList = world.lists.FindById(1025);

    RandomSetState(0x00000000000000ffull);
    world.ai->Run(chr);
    // SetNewRandom drew a byte below 255 (or equal), so the test jumped.
    Check((chr.random < 255) == (chr.morale == 9), "a conditional command jumps on its test");
    Check(chr.aiOffset == 12, "a yield saves the place after it");
    Check(chr.morale == 9 || chr.morale == 7, "and the commands before it ran");

    // The next run resumes at the saved offset: past the yield, at the end.
    chr.morale = 0;
    world.ai->Run(chr);
    Check(chr.morale == 0 && chr.aiOffset == 12, "an end of list stops the run where it was");
}

void TestFlow() {
    // List 1025 swaps to 1026, which returns to a list set earlier; 1027 is the returned-to list.
    const std::vector<uint8_t> first =
        Bytes({ AI_SetReturnAiList, 0x04, 0x03, AI_SetChrAiList, 0xFD, 0x04, 0x02, AI_EndList });
    const std::vector<uint8_t> second = Bytes({ AI_SetMyMorale, 4, AI_Return, AI_EndList });
    const std::vector<uint8_t> third = Bytes({ AI_SetMyAlertness, 5, AI_Yield, AI_EndList });
    World world({ first, second, third });
    Chr& chr = world.Spawn(0);
    chr.aiList = world.lists.FindById(1025);
    Check(chr.aiList != nullptr, "the lists are found by id");
    // 0x0403 is list 1027; 0x0402 is 1026.
    world.ai->Run(chr);
    Check(chr.aiReturnList == 0x403 && chr.morale == 4 && chr.alertness == 5 && chr.aiList != nullptr &&
              chr.aiList->id == 0x403,
          "a list sets its return list, switches to another that returns, and the returned-to list runs");
    Check(chr.aiOffset == 3, "and its yield saved the place in that list");
}

void TestFlagsAndTimers() {
    const std::vector<uint8_t> script = Bytes({ AI_SetMyMorale,
                                                250,
                                                AI_AddToMyMorale,
                                                10,
                                                AI_SubtractFromMyAlertness,
                                                3,
                                                AI_SetMyFlags2,
                                                0x05,
                                                AI_UnsetMyFlags2,
                                                0x01,
                                                AI_MyTimerStart,
                                                AI_SetObjectiveBitfield,
                                                0,
                                                0,
                                                0,
                                                0x0C,
                                                AI_UnsetObjectiveBitfield,
                                                0,
                                                0,
                                                0,
                                                0x04,
                                                AI_SetMychrflags,
                                                0,
                                                0,
                                                0,
                                                0x10,
                                                AI_SetMyHealthTotal,
                                                0,
                                                100,
                                                AI_Yield,
                                                AI_EndList });
    World world({ script });
    Chr& chr = world.Spawn(0);
    chr.aiList = world.lists.FindById(1025);
    chr.alertness = 2;
    world.ai->Run(chr);
    Check(chr.morale == 255, "morale clamps at 255");
    Check(chr.alertness == 0, "alertness clamps at 0");
    Check(chr.flags2 == 0x04, "flags2 sets and clears bits");
    Check((chr.hidden & ChrHidden::TimerActive) != 0 && chr.timer60 == 0, "the timer starts");
    Check(world.ai->State().objectiveRegisters == 0x08, "objective bits set and clear");
    Check((chr.chrflags & 0x10) != 0, "character flags set");
    Check(Near(chr.maxDamage, 10.0f), "the health total is tenths");

    // chrlvActionTick counts the timer and runs the list when the sleep is over.
    chr.sleep = 5;
    chr.actionType = ChrAction::Stand;
    world.chrs.SetTiming({ 101, 1.0f, 2 });
    world.ai->ActionTick(chr);
    Check(chr.timer60 == 2 && chr.sleep == 3, "a tick adds its clock to the timer and takes it from sleep");
    // sleep is an s8.
    chr.sleep = -128;
    world.chrs.SetTiming({ 102, 1.0f, 1 });
    world.ai->ActionTick(chr);
    Check(chr.sleep == 127, "sleep wraps as a signed byte");
}

void TestSenses() {
    World world;
    Chr& chr = world.Spawn(0);
    chr.visionRange = 250.0f;
    // The guard faces +z (heading 0) and the player stands 600 units along +x: outside the view cone.
    Check(Near(world.ai->DistanceToBond(chr), 600.0f, 1.0f), "the distance to the player");
    // get_distance_actor_to_position: the player is at 90 degrees to the guard's left.
    Check(Near(world.ai->AngleToBond(chr), 1.5707964f, 1e-3f), "the angle to the player");
    Check(Near(world.ai->AngleFromBond(chr), 4.712389f, 1e-3f), "and from them");

    world.chrs.SetTiming({ 500, 1.0f, 1 });
    world.ai->SetTargetToPlayer(chr);
    Check(chr.lastSeeTarget60 == 500 && world.ai->SawTargetRecently(chr), "a sighting is remembered");
    world.chrs.SetTiming({ 500 + 599, 1.0f, 1 });
    Check(world.ai->SawTargetRecently(chr), "for ten seconds");
    world.chrs.SetTiming({ 500 + 600, 1.0f, 1 });
    Check(!world.ai->SawTargetRecently(chr), "and no longer");

    world.ai->AlertToPlayer(chr);
    Check(world.ai->IsHearingBond(chr) && world.ai->HeardTargetRecently(chr), "a sound alerts the guard");
    // chrCheckGuardsHeardSound: the player is 600 away; hearing scale 1 and noise 5 reach 500.
    chr.hidden = 0;
    world.ai->GuardsHeardSound(5.0f);
    Check(!world.ai->IsHearingBond(chr), "a quiet sound does not reach far");
    world.ai->GuardsHeardSound(7.0f);
    Check(world.ai->IsHearingBond(chr), "a louder one does");

    // A player behind the guard cannot be seen however near.
    chr.lastSeeTarget60 = 0;
    RandomSetState(0x1234ull);
    Check(!world.ai->CheckTargetInSight(chr), "a player outside the cone is not seen");
    Check(world.ai->IsNotDeadOrShot(chr) && !world.ai->IsDead(chr), "a standing guard can act");
    chr.actionType = ChrAction::Die;
    Check(!world.ai->IsNotDeadOrShot(chr) && world.ai->IsDead(chr), "a dying one cannot");
}

void TestKneelToStand() {
    World world;
    Chr& chr = world.Spawn(0);
    // wallcount is randomGetNext() % 120 + 180; with no weapons the idle animation draws a flip.
    RandomSetState(0x42ull);
    const uint64_t start = RandomState();
    world.ai->MergeKneelToStand(chr, 16.0f);
    Check(chr.actionType == ChrAction::Stand && chr.actStand.turning == 2 && chr.actStand.prestand == 0,
          "the guard stands");
    Check(chr.actStand.wallCount >= 180 && chr.actStand.wallCount < 300,
          "with a wall check between 3 and 5 seconds off");
    Check(chr.sleep == 16, "and sleeps the merge time");
    Check(world.actions.idle.size() == 1 && world.actions.idle[0].unarmed && world.actions.idle[0].duration == 16.0f,
          "an unarmed guard starts the unarmed idle");

    // Two draws: the wall count, then the idle's flip.
    RandomSetState(start);
    const uint32_t first = RandomGetNext();
    const uint32_t second = RandomGetNext();
    RandomSetState(start);
    Chr& other = world.Spawn(1, 31);
    world.ai->MergeKneelToStand(other, 0.0f);
    Check(other.actStand.wallCount == static_cast<int32_t>(first % 120u) + 180 &&
              world.actions.idle.back().mirror == ((second & 1) != 0),
          "the draws are the wall count and then the flip");
}

void TestCommandsWithFakeHost() {
    // TRYSidestepping falls through when the action cannot; IFChrDoesNotExist for a missing chr jumps.
    const std::vector<uint8_t> script = Bytes({ AI_TRYSidestepping,
                                                1,
                                                AI_SetMyMorale,
                                                1,
                                                AI_IFChrDoesNotExist,
                                                99,
                                                2,
                                                AI_SetMyMorale,
                                                2,
                                                AI_Label,
                                                2,
                                                AI_TextPrintBottom,
                                                0x12,
                                                0x34,
                                                AI_AlarmOn,
                                                AI_IFAlarmIsOn,
                                                3,
                                                AI_Yield,
                                                AI_Label,
                                                3,
                                                AI_HudCountdownSet,
                                                0,
                                                10,
                                                AI_HudCountdownStart,
                                                AI_EndList });
    World world({ script });
    Chr& chr = world.Spawn(0);
    chr.aiList = world.lists.FindById(1025);
    world.ai->Run(chr);
    Check(world.actions.sidesteps == 1 && chr.morale == 1, "a failing TRY command falls through");
    Check(world.host.bottomText == 0x1234, "text ids are big-endian");
    Check(world.host.alarm, "the alarm starts");
    Check(Near(world.host.countdown, 600.0f) && world.host.countdownRunning,
          "a list that jumped past the yield started the countdown (seconds are ticks over 60)");
    Check(chr.aiOffset == 0, "the end of the list did not save a place");
}

void TestBackgroundChrs() {
    World world({ Bytes({ AI_EndList }) });
    // The setup's list is 1025; a background list has an id from 4096.
    world.lists.SetLocal({ MakeList(1025, Bytes({ AI_EndList })), MakeList(4096, Bytes({ AI_Yield, AI_EndList })),
                           MakeList(4097, Bytes({ AI_EndList })) });
    world.chrs.CreateBackgroundChrs();
    Check(world.chrs.BackgroundCount() == 2, "one background character per background list");
    Chr& first = world.chrs.Background()[0];
    Check(first.chrnum == kBackgroundChrnum && first.actionType == ChrAction::Null && first.aiOffset == 0 &&
              first.aiReturnList == -1,
          "each starts on its list");
    Chr& self = world.Spawn(0);
    Check(world.chrs.FindById(self, 0xFE) == &first, "a character lookup falls back to the background ones");
    Check(Chrs::ResolveId(self, static_cast<uint8_t>(kChrSelf)) == self.chrnum, "CHR_SELF names the character");

    world.chrs.SetTiming({ 10, 1.0f, 1 });
    world.ai->AllChrTick();
    Check(first.aiOffset == 1, "the background characters run their lists");
}

// A model with a playback and no joints, and a factory that hands every clip
// the same ten-second stand-in.
class FakeBody final : public ModelHandle {
  public:
    Frigate::ModelPlayback& Playback() override {
        return playback;
    }
    bool AttachTo(ModelHandle&, size_t, float) override {
        return true;
    }
    void Detach() override {
    }
    void SetSwitchVisible(size_t, bool) override {
    }
    void SetGunfireVisible(size_t, bool) override {
    }
    Frigate::ModelPlayback playback;
};

class FakeClips final : public ModelFactory {
  public:
    FakeClips() {
        clip.frameCount = 600;
    }
    std::shared_ptr<ModelHandle> CreateBody(const std::string&, const std::string&, bool) override {
        return nullptr;
    }
    std::shared_ptr<ModelHandle> CreateProp(const std::string&) override {
        return nullptr;
    }
    const Frigate::AnimationClip* LoadClip(const std::string& name) override {
        loaded.push_back(name);
        return &clip;
    }
    Frigate::AnimationClip clip;
    std::vector<std::string> loaded;
};

void TestAnimationTable() {
    const auto table = AnimationTable::Parse("1\t0\tidle\n1\t1\t-\n1\t2\twalking\n2\t0\thelicopter_cradle\n");
    Check(table.has_value() && table->Character(0) == "idle" && table->Character(2) == "walking" &&
              table->Aircraft(0) == "helicopter_cradle",
          "the animation tables name their clips");
    Check(table->Character(1).empty() && table->Character(9).empty() && table->Character(-1).empty(),
          "a placeholder or an id outside the table has none");
    Check(!AnimationTable::Parse("1\t1\tidle\n").has_value() && !AnimationTable::Parse("3\t0\tidle\n").has_value() &&
              !AnimationTable::Parse("").has_value(),
          "a table that skips an id, names an unknown one or is empty is malformed");
}

void TestApplySpeed() {
    float position = 0.0f;
    float speed = 0.0f;
    ChrObjApplySpeed(&position, 10.0f, &speed, 0.5f, 1.0f, 2.0f, 100);
    Check(position == 10.0f, "an eased value reaches its target");

    // The angular form takes the short way round and stays within a turn.
    float angle = 6.0f;
    float turn = 0.0f;
    ChrObjCallsApplySpeed(&angle, 0.2f, &turn, 0.01f, 0.02f, 0.05f, 200);
    Check(Near(angle, 0.2f), "an angle across the wrap reaches its target");
    angle = 0.1f;
    turn = 0.0f;
    ChrObjCallsApplySpeed(&angle, 6.2f, &turn, 0.01f, 0.02f, 0.05f, 200);
    Check(Near(angle, 6.2f), "and turns the short way, wrapping below zero");
}

void TestChrActions() {
    World world;
    FakeClips clips;
    world.host.player.prop = world.playerProp;
    ChrActions actions(world.scene, world.host, &clips,
                       AnimationTable::Parse("1\t0\tidle\n1\t1\tyawning\n1\t2\twalking\n"));
    ChrAi ai(world.scene, world.host, actions);
    actions.Attach(&ai);

    Chr& chr = world.Spawn(0);
    auto body = std::make_shared<FakeBody>();
    chr.model = body;
    world.chrs.SetTiming({ 100, 1.0f, 1 });

    // Kneeling, then standing up from it.
    actions.Kneel(chr);
    Check(chr.actionType == ChrAction::Kneel && chr.sleep == 0 && body->playback.anim == &clips.clip &&
              body->playback.endFrame == 28.0f,
          "a guard kneels into the animation that holds its last frame at 28");
    actions.Stop(chr);
    Check(chr.actionType == ChrAction::Stand && chr.actStand.prestand == 1 && chr.actStand.checkFacingWall == 1 &&
              body->playback.endFrame == 140.0f,
          "stopping a kneeling guard stands it up, then has it look for a wall");
    actions.Stop(chr);
    Check(chr.actionType == ChrAction::Stand && chr.actStand.prestand == 0 && chr.actStand.checkFacingWall == 1,
          "and a standing one merges into the idle");

    // A scripted animation.
    actions.PlayAnimation(chr, 1, 5, 40, 0x10 | 0x08 | 0x01, 16);
    Check(chr.actionType == ChrAction::Anim && chr.actAnim.idleOnEnd && chr.actAnim.playSfx &&
              !chr.actAnim.holdLastFrame && !chr.actAnim.noTranslate && chr.sleep == 16 && body->playback.mirror &&
              body->playback.endFrame == 40.0f && body->playback.speed == 0.5f,
          "a scripted animation sets the action's flags, sleeps the interpolation and mirrors");
    actions.PlayAnimation(chr, 1, 0, -1, 0x80 | 0x04, 16);
    Check(chr.actAnim.holdLastFrame && chr.sleep == 0 && body->playback.speed == -0.5f &&
              body->playback.endFrame == -1.0f,
          "a reversed one runs backwards over the whole clip and sleeps nothing");

    // A guard that stood up from a finished animation.
    body->playback.frame = 700.0f;
    chr.actAnim.holdLastFrame = false;
    chr.sleep = 0;
    actions.Tick(chr);
    Check(chr.actionType == ChrAction::Stand, "a finished animation ends in standing");

    // Fading away.
    actions.FadeOut(chr);
    Check(chr.actionType == ChrAction::Dead && chr.actDead.fadeTicks == -1, "fading starts a dead guard's timer");
    actions.Tick(chr);
    Check(chr.actDead.fadeTicks == 0, "the first tick arms it");
    world.chrs.SetTiming({ 100, 1.0f, 45 });
    actions.Tick(chr);
    Check(chr.actDead.fadeTicks == 45 && chr.fadeAlpha == 127 && (chr.hidden & ChrHidden::Remove) == 0,
          "half way through it the guard is half faded");
    actions.Tick(chr);
    Check((chr.hidden & ChrHidden::Remove) != 0, "and after 90 ticks it is removed");
    world.chrs.SetTiming({ 100, 1.0f, 1 });

    // Patrolling.
    Chr& walker = world.Spawn(0, 31);
    walker.model = std::make_shared<FakeBody>();
    world.paths.InitLinks();
    const uint64_t before = Frigate::RandomState();
    actions.StartPatrol(walker, 0);
    Check(walker.actionType == ChrAction::Patrol && walker.actPatrol.path == 0 && walker.actPatrol.forward &&
              walker.actPatrol.lastVisible60 == -1 && walker.actPatrol.way.mode == WayMode::Mode0 &&
              walker.actPatrol.way.age < 100 && Frigate::RandomState() != before,
          "a patrol starts on its path with a random age");

    // The AI refuses actions of the dead.
    Check(!actions.Sidestep(chr) || chr.actionType != ChrAction::Dead, "a faded guard does not sidestep");
    chr.actionType = ChrAction::Die;
    Check(!actions.Sidestep(chr) && !actions.SideHop(chr) && !actions.SideRun(chr),
          "a dying guard does not sidestep, hop or run sideways");
}

} // namespace

int main() {
    TestLists();
    TestSetupSections();
    TestWaypoints();
    TestInterpreter();
    TestFlow();
    TestFlagsAndTimers();
    TestSenses();
    TestKneelToStand();
    TestCommandsWithFakeHost();
    TestBackgroundChrs();
    TestAnimationTable();
    TestApplySpeed();
    TestChrActions();
    if (gFailures == 0) {
        std::puts("ok");
    }
    return gFailures == 0 ? 0 : 1;
}
