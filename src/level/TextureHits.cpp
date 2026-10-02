#include "level/TextureHits.h"

#include <sstream>

#include "ship/utils/StrHash64.h"

namespace Frigate::Level {

namespace {

constexpr std::array<SurfaceImpacts, HitType::Count> kImpacts = { {
    { { 0x7, 0, 0, 0 }, 1 },        // default
    { { 0x1, 0, 0, 0 }, 1 },        // stone
    { { 0x1, 0, 0, 0 }, 1 },        // wood
    { { 0x7, 0, 0, 0 }, 1 },        // metal
    { { 0x4, 0x5, 0x6, 0 }, 3 },    // glass
    { { 0, 0, 0, 0 }, 0 },          // water
    { { 0x1, 0, 0, 0 }, 1 },        // snow
    { { 0x2, 0, 0, 0 }, 1 },        // dirt
    { { 0x2, 0, 0, 0 }, 1 },        // mud
    { { 0x1, 0, 0, 0 }, 1 },        // tile
    { { 0x1, 0x7, 0, 0 }, 2 },      // metal object
    { { 0x2, 0, 0, 0 }, 1 },        // character
    { { 0x11, 0x12, 0x13, 0 }, 3 }, // translucent glass
} };

} // namespace

const SurfaceImpacts& SurfaceImpactsFor(int hitType) {
    if (hitType < 0 || hitType >= HitType::Count) {
        return kImpacts[HitType::Default];
    }
    return kImpacts[static_cast<size_t>(hitType)];
}

TextureHits TextureHits::Parse(const std::string& text) {
    TextureHits hits;
    std::istringstream in(text);
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string name;
        int sound;
        int impact;
        if (!(fields >> name >> sound >> impact)) {
            if (line.empty()) {
                continue;
            }
            return TextureHits();
        }
        const TextureHit hit = { number, static_cast<uint8_t>(sound), static_cast<uint8_t>(impact) };
        const std::string path = "textures/" + name;
        hits.byHash_[CRC64(path.c_str())] = hit;
        hits.byHash_[CRC64((path + ".pal").c_str())] = hit;
        hits.byNumber_[number] = hit;
        number++;
    }
    hits.count_ = static_cast<size_t>(number);
    return hits;
}

const TextureHit* TextureHits::Find(uint64_t resourceHash) const {
    const auto found = byHash_.find(resourceHash);
    return found == byHash_.end() ? nullptr : &found->second;
}

const TextureHit* TextureHits::Find(int number) const {
    const auto found = byNumber_.find(number);
    return found == byNumber_.end() ? nullptr : &found->second;
}

int TextureHits::NumberOf(uint64_t resourceHash) const {
    const TextureHit* hit = Find(resourceHash);
    return hit != nullptr ? hit->number : -1;
}

} // namespace Frigate::Level
