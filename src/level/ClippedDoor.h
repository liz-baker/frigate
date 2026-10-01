#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "level/PropModels.h"

namespace Frigate::Level {

// One model vertex (docs/formats/goldeneye-vertex.md), laid out as the N64 and
// libultraship's Vtx_t do.
struct ModelVertex {
    int16_t position[3];
    uint16_t flag;
    int16_t s;
    int16_t t;
    uint8_t colour[4];
};
static_assert(sizeof(ModelVertex) == 16, "a model vertex is 16 bytes");

// The plane doorBuildClippedVertices cuts a sliding door's mesh at: the top of
// the door's box for a door that rises (`vertical`), else the low end of its
// box along its length (both truncated to a whole model unit, plus a half).
int16_t DoorClipCutoff(const ModelBounds& bbox, bool vertical);

// doorBuildClippedVertices: `source` (the model's vertices for the door's
// collision node, in groups of four that make a quad) with every vertex
// beyond `cutoff` pulled back onto it and its texture coordinates moved along
// the edge to match, so the door resizes instead of poking through the wall
// it slides into.
std::vector<ModelVertex> ClipDoorVertices(const ModelVertex* source, size_t count, bool vertical, int16_t cutoff);

} // namespace Frigate::Level
