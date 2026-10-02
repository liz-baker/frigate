#include <algorithm>
#include <cmath>
#include <cstring>

#include "level/ChrActions.h"
#include "level/HitParts.h"
#include "level/LevelModels.h"
#include "level/RayTests.h"
#include "level/Shots.h"
#include "level/StanIntersection.h"

// The hit tests of projectile flight (projectileFindCollidingProp and what it
// calls in propobj.c): the same model and footprint tests a shot makes, along a
// segment of the world.
namespace Frigate::Level {

namespace {

// The instance behind a level model handle.
Model::ModelInstance& Instance(ModelHandle& handle) {
    return *static_cast<LevelModel&>(handle).Instance();
}

float SquareSum(const Vec3& v) {
    return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]);
}

void ToBounds(const ModelBounds& source, float (&out)[6]) {
    out[0] = source.xmin;
    out[1] = source.xmax;
    out[2] = source.ymin;
    out[3] = source.ymax;
    out[4] = source.zmin;
    out[5] = source.zmax;
}

// A normal that is flat, or the z axis when it has no length.
Vec3 NormalizedFlat(Vec3 normal) {
    if (normal[0] != 0.0f || normal[2] != 0.0f) {
        const float length = std::sqrt(SquareSum(normal));
        return { normal[0] / length, normal[1] / length, normal[2] / length };
    }
    normal[2] = 1.0f;
    return normal;
}

} // namespace

Model::ModelInstance* Shots::PoseChain(int propId) {
    const Prop& prop = (*scene_.props)[propId];
    const PlacedObject* object = prop.type == PropType::Door ? nullptr : scene_.objects->ForProp(propId);
    const Model::ModelInstance* parent = nullptr;
    if (object != nullptr && (object->runtimeFlags & ObjectRuntime::Embedded) != 0 && prop.parent >= 0) {
        parent = PoseChain(prop.parent);
    }
    return PoseProp(propId, parent);
}

bool Shots::LineTestModel(int propId, const Model::ModelInstance* parent, const Vec3& viewStart,
                          const Vec3& viewDirection, Vec3* hitPos, Vec3* hitNormal, const Model::ModelInstance** model,
                          int32_t* node) {
    // projectileLineTestModel.
    PlacedDoor* door = (*scene_.props)[propId].type == PropType::Door ? scene_.doors->ForProp(propId) : nullptr;
    Model::ModelInstance* instance = PoseProp(propId, parent);
    ModelGeometrySource* geometry = scene_.models != nullptr ? scene_.models->Geometry() : nullptr;
    if (instance == nullptr || geometry == nullptr) {
        return false;
    }

    int found = 0;
    int32_t candidate = kNoNode;
    HitThing hit;
    int matrixIndex = 0;
    int32_t hitNode = kNoNode;

    if (door != nullptr) {
        NodeRef root;
        root.model = instance;
        root.node = 0;
        const Mtxf* rootMatrix = NodeMatrix(root);
        float box[6];
        ToBounds(door->bbox, box);
        found = rootMatrix != nullptr && RayIntersectsTransformedBox(box, *rootMatrix, viewStart, viewDirection) ? 1 : 0;
        candidate = 0;
        if (found > 0 && !FindTriangleHit(*instance, candidate, viewStart, viewDirection, *geometry, scene_.textures,
                                          &hit, &matrixIndex, &hitNode)) {
            found = 0;
        }
    } else {
        do {
            found = FindProjectileHitCandidate(*instance, viewStart, viewDirection, &candidate);
            if (found > 0 && FindTriangleHit(*instance, candidate, viewStart, viewDirection, *geometry, scene_.textures,
                                             &hit, &matrixIndex, &hitNode)) {
                break;
            }
        } while (found > 0);
    }

    if (found <= 0) {
        return false;
    }
    const auto& matrices = instance->Walk().matrices;
    if (matrixIndex < 0 || static_cast<size_t>(matrixIndex) >= matrices.size()) {
        return false;
    }
    Mtxf matrix;
    std::memcpy(matrix.m, matrices[static_cast<size_t>(matrixIndex)].m, sizeof(matrix.m));
    const Mtxf& viewToWorld = scene_.camera->ViewToWorld();

    *hitPos = TransformVector(viewToWorld, TransformVector(matrix, hit.hitpos));

    Vec3 normal = RotateVector(matrix, hit.normal);
    if (normal[0] * viewDirection[0] + normal[1] * viewDirection[1] + normal[2] * viewDirection[2] > 0.0f) {
        normal = { -normal[0], -normal[1], -normal[2] };
    }
    normal = RotateVector(viewToWorld, normal);
    if (normal[0] != 0.0f || normal[1] != 0.0f || normal[2] != 0.0f) {
        const float length = std::sqrt(SquareSum(normal));
        normal = { normal[0] / length, normal[1] / length, normal[2] / length };
    } else {
        normal[2] = 1.0f;
    }
    *hitNormal = normal;
    *model = instance;
    *node = hitNode;
    return true;
}

bool Shots::LineTestFootprint(int propId, const Vec3& start, const Vec3& end, const Vec3& direction, Vec3* hitPos,
                              Vec3* hitNormal, float* hitDistance) {
    // sub_GAME_7F041400.
    PropCollider collider;
    if (!scene_.objects->CollisionBounds(propId, &collider) || collider.edges <= 0) {
        return false;
    }
    const float top = collider.top;
    const float bottom = collider.bottom;
    if ((top < start[1] && top < end[1]) || (start[1] < bottom && end[1] < bottom)) {
        return false;
    }

    const Vec2 start2d = { start[0], start[2] };
    const Vec2 end2d = { end[0], end[2] };
    float bestFraction = 1.0f;
    int bestEdge = -1;
    for (int i = 0; i < collider.edges; i++) {
        const int next = (i + 1) % collider.edges;
        const Vec2& from = collider.polygon[static_cast<size_t>(i)];
        const Vec2& to = collider.polygon[static_cast<size_t>(next)];
        if (SegmentsIntersect(start2d, end2d, from, to)) {
            const float fraction = SegmentIntersectionFraction(start2d, end2d, from, to);
            if (fraction < bestFraction) {
                bestFraction = fraction;
                bestEdge = i;
            }
        }
    }
    // The decomp tests the edge index with `> 0`, so a hit on the first edge counts
    // as none.
    if (bestEdge <= 0) {
        return false;
    }

    const int next = (bestEdge + 1) % collider.edges;
    const Vec3 edgeStart = { collider.polygon[static_cast<size_t>(bestEdge)][0], 0.0f,
                             collider.polygon[static_cast<size_t>(bestEdge)][1] };
    const Vec3 edgeEnd = { collider.polygon[static_cast<size_t>(next)][0], 0.0f,
                           collider.polygon[static_cast<size_t>(next)][1] };
    const Vec3 intersection = ChrActions::LineLineIntersection(edgeStart, edgeEnd, start, direction);
    const float distance =
        (direction[2] * (intersection[2] - start[2])) +
        (((intersection[0] - start[0]) * direction[0]) + ((intersection[1] - start[1]) * direction[1]));
    if (!(distance < *hitDistance)) {
        return false;
    }
    *hitDistance = distance;
    *hitPos = intersection;
    *hitNormal = NormalizedFlat({ -direction[0], 0.0f, -direction[2] });
    return true;
}

bool Shots::TestProjectileObject(int propId, const Model::ModelInstance* parent, const Vec3& worldStart,
                                 const Vec3& worldEnd, const Vec3& worldDirection, float maxDistance,
                                 const Vec3& viewStart, const Vec3& viewDirection, Vec3* bestPos, Vec3* bestNormal,
                                 float* bestDistance, ProjectileContact* contact) {
    // projectileTestObjectCollisionRecursive and projectileTestObjectCollision.
    PropList& props = *scene_.props;
    const Prop& prop = props[propId];
    PlacedObject* object = prop.type == PropType::Door ? nullptr : scene_.objects->ForProp(propId);
    PlacedDoor* door = prop.type == PropType::Door ? scene_.doors->ForProp(propId) : nullptr;
    if (object == nullptr && door == nullptr) {
        return false;
    }
    const ModelBounds* bounds = object != nullptr ? scene_.objects->BoundsOf(*object) : &door->modelBounds;
    const float scale = object != nullptr ? object->scale : 1.0f;
    // getinstsize.
    float instSize = 0.0f;
    if (bounds != nullptr) {
        const float dx = bounds->xmax - bounds->xmin;
        const float dy = bounds->ymax - bounds->ymin;
        const float dz = bounds->zmax - bounds->zmin;
        instSize = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz) * scale;
    }
    const Vec3& position = object != nullptr ? object->runtimePosition : door->centre;

    bool found = false;
    float value = 0.0f;
    if (prop.parent < 0) {
        const float dx = position[0] - worldStart[0];
        const float dy = position[1] - worldStart[1];
        const float dz = position[2] - worldStart[2];
        value = (dz * worldDirection[2]) + ((dx * worldDirection[0]) + (dy * worldDirection[1]));
    }

    if (-instSize <= value && value <= maxDistance + instSize) {
        Vec3 hitPos{};
        Vec3 hitNormal{};
        float hitDistance = 0.0f;
        const Model::ModelInstance* hitModel = nullptr;
        int32_t hitNode = kNoNode;
        bool hit = false;
        if ((prop.flags & PropFlag::OnScreen) != 0) {
            if (LineTestModel(propId, parent, viewStart, viewDirection, &hitPos, &hitNormal, &hitModel, &hitNode)) {
                const float dx = hitPos[0] - worldStart[0];
                const float dy = hitPos[1] - worldStart[1];
                const float dz = hitPos[2] - worldStart[2];
                value = (dz * worldDirection[2]) + ((dx * worldDirection[0]) + (dy * worldDirection[1]));
                if (0.0f <= value && value <= maxDistance) {
                    hitDistance = value;
                    hit = true;
                }
            }
        } else {
            // projectileTestPropBoundingSphere, then the footprint.
            const Vec3 vector = { position[0] - worldStart[0], position[1] - worldStart[1],
                                  position[2] - worldStart[2] };
            const float along =
                worldDirection[0] * vector[0] + worldDirection[1] * vector[1] + worldDirection[2] * vector[2];
            if (along > 0) {
                const float a = SquareSum(worldDirection);
                const float b = SquareSum(vector);
                if ((b - instSize * instSize) * a <= along * along) {
                    hitDistance = maxDistance;
                    if (LineTestFootprint(propId, worldStart, worldEnd, worldDirection, &hitPos, &hitNormal,
                                          &hitDistance)) {
                        hit = true;
                        hitModel = PoseProp(propId, parent);
                        hitNode = 0;
                    }
                }
            }
        }
        if (hit && hitDistance < *bestDistance) {
            *bestDistance = hitDistance;
            *bestPos = hitPos;
            *bestNormal = hitNormal;
            contact->prop = propId;
            contact->hitpart = -1;
            contact->model = hitModel;
            contact->node = hitNode;
            found = true;
        }
    }

    if ((prop.flags & PropFlag::OnScreen) != 0) {
        const Model::ModelInstance* self = PoseProp(propId, parent);
        for (int child = prop.child; child >= 0; child = props[child].prev) {
            if ((props[child].flags & PropFlag::OnScreen) != 0 &&
                TestProjectileObject(child, self, worldStart, worldEnd, worldDirection, maxDistance, viewStart,
                                     viewDirection, bestPos, bestNormal, bestDistance, contact)) {
                found = true;
            }
        }
    }
    return found;
}

bool Shots::LineTestChr(int propId, const Vec3& worldStart, const Vec3& worldDirection, float maxDistance,
                        const Vec3& viewStart, const Vec3& viewDirection, Vec3* hitPos, Vec3* hitNormal,
                        float* hitDistance, ProjectileContact* contact) {
    // sub_GAME_7F041BB8.
    PropList& props = *scene_.props;
    const Prop& prop = props[propId];
    Chr& chr = (*scene_.chrs)[prop.object];
    if (!chr.model) {
        return false;
    }
    const Model::ModelInstance& model = Instance(*chr.model);
    const float instSize = InstanceSize(model);
    const float dx = prop.position[0] - worldStart[0];
    const float dy = prop.position[1] - worldStart[1];
    const float dz = prop.position[2] - worldStart[2];
    const float planeDist = (dz * worldDirection[2]) + ((dx * worldDirection[0]) + (dy * worldDirection[1]));
    if (!(-instSize <= planeDist && planeDist <= (maxDistance + instSize) && (prop.flags & PropFlag::OnScreen) != 0)) {
        return false;
    }

    std::vector<const Model::ModelInstance*> models;
    for (const auto& handle : chr.drawList) {
        models.push_back(&Instance(*handle));
    }
    JointHitSearch search(models);
    NodeRef node;
    const int bodyPart = search.First(viewStart, viewDirection, &node);
    if (bodyPart <= 0) {
        return false;
    }
    const Mtxf* matrix = NodeMatrix(node);
    if (matrix == nullptr) {
        return false;
    }
    const float ex = matrix->m[3][0] - viewStart[0];
    const float ey = matrix->m[3][1] - viewStart[1];
    const float ez = matrix->m[3][2] - viewStart[2];
    const float distance = (ez * viewDirection[2]) + ((ex * viewDirection[0]) + (ey * viewDirection[1]));
    if (!(distance < *hitDistance)) {
        return false;
    }
    *hitDistance = distance;
    *hitPos = { (worldDirection[0] * distance) + worldStart[0], (worldDirection[1] * distance) + worldStart[1],
                (worldDirection[2] * distance) + worldStart[2] };
    *hitNormal = NormalizedFlat({ -worldDirection[0], 0.0f, -worldDirection[2] });
    contact->prop = propId;
    contact->hitpart = bodyPart;
    contact->model = node.model;
    contact->node = node.node;
    return true;
}

bool Shots::FindCollidingProp(int ignoreProp, const Vec3& start, const Vec3& end, int cdTypes, const int* rooms,
                              Vec3* hitPos, Vec3* hitNormal, ProjectileContact* contact) {
    PropList& props = *scene_.props;
    Vec3 direction = { end[0] - start[0], end[1] - start[1], end[2] - start[2] };
    const float distance = std::sqrt(SquareSum(direction));
    if (distance == 0.0f) {
        return false;
    }
    direction = { direction[0] * (1.0f / distance), direction[1] * (1.0f / distance),
                  direction[2] * (1.0f / distance) };

    Mtxf worldToView;
    std::memcpy(worldToView.m, scene_.camera->WorldToScreen(), sizeof(worldToView.m));
    const Vec3 viewStart = TransformVector(worldToView, start);
    const Vec3 viewDirection = RotateVector(worldToView, direction);

    float bestDistance = distance;
    bool found = false;
    if (cdTypes != 0) {
        for (const int16_t id : props.RoomProps(rooms)) {
            if (id == ignoreProp) {
                continue;
            }
            const Prop& prop = props[id];
            if (prop.type == PropType::Obj || prop.type == PropType::Weapon || prop.type == PropType::Door) {
                const PlacedObject* object = prop.type == PropType::Door ? nullptr : scene_.objects->ForProp(id);
                const PlacedDoor* door = prop.type == PropType::Door ? scene_.doors->ForProp(id) : nullptr;
                if (object == nullptr && door == nullptr) {
                    continue;
                }
                const uint32_t runtime = object != nullptr ? object->runtimeFlags : door->runtimeFlags;
                const uint32_t flags2 = object != nullptr ? object->flags2 : door->flags2;
                if ((runtime & ObjectRuntime::IsRetick) != 0 || (flags2 & ObjectFlag2::ThrowThrough) != 0) {
                    continue;
                }
                if (prop.type == PropType::Door) {
                    if ((cdTypes & CdType::Doors) == 0 && !scene_.doors->Passes(id, cdTypes)) {
                        continue;
                    }
                } else if ((cdTypes & CdType::Objs) == 0) {
                    continue;
                }
                if (TestProjectileObject(id, nullptr, start, end, direction, distance, viewStart, viewDirection, hitPos,
                                         hitNormal, &bestDistance, contact)) {
                    found = true;
                }
            } else if (prop.type == PropType::Chr) {
                const Chr& chr = (*scene_.chrs)[prop.object];
                if ((chr.hidden & ChrHidden::Moving) != 0 || (cdTypes & CdType::Chrs) == 0) {
                    continue;
                }
                if (LineTestChr(id, start, direction, distance, viewStart, viewDirection, hitPos, hitNormal,
                                &bestDistance, contact)) {
                    found = true;
                }
            } else if (prop.type == PropType::Viewer) {
                // The player has no character of its own in a solo stage: its
                // footprint is tested.
                if (LineTestFootprint(id, start, end, direction, hitPos, hitNormal, &bestDistance)) {
                    contact->prop = id;
                    contact->hitpart = HitPart::NullPart;
                    contact->model = nullptr;
                    contact->node = kNoNode;
                    found = true;
                }
            }
        }
    }

    if (found) {
        contact->worldDirection = direction;
        contact->viewDirection = viewDirection;
    }
    return found;
}

} // namespace Frigate::Level
