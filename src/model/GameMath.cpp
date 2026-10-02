#include "model/GameMath.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace Frigate {

namespace {

constexpr float kPi = 3.1415927f;
constexpr float kHalfPi = kPi / 2;
constexpr float kThreeHalfPi = 3 * kHalfPi;
constexpr float kTau = 6.2831855f;

// The arccosine of 0 to 1 as a fraction of 0x8000, sampled every 512 steps of
// the argument, then every 32 from 0x7800 and every 8 from 0x7FE0 where the
// curve steepens. Each table ends on the first entry of the next.
constexpr std::array<uint16_t, 62> kCoarse = { 0x8000, 0x7EBA, 0x7D74, 0x7C2D, 0x7AE7, 0x79A0, 0x7859, 0x7711, 0x75C9,
                                               0x7480, 0x7337, 0x71EC, 0x70A1, 0x6F55, 0x6E07, 0x6CB8, 0x6B68, 0x6A17,
                                               0x68C4, 0x6770, 0x661A, 0x64C1, 0x6367, 0x620B, 0x60AD, 0x5F4C, 0x5DE9,
                                               0x5C83, 0x5B1A, 0x59AE, 0x583E, 0x56CB, 0x5555, 0x53DB, 0x525C, 0x50D9,
                                               0x4F51, 0x4DC5, 0x4C32, 0x4A9A, 0x48FC, 0x4757, 0x45AB, 0x43F7, 0x423A,
                                               0x4075, 0x3EA5, 0x3CCB, 0x3AE5, 0x38F1, 0x36EF, 0x34DC, 0x32B7, 0x307D,
                                               0x2E2B, 0x2BBD, 0x292E, 0x2678, 0x2391, 0x206C, 0x1CF6, 0 };
constexpr std::array<uint16_t, 64> kFine = {
    0x1CF6, 0x1CBB, 0x1C80, 0x1C45, 0x1C08, 0x1BCC, 0x1B8F, 0x1B51, 0x1B13, 0x1AD4, 0x1A95, 0x1A55, 0x1A14,
    0x19D3, 0x1992, 0x194F, 0x190C, 0x18C9, 0x1884, 0x183F, 0x17F9, 0x17B3, 0x176B, 0x1723, 0x16DA, 0x1690,
    0x1645, 0x15F9, 0x15AC, 0x155E, 0x150F, 0x14BE, 0x146D, 0x141A, 0x13C6, 0x1370, 0x1319, 0x12C1, 0x1267,
    0x120B, 0x11AD, 0x114E, 0x10EC, 0x1088, 0x1022, 0x0FB9, 0x0F4D, 0x0EDE, 0x0E6C, 0x0DF7, 0x0D7D, 0x0D00,
    0x0C7D, 0x0BF4, 0x0B66, 0x0AD0, 0x0A31, 0x0989, 0x08D3, 0x080E, 0x0734, 0x063D, 0x0518, 0x039A
};
constexpr std::array<uint16_t, 6> kFinest = { 0x039A, 0x031E, 0x028C, 0x01CD, 0, 0 };

// The table lookup both arc functions share, for an argument from 0 to 0x7FFF.
int32_t ArcTable(int32_t argument) {
    int32_t mask;
    int32_t shift;
    const uint16_t* table;

    if (argument >= 0x7FE0) {
        mask = 7;
        shift = 3;
        table = kFinest.data();
        argument -= 0x7FE0;
    } else if (argument >= 0x7800) {
        mask = 0x1F;
        shift = 5;
        table = kFine.data();
        argument -= 0x7800;
    } else {
        mask = 0x1FF;
        shift = 9;
        table = kCoarse.data();
    }

    const int32_t first = table[argument >> shift];
    const int32_t second = table[(argument >> shift) + 1];
    return first - (((first - second) * (argument & mask)) >> shift);
}

// The argument as a signed 16-bit fraction of one.
int16_t ToFraction(float value) {
    if (1.0f <= value) {
        return 0x7FFF;
    }
    if (value <= -1.0f) {
        return -0x7FFF;
    }
    return static_cast<int16_t>(value * 32767.0f);
}

// The sine's polynomial in the argument and its square, a half turn split in
// two so the reduction keeps its precision, one over a half turn, and a quarter
// turn: the float words the game stores them as.
constexpr float kSinC1 = std::bit_cast<float>(0x362EDEF8u);
constexpr float kSinC2 = std::bit_cast<float>(0xB94FB7FFu);
constexpr float kSinC3 = std::bit_cast<float>(0x3C08876Au);
constexpr float kSinC4 = std::bit_cast<float>(0xBE2AAAA6u);
constexpr float kPiHigh = std::bit_cast<float>(0x40490FDBu);
constexpr float kPiLow = std::bit_cast<float>(0x330885A3u);
constexpr float kOverPi = std::bit_cast<float>(0x3EA2F983u);
constexpr float kQuarterTurn = std::bit_cast<float>(0x3FC90FDAu);

float SinPolynomial(float x) {
    const float square = x * x;
    float p = kSinC1 * square;
    p = kSinC2 + p;
    p *= square;
    p = kSinC3 + p;
    p *= square;
    p = kSinC4 + p;
    p *= square;
    p *= x;
    return p + x;
}

} // namespace

float GameSinf(float angle) {
    // The magnitude's exponent and top mantissa bit: 0xFF is 1.5, 0xE6 is 2^-12.
    const int32_t magnitude = static_cast<int32_t>(std::bit_cast<uint32_t>(angle) >> 22) & 0x1FF;
    if (magnitude < 0xFF) {
        if (magnitude < 0xE6) {
            return angle;
        }
        return SinPolynomial(angle);
    }
    if (magnitude >= 0x136) {
        return 0.0f;
    }

    const float turns = std::nearbyint(kOverPi * angle);
    float reduced = angle - kPiHigh * turns;
    reduced -= kPiLow * turns;
    float result = SinPolynomial(reduced);
    if ((static_cast<int32_t>(turns) & 1) != 0) {
        result = -result;
    }
    return result;
}

float GameCosf(float angle) {
    return GameSinf(angle + kQuarterTurn);
}

float GameAcosf(float value) {
    const int16_t fraction = ToFraction(value);
    int32_t result = ArcTable(fraction >= 0 ? fraction : -fraction);
    if (fraction < 0) {
        result = 0xFFFF - result;
    }
    return (static_cast<float>(static_cast<uint16_t>(result)) * kPi) / 65535.0f;
}

float GameAsinf(float value) {
    const int16_t fraction = ToFraction(value);
    const int32_t arc = ArcTable(fraction >= 0 ? fraction : -fraction);
    const int32_t result = fraction >= 0 ? 0x7FFF - arc : arc - 0x8000;
    return (static_cast<float>(static_cast<int16_t>(result)) * kPi) / 65535.0f;
}

float GameAtan2f(float y, float x) {
    float angle;
    if (y == 0.0f) {
        angle = 0.0f <= x ? 0.0f : kPi;
    } else if (x == 0.0f) {
        angle = 0.0f < y ? kHalfPi : kThreeHalfPi;
    } else {
        angle = std::sqrt((y * y) + (x * x));
        if (x < y) {
            angle = GameAcosf(x / angle);
            if (y < 0.0f) {
                angle = kTau - angle;
            }
        } else {
            angle = kHalfPi - GameAcosf(y / angle);
            if (x < 0.0f) {
                angle = kPi - angle;
            }
            if (angle < 0.0f) {
                angle += kTau;
            }
        }
    }
    return angle;
}

} // namespace Frigate
