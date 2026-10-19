// Unit tests for the objects archive's table builders (src/extract/ObjectTables.*), ported from the intent of
// tools/o2r_packer/test_pack_{chr_records,prop_models,weapon_stats,chr_combat,monitor_lists,animation_table,
// ramrom,ai_lists}.py. They run on synthetic tables and model images; the generated tables get consistency checks,
// and the ROM-gated check at the end needs the US ROM in FRIGATE_ROMS and skips otherwise.

#include "extract/DecompTables.h"
#include "extract/Extractor.h"
#include "extract/ObjectTables.h"
#include "extract/Region.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using namespace Frigate::Extract;
namespace fs = std::filesystem;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

template <typename T> void CheckEqual(const T& actual, const T& expected, const char* what) {
    if (!(actual == expected)) {
        std::fprintf(stderr, "FAIL: %s\n  got:      %s\n  expected: %s\n", what, std::string(actual).c_str(),
                     std::string(expected).c_str());
        gFailures++;
    }
}

using Bytes = std::vector<uint8_t>;

void TestFormatDouble() {
    // The cases Python's repr() distinguishes: shortest round trip, the ".0" suffix, and the exponent thresholds.
    CheckEqual(FormatDouble(0.1), std::string("0.1"), "0.1");
    CheckEqual(FormatDouble(1.0), std::string("1.0"), "1.0");
    CheckEqual(FormatDouble(-50.0), std::string("-50.0"), "-50.0");
    CheckEqual(FormatDouble(0.0), std::string("0.0"), "0.0");
    CheckEqual(FormatDouble(-0.0), std::string("-0.0"), "-0.0");
    CheckEqual(FormatDouble(0.80000001), std::string("0.80000001"), "0.80000001");
    CheckEqual(FormatDouble(double(3.4f)), std::string("3.4000000953674316"), "a single-precision value");
    CheckEqual(FormatDouble(0.0001), std::string("0.0001"), "0.0001 is still fixed notation");
    CheckEqual(FormatDouble(0.00001), std::string("1e-05"), "1e-05");
    CheckEqual(FormatDouble(1.5e-7), std::string("1.5e-07"), "1.5e-07");
    CheckEqual(FormatDouble(1e15), std::string("1000000000000000.0"), "1e15 is fixed");
    CheckEqual(FormatDouble(1e16), std::string("1e+16"), "1e+16");
    CheckEqual(FormatDouble(1.5e20), std::string("1.5e+20"), "1.5e+20");
    CheckEqual(FormatDouble(123456.789), std::string("123456.789"), "123456.789");
    CheckEqual(FormatDouble(29.999998), std::string("29.999998"), "29.999998");
}

void TestChrRecords() {
    const ModelHeader headers[] = {
        { "camguard", 0, 0, nullptr, 20.5 },
        { "guard", 0, 0, "standard_chr", 100.0 },
    };
    const ChrRecord records[] = {
        { "BODY_Guard,BODY_Alias", "CguardZ", 1, 1, "0.1", "0.5" },
        { "HEAD_Cam", "CcamguardZ", 0, 0, "1.0", "1.0" },
    };
    std::string out;
    Check(BuildChrRecords(records, headers, out).ok(), "records build");
    CheckEqual(out,
               std::string("0\tBODY_Guard,BODY_Alias\tCguardZ\t1\t1\t0.1\t0.5\t100.0\n"
                           "1\tHEAD_Cam\tCcamguardZ\t0\t0\t1.0\t1.0\t20.5\n"),
               "one line per slot, with the header's radius");

    const ChrRecord missing[] = { { "BODY_X", "CnoneZ", 1, 1, "1.0", "1.0" } };
    out.clear();
    Check(!BuildChrRecords(missing, headers, out).ok(), "a record without a model header is an error");

    const ChrHat hats[] = { { { "1.5", "-2", "3", "4", "5", "6e-1" } }, { { "0", "0", "0", "1", "1", "1" } } };
    CheckEqual(BuildChrHats(hats), std::string("0\t1.5\t-2\t3\t4\t5\t6e-1\n1\t0\t0\t0\t1\t1\t1\n"),
               "hats keep the text");
}

// A model image like test_pack_prop_models.py's: nodes of 0x18 bytes (type, pad, data, parent, next, prev, child).
void Be16(Bytes& b, uint32_t v) {
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v));
}
void Be32(Bytes& b, uint32_t v) {
    Be16(b, v >> 16);
    Be16(b, v);
}
constexpr uint32_t kSeg5 = 0x05000000;
void Node(Bytes& b, uint32_t type, uint32_t data = 0, uint32_t parent = 0, uint32_t next = 0, uint32_t prev = 0,
          uint32_t child = 0) {
    Be16(b, type);
    Be16(b, 0);
    Be32(b, data);
    Be32(b, parent);
    Be32(b, next);
    Be32(b, prev);
    Be32(b, child);
}
void BoxRecord(Bytes& b) {
    Be32(b, 0);
    for (float value : { -1.0f, 2.0f, -3.0f, 4.0f, -5.0f, 6.0f }) {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        Be32(b, bits);
    }
}

Bytes SiblingBox() {
    Bytes b;
    Node(b, 0x01, 0, 0, 0, 0, kSeg5 + 0x18);
    Node(b, 0x02, 0, kSeg5, kSeg5 + 0x30);
    Node(b, 0x0A, kSeg5 + 0x48, kSeg5, 0, kSeg5 + 0x18);
    BoxRecord(b);
    return b;
}
Bytes NestedBox() {
    Bytes b;
    Node(b, 0x01, 0, 0, 0, 0, kSeg5 + 0x18);
    Node(b, 0x02, 0, kSeg5, 0, 0, kSeg5 + 0x30);
    Node(b, 0x0A, kSeg5 + 0x48, kSeg5 + 0x18);
    BoxRecord(b);
    return b;
}
Bytes NoBox() {
    Bytes b;
    Node(b, 0x01, 0, 0, 0, 0, kSeg5 + 0x18);
    Node(b, 0x02, 0, kSeg5);
    return b;
}

void TestModelBounds() {
    const ModelHeader header = { "m", 0, 0, nullptr, 1.0 };
    std::optional<ModelBounds> bounds;
    Bytes sibling = SiblingBox();
    Check(FindModelBounds(sibling, header, bounds).ok() && bounds && (*bounds)[0] == -1.0f && (*bounds)[5] == 6.0f,
          "a box that is a sibling of the root's first child");
    Bytes nested = NestedBox();
    Check(FindModelBounds(nested, header, bounds).ok() && bounds && (*bounds)[1] == 2.0f,
          "a box that is a child of the root's first child");
    Bytes none = NoBox();
    Check(FindModelBounds(none, header, bounds).ok() && !bounds, "a model with no box has no bounds");

    // The switch array and texture table come before the root node.
    Bytes shifted(4 + 12, 0);
    Bytes shiftedNodes;
    Node(shiftedNodes, 0x01, 0, 0, 0, 0, kSeg5 + 16 + 0x18);
    Node(shiftedNodes, 0x0A, kSeg5 + 16 + 0x30);
    BoxRecord(shiftedNodes);
    shifted.insert(shifted.end(), shiftedNodes.begin(), shiftedNodes.end());
    const ModelHeader counted = { "m", 1, 1, nullptr, 1.0 };
    Check(FindModelBounds(shifted, counted, bounds).ok() && bounds && (*bounds)[4] == -5.0f,
          "the counts place the root node");

    Bytes truncated(sibling.begin(), sibling.begin() + 0x30);
    Check(!FindModelBounds(truncated, header, bounds).ok(), "a link off the end of the image is an error");
    Bytes badLink = NoBox();
    badLink[0x17] = 0x01;
    badLink[0x14] = 0x06;
    Check(!FindModelBounds(badLink, header, bounds).ok(), "a link outside segment 5 is an error");
    Bytes tiny(8, 0);
    Check(!FindModelBounds(tiny, header, bounds).ok(), "an image without room for its root is an error");
}

void TestPropModels() {
    const ModelHeader headers[] = {
        { "nested", 0, 0, "standard_object", 1.0 },
        { "nobox", 0, 0, nullptr, 2.5 },
    };
    const PropRecord records[] = {
        { "PROP_NESTED", "nested", "0.1", 2, { 0, 1, 2, 3, 4, 5 } },
        { "PROP_NOBOX", "nobox", "1.0", 0, { 0, 0, 0, 0, 0, 0 } },
    };
    Bytes nested = NestedBox();
    Bytes nobox = NoBox();
    auto reader = [&](const PropRecord& record, const ModelHeader& header, std::optional<ModelBounds>& bounds) {
        return FindModelBounds(std::string(record.model) == "nested" ? nested : nobox, header, bounds);
    };
    std::string out;
    Check(BuildPropModels(records, headers, reader, out).ok(), "props build");
    CheckEqual(out,
               std::string(
                   "model\t0\tnested\t0.1\t-1.0\t2.0\t-3.0\t4.0\t-5.0\t6.0\tstandard_object\t2\t0\t1\t2\t3\t4\t5\t1.0\n"
                   "model\t1\tnobox\t1.0\t-\t-\t-\t-\t-\t-\t-\t0\t0\t0\t0\t0\t0\t0\t2.5\n"),
               "a line per prop, dashes for a model with no box or skeleton");

    const PropRecord unknown[] = { { "PROP_X", "ghost", "1.0", 0, {} } };
    out.clear();
    Check(!BuildPropModels(unknown, headers, reader, out).ok(), "a prop without a model header is an error");
}

void TestWeaponStats() {
    WeaponItem item = {};
    item.name = "ITEM_X";
    item.hasHeader = 1;
    item.ammoType = 2;
    item.magazine = 30;
    item.flags = 0x1a0d91;
    item.model = "models/gun/GxZ";
    item.skeleton = "standard_gun";
    for (int i = 0; i < 11; i++) {
        item.placement[i] = i + 0.5;
    }
    item.firing = { -1, 6, { 0, 1, -2, 3 }, 0.25, 0.5, "models/gun/GcartZ", { 1, 2, 3, 4, 5, 6, 7, 8, 9 }, 12, 345 };
    WeaponItem bare = {};
    bare.name = "ITEM_Y";
    const WeaponWatch watch[] = { { 0x1234, 0, { 1.5, 2, 3, 4, 5 }, 0xabc, 0, { 7, 8, 9 } } };
    const WeaponAmmo ammo[] = { { "AMMO_BULLET", 800, 0x1c4, 0.5 }, { nullptr, 0, 0, 0 } };
    WeaponItem items[] = { item, bare };
    WeaponTables tables = { items, 2, watch, 1, ammo, 2 };

    CheckEqual(
        BuildWeaponStats(tables),
        std::string(
            "item\t0\tITEM_X\t1\t2\t30\t0x1a0d91\tmodels/gun/GxZ\tstandard_gun\t0.5\t1.5\t2.5\t3.5\t4.5\t5.5\t6.5\t7.5"
            "\t8.5\t9.5\t10.5\t-1\t6\t0\t1\t-2\t3\t0.25\t0.5\tmodels/gun/GcartZ\t1.0\t2.0\t3.0\t4.0\t5.0"
            "\t6.0\t7.0\t8.0\t9.0\t12\t345\n"
            "item\t1\tITEM_Y\t0\t0\t0\t0x0\t-\t-\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0\t0"
            "\t0\t0\t0\t0\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0\t0\n"
            "watch\t0\t0x1234\t0x0\t1.5\t2.0\t3.0\t4.0\t5.0\t0xabc\t0x0\t7.0\t8.0\t9.0\n"
            "ammo\t0\tAMMO_BULLET\t800\t0x1c4\t0.5\n"
            "ammo\t1\t-\t0\t0x0\t0.0\n"),
        "item, watch and ammo lines");
}

void TestChrCombat() {
    const StruckAnim struck[] = { { "death", 0, "fall", 1, 0.5, 2.0, 3, 4.0, 5.0 } };
    const HitReaction reactions[] = { { 7, 2, 0.5, 1, 1.5, "death", nullptr } };
    FiringAnim firing = { "stand", 0, "shoot", {} };
    firing.values[0] = 1.25;
    const FiringAnim firings[] = { firing };
    const FiringGroup groups[] = { { "rifle", 0, "stand" } };
    const ExplosionDeath deaths[] = { { "boom", 1, { 1, 2, 3, 4, 5 } } };
    ExplosionGroup group = { 2, { 0, 3 } };
    const ExplosionGroup explosionGroups[] = { group, { 0, {} } };
    // Six stances to a group; a row names a firing table entry or a bare animation.
    MultiFiring bare = { nullptr, 0, "walk", 1, 2, 3 };
    MultiFiring entry = { "stand", 0, nullptr, 4, 5, 6 };
    const MultiFiring multi[] = { entry, bare, entry, bare, entry, bare, bare };
    WeaponItem items[2] = {};
    items[1].forceOfImpact = 29.999998;
    WeaponTables weapons = { items, 2, nullptr, 0, nullptr, 0 };

    ChrCombatTables tables = { struck, reactions, firings, groups, deaths, explosionGroups, multi };
    std::string out = BuildChrCombat(tables, weapons);
    std::string expected = "struck\tdeath\t0\tfall\t1\t0.5\t2.0\t3\t4.0\t5.0\n"
                           "reaction\t0\t7\t2\t0.5\t1\t1.5\tdeath\t-\n"
                           "firing\tstand\t0\tshoot\t1.25\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0.0\t0."
                           "0\t0.0\t0.0\t0.0\t0.0\n"
                           "group\trifle\t0\tstand\n"
                           "explosiondeath\t0\tboom\t1\t1.0\t2.0\t3.0\t4.0\t5.0\n"
                           "explosiongroup\t0\t0\t3\n"
                           "explosiongroup\t1\n"
                           "multifiring\t0\t0\tstand\t0\t-\t4.0\t5.0\t6.0\n"
                           "multifiring\t0\t1\t-\t0\twalk\t1.0\t2.0\t3.0\n"
                           "multifiring\t0\t2\tstand\t0\t-\t4.0\t5.0\t6.0\n"
                           "multifiring\t0\t3\t-\t0\twalk\t1.0\t2.0\t3.0\n"
                           "multifiring\t0\t4\tstand\t0\t-\t4.0\t5.0\t6.0\n"
                           "multifiring\t0\t5\t-\t0\twalk\t1.0\t2.0\t3.0\n"
                           "multifiring\t1\t0\t-\t0\twalk\t1.0\t2.0\t3.0\n"
                           "impact\t1\t29.999998\n";
    CheckEqual(out, expected, "chr combat lines, the impact lines last");
}

void TestSmallTables() {
    const AnimationId ids[] = { { 1, 0, "idle" }, { 1, 1, nullptr }, { 2, 0, "helicopter_cradle" } };
    CheckEqual(BuildAnimationTable(ids), std::string("1\t0\tidle\n1\t1\t-\n2\t0\thelicopter_cradle\n"),
               "the animation table marks ids with no clip");

    const MonitorWord loop[] = { Num(7), Ref("monB"), Num(4294967295u) };
    const MonitorWord empty[] = { Num(0) };
    const MonitorList lists[] = { { "monA", loop }, { "monB", { empty, 0 } } };
    const MonitorImage images[] = { { 3, "monB" } };
    CheckEqual(BuildMonitorLists(lists, "monA", images),
               std::string("list\tmonA\t7\t@monB\t4294967295\nlist\tmonB\ndefault\tmonA\nimage\t3\tmonB\n"),
               "monitor lists reference each other by name");

    const RamromDemo demos[] = { { "Dam_1", 0 }, { "Train", 2 } };
    CheckEqual(BuildRamromTable(demos), std::string("Dam_1\t0\nTrain\t2\n"), "the demo table");
}

void PutU32(Bytes& b, size_t at, uint32_t v) {
    b[at] = uint8_t(v >> 24);
    b[at + 1] = uint8_t(v >> 16);
    b[at + 2] = uint8_t(v >> 8);
    b[at + 3] = uint8_t(v);
}

void TestGlobalAiLists() {
    RegionConstants constants = {};
    constants.cdataLoadAddress = 0x80020000;
    constants.aiFirstListAddress = 0x80020010;
    constants.aiListTableAddress = 0x80020030;
    // Two lists from the first address to the table, then the table: {address, id} pairs and a null.
    Bytes cdata(0x30 + 8 * 3, 0);
    cdata[0x10] = 0x02;
    cdata[0x11] = 0x04;
    cdata[0x20] = 0x03;
    cdata[0x2f] = 0x7e;
    PutU32(cdata, 0x30, 0x80020020);
    PutU32(cdata, 0x34, 0); // the list at 0x20 is id 0
    PutU32(cdata, 0x38, 0x80020010);
    PutU32(cdata, 0x3c, 1);

    std::string out;
    Check(BuildGlobalAiLists(cdata, constants, out).ok(), "the lists are read");
    // Each list runs to the next in memory (padding included); the last runs to the table.
    CheckEqual(out, std::string("0\t03") + std::string(14 * 2, '0') + "7e\n1\t0204" + std::string(14 * 2, '0') + "\n",
               "the lists, by id");

    Bytes wrongFirst = cdata;
    PutU32(wrongFirst, 0x38, 0x80020012);
    out.clear();
    Check(!BuildGlobalAiLists(wrongFirst, constants, out).ok(), "a first list that is not where expected is an error");
    Bytes gap = cdata;
    PutU32(gap, 0x3c, 3);
    Check(!BuildGlobalAiLists(gap, constants, out).ok(), "ids that do not run from 0 are an error");
    Bytes noTable(0x30 + 8, 0);
    Check(!BuildGlobalAiLists(noTable, constants, out).ok(), "a table that is empty is an error");
}

// The generated tables agree with each other, as the packers need them to.
void TestGeneratedTables() {
    std::span<const ModelHeader> headers(Detail::kModelHeaders, Detail::kModelHeaderCount);
    bool sorted = true;
    for (size_t i = 1; i < headers.size(); i++) {
        sorted = sorted && std::strcmp(headers[i - 1].name, headers[i].name) < 0;
    }
    Check(!headers.empty() && sorted, "model headers are sorted by name and unique");
    Check(FindModelHeader(headers, "camguard") != nullptr, "a known header is found");
    Check(FindModelHeader(headers, "no such model") == nullptr, "an unknown header is not");

    std::string out;
    Check(BuildChrRecords({ Detail::kChrRecords, Detail::kChrRecordCount }, headers, out).ok(),
          "every chr record has a model header");
    Check(Detail::kChrHatCount % 6 == 0 && Detail::kChrHatCount != 0, "hats come six to a head");
    for (size_t i = 0; i < Detail::kPropRecordCount; i++) {
        Check(FindModelHeader(headers, Detail::kPropRecords[i].model) != nullptr, "every prop has a model header");
    }
    for (Region region : { Region::Us, Region::Eur, Region::Jp }) {
        const WeaponTables& tables = GetWeaponTables(region);
        Check(tables.itemCount != 0 && tables.itemCount == tables.watchCount, "a watch row per item");
        Check(tables.ammoCount != 0, "ammo rows");
    }
    Check(GetWeaponTables(Region::Us).itemCount == GetWeaponTables(Region::Eur).itemCount,
          "the regions have the same items");
    // The releases differ in their scale constants only.
    bool differs = false;
    for (size_t i = 0; i < GetWeaponTables(Region::Us).itemCount; i++) {
        differs = differs || GetWeaponTables(Region::Us).items[i].placement[7] !=
                                 GetWeaponTables(Region::Eur).items[i].placement[7];
    }
    Check(differs, "the European weapon table has its own scale constants");
}

std::optional<fs::path> FindUsRom() {
    const char* env = std::getenv("FRIGATE_ROMS");
    if (env == nullptr) {
        return std::nullopt;
    }
    std::string list = env;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(':', start);
        if (end == std::string::npos) {
            end = list.size();
        }
        fs::path location = list.substr(start, end - start);
        start = end + 1;
        std::error_code ec;
        if (fs::is_regular_file(location, ec) && IsUsRom(location)) {
            return location;
        }
        if (fs::is_directory(location, ec)) {
            for (const auto& entry : fs::directory_iterator(location, ec)) {
                if (entry.is_regular_file() && IsUsRom(entry.path())) {
                    return entry.path();
                }
            }
        }
    }
    return std::nullopt;
}

void TestRom() {
    std::optional<fs::path> path = FindUsRom();
    if (!path) {
        std::printf("skip: no US ROM (set FRIGATE_ROMS)\n");
        return;
    }
    std::ifstream file(*path, std::ios::binary);
    Bytes rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    StageContext context(Region::Us, rom, nullptr, nullptr);

    fs::path dir = fs::temp_directory_path() / "frigate_object_tables_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    O2rWriter writer;
    Check(writer.Open(dir / "objects.o2r").ok(), "archive opens");
    Error err = PackObjectTables(context, writer);
    Check(err.ok(), err.message.c_str());
    // chr/{records,hats,combat}, animations/table, monitor/lists, ai/globals, prop/models, weapon/stats,
    // ramrom/table and the 14 demos.
    Check(writer.MemberCount() == 9 + 14, "the stage writes every table and demo");
    Check(writer.Close().ok(), "archive closes");
    fs::remove_all(dir);

    // The other regions' ROM-side constants are not mapped: the stage says so instead of writing half a set.
    StageContext europe(Region::Eur, rom, nullptr, nullptr);
    O2rWriter unused;
    Check(PackObjectTables(europe, unused).code == ErrorCode::RegionUnsupported,
          "the European ROM is reported as unsupported");
}

} // namespace

int main() {
    TestFormatDouble();
    TestChrRecords();
    TestModelBounds();
    TestPropModels();
    TestWeaponStats();
    TestChrCombat();
    TestSmallTables();
    TestGlobalAiLists();
    TestGeneratedTables();
    TestRom();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
