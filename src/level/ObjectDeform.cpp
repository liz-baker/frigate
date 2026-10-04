#include "level/ObjectDeform.h"

#include <cmath>

#include "level/LevelModels.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

// PROP_* model numbers the deformation names.
constexpr int kPropDesk1 = 38;
constexpr int kPropDesk2 = 39;
constexpr int kPropTvHolder = 76;
constexpr int kPropWoodenTable1 = 85;

// E_EXPLOSIONTYPE values: the deformation's explosionType is a third of one.
constexpr int kDeformMedium = 1;
constexpr int kDeformSmall = 3;

// The pool for a solo stage (dword_CODE_bss_8007A0D8 and ...DC), in vertices
// and blocks.
constexpr int kPoolVertices = 0x5DC;
constexpr int kPoolBlocks = 0x28;
constexpr int kSmallPoolVertices = 0x1F4;
constexpr int kSmallPoolBlocks = 0x14;

bool IsHalfPool(LevelId stage) {
    return stage == LevelId::Streets || stage == LevelId::Depot;
}

} // namespace

void ObjectDeform::Reset(LevelId stage, int players) {
    int blocks = kPoolBlocks;
    total_ = kPoolVertices;
    if (players >= 2 || IsHalfPool(stage)) {
        total_ = kSmallPoolVertices;
        blocks = kSmallPoolBlocks;
    }
    blocks_.assign(static_cast<size_t>(blocks), Block{});
    blocks_[0].size = total_;
    blocks_[0].users = 0;
    free_ = total_;
    nodes_.clear();
}

const ObjectDeform::CollisionNode& ObjectDeform::NodeOf(int model) {
    auto found = nodes_.find(model);
    if (found != nodes_.end()) {
        return found->second;
    }
    CollisionNode& info = nodes_[model];
    const PropModel* propModel = objects_.Model(model);
    if (propModel == nullptr || models_ == nullptr) {
        return info;
    }
    const std::string path = "models/prop/P" + propModel->name + "Z";
    auto handle = models_->CreateProp(path);
    if (!handle) {
        return info;
    }
    const Model::ModelInstance& instance = *static_cast<LevelModel&>(*handle).Instance();
    const std::vector<ModelNode>& nodes = instance.Nodes();
    if (nodes.empty()) {
        return info;
    }

    // sub_GAME_7F04B478: the first collision node in the tree's order.
    constexpr uint8_t kCollisionNodeType = 0x18;
    int32_t at = 0;
    while (at != kNoNode && nodes[static_cast<size_t>(at)].type != kCollisionNodeType) {
        const ModelNode& node = nodes[static_cast<size_t>(at)];
        if (node.child != kNoNode) {
            at = node.child;
            continue;
        }
        int32_t up = at;
        at = kNoNode;
        while (up != kNoNode) {
            const ModelNode& current = nodes[static_cast<size_t>(up)];
            if (current.next != kNoNode) {
                at = current.next;
                break;
            }
            up = current.parent;
        }
    }
    if (at == kNoNode) {
        return info;
    }
    const ModelNode& collision = nodes[static_cast<size_t>(at)];

    // sub_GAME_7F04B590: the screens of a console are not deformed.
    const auto isSwitch = [&](size_t slot) {
        const ModelNode* node = instance.SwitchNode(slot);
        return node != nullptr && node->offset == collision.offset;
    };
    if (propModel->skeleton == "console_one_screen" && isSwitch(0)) {
        return info;
    }
    if (propModel->skeleton == "console_four_screen" && (isSwitch(0) || isSwitch(1) || isSwitch(2) || isSwitch(3))) {
        return info;
    }

    ModelGeometrySource* geometry = models_->Geometry();
    if (geometry == nullptr) {
        return info;
    }
    const auto loaded = geometry->LoadNode(path, collision);
    if (loaded == nullptr || loaded->vertices == nullptr || loaded->vertexCount == 0) {
        return info;
    }
    info.offset = collision.offset;
    info.original.reserve(loaded->vertexCount);
    for (size_t i = 0; i < loaded->vertexCount; i++) {
        info.original.push_back(loaded->vertices[i].v);
    }
    info.valid = true;
    return info;
}

int ObjectDeform::Allocate(int count, int model, int level) {
    // vtxstore_allocate for the pool objects draw from.
    const int blockCount = static_cast<int>(blocks_.size());
    int at = 0;
    int visited = 0;
    int state = 0;
    do {
        if (blocks_[static_cast<size_t>(at)].users == 0 && blocks_[static_cast<size_t>(at)].size >= count) {
            state = 1;
        } else {
            at = blocks_[static_cast<size_t>(at)].next;
            visited++;
            if (at == -1 || blockCount < visited) {
                state = -1;
            }
        }
    } while (state == 0);
    if (blockCount < visited || state != 1) {
        return -1;
    }

    Block& block = blocks_[static_cast<size_t>(at)];
    const int size = block.size;
    block.model = model;
    block.level = level;
    block.users++;
    bool split = false;
    if (size != count) {
        for (int slot = 0; slot < blockCount; slot++) {
            Block& spare = blocks_[static_cast<size_t>(slot)];
            if (spare.users == -1) {
                block.size = count;
                spare.size = size - count;
                spare.users = 0;
                spare.prev = at;
                split = true;
                spare.next = block.next;
                if (block.next >= 0) {
                    blocks_[static_cast<size_t>(block.next)].prev = slot;
                }
                block.next = slot;
                break;
            }
        }
    }
    free_ -= split ? count : size;
    return at;
}

void ObjectDeform::Free(int index) {
    // vtxstore_free.
    if (index < 0 || static_cast<size_t>(index) >= blocks_.size()) {
        return;
    }
    Block& block = blocks_[static_cast<size_t>(index)];
    block.users--;
    if (block.users != 0) {
        return;
    }
    free_ += block.size;

    int at = index;
    // Merge with the free block after it.
    int next = blocks_[static_cast<size_t>(at)].next;
    if (next >= 0 && blocks_[static_cast<size_t>(next)].users == 0) {
        blocks_[static_cast<size_t>(at)].size += blocks_[static_cast<size_t>(next)].size;
        blocks_[static_cast<size_t>(at)].next = blocks_[static_cast<size_t>(next)].next;
        blocks_[static_cast<size_t>(next)].users = -1;
        next = blocks_[static_cast<size_t>(at)].next;
        if (next >= 0) {
            blocks_[static_cast<size_t>(next)].prev = at;
        }
    }
    // Merge with the free block before it.
    const int previous = blocks_[static_cast<size_t>(at)].prev;
    if (previous >= 0 && blocks_[static_cast<size_t>(previous)].users == 0) {
        blocks_[static_cast<size_t>(previous)].size += blocks_[static_cast<size_t>(at)].size;
        blocks_[static_cast<size_t>(previous)].next = blocks_[static_cast<size_t>(at)].next;
        blocks_[static_cast<size_t>(at)].users = -1;
        at = previous;
        next = blocks_[static_cast<size_t>(at)].next;
        if (next >= 0) {
            blocks_[static_cast<size_t>(next)].prev = at;
        }
    }
}

void ObjectDeform::FixReferences(int find, int replacement) {
    PropList& props = objects_.Props();
    for (int prop = props.ActiveTail(); prop >= 0; prop = props.ActiveBefore(prop)) {
        if (props[prop].type != PropType::Obj) {
            continue;
        }
        if (PlacedObject* object = objects_.ForProp(prop); object != nullptr && object->deformBlock == find) {
            object->deformBlock = replacement;
        }
    }
}

const Model::NodeVertices* ObjectDeform::VerticesOf(const PlacedObject& object) const {
    if (object.deformBlock < 0 || static_cast<size_t>(object.deformBlock) >= blocks_.size()) {
        return nullptr;
    }
    return &blocks_[static_cast<size_t>(object.deformBlock)].data;
}

void ObjectDeform::Release(PlacedObject& object) {
    if (object.deformBlock >= 0) {
        Free(object.deformBlock);
        object.deformBlock = -1;
    }
}

void ObjectDeform::Deform(PlacedObject& object, int explosionType) {
    const PropModel* propModel = objects_.Model(object.model);
    if (propModel == nullptr) {
        return;
    }
    const CollisionNode& info = NodeOf(object.model);
    if (!info.valid) {
        return;
    }
    const int count = static_cast<int>(info.original.size());

    // An object_explosion_details record is a TypeID followed by six seeds, and
    // the seed index runs to 7: the last two read into the next record (its
    // TypeID, then its first seed).
    const auto seedIndex = static_cast<size_t>((random_() & 1) != 0 ? explosionType : explosionType + 3);
    uint32_t seed = 0;
    if (seedIndex < propModel->deformSeeds.size()) {
        seed = propModel->deformSeeds[seedIndex];
    } else if (const PropModel* next = objects_.Model(object.model + 1)) {
        // The index tops out at 7 (a type of 4), one seed into the next record.
        const size_t spill = seedIndex - propModel->deformSeeds.size();
        seed = spill == 0 ? static_cast<uint16_t>(next->explosion) : next->deformSeeds[spill - 1];
    }
    if (seed == 0) {
        seed = random_();
    }

    impacts_.ClearForProp(object.prop);

    const bool adjustHeight = object.model != kPropTvHolder;

    const int level = Objects::DestroyedLevel(object);
    const int block = Allocate(count, object.model, level);
    if (block >= 0) {
        Block& fresh = blocks_[static_cast<size_t>(block)];
        fresh.data.node = info.offset;
        if (object.deformBlock >= 0) {
            fresh.data.vertices = blocks_[static_cast<size_t>(object.deformBlock)].data.vertices;
            Free(object.deformBlock);
        } else {
            fresh.data.vertices = info.original;
        }
        object.deformBlock = block;
    } else {
        // The pool is full: the object is squashed instead, and one that was
        // already deformed is removed.
        if (object.deformBlock >= 0) {
            Free(object.deformBlock);
            object.deformBlock = -1;
            object.runtimeFlags |= ObjectRuntime::Remove;
        }
        for (size_t axis = 0; axis < 3; axis++) {
            object.mtx.m[1][axis] *= 0.85f;
        }
        if (!adjustHeight) {
            return;
        }
        const ModelBounds* bounds = objects_.BoundsOf(object);
        const float lift = (object.scale * (bounds != nullptr ? bounds->ymin : 0.0f)) * 0.15f;
        objects_.Props()[object.prop].position[1] += lift;
        object.runtimePosition[1] += lift;
        return;
    }

    // The gray desks and wood table are not blackened.
    const bool allowBlackening =
        object.model != kPropDesk1 && object.model != kPropDesk2 && object.model != kPropWoodenTable1;

    std::vector<Vtx_t>& vertices = blocks_[static_cast<size_t>(block)].data.vertices;
    int ymin = 99999;
    int ymax = -99999;
    for (const Vtx_t& vertex : vertices) {
        ymin = std::min<int>(ymin, vertex.ob[1]);
        ymax = std::max<int>(ymax, vertex.ob[1]);
    }
    int ymid = (ymin + ymax) >> 1;

    if (explosionType == kDeformSmall) {
        if (ymid - ymin >= 41) {
            ymid = ymin + 40;
        }
    }

    float yscale = 1.0f;
    if (ymax - ymin >= 61) {
        if (explosionType < kDeformSmall) {
            yscale = 0.89999998f;
        } else {
            yscale = static_cast<float>((ymax - ymin) - 60) / static_cast<float>(ymax - ymin);
        }
    }

    for (size_t i = 0; i < vertices.size(); i++) {
        Vtx_t& vertex = vertices[i];
        const Vtx_t& source = info.original[i];
        ChrObjRandomSetSeed(static_cast<uint32_t>(source.ob[2] + source.ob[0] + source.ob[1]) + seed);

        int chance = 0;
        if (allowBlackening) {
            if (object.mtx.m[1][1] >= 0.0f) {
                if (vertex.ob[1] >= ymid) {
                    chance = adjustHeight ? 90 : 20;
                } else {
                    chance = adjustHeight ? 20 : 90;
                }
            } else if (ymid >= vertex.ob[1]) {
                chance = adjustHeight ? 90 : 20;
            } else {
                chance = adjustHeight ? 20 : 90;
            }
        }

        if (static_cast<int32_t>(ChrObjRandomGetNext() % 100) < chance) {
            vertex.cn[0] = 0;
            vertex.cn[1] = 0;
            vertex.cn[2] = 0;
            vertex.cn[3] = 255;
        } else if (explosionType == kDeformMedium) {
            vertex.cn[3] = 0;
        }

        vertex.ob[1] =
            static_cast<int16_t>((static_cast<float>(vertex.ob[1] - ymin) * yscale) + static_cast<float>(ymin));
        vertex.ob[0] = static_cast<int16_t>(vertex.ob[0] + static_cast<int32_t>(ChrObjRandomGetNext() % 80) - 40);
        vertex.ob[1] = static_cast<int16_t>(vertex.ob[1] + static_cast<int32_t>(ChrObjRandomGetNext() % 80) - 40);
        vertex.ob[2] = static_cast<int16_t>(vertex.ob[2] + static_cast<int32_t>(ChrObjRandomGetNext() % 80) - 40);
        if (vertex.ob[1] < ymin) {
            vertex.ob[1] = static_cast<int16_t>(ymin);
        }
    }
}

void ObjectDeform::Tick() {
    // vtxstore_tick: with the pool a quarter full or less, objects of one model
    // deformed alike share a block.
    bool merged = false;
    if (free_ < (total_ >> 2)) {
        const int blockCount = static_cast<int>(blocks_.size());
        for (int first = 0; first < blockCount - 1; first++) {
            if (blocks_[static_cast<size_t>(first)].users <= 0) {
                continue;
            }
            for (int second = first + 1; second < blockCount; second++) {
                Block& a = blocks_[static_cast<size_t>(first)];
                Block& b = blocks_[static_cast<size_t>(second)];
                if (b.users > 0 && a.model == b.model && a.level == b.level) {
                    FixReferences(second, first);
                    merged = true;
                    a.users += b.users;
                    b.users = 0;
                    free_ += b.size;
                }
            }
        }
    }

    if (merged) {
        int at = 0;
        while (true) {
            int next = blocks_[static_cast<size_t>(at)].next;
            if (next < 0) {
                break;
            }
            if (blocks_[static_cast<size_t>(at)].users == 0 && blocks_[static_cast<size_t>(next)].users == 0) {
                blocks_[static_cast<size_t>(at)].size += blocks_[static_cast<size_t>(next)].size;
                blocks_[static_cast<size_t>(next)].users = -1;
                next = blocks_[static_cast<size_t>(next)].next;
                blocks_[static_cast<size_t>(at)].next = next;
                if (next >= 0) {
                    blocks_[static_cast<size_t>(next)].prev = at;
                }
                continue;
            }
            at = next;
        }
    }

    if (free_ < (total_ >> 2)) {
        FreeOneDestroyed();
    }
}

void ObjectDeform::FreeOneDestroyed() {
    PropList& props = objects_.Props();
    for (int prop = props.ActiveTail(); prop >= 0; prop = props.ActiveBefore(prop)) {
        if (props[prop].type != PropType::Obj || (props[prop].flags & PropFlag::OnScreen) != 0) {
            continue;
        }
        PlacedObject* object = objects_.ForProp(prop);
        if (object == nullptr || (object->state & ObjectState::Destroyed) == 0) {
            continue;
        }
        if (!NodeOf(object->model).valid) {
            // No node to deform, or none that may be.
            return;
        }
        if (object->deformBlock >= 0) {
            if (hooks_.freeObject) {
                hooks_.freeObject(prop);
            }
            return;
        }
    }
}

} // namespace Frigate::Level
