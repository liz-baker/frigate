#include "level/BgHit.h"

#include <climits>

#include "level/LightFixtures.h"
#include "level/RayTests.h"
#include "level/TriangleList.h"

namespace Frigate::Level {

namespace {

// The vertices a vertex load brings in: its operand is an address in the room's
// vertex array once the interpreter has run it, or still an offset into it.
const Vtx* LoadedVertices(const Gfx& load, const RoomGeometry& geometry) {
    const uintptr_t operand = load.words.w1;
    if (operand > 0xFFFFF) {
        return reinterpret_cast<const Vtx*>(operand);
    }
    return reinterpret_cast<const Vtx*>(static_cast<const char*>(geometry.vertices.data) + operand);
}

// dx * dx + dy * dy + dz * dz in the game's 32-bit integers, which wrap.
int32_t SquaredDistance(int32_t dx, int32_t dy, int32_t dz) {
    const auto square = [](int32_t value) { return static_cast<uint32_t>(value) * static_cast<uint32_t>(value); };
    return static_cast<int32_t>(square(dx) + square(dy) + square(dz));
}

// The whole numbers of a room's position, which the batch bounds are moved by.
int32_t Whole(float value) {
    return static_cast<int32_t>(value);
}

} // namespace

void BgHit::Reset() {
    bounds_.clear();
    built_.clear();
}

bool BgHit::HasBounds(int room) const {
    return room >= 0 && static_cast<size_t>(room) < built_.size() && built_[static_cast<size_t>(room)];
}

void BgHit::DropBounds(int room) {
    if (room >= 0 && static_cast<size_t>(room) < built_.size()) {
        bounds_[static_cast<size_t>(room)].clear();
        built_[static_cast<size_t>(room)] = false;
    }
}

void BgHit::BuildBounds(int room, const RoomGeometry& geometry) {
    const auto index = static_cast<size_t>(room);
    if (bounds_.size() <= index) {
        bounds_.resize(index + 1);
        built_.resize(index + 1, false);
    }
    if (built_[index] || geometry.primaryList.Empty() || geometry.vertices.Empty()) {
        return;
    }

    const auto* commands = static_cast<const Gfx*>(geometry.primaryList.data);
    const Vec3& position = level_.RoomEntry(room).position;
    std::vector<Batch>& batches = bounds_[index];
    for (size_t i = 0; i < geometry.primaryList.count && !IsEndOfList(commands[i]); i += CommandWidth(commands[i])) {
        if (!IsVertexLoad(commands[i])) {
            continue;
        }
        Batch batch;
        batch.gdlIndex = static_cast<int>(i);
        for (int axis = 0; axis < 3; axis++) {
            batch.min[axis] = 0x7fff;
            batch.max[axis] = -0x8000;
        }
        const VertexLoad load = DecodeVertexLoad(commands[i]);
        const Vtx* vertices = LoadedVertices(commands[i], geometry);
        for (uint32_t v = 0; v < load.count; v++) {
            for (int axis = 0; axis < 3; axis++) {
                const int32_t coordinate = vertices[v].v.ob[axis];
                if (batch.min[axis] > coordinate) {
                    batch.min[axis] = coordinate;
                }
                if (batch.max[axis] < coordinate) {
                    batch.max[axis] = coordinate;
                }
            }
        }
        for (int axis = 0; axis < 3; axis++) {
            if (batch.min[axis] == batch.max[axis]) {
                batch.max[axis]++;
            }
            batch.min[axis] += Whole(position[static_cast<size_t>(axis)]);
            batch.max[axis] += Whole(position[static_cast<size_t>(axis)]);
        }
        batches.push_back(batch);
    }
    built_[index] = true;
}

bool BgHit::TestBatch(const Vec3& from, const Vec3& to, const Vec3& dir, const Batch& batch, int room,
                      const RoomGeometry& geometry, HitThing* hit) const {
    const auto* commands = static_cast<const Gfx*>(geometry.primaryList.data);
    const size_t count = geometry.primaryList.count;
    const Vec3& position = level_.RoomEntry(room).position;
    const Vtx* vertices = LoadedVertices(commands[batch.gdlIndex], geometry);
    const VertexLoad load = DecodeVertexLoad(commands[batch.gdlIndex]);

    // The image the list last loaded before this batch's vertices, which a
    // triangle without a load of its own after them is drawn with.
    uint64_t imageHash = 0;
    for (size_t i = 0; i < static_cast<size_t>(batch.gdlIndex); i += CommandWidth(commands[i])) {
        if (IsImageLoad(commands[i]) && i + 1 < count) {
            imageHash = OperandHash(commands[i + 1]);
        }
    }

    int32_t bestScore = INT32_MAX;

    // Tests the triangle of slots `slot` (relative to the batch's first) of
    // the command at `index`.
    const auto testTriangle = [&](size_t index, int16_t type, const int (&slot)[3]) {
        int idx[3];
        for (int i = 0; i < 3; i++) {
            idx[i] = slot[i] - static_cast<int>(load.firstSlot);
        }
        int32_t bboxMin[3] = { 0x7FFF, 0x7FFF, 0x7FFF };
        int32_t bboxMax[3] = { -0x8000, -0x8000, -0x8000 };
        for (int i = 0; i < 3; i++) {
            const Vtx& v = vertices[idx[i]];
            for (int axis = 0; axis < 3; axis++) {
                if (v.v.ob[axis] < bboxMin[axis]) {
                    bboxMin[axis] = v.v.ob[axis];
                }
                if (bboxMax[axis] < v.v.ob[axis]) {
                    bboxMax[axis] = v.v.ob[axis];
                }
            }
        }
        for (int axis = 0; axis < 3; axis++) {
            bboxMin[axis] += Whole(position[static_cast<size_t>(axis)]);
            bboxMax[axis] += Whole(position[static_cast<size_t>(axis)]);
        }
        if (!RayIntersectsBox(from, dir, bboxMin, bboxMax)) {
            return;
        }

        TriangleHit triangle;
        if (!RayIntersectsTriangle(vertices[idx[0]].v.ob, vertices[idx[1]].v.ob, vertices[idx[2]].v.ob, position, from,
                                   to, dir, &triangle)) {
            return;
        }

        const auto dx = static_cast<int32_t>(triangle.position[0]) - static_cast<int32_t>(from[0]);
        const auto dy = static_cast<int32_t>(triangle.position[1]) - static_cast<int32_t>(from[1]);
        const auto dz = static_cast<int32_t>(triangle.position[2]) - static_cast<int32_t>(from[2]);
        const int32_t dist = SquaredDistance(dx, dy, dz);
        int32_t score = dist;

        // The image of the nearest load before the triangle's command.
        uint64_t hash = imageHash;
        for (size_t i = static_cast<size_t>(batch.gdlIndex); i < index; i += CommandWidth(commands[i])) {
            if (IsImageLoad(commands[i]) && i + 1 < count) {
                hash = OperandHash(commands[i + 1]);
            }
        }
        const int texture = textures_ != nullptr && hash != 0 ? textures_->NumberOf(hash) : -1;
        if (hash != 0 && LightFixtures::IsLightTexture(hash)) {
            score = dist - 4;
        }

        if (score < bestScore && texture != kLightShaftTexture) {
            bestScore = score;
            hit->hitpos = triangle.position;
            hit->normal = triangle.normal;
            hit->triCommand = &commands[index];
            hit->triType = type;
            hit->texture = texture;
            hit->textureHash = hash;
        }
    };

    size_t index = static_cast<size_t>(batch.gdlIndex) + CommandWidth(commands[batch.gdlIndex]);
    while (index < count && !IsVertexLoad(commands[index]) && !IsEndOfList(commands[index])) {
        const Gfx& command = commands[index];
        if (Opcode(command) == static_cast<uint8_t>(Fast::F3DEX_G_TRI1)) {
            int slot[3];
            TriangleSlots(&command, 0, slot);
            testTriangle(index, 0, slot);
        } else if (Opcode(command) == static_cast<uint8_t>(Fast::INDY_G_TRI4)) {
            for (int16_t type = 1; type <= 4; type++) {
                int slot[3];
                TriangleSlots(&command, static_cast<uint32_t>(type), slot);
                testTriangle(index, type, slot);
            }
        }
        index += CommandWidth(command);
    }
    return bestScore != INT32_MAX;
}

bool BgHit::TestBullet(const Vec3& from, const Vec3& to, int room, HitThing* hit, const RoomGeometry& geometry) const {
    if (!HasBounds(room)) {
        return false;
    }

    const float scale = level_.Level().scale;
    Vec3 fromScaled{};
    Vec3 toScaled{};
    Vec3 dir{};
    for (size_t axis = 0; axis < 3; axis++) {
        fromScaled[axis] = from[axis] * scale;
        toScaled[axis] = to[axis] * scale;
        dir[axis] = toScaled[axis] - fromScaled[axis];
    }

    bool found = false;
    int32_t bestDist = INT32_MAX;
    for (const Batch& batch : bounds_[static_cast<size_t>(room)]) {
        if (!RayIntersectsBox(fromScaled, dir, batch.min, batch.max)) {
            continue;
        }
        HitThing candidate;
        if (!TestBatch(fromScaled, toScaled, dir, batch, room, geometry, &candidate)) {
            continue;
        }

        const auto dx = static_cast<int32_t>(candidate.hitpos[0] - fromScaled[0]);
        const auto dy = static_cast<int32_t>(candidate.hitpos[1] - fromScaled[1]);
        const auto dz = static_cast<int32_t>(candidate.hitpos[2] - fromScaled[2]);
        const int32_t dist = SquaredDistance(dx, dy, dz);
        int32_t score = dist;
        found = true;
        if (candidate.textureHash != 0 && LightFixtures::IsLightTexture(candidate.textureHash)) {
            score = dist - 4;
        }
        if (score < bestDist) {
            bestDist = score;
            *hit = candidate;
        }
    }
    return found;
}

} // namespace Frigate::Level
