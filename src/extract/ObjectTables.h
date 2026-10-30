#pragma once

#include "extract/DecompTables.h"
#include "extract/Error.h"
#include "extract/Extractor.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Frigate::Extract {

// The objects archive's table resources: the chr, prop, weapon, animation, monitor, AI and attract-demo tables. Most
// are text built from the generated decomp tables (DecompTables.h); the prop model bounds, the global AI lists and
// the demo recordings come from the ROM. Formats: docs/formats/goldeneye-{chr-records,prop-models,weapon-stats,
// chr-combat,animation-table,monitor-lists,ai-lists,ramrom}.md.

// A double the way Python's repr() writes it (the shortest text that reads back as the same value).
std::string FormatDouble(double value);

// The `MODELFILEHEADER` named `name`, or nullptr.
const ModelHeader* FindModelHeader(std::span<const ModelHeader> headers, std::string_view name);

// `chr/records`: one line per slot, `<index>\t<identifiers>\t<file>\t<isMale>\t<hasHead>\t<scale>\t<pov>\t<radius>`.
Error BuildChrRecords(std::span<const ChrRecord> records, std::span<const ModelHeader> headers, std::string& out);
// `chr/hats`: one line per hat placement, `<index>` and its six values.
std::string BuildChrHats(std::span<const ChrHat> hats);

// The six bounds (xmin, xmax, ymin, ymax, zmin, zmax) of a model's first bounding box node, as chrobjGetBboxFromObjFile
// finds it; `bounds` is left empty for a model with none. `image` is the inflated model file, `header` supplies the
// switch and texture counts that place its root node.
using ModelBounds = std::array<float, 6>;
Error FindModelBounds(std::span<const uint8_t> image, const ModelHeader& header, std::optional<ModelBounds>& bounds);

// Supplies the bounds of a prop's model file (`P<model>Z`).
using BoundsReader = std::function<Error(const PropRecord&, const ModelHeader&, std::optional<ModelBounds>&)>;
// `prop/models`: one `model` line per PROP value.
Error BuildPropModels(std::span<const PropRecord> records, std::span<const ModelHeader> headers,
                      const BoundsReader& readBounds, std::string& out);

// `weapon/stats`: an `item` and a `watch` line per item, an `ammo` line per ammo type.
std::string BuildWeaponStats(const WeaponTables& tables);

// The decomp's chr.c and bondview.c tables behind `chr/combat`; Detail::* holds the generated ones.
struct ChrCombatTables {
    std::span<const StruckAnim> struck;
    std::span<const HitReaction> reactions;
    std::span<const FiringAnim> firing;
    std::span<const FiringGroup> groups;
    std::span<const ExplosionDeath> deaths;
    std::span<const ExplosionGroup> explosionGroups;
    std::span<const MultiFiring> multiFiring;
};
ChrCombatTables GeneratedChrCombatTables();
// `chr/combat`; `weapons` supplies the forces of impact (`impact` lines for the nonzero ones).
std::string BuildChrCombat(const ChrCombatTables& tables, const WeaponTables& weapons);

// `animations/table`: `<table>\t<id>\t<clip or ->` per id.
std::string BuildAnimationTable(std::span<const AnimationId> ids);

// `monitor/lists`: a `list` line per command list, then the `default` list and an `image` line per case.
std::string BuildMonitorLists(std::span<const MonitorList> lists, std::string_view defaultList,
                              std::span<const MonitorImage> images);

// `ramrom/table`: `<name>\t<locked>` per demo.
std::string BuildRamromTable(std::span<const RamromDemo> demos);

// `ai/globals`: one `<id>\t<hex bytes>` line per list of g_GlobalAILists, read from the inflated data segment
// `cdata`. `constants` supplies the table's address, the first list's, and the address the segment is loaded at.
Error BuildGlobalAiLists(std::span<const uint8_t> cdata, const RegionConstants& constants, std::string& out);

// The objects archive's table stage: everything above, from the ROM and the generated tables. The US ROM only; the
// other regions' ROM-side constants are not mapped.
Error PackObjectTables(StageContext& context, O2rWriter& writer);

void RegisterObjectTablePackers(Extractor& extractor);

} // namespace Frigate::Extract
