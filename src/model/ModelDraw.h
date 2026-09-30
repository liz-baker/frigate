#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gfx/FixedMtx.h"
#include "libultraship/libultra/gbi.h"
#include "model/JointDraw.h"
#include "model/ModelWalk.h"
#include "model/RenderState.h"
#include "resource/type/ModelTree.h"

namespace Ship {
class IResource;
class ResourceManager;
} // namespace Ship

namespace Fast {
class DisplayList;
} // namespace Fast

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

    // The vertices some nodes draw with in place of the model's own
    // (ModelDrawOverrides::vertices); `commands` points G_VTX at them.
    std::vector<NodeVertices> vertices;

    // The tree the model was walked from and the walk's result.
    std::shared_ptr<ModelTree> tree;
    ModelWalk walk;

    // modelFindNodeMtx for the node in slot `slot` of the model's switch array;
    // null for an empty slot.
    const Mat4* SwitchMatrix(size_t slot) const;
};

// What a draw changes of a model that is not what its file says.
struct ModelDrawOverrides {
    // The node matrices, indexed by the matrix numbers the model's lists name,
    // in place of the walk building them (as the first-person guns do); a
    // node that owns none takes its nearest owning ancestor's. Called with the
    // model's tree.
    std::function<std::vector<Mat4>(const ModelTree&)> matrices;
    // Vertices to draw nodes with instead of their own.
    std::vector<NodeVertices> vertices;
};

// A G_DL call to the display-list resource at `path` (hashed, as the packer's
// own list-to-list calls are), plus the loaded list so the caller can keep it
// alive until the interpreter has resolved the call. Returns std::nullopt
// (after logging why) if the list doesn't load.
struct ListCall {
    std::array<Gfx, 2> commands;
    std::shared_ptr<Ship::IResource> list;
};
std::optional<ListCall> CallList(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                 const std::string& path);

// True if the list loads a texture before anything in it draws while sampling
// one. A draw that samples with nothing loaded crashes the backend, so a list
// this returns false for has to be drawn shade-only.
bool ListLoadsTextureBeforeDrawing(const Fast::DisplayList& list);

// Rewrites a list's own combiner and texture-enable commands to shade-only.
// This edits the cached resource, so it holds for every later draw of the list.
void DisableListSampling(Fast::DisplayList& list);

// A combiner that draws shade only.
Gfx ShadeOnlyCombiner();

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
// With `render`, each node draws under its own render state (subdraw), the
// way the first-person guns do, in place of the fixed shade-only setup above;
// `textures` then has no effect.
//
// `overrides` changes what the file gives: its matrices, and the vertices of
// some nodes (taken from it, to live as long as the draw).
//
// Returns std::nullopt (after logging why) if the model has no lists or any
// of them fails to load.
std::optional<ModelDraw> BuildModelDraw(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                        const std::string& modelPath, const Mat4& base, bool textures,
                                        const std::vector<Gfx>& prefix,
                                        const std::optional<std::array<float, 3>>& instanceOffset = std::nullopt,
                                        bool hideSwitches = false, const ModelRenderParams* render = nullptr,
                                        ModelDrawOverrides* overrides = nullptr);

} // namespace Frigate::Model
