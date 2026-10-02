#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Frigate::Level {

// HIT_TYPE: the surface types an image has.
namespace HitType {
constexpr int Default = 0;
constexpr int Stone = 1;
constexpr int Wood = 2;
constexpr int Metal = 3;
constexpr int Glass = 4;
constexpr int Water = 5;
constexpr int Snow = 6;
constexpr int Dirt = 7;
constexpr int Mud = 8;
constexpr int Tile = 9;
constexpr int MetalObj = 10;
constexpr int Chr = 11;
constexpr int GlassXlu = 12;
constexpr int Count = 13;
} // namespace HitType

// g_HitTypeSounds' impact marks: the bullet impact types (thing2) a surface
// type leaves, one of which a hit picks at random. The hit sounds belong to the
// audio port.
// TODO(https://github.com/liz-baker/frigate/issues/136): the hit sound effects
// (sfx and sfx_len).
struct SurfaceImpacts {
    std::array<uint8_t, 4> types;
    int count;
};
// g_HitTypeSounds[type]; the default for a type the table lacks.
const SurfaceImpacts& SurfaceImpactsFor(int hitType);

// One image's number and surface types (g_Textures).
struct TextureHit {
    // The image's index in images.def: the texture number lists name it by.
    int number = -1;
    // hitSound: the surface type the hit sound is chosen by.
    uint8_t sound = 0;
    // hitTexture: the surface type the impact mark is chosen by.
    uint8_t impact = 0;
};

// The `textures/hits` table (docs/formats/goldeneye-texture-hits.md), looked up
// by the hash of an image's resource path as a list names it.
class TextureHits {
  public:
    // Parses the table's text; an empty table for a line it can't read.
    static TextureHits Parse(const std::string& text);

    // The image whose `textures/<name>` (or palette, `textures/<name>.pal`) has
    // the hash `resourceHash`, or null.
    const TextureHit* Find(uint64_t resourceHash) const;
    // The texture numbered `number`, or null.
    const TextureHit* Find(int number) const;
    // The number of the image `resourceHash` names, -1 for none.
    int NumberOf(uint64_t resourceHash) const;
    // The images in the table.
    size_t Size() const {
        return count_;
    }

  private:
    std::unordered_map<uint64_t, TextureHit> byHash_;
    std::unordered_map<int, TextureHit> byNumber_;
    size_t count_ = 0;
};

// Texture 0x4FD is the light shafts that come through the Archives' windows,
// which a shot passes through.
constexpr int kLightShaftTexture = 0x4FD;

} // namespace Frigate::Level
