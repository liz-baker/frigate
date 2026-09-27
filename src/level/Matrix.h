#pragma once

#include <cmath>

#include "level/BgHeader.h"

namespace Frigate::Level {

// The 4x4 matrices of matrixmath.c: `m[row][column]` in the game's
// row-vector convention, translation in row 3, composed in the game's
// multiplication order.
struct Mtxf {
    float m[4][4];
};

// matrix_4x4_multiply: `rhs` followed by `lhs`. matrix_4x4_multiply_in_place
// is `rhs = Multiply(lhs, rhs)`.
inline Mtxf Multiply(const Mtxf& lhs, const Mtxf& rhs) {
    Mtxf result{};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[j][i] = lhs.m[0][i] * rhs.m[j][0] + lhs.m[1][i] * rhs.m[j][1] + lhs.m[2][i] * rhs.m[j][2] +
                             lhs.m[3][i] * rhs.m[j][3];
        }
    }
    return result;
}

inline Mtxf RotationX(float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return { { { 1, 0, 0, 0 }, { 0, c, s, 0 }, { 0, -s, c, 0 }, { 0, 0, 0, 1 } } };
}

inline Mtxf RotationY(float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return { { { c, 0, -s, 0 }, { 0, 1, 0, 0 }, { s, 0, c, 0 }, { 0, 0, 0, 1 } } };
}

inline Mtxf RotationZ(float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return { { { c, s, 0, 0 }, { -s, c, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
}

// matrix_4x4_set_basis_and_position_target at the origin: `target` is the
// direction the basis points away from.
inline Mtxf BasisToward(const Vec3& target, const Vec3& upIn) {
    float b[3];
    const float normBasis = -1.0f / std::sqrt(target[0] * target[0] + target[1] * target[1] + target[2] * target[2]);
    for (int i = 0; i < 3; i++) {
        b[i] = target[i] * normBasis;
    }
    float r[3] = { upIn[1] * b[2] - upIn[2] * b[1], upIn[2] * b[0] - upIn[0] * b[2], upIn[0] * b[1] - upIn[1] * b[0] };
    const float normRight = 1.0f / std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (float& v : r) {
        v *= normRight;
    }
    float u[3] = { b[1] * r[2] - b[2] * r[1], b[2] * r[0] - b[0] * r[2], b[0] * r[1] - b[1] * r[0] };
    const float normUp = 1.0f / std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (float& v : u) {
        v *= normUp;
    }
    Mtxf out{};
    for (int i = 0; i < 3; i++) {
        out.m[0][i] = r[i];
        out.m[1][i] = u[i];
        out.m[2][i] = b[i];
    }
    out.m[3][3] = 1.0f;
    return out;
}

// matrix_scalar_multiply: scales the upper three rows.
inline void ScaleRows(Mtxf* matrix, float scalar) {
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) {
            matrix->m[row][column] *= scalar;
        }
    }
}

// matrix_column_1/2/3_scalar_multiply(_2): scales the first three entries of
// one row.
inline void ScaleRowBasis(Mtxf* matrix, int row, float scalar) {
    for (int i = 0; i < 3; i++) {
        matrix->m[row][i] *= scalar;
    }
}

} // namespace Frigate::Level
