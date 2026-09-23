#include "gfx/Gu.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frigate::Gu {

namespace {

struct Vec3 {
    float x, y, z;
};

Vec3 Cross(const Vec3& a, const Vec3& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

Vec3 Normalize(const Vec3& v) {
    const float inv = 1.0f / std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return { v.x * inv, v.y * inv, v.z * inv };
}

// A unit-length component as the signed 8-bit direction a Light stores
// (1.0 saturates to 127).
int8_t ToDirByte(float unit) {
    return static_cast<int8_t>(static_cast<int>(std::min(unit * 128.0f, 127.0f)) & 0xff);
}

void SetDir(Light& light, const Vec3& dir) {
    light.l.dir[0] = ToDirByte(dir.x);
    light.l.dir[1] = ToDirByte(dir.y);
    light.l.dir[2] = ToDirByte(dir.z);
}

void Identity(Gu::MatrixF& mf) {
    std::memset(mf, 0, sizeof(mf));
    for (int i = 0; i < 4; i++) {
        mf[i][i] = 1.0f;
    }
}

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

struct ViewBasis {
    Vec3 right;
    Vec3 up;
    Vec3 look;
};

// The eye's basis. `look` points from the target back toward the eye (+Z is
// behind the viewer).
ViewBasis MakeViewBasis(float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp,
                        float zUp) {
    ViewBasis basis;
    basis.look = Normalize({ xEye - xAt, yEye - yAt, zEye - zAt });
    basis.right = Normalize(Cross({ xUp, yUp, zUp }, basis.look));
    basis.up = Normalize(Cross(basis.look, basis.right));
    return basis;
}

} // namespace

void LookAtF(MatrixF& mf, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp,
             float zUp) {
    const ViewBasis b = MakeViewBasis(xEye, yEye, zEye, xAt, yAt, zAt, xUp, yUp, zUp);
    Identity(mf);
    const Vec3 eye = { xEye, yEye, zEye };
    const Vec3* axes[3] = { &b.right, &b.up, &b.look };
    for (int col = 0; col < 3; col++) {
        mf[0][col] = axes[col]->x;
        mf[1][col] = axes[col]->y;
        mf[2][col] = axes[col]->z;
        mf[3][col] = -(eye.x * axes[col]->x + eye.y * axes[col]->y + eye.z * axes[col]->z);
    }
}

void PerspectiveF(MatrixF& mf, float fovyDegrees, float aspect, float near, float far, float scale) {
    const float halfAngle = fovyDegrees * kDegreesToRadians / 2.0f;
    const float cot = std::cos(halfAngle) / std::sin(halfAngle);
    std::memset(mf, 0, sizeof(mf));
    mf[0][0] = cot / aspect;
    mf[1][1] = cot;
    mf[2][2] = (near + far) / (near - far);
    mf[2][3] = -1.0f;
    mf[3][2] = 2.0f * near * far / (near - far);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            mf[i][j] *= scale;
        }
    }
}

void RotateF(MatrixF& mf, float angleDegrees, float x, float y, float z) {
    const Vec3 axis = Normalize({ x, y, z });
    const float angle = angleDegrees * kDegreesToRadians;
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    const float oneMinusC = 1.0f - c;

    Identity(mf);
    mf[0][0] = axis.x * axis.x * oneMinusC + c;
    mf[1][1] = axis.y * axis.y * oneMinusC + c;
    mf[2][2] = axis.z * axis.z * oneMinusC + c;

    mf[0][1] = axis.x * axis.y * oneMinusC + axis.z * s;
    mf[1][0] = axis.x * axis.y * oneMinusC - axis.z * s;
    mf[2][0] = axis.z * axis.x * oneMinusC + axis.y * s;
    mf[0][2] = axis.z * axis.x * oneMinusC - axis.y * s;
    mf[1][2] = axis.y * axis.z * oneMinusC + axis.x * s;
    mf[2][1] = axis.y * axis.z * oneMinusC - axis.x * s;
}

void TranslateF(MatrixF& mf, float x, float y, float z) {
    Identity(mf);
    mf[3][0] = x;
    mf[3][1] = y;
    mf[3][2] = z;
}

void ScaleF(MatrixF& mf, float x, float y, float z) {
    Identity(mf);
    mf[0][0] = x;
    mf[1][1] = y;
    mf[2][2] = z;
}

void OrthoF(MatrixF& mf, float l, float r, float b, float t, float n, float f, float scale) {
    Identity(mf);
    mf[0][0] = 2.0f / (r - l);
    mf[1][1] = 2.0f / (t - b);
    mf[2][2] = -2.0f / (f - n);
    mf[3][0] = -(r + l) / (r - l);
    mf[3][1] = -(t + b) / (t - b);
    mf[3][2] = -(f + n) / (f - n);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            mf[i][j] *= scale;
        }
    }
}

void LookAtReflect(LookAt& lookAt, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp,
                   float yUp, float zUp) {
    const ViewBasis basis = MakeViewBasis(xEye, yEye, zEye, xAt, yAt, zAt, xUp, yUp, zUp);
    const Vec3& right = basis.right;
    const Vec3& up = basis.up;

    SetDir(lookAt.l[0], right);
    SetDir(lookAt.l[1], up);

    for (int i = 0; i < 3; i++) {
        lookAt.l[0].l.col[i] = 0;
        lookAt.l[0].l.colc[i] = 0;
    }
    lookAt.l[0].l.pad1 = 0;
    lookAt.l[0].l.pad2 = 0;

    // The Y light carries a 0x80 green component; X's is zero.
    const uint8_t yLightColor[3] = { 0x00, 0x80, 0x00 };
    for (int i = 0; i < 3; i++) {
        lookAt.l[1].l.col[i] = yLightColor[i];
        lookAt.l[1].l.colc[i] = yLightColor[i];
    }
    lookAt.l[1].l.pad1 = 0;
    lookAt.l[1].l.pad2 = 0;
}

} // namespace Frigate::Gu
