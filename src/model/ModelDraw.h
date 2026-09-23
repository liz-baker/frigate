#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gfx/FixedMtx.h"
#include "libultraship/libultra/gbi.h"
#include "model/ModelWalk.h"

namespace Ship {
class IResource;
class ResourceManager;
} // namespace Ship

namespace Frigate::Model {

// The reusable core of drawing a model's node tree as a display list, shared
// by src/viewer/ModelStub.cpp and src/game/'s real menu constructors -- both
// load a ModelTree resource, walk it, and emit the same segment-pointer/
// texture-toggle/per-list-call sequence around it; the only real difference
// between callers is which camera commands (viewport, modelview/projection
// load) precede that sequence, so those are supplied as `prefix` rather than
// built in here.
struct ModelDraw {
    // `prefix`, then the model's own draw commands -- no EndDisplayList
    // appended, since some callers (the game loop) append more after it.
    std::vector<Gfx> commands;

    // Segment 3 of the model's lists (kMatrixSegment below); `commands`
    // points a G_MOVEWORD at this vector's storage, so it must outlive
    // `commands`.
    std::vector<Mtx::FixedMtx> matrices;

    // Held so the resource cache can't drop a list (and its edits) between
    // loading it here and the interpreter resolving it by hash.
    std::vector<std::shared_ptr<Ship::IResource>> lists;
};

// Loads `modelPath`'s ModelTree resource, walks it with `base` as the root
// transform (baked into every node's matrix -- e.g. a camera's view matrix,
// or a fixed lookat for a static prop), and builds a ModelDraw appending
// after `prefix`. `textures` matches ModelStub::Create's flag: draw each
// list's own texture if it loads one, or force shading-only if false.
//
// `instanceOffset` mirrors setsuboffset(): it replaces the origin of a
// model whose root is a group node, in place of the origin stored in the file.
// Leave it empty to keep the stored origin.
//
// Returns std::nullopt (after logging why) if the model has no lists or any
// of them fails to load.
std::optional<ModelDraw> BuildModelDraw(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                        const std::string& modelPath, const Mat4& base, bool textures,
                                        const std::vector<Gfx>& prefix,
                                        const std::optional<std::array<float, 3>>& instanceOffset = std::nullopt);

} // namespace Frigate::Model
