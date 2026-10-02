#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "level/BulletImpacts.h"
#include "level/LevelInfo.h"
#include "level/ModelHit.h"
#include "level/Objects.h"
#include "model/JointDraw.h"

namespace Frigate::Level {

class ModelFactory;

// What the deformation of an object reaches into the rest of the stage for.
struct ObjectDeformHooks {
    // objFreePermanently(obj, TRUE), which also clears the marks on the object.
    std::function<void(int prop)> freeObject;
};

// The vertices objects keep once they have been blown out of shape
// (objDeform), drawn from a pool that two objects of one model share when
// they are deformed alike (vtxstore.c).
class ObjectDeform {
  public:
    using RandomFn = std::function<uint32_t()>;

    ObjectDeform(Objects& objects, BulletImpacts& impacts) : objects_(objects), impacts_(impacts) {
    }

    void Attach(ModelFactory* models, ObjectDeformHooks hooks, RandomFn random) {
        models_ = models;
        hooks_ = std::move(hooks);
        random_ = std::move(random);
    }
    // vtxstore_reset: the pool for a stage with `players` players.
    void Reset(LevelId stage, int players);

    // objDeform.
    void Deform(PlacedObject& object, int explosionType);
    // sub_GAME_7F050DE8: the object goes back to its model's own vertices.
    void Release(PlacedObject& object);
    // vtxstore_tick: duplicate deformations merge, and a full pool frees the
    // destroyed objects out of sight.
    void Tick();

    // The vertices the object's model draws a node with in place of its own, null
    // for an object that has not been deformed.
    const Model::NodeVertices* VerticesOf(const PlacedObject& object) const;

  private:
    // One block of the pool (struct unk_09B7A0_struct_parent).
    struct Block {
        int size = 0;
        // Zero while free, -1 for a slot that holds no block, else the objects using it.
        int users = -1;
        int next = -1;
        int prev = -1;
        int model = 0;
        int level = 0;
        Model::NodeVertices data;
    };
    // The collision node of a model that a deformation changes.
    struct CollisionNode {
        bool valid = false;
        uint32_t offset = 0;
        // The model's own vertices.
        std::vector<Vtx_t> original;
    };

    const CollisionNode& NodeOf(int model);
    // vtxstore_allocate and vtxstore_free (sub_GAME_7F09C044): a block of `count`
    // vertices, or -1.
    int Allocate(int count, int model, int level);
    void Free(int block);
    // sub_GAME_7F09BAC4: every object using block `find` uses `replacement`.
    void FixReferences(int find, int replacement);
    // sub_GAME_7F056690: frees one destroyed object out of sight whose vertices
    // were deformed.
    void FreeOneDestroyed();

    Objects& objects_;
    BulletImpacts& impacts_;
    ModelFactory* models_ = nullptr;
    ObjectDeformHooks hooks_;
    RandomFn random_;
    std::vector<Block> blocks_;
    int total_ = 0;
    // word_CODE_bss_8007A0F2: the vertices not in use.
    int free_ = 0;
    std::unordered_map<int, CollisionNode> nodes_;
};

} // namespace Frigate::Level
