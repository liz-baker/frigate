#pragma once

#include <array>
#include <vector>

#include "level/StanIntersection.h"

namespace Frigate::Level {

// CDTYPE_*: which props a stan line or circle test also collides with. 0
// tests the tiles only.
namespace CdType {
constexpr int Objs = 0x0001;
constexpr int Doors = 0x0002;
constexpr int Players = 0x0004;
constexpr int Chrs = 0x0008;
constexpr int PathBlocker = 0x0010;
constexpr int Bg = 0x0020;
constexpr int AiOpaque = 0x0100;
constexpr int ObjsImmuneToExplosions = 0x0200;
constexpr int ClosedDoors = 0x1000;
constexpr int All = 0x003f;
} // namespace CdType

// A prop's collision polygon (chraiGetCollisionBounds), in world units.
struct PropCollider {
    std::array<Vec2, 8> polygon{};
    int edges = 0;
    // The polygon's vertical extent.
    float top = 0;
    float bottom = 0;
    // The prop's slot in the prop list.
    int prop = -1;
};

// The props the stan's collision tests walk (roomGetProps and propIsOfCdType,
// with chraiGetCollisionBounds for each).
class StanProps {
  public:
    virtual ~StanProps() = default;
    // Appends, in room-list order, each prop in the rooms (a -1-terminated
    // list) that `cdTypes` selects and that has a collision polygon.
    virtual void Colliders(const int* rooms, int cdTypes, std::vector<PropCollider>* out) const = 0;
};

} // namespace Frigate::Level
