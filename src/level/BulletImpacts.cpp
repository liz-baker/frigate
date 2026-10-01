#include "level/BulletImpacts.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kFraction = 1.0f / 4294967296.0f;
// SMOKE_PARTS_LEN: how many marks ahead of the newest fade in.
constexpr int kFadeSlots = 10;

// g_ImpactTypes.
constexpr std::array<ImpactType, kImpactTypeCount> kImpactTypes = { {
    { 10.0f, 10.0f, 1, 2, 8 }, { 6.0f, 6.0f, 1, 2, 8 },   { 8.0f, 8.0f, 0, 2, 8 },   { 20.0f, 20.0f, 1, 2, 8 },
    { 6.0f, 6.0f, 1, 2, 8 },   { 8.0f, 8.0f, 1, 2, 8 },   { 12.0f, 12.0f, 1, 2, 8 }, { 6.0f, 6.0f, 1, 2, 8 },
    { 20.0f, 20.0f, 1, 2, 8 }, { 20.0f, 20.0f, 1, 2, 8 }, { 20.0f, 20.0f, 1, 2, 8 }, { 20.0f, 20.0f, 1, 2, 8 },
    { 20.0f, 20.0f, 1, 2, 8 }, { 24.0f, 24.0f, 1, 2, 8 }, { 6.0f, 6.0f, 1, 2, 1 },   { 6.0f, 6.0f, 1, 2, 1 },
    { 24.0f, 24.0f, 2, 2, 8 }, { 6.0f, 6.0f, 1, 2, 1 },   { 8.0f, 8.0f, 1, 2, 1 },   { 12.0f, 12.0f, 1, 2, 1 },
} };

float Fraction(const BulletImpacts::RandomFn& random) {
    return static_cast<float>(random()) * kFraction;
}

// explosionRoundFloat.
int32_t RoundFloat(float value) {
    if (value >= 0.0f) {
        return static_cast<int32_t>(value + 0.5f);
    }
    return static_cast<int32_t>(value - 0.5f);
}

int16_t Short(int32_t value) {
    return static_cast<int16_t>(value);
}

} // namespace

bool EffectImages::Parse(const std::string& text) {
    sizes_.clear();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        unsigned offset;
        int width;
        int height;
        if (std::sscanf(line.c_str(), "%x %d %d", &offset, &width, &height) != 3) {
            return false;
        }
        sizes_[static_cast<int>(offset)] = { width, height };
    }
    return true;
}

EffectImageSize EffectImages::Size(int offset) const {
    const auto found = sizes_.find(offset);
    return found == sizes_.end() ? EffectImageSize{} : found->second;
}

const ImpactType& ImpactTypeOf(int type) {
    return kImpactTypes[static_cast<size_t>(type)];
}

void BulletImpacts::Reset() {
    nextImpact_ = 0;
    nextScorch_ = 0;
    for (BulletImpact& impact : impacts_) {
        impact = BulletImpact{};
    }
    for (Scorch& scorch : scorches_) {
        scorch = Scorch{};
    }
}

void BulletImpacts::ClearByFlag(int prop, int8_t flag) {
    for (BulletImpact& impact : impacts_) {
        if (prop == impact.prop && flag == impact.clearFlag) {
            impact.room = -1;
        }
    }
}

void BulletImpacts::ClearForProp(int prop) {
    for (BulletImpact& impact : impacts_) {
        if (prop == impact.prop && ImpactTypeOf(impact.type).unk1 == 2) {
            impact.room = -1;
        }
    }
}

void BulletImpacts::SetAlpha(int start) {
    for (int i = 0; i < kFadeSlots; i++) {
        const auto value = static_cast<uint8_t>(static_cast<uint32_t>((static_cast<float>(i) / 10.0f) * 255.0f));
        for (EffectVertex& vertex : impacts_[static_cast<size_t>(start)].vertices) {
            vertex.colour[3] = value;
        }
        if (++start >= kMaxImpacts) {
            start = 0;
        }
    }
}

uint8_t BulletImpacts::Create(const ImpactPlacement& placement, const RandomFn& random) {
    int impactType = placement.type;
    if (placement.paintball) {
        impactType = 0x10;
    }

    Vec3 position = placement.position;
    const ImpactType& type = ImpactTypeOf(impactType);
    float width = type.width;
    float height = type.height;

    float spBC;
    float spB8;
    float spB4;
    float spB0;
    float spC4;
    const Vec3& normal = placement.normal;
    if (normal[0] == 0.0f && normal[2] == 0.0f) {
        spB8 = 0.0f;
        spB4 = 0.0f;
        spBC = 0.0f;
        spB0 = 1.0f;
        spC4 = 1.0f;
    } else {
        float length = std::sqrt((normal[0] * normal[0]) + (normal[1] * normal[1]) + (normal[2] * normal[2]));
        const float spDC = normal[0] / length;
        const float spD8 = normal[1] / length;
        const float spD4 = normal[2] / length;

        length = std::sqrt((spDC * spDC) + (spD4 * spD4));
        const float x = spDC / length;
        const float z = spD4 / length;

        spBC = -x;
        spB8 = spD8 * x;
        spB4 = -length;
        spB0 = spD8 * z;
        spC4 = z;
    }

    uint8_t stateBit = 0;
    if (placement.prop >= 0 && placement.propMatrix != nullptr) {
        const Mtxf& matrix = *placement.propMatrix;
        const Vec3 across = RotateVector(matrix, { spC4, 0.0f, spBC });
        const Vec3 down = RotateVector(matrix, { spB8, spB4, spB0 });

        const float acrossLength =
            std::sqrt((across[0] * across[0]) + (across[1] * across[1]) + (across[2] * across[2]));
        const float downLength = std::sqrt((down[0] * down[0]) + (down[1] * down[1]) + (down[2] * down[2]));

        width /= acrossLength;
        height /= downLength;

        if (type.unk2 < 2 && type.unk1 == 2) {
            stateBit = kStateMarkBlended;
        } else {
            stateBit = kStateMarkOpaque;
        }
    } else {
        for (size_t axis = 0; axis < 3; axis++) {
            position[axis] = (position[axis] * placement.levelScale) - placement.roomPosition[axis];
        }
        width *= placement.levelScale;
        height *= placement.levelScale;
    }

    BulletImpact& impact = impacts_[static_cast<size_t>(nextImpact_)];
    impact.prop = placement.prop;
    impact.matrixIndex = placement.matrixIndex;
    impact.clearFlag = placement.clearFlag;
    impact.room = static_cast<int16_t>(placement.room);
    impact.type = static_cast<int16_t>(impactType);

    const EffectImageSize image = images_ != nullptr
                                      ? images_->Size(EffectImage::kImpacts + (impactType * EffectImage::kEntrySize))
                                      : EffectImageSize{};
    const float zero = 0.0f;
    for (EffectVertex& vertex : impact.vertices) {
        vertex = EffectVertex{};
        vertex.colour = { 0, 0, 0, 0xDC };
    }

    EffectVertex* v = impact.vertices.data();
    v[0].position[0] = Short(RoundFloat((position[0] - (width * spC4)) - (height * spB8)));
    v[0].position[1] = Short(RoundFloat((position[1] - zero) - (height * spB4)));
    v[0].position[2] = Short(RoundFloat((position[2] - (width * spBC)) - (height * spB0)));
    v[0].texture = { 0, Short(image.height << 5) };

    v[1].position[0] = Short(RoundFloat((position[0] - (width * spC4)) + (height * spB8)));
    v[1].position[1] = Short(RoundFloat((position[1] - zero) + (height * spB4)));
    v[1].position[2] = Short(RoundFloat((position[2] - (width * spBC)) + (height * spB0)));
    v[1].texture = { 0, 0 };

    v[2].position[0] = Short(RoundFloat(position[0] + (width * spC4) + (height * spB8)));
    v[2].position[1] = Short(RoundFloat(position[1] + zero + (height * spB4)));
    v[2].position[2] = Short(RoundFloat(position[2] + (width * spBC) + (height * spB0)));
    v[2].texture = { Short(image.width << 5), 0 };

    v[3].position[0] = Short(RoundFloat((position[0] + (width * spC4)) - (height * spB8)));
    v[3].position[1] = Short(RoundFloat((position[1] + zero) - (height * spB4)));
    v[3].position[2] = Short(RoundFloat((position[2] + (width * spBC)) - (height * spB0)));
    v[3].texture = { Short(image.width << 5), Short(image.height << 5) };

    uint8_t previousGreen = 0;
    uint8_t previousBlue = 0;
    for (EffectVertex& vertex : impact.vertices) {
        uint8_t red;
        uint8_t green = previousGreen;
        uint8_t blue = previousBlue;
        switch (type.appearance) {
            case 1: {
                const auto value = static_cast<uint8_t>(0xFF - (random() % 40U));
                red = green = blue = value;
                break;
            }
            case 0: {
                const auto value = static_cast<uint8_t>(random() % 40U);
                red = green = blue = value;
                break;
            }
            case 2:
                red = (random() & 1) ? 0xFF : 0;
                green = (random() & 1) ? 0xFF : 0;
                blue = (random() & 1) ? 0xFF : 0;
                break;
            default:
                red = 0;
                break;
        }
        previousGreen = green;
        previousBlue = blue;
        vertex.colour = { red, green, blue, 0xFF };
    }

    nextImpact_++;
    if (nextImpact_ >= kMaxImpacts) {
        nextImpact_ = 0;
    }

    SetAlpha(nextImpact_);
    impacts_[static_cast<size_t>(nextImpact_)].room = -1;
    return stateBit;
}

void BulletImpacts::CreateScorch(Vec3 position, float size, int room, const Vec3& roomPosition, float levelScale,
                                 const RandomFn& random) {
    const float angle = Fraction(random) * kTau;
    const auto alpha = static_cast<uint8_t>(0xFF - (random() % 80U));

    // getPlayerCount() < 2: a scorch is a single-player effect.
    if (size > 200.0f) {
        size = 200.0f;
    }
    size *= 0.8f + (0.2f * Fraction(random));

    for (size_t axis = 0; axis < 3; axis++) {
        position[axis] = (position[axis] * levelScale) - roomPosition[axis];
    }
    size *= levelScale;

    const float cosine = std::cos(angle) * size;
    const float sine = std::sin(angle) * size;

    const EffectImageSize image = images_ != nullptr ? images_->Size(EffectImage::kGeneric) : EffectImageSize{};
    Scorch& scorch = scorches_[static_cast<size_t>(nextScorch_)];
    scorch.room = static_cast<int16_t>(room);
    for (EffectVertex& vertex : scorch.vertices) {
        vertex = EffectVertex{};
    }

    EffectVertex* v = scorch.vertices.data();
    const auto place = [&](EffectVertex& vertex, float x, float z, int16_t s, int16_t t) {
        vertex.position[0] = static_cast<int16_t>(static_cast<int32_t>(x));
        vertex.position[1] = static_cast<int16_t>(static_cast<int32_t>(position[1] + 0.5f));
        vertex.position[2] = static_cast<int16_t>(static_cast<int32_t>(z));
        vertex.texture = { s, t };
        const auto shade = static_cast<uint8_t>(random() % 50U);
        vertex.colour = { shade, shade, shade, alpha };
    };
    place(v[0], position[0] + cosine, position[2] + sine, 0, Short(image.width << 5));
    place(v[1], position[0] + sine, position[2] - cosine, 0, 0);
    place(v[2], position[0] - cosine, position[2] - sine, Short(image.height << 5), 0);
    place(v[3], position[0] - sine, position[2] + cosine, Short(image.width << 5), Short(image.height << 5));

    nextScorch_++;
    if (nextScorch_ >= kMaxScorches) {
        nextScorch_ = 0;
    }
}

} // namespace Frigate::Level
