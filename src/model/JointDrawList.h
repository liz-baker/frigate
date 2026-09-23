#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "model/ModelNode.h"
#include "model/ModelWalk.h"

// The order the game draws a posed model in. It draws joint by joint (each
// joint's subtree in one go) from the farthest back to the nearest, because
// some scenes run with the z-buffer off and rely on that order. These are the
// decomp's ModelHitEntry list functions: sub_GAME_7F06B120 builds the entries,
// sub_GAME_7F06B29C gives each a depth, sub_GAME_7F06BB28 sorts them.
namespace Frigate {

// One model taking part in a draw.
struct JointModel {
    const std::vector<ModelNode>* nodes = nullptr;
    // The walk that posed it: its matrices and the nodes it visited.
    const ModelWalk* walk = nullptr;
    // A head attachment's node array, indexed as VisitedNode::attachment.
    std::vector<const std::vector<ModelNode>*> attachmentNodes;
    // How far the root is above the ground (the header's ground minus its
    // height), which a shadow's depth is taken at.
    float shadowHeight = 0.0f;
};

struct JointEntry {
    size_t model = 0; // index into the JointModel array
    size_t visit = 0; // index into that model's walk->visited of the entry's root node
    float sortValue = 0.0f;
};

// Whether a node starts a joint's draw entry. Only the node types some ROM
// model contains are covered (the packer rejects the rest).
bool StartsJointEntry(uint8_t nodeType);

// One entry per visited joint node of each model, models in order.
std::vector<JointEntry> BuildJointEntries(const std::vector<JointModel>& models);

// Sets each entry's depth (larger is farther), from its matrix and its node's
// sort data.
void ComputeJointSortValues(std::vector<JointEntry>& entries, const std::vector<JointModel>& models);

// Orders entries farthest first; equal depths keep their order.
void SortJointEntries(std::vector<JointEntry>& entries);

std::vector<JointEntry> BuildSortedJointEntries(const std::vector<JointModel>& models);

// The visited nodes drawn for one entry after its own root: everything below
// it, except the joint nodes that draw as entries of their own, and what is
// below those. Indices into the model's walk->visited.
std::vector<size_t> JointEntryNodes(const JointModel& model, size_t visit);

// A visited node's record.
const ModelNode& VisitedModelNode(const JointModel& model, const VisitedNode& visited);

} // namespace Frigate
