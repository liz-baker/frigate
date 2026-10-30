#include "extract/ObjectTables.h"

#include "extract/Inflate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace Frigate::Extract {

namespace {

std::string Hex(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%x", value);
    return text;
}

std::string OrDash(const char* text) {
    return text != nullptr ? text : "-";
}

// A tab-separated line under construction.
class Line {
  public:
    Line& operator()(std::string_view field) {
        if (mStarted) {
            mText += '\t';
        }
        mStarted = true;
        mText += field;
        return *this;
    }
    Line& Num(double value) {
        return (*this)(FormatDouble(value));
    }
    Line& Int(int64_t value) {
        return (*this)(std::to_string(value));
    }
    void EndInto(std::string& out) {
        out += mText;
        out += '\n';
    }

  private:
    std::string mText;
    bool mStarted = false;
};

uint32_t ReadU32(std::span<const uint8_t> data, size_t at) {
    return (uint32_t(data[at]) << 24) | (uint32_t(data[at + 1]) << 16) | (uint32_t(data[at + 2]) << 8) | data[at + 3];
}

uint16_t ReadU16(std::span<const uint8_t> data, size_t at) {
    return uint16_t((data[at] << 8) | data[at + 1]);
}

Error Corrupt(const std::string& message) {
    return Error::Make(ErrorCode::Corrupt, message);
}

} // namespace

std::string FormatDouble(double value) {
    if (std::isnan(value)) {
        return "nan";
    }
    if (std::isinf(value)) {
        return value < 0 ? "-inf" : "inf";
    }
    if (value == 0) {
        return std::signbit(value) ? "-0.0" : "0.0";
    }
    // The shortest digit string that reads back exactly.
    char buffer[48];
    for (int precision = 1; precision <= 17; precision++) {
        std::snprintf(buffer, sizeof(buffer), "%.*e", precision - 1, value);
        if (std::strtod(buffer, nullptr) == value) {
            break;
        }
    }
    const char* text = buffer;
    std::string sign;
    if (*text == '-') {
        sign = "-";
        text++;
    }
    std::string digits;
    while (*text != 'e') {
        if (*text != '.') {
            digits += *text;
        }
        text++;
    }
    int exponent = std::atoi(text + 1);
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
    }
    int decimalPoint = exponent + 1;
    std::string out = sign;
    if (decimalPoint <= -4 || decimalPoint > 16) {
        out += digits[0];
        if (digits.size() > 1) {
            out += '.';
            out.append(digits, 1, std::string::npos);
        }
        char tail[16];
        std::snprintf(tail, sizeof(tail), "e%c%02d", exponent < 0 ? '-' : '+', std::abs(exponent));
        out += tail;
    } else if (decimalPoint <= 0) {
        out += "0.";
        out.append(size_t(-decimalPoint), '0');
        out += digits;
    } else if (size_t(decimalPoint) >= digits.size()) {
        out += digits;
        out.append(size_t(decimalPoint) - digits.size(), '0');
        out += ".0";
    } else {
        out.append(digits, 0, size_t(decimalPoint));
        out += '.';
        out.append(digits, size_t(decimalPoint), std::string::npos);
    }
    return out;
}

const ModelHeader* FindModelHeader(std::span<const ModelHeader> headers, std::string_view name) {
    auto it = std::lower_bound(headers.begin(), headers.end(), name,
                               [](const ModelHeader& header, std::string_view key) { return header.name < key; });
    if (it == headers.end() || name != it->name) {
        return nullptr;
    }
    return &*it;
}

Error BuildChrRecords(std::span<const ChrRecord> records, std::span<const ModelHeader> headers, std::string& out) {
    for (size_t index = 0; index < records.size(); index++) {
        const ChrRecord& record = records[index];
        std::string_view file = record.file;
        // A model header is named for its file without the directory letter and `Z`.
        const ModelHeader* header =
            file.size() >= 2 ? FindModelHeader(headers, file.substr(1, file.size() - 2)) : nullptr;
        if (header == nullptr) {
            return Corrupt(std::string(record.file) + ": no model header.");
        }
        Line line;
        line.Int(int64_t(index))(record.names)(record.file)
            .Int(record.isMale)
            .Int(record.hasHead)(record.scale)(record.pov)
            .Num(header->radius)
            .EndInto(out);
    }
    return {};
}

std::string BuildChrHats(std::span<const ChrHat> hats) {
    std::string out;
    for (size_t index = 0; index < hats.size(); index++) {
        Line line;
        line.Int(int64_t(index));
        for (const char* value : hats[index].values) {
            line(value);
        }
        line.EndInto(out);
    }
    return out;
}

namespace {

constexpr uint32_t kModelSegmentBase = 0x05000000;
constexpr size_t kNodeSize = 0x18;
constexpr size_t kTextureEntrySize = 12;
constexpr uint16_t kNodeBoundingBox = 0x0A;
// A node's data, next and child links, and the box that follows the bounding box record's model number.
constexpr size_t kNodeDataOffset = 0x04;
constexpr size_t kNodeNextOffset = 0x0C;
constexpr size_t kNodeChildOffset = 0x14;
constexpr size_t kBoundsOffset = 0x04;

// A segment-5 address as an offset into the image: nullopt for null, an error for one outside the image.
Error ResolveLink(std::span<const uint8_t> image, uint32_t address, const char* what, std::optional<size_t>& out) {
    out.reset();
    if (address == 0) {
        return {};
    }
    if ((address & 0xFF000000) != kModelSegmentBase || (address & 0x00FFFFFF) >= image.size()) {
        return Corrupt(std::string("A model's ") + what + " link is not an address inside the model.");
    }
    out = size_t(address & 0x00FFFFFF);
    return {};
}

} // namespace

Error FindModelBounds(std::span<const uint8_t> image, const ModelHeader& header, std::optional<ModelBounds>& bounds) {
    bounds.reset();
    size_t root = size_t(header.numSwitches) * 4 + size_t(header.numTextures) * kTextureEntrySize;
    if (root + kNodeSize > image.size()) {
        return Corrupt("A model's switch array and texture table run past the file.");
    }
    auto link = [&](size_t node, size_t field, const char* what, std::optional<size_t>& out) -> Error {
        if (node + kNodeSize > image.size()) {
            return Corrupt("A model node runs off the file.");
        }
        return ResolveLink(image, ReadU32(image, node + field), what, out);
    };

    std::optional<size_t> firstChild;
    if (Error err = link(root, kNodeChildOffset, "child", firstChild); !err.ok() || !firstChild) {
        return err;
    }
    std::vector<size_t> starts = { *firstChild };
    std::optional<size_t> grandChild;
    if (Error err = link(*firstChild, kNodeChildOffset, "child", grandChild); !err.ok()) {
        return err;
    }
    if (grandChild) {
        starts.push_back(*grandChild);
    }
    for (size_t start : starts) {
        std::optional<size_t> at = start;
        // A chain longer than the file has nodes is a cycle.
        for (size_t steps = 0; at && steps <= image.size() / kNodeSize; steps++) {
            if (*at + kNodeSize > image.size()) {
                return Corrupt("A model node runs off the file.");
            }
            if ((ReadU16(image, *at) & 0xFF) == kNodeBoundingBox) {
                std::optional<size_t> data;
                if (Error err = link(*at, kNodeDataOffset, "data", data); !err.ok()) {
                    return err;
                }
                if (!data || *data + kBoundsOffset + 24 > image.size()) {
                    return Corrupt("A model's bounding box runs off the file.");
                }
                ModelBounds values;
                for (size_t i = 0; i < 6; i++) {
                    uint32_t bits = ReadU32(image, *data + kBoundsOffset + 4 * i);
                    std::memcpy(&values[i], &bits, sizeof(float));
                }
                bounds = values;
                return {};
            }
            std::optional<size_t> next;
            if (Error err = link(*at, kNodeNextOffset, "next", next); !err.ok()) {
                return err;
            }
            at = next;
        }
    }
    return {};
}

Error BuildPropModels(std::span<const PropRecord> records, std::span<const ModelHeader> headers,
                      const BoundsReader& readBounds, std::string& out) {
    for (size_t index = 0; index < records.size(); index++) {
        const PropRecord& record = records[index];
        const ModelHeader* header = FindModelHeader(headers, record.model);
        if (header == nullptr) {
            return Corrupt(std::string(record.name) + ": no model header for " + record.model + ".");
        }
        std::optional<ModelBounds> bounds;
        if (Error err = readBounds(record, *header, bounds); !err.ok()) {
            return err;
        }
        // `model`, not the PROP_ name: the archived model's path is derived from this field.
        Line line;
        line("model").Int(int64_t(index))(record.model)(record.scale);
        for (size_t i = 0; i < 6; i++) {
            if (bounds) {
                line.Num((*bounds)[i]);
            } else {
                line("-");
            }
        }
        line(OrDash(header->skeleton)).Int(record.explosionType);
        for (int32_t seed : record.seeds) {
            line.Int(seed);
        }
        line.Num(header->radius).EndInto(out);
    }
    return {};
}

std::string BuildWeaponStats(const WeaponTables& tables) {
    std::string out;
    for (size_t index = 0; index < tables.itemCount; index++) {
        const WeaponItem& item = tables.items[index];
        const WeaponFiring& f = item.firing;
        Line line;
        line("item")
            .Int(int64_t(index))(item.name)
            .Int(item.hasHeader)
            .Int(item.ammoType)
            .Int(item.magazine)(Hex(item.flags))(OrDash(item.model))(OrDash(item.skeleton));
        for (double value : item.placement) {
            line.Num(value);
        }
        line.Int(f.automaticRate).Int(f.singleRate);
        for (int32_t byte : f.recoilSpeed) {
            line.Int(byte);
        }
        line.Num(f.recoilBack).Num(f.recoilUp)(OrDash(f.cartridge));
        for (double value : f.floats) {
            line.Num(value);
        }
        line.Int(f.soundTriggerRate).Int(f.sound).EndInto(out);
    }
    for (size_t index = 0; index < tables.watchCount; index++) {
        const WeaponWatch& w = tables.watch[index];
        Line line;
        line("watch").Int(int64_t(index))(Hex(w.upperText))(Hex(w.lowerText));
        for (double value : w.placement) {
            line.Num(value);
        }
        line(Hex(w.choiceText))(Hex(w.equipmentText));
        for (double value : w.equip) {
            line.Num(value);
        }
        line.EndInto(out);
    }
    for (size_t index = 0; index < tables.ammoCount; index++) {
        const WeaponAmmo& a = tables.ammo[index];
        Line line;
        line("ammo").Int(int64_t(index))(OrDash(a.name)).Int(a.limit)(Hex(a.icon)).Num(a.iconYOffset).EndInto(out);
    }
    return out;
}

ChrCombatTables GeneratedChrCombatTables() {
    return {
        { Detail::kStruckAnims, Detail::kStruckAnimCount },
        { Detail::kHitReactions, Detail::kHitReactionCount },
        { Detail::kFiringAnims, Detail::kFiringAnimCount },
        { Detail::kFiringGroups, Detail::kFiringGroupCount },
        { Detail::kExplosionDeaths, Detail::kExplosionDeathCount },
        { Detail::kExplosionGroups, Detail::kExplosionGroupCount },
        { Detail::kMultiFiring, Detail::kMultiFiringCount },
    };
}

std::string BuildChrCombat(const ChrCombatTables& tables, const WeaponTables& weapons) {
    std::string out;
    for (const StruckAnim& e : tables.struck) {
        Line line;
        line("struck")(e.list)
            .Int(e.index)(e.anim)
            .Int(e.flip)
            .Num(e.endFrame)
            .Num(e.speed)
            .Int(e.knockback)
            .Num(e.thudFrame1)
            .Num(e.thudFrame2)
            .EndInto(out);
    }
    for (size_t index = 0; index < tables.reactions.size(); index++) {
        const HitReaction& r = tables.reactions[index];
        Line line;
        line("reaction")
            .Int(int64_t(index))
            .Int(r.hitPart)
            .Int(r.impactPuffs)
            .Num(r.impactSize)
            .Int(r.backPuffs)
            .Num(r.backSize)(OrDash(r.deathList))(OrDash(r.flinchList))
            .EndInto(out);
    }
    for (const FiringAnim& e : tables.firing) {
        Line line;
        line("firing")(e.table).Int(e.index)(e.anim);
        for (double value : e.values) {
            line.Num(value);
        }
        line.EndInto(out);
    }
    for (const FiringGroup& g : tables.groups) {
        Line line;
        line("group")(g.set).Int(g.index)(g.table).EndInto(out);
    }
    for (size_t index = 0; index < tables.deaths.size(); index++) {
        const ExplosionDeath& d = tables.deaths[index];
        Line line;
        line("explosiondeath").Int(int64_t(index))(d.anim).Int(d.flip);
        for (double value : d.values) {
            line.Num(value);
        }
        line.EndInto(out);
    }
    for (size_t index = 0; index < tables.explosionGroups.size(); index++) {
        const ExplosionGroup& g = tables.explosionGroups[index];
        Line line;
        line("explosiongroup").Int(int64_t(index));
        for (uint32_t i = 0; i < g.count; i++) {
            line.Int(g.deaths[i]);
        }
        line.EndInto(out);
    }
    // Six stances to a group.
    for (size_t index = 0; index < tables.multiFiring.size(); index++) {
        const MultiFiring& m = tables.multiFiring[index];
        Line line;
        line("multifiring")
            .Int(int64_t(index / 6))
            .Int(int64_t(index % 6))(OrDash(m.table))
            .Int(m.index)(OrDash(m.anim))
            .Num(m.x)
            .Num(m.y)
            .Num(m.z)
            .EndInto(out);
    }
    for (size_t item = 0; item < weapons.itemCount; item++) {
        if (weapons.items[item].forceOfImpact != 0.0) {
            Line line;
            line("impact").Int(int64_t(item)).Num(weapons.items[item].forceOfImpact).EndInto(out);
        }
    }
    return out;
}

std::string BuildAnimationTable(std::span<const AnimationId> ids) {
    std::string out;
    for (const AnimationId& id : ids) {
        Line line;
        line.Int(id.table).Int(id.id)(OrDash(id.name)).EndInto(out);
    }
    return out;
}

std::string BuildMonitorLists(std::span<const MonitorList> lists, std::string_view defaultList,
                              std::span<const MonitorImage> images) {
    std::string out;
    for (const MonitorList& list : lists) {
        Line line;
        line("list")(list.name);
        for (const MonitorWord& word : list.words) {
            if (word.list != nullptr) {
                line(std::string("@") + word.list);
            } else {
                line.Int(word.value);
            }
        }
        line.EndInto(out);
    }
    Line fallback;
    fallback("default")(defaultList).EndInto(out);
    for (const MonitorImage& image : images) {
        Line line;
        line("image").Int(image.number)(image.list).EndInto(out);
    }
    return out;
}

std::string BuildRamromTable(std::span<const RamromDemo> demos) {
    std::string out;
    for (const RamromDemo& demo : demos) {
        Line line;
        line(demo.name).Int(demo.locked).EndInto(out);
    }
    return out;
}

Error BuildGlobalAiLists(std::span<const uint8_t> cdata, const RegionConstants& constants, std::string& out) {
    // The table has 18 entries and a terminator; a longer one means the address is wrong.
    constexpr size_t kMaxEntries = 64;
    auto at = [&](uint32_t address, size_t& offset) -> Error {
        if (address < constants.cdataLoadAddress || address - constants.cdataLoadAddress >= cdata.size()) {
            return Corrupt("A global AI list address is outside the data segment.");
        }
        offset = address - constants.cdataLoadAddress;
        return {};
    };

    struct Entry {
        uint32_t address;
        uint32_t id;
    };
    std::vector<Entry> entries;
    size_t cursor = 0;
    if (Error err = at(constants.aiListTableAddress, cursor); !err.ok()) {
        return err;
    }
    for (;;) {
        if (entries.size() >= kMaxEntries || cursor + 8 > cdata.size()) {
            return Corrupt("g_GlobalAILists is not where expected.");
        }
        uint32_t address = ReadU32(cdata, cursor);
        if (address == 0) {
            break;
        }
        entries.push_back({ address, ReadU32(cdata, cursor + 4) });
        cursor += 8;
    }
    if (entries.empty()) {
        return Corrupt("g_GlobalAILists is empty.");
    }
    std::vector<uint32_t> ids;
    uint32_t first = entries[0].address;
    for (const Entry& e : entries) {
        ids.push_back(e.id);
        first = std::min(first, e.address);
    }
    std::sort(ids.begin(), ids.end());
    for (size_t i = 0; i < ids.size(); i++) {
        if (ids[i] != i) {
            return Corrupt("g_GlobalAILists does not number its lists from 0.");
        }
    }
    if (first != constants.aiFirstListAddress) {
        return Corrupt("The first global AI list is not where expected.");
    }

    // A list runs to the start of the next one in memory (it may carry alignment padding after its end-of-list
    // command, which is never run); the last runs to the table.
    std::vector<uint32_t> bounds;
    for (const Entry& e : entries) {
        bounds.push_back(e.address);
    }
    bounds.push_back(constants.aiListTableAddress);
    std::sort(bounds.begin(), bounds.end());
    std::map<uint32_t, std::string> lists;
    for (const Entry& e : entries) {
        uint32_t following = *std::upper_bound(bounds.begin(), bounds.end(), e.address);
        size_t begin = 0;
        size_t end = 0;
        if (Error err = at(e.address, begin); !err.ok()) {
            return err;
        }
        if (Error err = at(following, end); !err.ok()) {
            return err;
        }
        std::string hex;
        for (size_t i = begin; i < end; i++) {
            char byte[3];
            std::snprintf(byte, sizeof(byte), "%02x", cdata[i]);
            hex += byte;
        }
        lists[e.id] = hex;
    }
    for (const auto& [id, hex] : lists) {
        out += std::to_string(id) + '\t' + hex + '\n';
    }
    return {};
}

namespace {

Error AddText(O2rWriter& writer, std::string_view path, const std::string& text) {
    return writer.AddResource(path, "OBLB", 0, 0,
                              std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

Error ReadNamedFile(StageContext& context, const std::string& name, std::vector<uint8_t>& out) {
    const FileEntry* entry = context.layout().FindFile(name);
    if (entry == nullptr) {
        return Corrupt(name + " is not in the file list.");
    }
    return context.ReadFile(*entry, out);
}

const Error kCancelled = Error::Make(ErrorCode::Cancelled, "Extraction was cancelled.");

} // namespace

const WeaponTables& GetWeaponTables(Region region) {
    switch (region) {
        case Region::Eur:
            return Detail::kWeaponTablesEur;
        case Region::Jp:
            return Detail::kWeaponTablesJp;
        case Region::Us:
            break;
    }
    return Detail::kWeaponTablesUs;
}

Error PackObjectTables(StageContext& context, O2rWriter& writer) {
    const RomLayout& layout = context.layout();
    if (!layout.constants.known) {
        return Error::Make(ErrorCode::RegionUnsupported, std::string("The ") + GetRegionInfo(context.region()).name +
                                                             " ROM's global AI lists are not mapped yet.");
    }
    std::span<const ModelHeader> headers(Detail::kModelHeaders, Detail::kModelHeaderCount);
    const WeaponTables& weapons = GetWeaponTables(context.region());
    std::string text;

    context.Report(0, 0, "animations/table");
    if (Error err = AddText(writer, "animations/table",
                            BuildAnimationTable({ Detail::kAnimationIds, Detail::kAnimationIdCount }));
        !err.ok()) {
        return err;
    }
    if (context.Cancelled()) {
        return kCancelled;
    }

    context.Report(0, 0, "chr/combat");
    if (Error err = AddText(writer, "chr/combat", BuildChrCombat(GeneratedChrCombatTables(), weapons)); !err.ok()) {
        return err;
    }

    context.Report(0, 0, "chr/records");
    if (Error err = BuildChrRecords({ Detail::kChrRecords, Detail::kChrRecordCount }, headers, text); !err.ok()) {
        return err;
    }
    if (Error err = AddText(writer, "chr/records", text); !err.ok()) {
        return err;
    }
    if (Error err = AddText(writer, "chr/hats", BuildChrHats({ Detail::kChrHats, Detail::kChrHatCount })); !err.ok()) {
        return err;
    }

    context.Report(0, 0, "monitor/lists");
    if (Error err =
            AddText(writer, "monitor/lists",
                    BuildMonitorLists({ Detail::kMonitorLists, Detail::kMonitorListCount }, Detail::kMonitorDefaultList,
                                      { Detail::kMonitorImages, Detail::kMonitorImageCount }));
        !err.ok()) {
        return err;
    }

    context.Report(0, 0, "ai/globals");
    {
        const RegionConstants& constants = layout.constants;
        std::span<const uint8_t> compressed;
        std::vector<uint8_t> cdata;
        if (Error err = context.RomRange(constants.cdataRomOffset, constants.cdataRomSize, compressed); !err.ok()) {
            return err;
        }
        if (Error err = Inflate1172(compressed, cdata); !err.ok()) {
            return err;
        }
        text.clear();
        if (Error err = BuildGlobalAiLists(cdata, constants, text); !err.ok()) {
            return err;
        }
        if (Error err = AddText(writer, "ai/globals", text); !err.ok()) {
            return err;
        }
    }
    if (context.Cancelled()) {
        return kCancelled;
    }

    context.Report(0, 0, "prop/models");
    BoundsReader readBounds = [&](const PropRecord& record, const ModelHeader& header,
                                  std::optional<ModelBounds>& bounds) -> Error {
        if (context.Cancelled()) {
            return kCancelled;
        }
        std::vector<uint8_t> image;
        if (Error err = ReadNamedFile(context, std::string("assets/obseg/prop/P") + record.model + "Z.bin", image);
            !err.ok()) {
            return err;
        }
        return FindModelBounds(image, header, bounds);
    };
    text.clear();
    if (Error err = BuildPropModels({ Detail::kPropRecords, Detail::kPropRecordCount }, headers, readBounds, text);
        !err.ok()) {
        return err;
    }
    if (Error err = AddText(writer, "prop/models", text); !err.ok()) {
        return err;
    }

    if (Error err = AddText(writer, "weapon/stats", BuildWeaponStats(weapons)); !err.ok()) {
        return err;
    }

    context.Report(0, 0, "ramrom");
    std::span<const RamromDemo> demos(Detail::kRamromDemos, Detail::kRamromDemoCount);
    if (Error err = AddText(writer, "ramrom/table", BuildRamromTable(demos)); !err.ok()) {
        return err;
    }
    for (const RamromDemo& demo : demos) {
        std::vector<uint8_t> recording;
        if (Error err = ReadNamedFile(context, std::string("assets/ramrom/ramrom_") + demo.name + ".bin", recording);
            !err.ok()) {
            return err;
        }
        if (Error err = writer.AddResource(std::string("ramrom/") + demo.name, "OBLB", 0, 0, recording); !err.ok()) {
            return err;
        }
    }
    return {};
}

void RegisterObjectTablePackers(Extractor& extractor) {
    extractor.RegisterStage(MakeStage("Object tables", ArchiveId::Objects, PackObjectTables));
}

} // namespace Frigate::Extract
