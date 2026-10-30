#pragma once

#include "extract/Extractor.h"

namespace Frigate::Extract {

// The model, level geometry and level data packers (tools/o2r_packer/pack_model_resources.py, pack_geometry.py,
// pack_water_lists.py).

// Every chr, gun and prop model as `models/<category>/<name>/...`: a vertex block per display-list node, the lists
// and embedded images re-emitted as resources, the node tree (`tree`, docs/formats/goldeneye-model-tree-resource.md),
// the textured gunfire and shadow images (`fx/`) and the skeleton. Objects archive. One model is held in memory at a
// time.
Error PackModels(StageContext& context, O2rWriter& writer);

// Every bg file's rooms as `bg/<stage>/room<NN>/...` plus the `bg/<stage>/header` blob. Geometry archive. One room is
// held in memory at a time.
Error PackBgRooms(StageContext& context, O2rWriter& writer);

// The stan, text, setup and brief segments as `obseg/<category>/<name>`, and each font bank as `font/<bank>`.
// Geometry archive.
Error PackLevelData(StageContext& context, O2rWriter& writer);

// The two water display lists, `bg/water/<image>` (docs/formats/goldeneye-water-lists.md). Geometry archive.
Error PackWaterLists(StageContext& context, O2rWriter& writer);

// Registers the stages above with their archives.
void RegisterModelPackers(Extractor& extractor);

} // namespace Frigate::Extract
