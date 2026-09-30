#include "level/StageSetup.h"

#include <cstddef>
#include <cstring>
#include <utility>

namespace Frigate::Level {

namespace {

constexpr size_t kHeaderWords = 10;
constexpr size_t kPadSize = 0x2C;
constexpr size_t kBoundPadSize = 0x44;
constexpr float kFixedPointOne = 65536.0f;

// Bounds-checked big-endian reads. A read past the end sets `failed`, after
// which every read returns 0.
class Reader {
  public:
    explicit Reader(const std::vector<uint8_t>& file) : file_(file) {
    }

    uint32_t U32(size_t at) {
        if (at > file_.size() || file_.size() - at < 4) {
            failed_ = true;
            return 0;
        }
        return (uint32_t{ file_[at] } << 24) | (uint32_t{ file_[at + 1] } << 16) | (uint32_t{ file_[at + 2] } << 8) |
               uint32_t{ file_[at + 3] };
    }
    int32_t S32(size_t at) {
        return static_cast<int32_t>(U32(at));
    }
    uint16_t U16(size_t at) {
        return static_cast<uint16_t>(U32(at) >> 16);
    }
    float F32(size_t at) {
        const uint32_t bits = U32(at);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    Vec3 Vector(size_t at) {
        return { F32(at), F32(at + 4), F32(at + 8) };
    }
    // The C string at `at`.
    std::string String(size_t at) {
        std::string text;
        for (; at < file_.size(); at++) {
            if (file_[at] == 0) {
                return text;
            }
            text.push_back(static_cast<char>(file_[at]));
        }
        failed_ = true;
        return text;
    }
    bool Failed() const {
        return failed_;
    }
    void Fail() {
        failed_ = true;
    }

  private:
    const std::vector<uint8_t>& file_;
    bool failed_ = false;
};

Vec3 Scaled(const Vec3& value, float scale) {
    return { value[0] * scale, value[1] * scale, value[2] * scale };
}

// The pad's fields at `at`; the position is scaled by `roomScale`.
Pad ReadPad(Reader& in, size_t at, float roomScale) {
    Pad pad;
    pad.position = Scaled(in.Vector(at), roomScale);
    pad.up = in.Vector(at + 0x0C);
    pad.look = in.Vector(at + 0x18);
    pad.tileName = in.String(in.U32(at + 0x24));
    return pad;
}

std::vector<std::string> ReadNames(Reader& in, uint32_t list) {
    std::vector<std::string> names;
    if (list == 0) {
        return names;
    }
    for (size_t at = list; !in.Failed(); at += 4) {
        const uint32_t offset = in.U32(at);
        if (offset == 0) {
            break;
        }
        names.push_back(in.String(offset));
    }
    return names;
}

// The -1-terminated array of s32 at `at`, without the -1.
std::vector<int32_t> ReadIndexArray(Reader& in, size_t at) {
    std::vector<int32_t> values;
    for (; !in.Failed(); at += 4) {
        const int32_t value = in.S32(at);
        if (value < 0) {
            break;
        }
        values.push_back(value);
    }
    return values;
}

// The path waypoints: 16-byte records ended by a negative pad index.
std::vector<Waypoint> ReadWaypoints(Reader& in, size_t at) {
    std::vector<Waypoint> waypoints;
    for (; !in.Failed() && in.S32(at) >= 0; at += 16) {
        Waypoint waypoint;
        waypoint.padId = in.S32(at);
        waypoint.neighbours = ReadIndexArray(in, in.U32(at + 4));
        waypoint.groupNum = in.S32(at + 8);
        waypoint.dist = in.S32(at + 12);
        waypoints.push_back(std::move(waypoint));
    }
    return waypoints;
}

// The waypoint groups: 12-byte records ended by a null neighbour list.
std::vector<Waygroup> ReadWaygroups(Reader& in, size_t at) {
    std::vector<Waygroup> groups;
    for (; !in.Failed() && in.U32(at) != 0; at += 12) {
        Waygroup group;
        group.neighbours = ReadIndexArray(in, in.U32(at));
        group.waypoints = ReadIndexArray(in, in.U32(at + 4));
        group.dist = in.S32(at + 8);
        groups.push_back(std::move(group));
    }
    return groups;
}

// The patrol paths: 8-byte records ended by a null waypoint list. proplvreset2
// sets each path's length to its waypoint count.
std::vector<PatrolPath> ReadPatrolPaths(Reader& in, size_t at) {
    std::vector<PatrolPath> paths;
    for (; !in.Failed() && in.U32(at) != 0; at += 8) {
        PatrolPath path;
        path.waypoints = ReadIndexArray(in, in.U32(at));
        path.id = static_cast<uint8_t>(in.U32(at + 4) >> 24);
        path.isLoop = ((in.U32(at + 4) >> 16) & 0xFF) != 0;
        path.len = static_cast<uint16_t>(path.waypoints.size());
        paths.push_back(std::move(path));
    }
    return paths;
}

// The AI lists: {list offset, id} records ended by a null offset. A list runs
// to its end-of-list command.
std::vector<AiList> ReadAiLists(Reader& in, const std::vector<uint8_t>& file, size_t at) {
    std::vector<AiList> lists;
    for (; !in.Failed() && in.U32(at) != 0; at += 8) {
        AiList list;
        list.id = in.S32(at + 4);
        const size_t start = in.U32(at);
        if (start >= file.size()) {
            in.Fail();
            break;
        }
        size_t end = start;
        while (end < file.size() && file[end] != AI_EndList) {
            end += AiItemSize(file.data(), file.size(), end);
        }
        if (end >= file.size()) {
            in.Fail();
            break;
        }
        list.bytes.assign(file.begin() + static_cast<std::ptrdiff_t>(start),
                          file.begin() + static_cast<std::ptrdiff_t>(end) + 1);
        lists.push_back(std::move(list));
    }
    return lists;
}

// bondviewLoadSetupIntroSection's walk, which also converts the fixed-point
// fields the records store.
std::vector<IntroRecord> ReadIntro(Reader& in, size_t at) {
    std::vector<IntroRecord> records;
    while (!in.Failed()) {
        const auto type = static_cast<IntroType>(in.S32(at));
        if (type == IntroType::End) {
            break;
        }
        switch (type) {
            case IntroType::Spawn:
                records.push_back(IntroSpawn{ in.S32(at + 4), in.S32(at + 8) });
                at += 0x0C;
                break;
            case IntroType::Item:
                records.push_back(IntroItem{ in.S32(at + 4), in.S32(at + 8), in.S32(at + 12) });
                at += 0x10;
                break;
            case IntroType::Ammo:
                records.push_back(IntroAmmo{ in.S32(at + 4), in.S32(at + 8), in.S32(at + 12) });
                at += 0x10;
                break;
            case IntroType::Swirl:
                records.push_back(IntroSwirl{
                    in.S32(at + 4),
                    { static_cast<float>(in.S32(at + 8)) / kFixedPointOne,
                      static_cast<float>(in.S32(at + 12)) / kFixedPointOne,
                      static_cast<float>(in.S32(at + 16)) / kFixedPointOne },
                    static_cast<float>(in.S32(at + 20)) / kFixedPointOne,
                    static_cast<float>(in.S32(at + 24)) / kFixedPointOne,
                    in.S32(at + 28),
                });
                at += 0x20;
                break;
            case IntroType::Anim:
                records.push_back(IntroAnim{ in.S32(at + 4) });
                at += 0x08;
                break;
            case IntroType::Cuff:
                records.push_back(IntroCuff{ in.S32(at + 4) });
                at += 0x08;
                break;
            case IntroType::Camera:
                records.push_back(IntroCamera{
                    { static_cast<float>(in.S32(at + 4)) / 100.0f, static_cast<float>(in.S32(at + 8)) / 100.0f,
                      static_cast<float>(in.S32(at + 12)) / 100.0f },
                    static_cast<float>(in.S32(at + 16)) / kFixedPointOne,
                    static_cast<float>(in.S32(at + 20)) / kFixedPointOne,
                    in.S32(at + 24),
                    { in.U16(at + 28), in.U16(at + 30) },
                    in.S32(at + 32),
                });
                at += 0x28;
                break;
            case IntroType::Watch:
                records.push_back(IntroWatch{ in.S32(at + 4), in.S32(at + 8) });
                at += 0x0C;
                break;
            case IntroType::Credits:
                records.push_back(IntroCredits{ in.U32(at + 4) });
                at += 0x08;
                break;
            default:
                // An unknown type is skipped as a bare type word.
                at += 4;
                break;
        }
    }
    return records;
}

// The propdef section's records, up to the end record; the reader fails when
// the section runs off the end of the file.
std::vector<PropDef> ReadPropDefs(Reader& in, size_t at, float* doorScale) {
    std::vector<PropDef> records;
    while (!in.Failed()) {
        PropDef def;
        def.offset = static_cast<uint32_t>(at);
        def.type = static_cast<uint8_t>(in.U32(at) & 0xFF);
        if (def.type == static_cast<uint8_t>(PropDefType::End)) {
            break;
        }
        def.words = PropDefWords(def.type);
        if (IsObjectRecord(def.type)) {
            def.extraScale = in.U16(at);
            def.state = static_cast<uint8_t>((in.U32(at) >> 8) & 0xFF);
            def.model = static_cast<int16_t>(in.U16(at + 4));
            def.pad = static_cast<int16_t>(in.U16(at + 6));
            def.flags = in.U32(at + 8);
            def.flags2 = in.U32(at + 12);
        } else if (def.Is(PropDefType::DoorScale)) {
            *doorScale = static_cast<float>(in.S32(at + 4)) / kFixedPointOne;
        }
        if (def.Is(PropDefType::Guard)) {
            def.guardChrnum = in.U16(at + 0x4);
            def.guardPad = in.U16(at + 0x6);
            def.guardBody = in.U16(at + 0x8);
            def.guardAiList = in.U16(at + 0xA);
            def.guardPreset = in.U16(at + 0xC);
            def.guardChrPreset1 = in.U16(at + 0xE);
            def.guardHealth = in.U16(at + 0x10);
            def.guardReactionTime = in.U16(at + 0x12);
            def.guardFlags = in.U16(at + 0x14);
            def.guardHead = static_cast<int16_t>(in.U16(at + 0x16));
        } else if (def.Is(PropDefType::GuardAttribute)) {
            def.attributeChrnum = in.S32(at + 0x4);
            def.grenadeProb = static_cast<uint8_t>(in.U32(at + 0x8) & 0xFF);
        } else if (def.Is(PropDefType::Link) || def.Is(PropDefType::Switch) || def.Is(PropDefType::LockDoor)) {
            def.refs = { in.S32(at + 0x4), in.S32(at + 0x8), 0 };
        } else if (def.Is(PropDefType::SafeItem)) {
            def.refs = { in.S32(at + 0x4), in.S32(at + 0x8), in.S32(at + 0xC) };
        } else if (def.Is(PropDefType::Tag)) {
            def.tagId = in.U16(at + 0x4);
            def.tagOffset = static_cast<int16_t>(in.U16(at + 0x6));
        } else if (def.Is(PropDefType::Rename)) {
            def.renameOffset = in.S32(at + 0x4);
            def.renameWeapon = in.S32(at + 0x8);
            def.renameTitle1 = in.U32(at + 0xC);
            def.renameTitle2 = in.U32(at + 0x10);
            def.renameShortText = in.U32(at + 0x14);
            def.renameLongText = in.U32(at + 0x18);
            def.renamePickupText = in.U32(at + 0x1C);
        }
        if (def.Is(PropDefType::Door)) {
            // DoorRecord's fields past the ObjectRecord header (offset 0x80);
            // everything after doorOpenSound (0xa8 on) is runtime state
            // doorInit fills in, not stored in the file.
            def.doorLinkedOffset = in.S32(at + 0x80);
            def.doorMaxFrac = static_cast<float>(in.S32(at + 0x84)) / kFixedPointOne;
            def.doorPerimFrac = static_cast<float>(in.S32(at + 0x88)) / kFixedPointOne;
            // The EU build scales accel/decel/maxSpeed by 1.2; this port
            // targets the NTSC/US build, which does not.
            def.doorAccel = static_cast<float>(in.S32(at + 0x8C)) / kFixedPointOne;
            def.doorDecel = static_cast<float>(in.S32(at + 0x90)) / kFixedPointOne;
            def.doorMaxSpeed = static_cast<float>(in.S32(at + 0x94)) / kFixedPointOne;
            def.doorFlags = in.U16(at + 0x98);
            def.doorType = in.U16(at + 0x9A);
            def.doorKeyFlags = in.U32(at + 0x9C);
            def.doorAutoCloseFrames = in.U32(at + 0xA0);
            def.doorOpenSound = in.U32(at + 0xA4);
        } else if (def.Is(PropDefType::Collectable)) {
            // WeaponObjRecord: weaponnum (s8), LinkedWeaponType (s8), timer
            // (s16, runtime-only, not read here) packed into one word.
            const uint32_t word = in.U32(at + 0x80);
            def.weaponNum = static_cast<int8_t>(word >> 24);
            def.weaponLinkedType = static_cast<int8_t>(word >> 16);
        } else if (def.Is(PropDefType::Monitor)) {
            // MonitorObjRecord: MonitorRecord (0x80 to 0xF3), then the owner.
            def.monitorOwnerOffset = in.S32(at + 0xF4);
            def.monitorOwnerPart = in.S32(at + 0xF8);
        } else if (def.Is(PropDefType::Ammo)) {
            for (size_t i = 0; i < PropDef::kAmmoSlotCount; i++) {
                const uint32_t word = in.U32(at + 0x80 + (i * 4));
                def.ammoSlots[i] = { static_cast<uint16_t>(word >> 16), static_cast<uint16_t>(word) };
            }
        } else if (def.Is(PropDefType::Armour)) {
            // BodyArmourRecord::initialamount; ::amount is runtime state the
            // decomp derives from it at load, not stored here.
            def.armourAmount = static_cast<float>(in.S32(at + 0x80)) / kFixedPointOne;
        } else if (def.Is(PropDefType::Key)) {
            def.keyFlags = in.U32(at + 0x80);
        }
        at += size_t{ def.words } * 4;
        records.push_back(def);
    }
    return records;
}

} // namespace

bool IsObjectRecord(uint8_t type) {
    switch (static_cast<PropDefType>(type)) {
        case PropDefType::Door:
        case PropDefType::Prop:
        case PropDefType::Key:
        case PropDefType::Alarm:
        case PropDefType::Cctv:
        case PropDefType::Magazine:
        case PropDefType::Collectable:
        case PropDefType::Monitor:
        case PropDefType::MultiMonitor:
        case PropDefType::Rack:
        case PropDefType::Autogun:
        case PropDefType::Hat:
        case PropDefType::Ammo:
        case PropDefType::Armour:
        case PropDefType::GasReleasing:
        case PropDefType::Vehicle:
        case PropDefType::Aircraft:
        case PropDefType::Glass:
        case PropDefType::Safe:
        case PropDefType::Tank:
        case PropDefType::TintedGlass:
            return true;
        default:
            return false;
    }
}

uint32_t PropDefWords(uint8_t type) {
    switch (static_cast<PropDefType>(type)) {
        case PropDefType::Guard:
            return 7;
        case PropDefType::Door:
            return 64;
        case PropDefType::DoorScale:
            return 2;
        case PropDefType::Prop:
        case PropDefType::Glass:
        case PropDefType::Safe:
        case PropDefType::GasReleasing:
        case PropDefType::Alarm:
        case PropDefType::Rack:
        case PropDefType::Hat:
            return 32;
        case PropDefType::TintedGlass:
            return 37;
        case PropDefType::Key:
            return 33;
        case PropDefType::Cctv:
            return 0x3B;
        case PropDefType::Magazine:
            return 0x21;
        case PropDefType::Collectable:
            return 0x22;
        case PropDefType::Monitor:
            return 0x40;
        case PropDefType::MultiMonitor:
            return 0x95;
        case PropDefType::Autogun:
            return 0x36;
        case PropDefType::Link:
        case PropDefType::GuardAttribute:
            return 3;
        case PropDefType::Switch:
        case PropDefType::Tag:
        case PropDefType::ObjectiveStart:
        case PropDefType::ObjectivePhotograph:
        case PropDefType::ObjectiveEnterRoom:
        case PropDefType::WatchMenuObjectiveText:
        case PropDefType::LockDoor:
            return 4;
        case PropDefType::SafeItem:
        case PropDefType::ObjectiveDepositObjectInRoom:
            return 5;
        case PropDefType::Ammo:
            return 0x2D;
        case PropDefType::Armour:
            return 0x22;
        case PropDefType::Rename:
            return 10;
        case PropDefType::ObjectiveDestroyObject:
        case PropDefType::ObjectiveCompleteCondition:
        case PropDefType::ObjectiveFailCondition:
        case PropDefType::ObjectiveCollectObject:
        case PropDefType::ObjectiveDepositObject:
            return 2;
        case PropDefType::Vehicle:
            return 0x2C;
        case PropDefType::Aircraft:
            return 0x2D;
        case PropDefType::Tank:
            return 0x38;
        case PropDefType::CameraPos:
            return 7;
        default:
            // ObjectiveEnd, ObjectiveNull and ObjectiveCopyItem are a bare
            // header, as is any type the game does not size.
            return 1;
    }
}

std::optional<StageSetup> StageSetup::Parse(const std::vector<uint8_t>& file, float roomScale, std::string* error) {
    Reader in(file);
    StageSetup setup;
    for (size_t i = 0; i < kHeaderWords; i++) {
        setup.header_[i] = in.U32(i * 4);
    }
    const uint32_t padsAt = setup.header_[6];
    const uint32_t boundPadsAt = setup.header_[7];

    for (size_t at = padsAt; !in.Failed() && in.U32(at + 0x24) != 0; at += kPadSize) {
        setup.pads_.push_back(ReadPad(in, at, roomScale));
    }
    for (size_t at = boundPadsAt; !in.Failed() && in.U32(at + 0x24) != 0; at += kBoundPadSize) {
        BoundPad bound;
        bound.pad = ReadPad(in, at, roomScale);
        for (size_t i = 0; i < bound.box.size(); i++) {
            bound.box[i] = in.F32(at + 0x2C + i * 4) * roomScale;
        }
        setup.boundPads_.push_back(std::move(bound));
    }
    setup.padNames_ = ReadNames(in, setup.header_[8]);
    setup.boundPadNames_ = ReadNames(in, setup.header_[9]);
    setup.intro_ = ReadIntro(in, setup.header_[2]);
    if (setup.header_[0] != 0) {
        setup.waypoints_ = ReadWaypoints(in, setup.header_[0]);
    }
    if (setup.header_[1] != 0) {
        setup.waygroups_ = ReadWaygroups(in, setup.header_[1]);
    }
    if (setup.header_[4] != 0) {
        setup.patrolPaths_ = ReadPatrolPaths(in, setup.header_[4]);
    }
    if (setup.header_[5] != 0) {
        setup.aiLists_ = ReadAiLists(in, file, setup.header_[5]);
    }
    // An offset of 0 marks a file with no section; the header is there.
    if (setup.header_[3] != 0) {
        setup.propDefs_ = ReadPropDefs(in, setup.header_[3], &setup.doorScale_);
    }

    if (in.Failed()) {
        if (error != nullptr) {
            *error = "setup file is truncated";
        }
        return std::nullopt;
    }
    return setup;
}

int StageSetup::Count(PropDefType type) const {
    int count = 0;
    for (const PropDef& def : propDefs_) {
        count += def.Is(type) ? 1 : 0;
    }
    return count;
}

PropCounts StageSetup::Counts() const {
    PropCounts counts;
    counts.guards = Count(PropDefType::Guard);
    counts.links = Count(PropDefType::Link);
    counts.animatedObjects = counts.guards + Count(PropDefType::Aircraft);
    for (const PropDefType type :
         { PropDefType::Collectable,  PropDefType::Key,      PropDefType::Hat,   PropDefType::Door,
           PropDefType::Cctv,         PropDefType::Autogun,  PropDefType::Rack,  PropDefType::Monitor,
           PropDefType::MultiMonitor, PropDefType::Armour,   PropDefType::Prop,  PropDefType::Glass,
           PropDefType::TintedGlass,  PropDefType::Safe,     PropDefType::Unk41, PropDefType::GasReleasing,
           PropDefType::Alarm,        PropDefType::Magazine, PropDefType::Ammo,  PropDefType::Vehicle,
           PropDefType::Tank }) {
        counts.objects += Count(type);
    }
    return counts;
}

bool StageSetup::LoadsObject(const PropDef& def, int difficulty, int playerCount) {
    // proplvreset2's skip mask: bit 4 + difficulty, and for two or more
    // players bit 20 + the player count.
    uint32_t mask = uint32_t{ 1 } << (difficulty + 4);
    if (playerCount >= 2) {
        mask |= uint32_t{ 1 } << (playerCount + 20);
    }
    return (def.flags2 & mask) == 0;
}

StageSetup::StanResolution StageSetup::ResolveStanTiles(Stan& stan) {
    StanResolution counts;
    auto resolve = [&](Pad& pad) {
        switch (stan.ResolvePadTile(pad.position, pad.tileName, &pad.stanTile)) {
            case 1:
                counts.named++;
                break;
            case 2:
                counts.moved++;
                break;
            default:
                counts.missing++;
                break;
        }
    };
    for (Pad& pad : pads_) {
        resolve(pad);
    }
    for (BoundPad& bound : boundPads_) {
        resolve(bound.pad);
    }
    return counts;
}

} // namespace Frigate::Level
