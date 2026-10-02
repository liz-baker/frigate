#pragma once

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "gfx/FixedMtx.h"
#include "model/JointDrawList.h"
#include "model/ModelInstance.h"
#include "model/RenderState.h"

namespace Ship {
class IResource;
class ResourceManager;
} // namespace Ship

namespace Frigate::Model {

// The vertices a draw gives one node in place of the ones its lists load, for
// a model whose mesh changes live (a door resized as it slides).
struct NodeVertices {
    // ModelNode::offset.
    uint32_t node = 0;
    std::vector<Vtx_t> vertices;
};

// The commands that draw posed models the way the decomp's drawjointlist
// does: joint by joint from the farthest back to the nearest (model/
// JointDrawList.h), twice over -- first each node's primary list, then the
// secondary lists that draw after the opaque surfaces -- with each node's
// render state (model/RenderState.h) set ahead of its lists.
struct JointDraw {
    std::vector<Gfx> commands;

    // Segment 3 of each model's lists, in the order the models were given;
    // `commands` points G_MOVEWORDs at these vectors' storage, so they must
    // outlive `commands`.
    std::vector<std::vector<Mtx::FixedMtx>> matrices;

    // Held so the resource cache can't drop a list (and its edits) between
    // loading it here and the interpreter resolving it by hash.
    std::vector<std::shared_ptr<Ship::IResource>> lists;

    // The quads of gunfire and shadow nodes, built for this draw; `commands`
    // points G_VTX at them, so they must outlive `commands`. A deque keeps
    // each where it was put.
    std::deque<std::array<Vtx_t, 4>> vertices;

    // The vertices of star gunfire nodes, one node's to a vector, which the
    // lists of their nodes load in place of the model's own; held like
    // `vertices`.
    std::deque<std::vector<NodeVertices>> starVertices;

    // The vertices of nodes a model paints, one node's to a vector; held like
    // `starVertices`.
    std::deque<std::vector<NodeVertices>> paintedVertices;
};

// The passes' flags in the decomp's ModelRenderData: primary lists, then
// secondary lists.
constexpr uint32_t kPrimaryPass = 1;
constexpr uint32_t kSecondaryPass = 2;

// One node of a model for one pass (modelRenderNodeDl and
// modelRenderNodeGundl): the node's render state, then its lists, appended to
// `commands`; the lists are added to `lists` to keep them alive. A node that is
// not a display-list node draws nothing. Returns false (after logging why) if a
// list doesn't load. `modelPath` is the model's resource base path. A node
// named in `replaced` draws with those vertices, and must not be dropped while
// `commands` may still run.
bool DrawDisplayListNode(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const ModelRenderParams& render,
                         uint32_t pass, const std::string& modelPath, const ModelNode& node, std::vector<Gfx>& commands,
                         std::vector<std::shared_ptr<Ship::IResource>>& lists,
                         const std::vector<NodeVertices>* replaced = nullptr);

// The model as the joint draw list reads it: its nodes and walk, the head's
// nodes for each attachment the walk made, and how far its root is above the
// ground (the shadow's depth).
Frigate::JointModel MakeJointModel(const ModelInstance& model);

// Draws `models`, each already posed (ModelInstance::Pose), together so their
// joints sort into one order. Returns std::nullopt (after logging why) if a
// list doesn't load.
std::optional<JointDraw> BuildJointDraw(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                        const std::vector<std::shared_ptr<ModelInstance>>& models,
                                        const ModelRenderParams& render);

} // namespace Frigate::Model
