#pragma once

#include <memory>
#include <string>
#include <vector>

#include "level/BgHit.h"
#include "level/Matrix.h"
#include "level/RayTests.h"
#include "libultraship/libultra/gbi.h"
#include "model/JointDraw.h"
#include "model/JointDrawList.h"
#include "model/ModelInstance.h"

namespace Frigate::Level {

// A node of a posed model: the model, the node's index in the model's tree
// (or in its head's, when `attachment` names one of the model's head
// attachments).
struct NodeRef {
    const Model::ModelInstance* model = nullptr;
    int32_t node = kNoNode;
    int32_t attachment = -1;

    bool Valid() const {
        return model != nullptr && node != kNoNode;
    }
};

// The lists and vertices of a display-list node, read from the packed model.
struct NodeGeometry {
    const Gfx* primary = nullptr;
    size_t primaryCount = 0;
    const Gfx* secondary = nullptr;
    size_t secondaryCount = 0;
    const Vtx* vertices = nullptr;
    size_t vertexCount = 0;
    // Holds the resources the pointers are into.
    std::vector<std::shared_ptr<void>> keep;
};

// Where the hit tests read a node's triangles from.
class ModelGeometrySource {
  public:
    virtual ~ModelGeometrySource() = default;
    // The lists and vertices of `node` of the model whose resource path is
    // `modelPath`; null, after logging why, when they do not load.
    virtual std::shared_ptr<const NodeGeometry> LoadNode(const std::string& modelPath, const ModelNode& node) = 0;
};

// The node a ref names.
const ModelNode& NodeOf(const NodeRef& ref);
// modelFindNodeMtx: the view-space matrix of the node, or of its nearest
// ancestor that owns one; null for none.
const Mtxf* NodeMatrix(const NodeRef& ref);
// modelFindNodeMtxIndex for a node of the model's own tree: the index of the
// matrix it, or its nearest owning ancestor, owns; -1 for none.
int NodeMatrixIndex(const Model::ModelInstance& model, int32_t node);
// getjointsize: the radius of the nearest joint at or above the node, times the
// model's scale.
float JointSize(const NodeRef& ref);
// getinstsize: the model's bounding radius times its scale.
float InstanceSize(const Model::ModelInstance& model);

// modelFindNextProjectileHitCandidate: walks the model's tree from the root (or
// from where `*node` left off) for the next bounding box or hit sphere the ray
// from `origin` along `dir` meets. Returns its hit part, with `*node` the node;
// 0 for none.
int FindProjectileHitCandidate(const Model::ModelInstance& model, const Vec3& origin, const Vec3& dir, int32_t* node);

// propobjFindHit: the nearest triangle of the display-list nodes at and below
// `startNode` the ray meets. On a hit `*matrixIndex` is the matrix of the node
// the triangle is in and `*hitNode` the node.
bool FindTriangleHit(const Model::ModelInstance& model, int32_t startNode, const Vec3& origin, const Vec3& dir,
                     ModelGeometrySource& geometry, const TextureHits* textures, HitThing* hit, int* matrixIndex,
                     int32_t* hitNode);

// sub_GAME_7F06C010 and probably_damage_detail_blood_effect_related: searches
// a character's models (itself, the weapons it holds and its hat) joint by
// joint, nearest first, for the bounding boxes and hit spheres a ray meets.
class JointHitSearch {
  public:
    explicit JointHitSearch(const std::vector<const Model::ModelInstance*>& models);

    // The first hit: the part, with `*hit` the node. 0 for none.
    int First(const Vec3& origin, const Vec3& dir, NodeRef* hit);
    // The next hit after the last one returned.
    int Next(const Vec3& origin, const Vec3& dir, NodeRef* hit);

  private:
    std::vector<const Model::ModelInstance*> models_;
    std::vector<JointModel> jointModels_;
    std::vector<JointEntry> entries_;
    // The entry being searched (counting down from the nearest), and where in
    // its model's visited nodes to go on from.
    int entry_ = -1;
    size_t resume_ = 0;
    bool resuming_ = false;
};

} // namespace Frigate::Level
