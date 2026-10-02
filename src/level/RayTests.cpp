#include "level/RayTests.h"

#include <cmath>
#include <utility>

namespace Frigate::Level {

bool RayIntersectsBox(const Vec3& origin, const Vec3& dir, const int32_t (&min)[3], const int32_t (&max)[3]) {
    // x
    float f18 = dir[0];
    float f16 = static_cast<float>(max[0]) - origin[0];
    float f14 = static_cast<float>(min[0]) - origin[0];

    if (f18 < 0.0f) {
        f18 = -f18;
        f14 = -f14;
        f16 = -f16;
    }
    if (f14 < 0.0f && f16 < 0.0f) {
        return false;
    }
    if (f16 < f14) {
        std::swap(f14, f16);
    }

    // y
    float f12 = dir[1];
    float f2 = static_cast<float>(max[1]) - origin[1];
    float f0 = static_cast<float>(min[1]) - origin[1];

    if (f12 < 0.0f) {
        f12 = -f12;
        f0 = -f0;
        f2 = -f2;
    }
    if (f0 < 0.0f && f2 < 0.0f) {
        return false;
    }
    if (f2 < f0) {
        std::swap(f0, f2);
    }

    const float f6 = f14 * f12;
    const float f10 = f0 * f18;
    float sp34;
    float sp30;

    if (f10 < f6) {
        if (f2 * f18 < f6) {
            return false;
        }
        sp34 = f14;
        sp30 = f18;
    } else {
        if (f16 * f12 < f10) {
            return false;
        }
        sp34 = f0;
        sp30 = f12;
    }

    float f0_2;
    float f14_2;
    if (f16 * f12 < f2 * f18) {
        f0_2 = f16;
        f14_2 = f18;
    } else {
        f0_2 = f2;
        f14_2 = f12;
    }

    // z
    float f2_2 = dir[2];
    float f12_2 = static_cast<float>(max[2]) - origin[2];
    float f18_2 = static_cast<float>(min[2]) - origin[2];

    if (f2_2 < 0.0f) {
        f2_2 = -f2_2;
        f18_2 = -f18_2;
        f12_2 = -f12_2;
    }
    if (f18_2 < 0.0f && f12_2 < 0.0f) {
        return false;
    }
    if (f12_2 < f18_2) {
        std::swap(f18_2, f12_2);
    }

    if (sp34 * f2_2 < f18_2 * sp30) {
        if (f0_2 * f2_2 < f18_2 * f14_2) {
            return false;
        }
    } else if (f12_2 * sp30 < sp34 * f2_2) {
        return false;
    }
    return true;
}

bool RayIntersectsTriangle(const int16_t (&v0)[3], const int16_t (&v1)[3], const int16_t (&v2)[3], const Vec3& offset,
                           const Vec3& rayStart, const Vec3& linePoint, const Vec3& dir, TriangleHit* hit) {
    double offset64[3];
    double dir64[3];
    double line64[3];
    for (size_t i = 0; i < 3; i++) {
        offset64[i] = offset[i];
        dir64[i] = dir[i];
        line64[i] = linePoint[i];
    }

    // The edges are in the triangle's local space: the shared offset cancels.
    double edge01[3];
    double edge12[3];
    double edge02[3];
    for (size_t i = 0; i < 3; i++) {
        edge01[i] = static_cast<double>(v1[i]) - v0[i];
        edge12[i] = static_cast<double>(v2[i]) - v1[i];
        edge02[i] = static_cast<double>(v2[i]) - v0[i];
    }

    const double normalX = (edge01[1] * edge12[2]) - (edge12[1] * edge01[2]);
    const double normalY = (edge01[2] * edge12[0]) - (edge12[2] * edge01[0]);
    const double normalZ = (edge01[0] * edge12[1]) - (edge12[0] * edge01[1]);

    const double planeConstant =
        ((normalX * (v0[0] + offset64[0])) + (normalY * (v0[1] + offset64[1]))) + (normalZ * (v0[2] + offset64[2]));

    double denominator = ((normalX * dir64[0]) + (normalY * dir64[1])) + (normalZ * dir64[2]);
    if (denominator == 0.0) {
        return false;
    }

    const double lineParameter =
        (((planeConstant - (normalX * line64[0])) - (normalY * line64[1])) - (normalZ * line64[2])) / denominator;

    double hitPosition[3];
    double relative[3];
    for (size_t i = 0; i < 3; i++) {
        hitPosition[i] = line64[i] + (dir64[i] * lineParameter);
        relative[i] = hitPosition[i] - (v0[i] + offset64[i]);
    }

    double baryU;
    denominator = (edge01[1] * edge12[0]) - (edge01[0] * edge12[1]);
    if (denominator != 0.0) {
        baryU = ((relative[0] * edge01[1]) - (relative[1] * edge01[0])) / denominator;
    } else {
        denominator = (edge01[2] * edge12[1]) - (edge01[1] * edge12[2]);
        if (denominator != 0.0) {
            baryU = ((relative[1] * edge01[2]) - (relative[2] * edge01[1])) / denominator;
        } else {
            denominator = (edge01[0] * edge12[2]) - (edge01[2] * edge12[0]);
            baryU = ((relative[2] * edge01[0]) - (relative[0] * edge01[2])) / denominator;
        }
    }

    double baryV;
    if (edge01[0] != 0.0) {
        baryV = (relative[0] - (baryU * edge02[0])) / edge01[0];
    } else if (edge01[1] != 0.0) {
        baryV = (relative[1] - (baryU * edge02[1])) / edge01[1];
    } else {
        baryV = (relative[2] - (baryU * edge02[2])) / edge01[2];
    }

    if (!((baryV >= 0.0) && (baryU >= 0.0) && ((baryV + baryU) <= 1.0))) {
        return false;
    }

    // The line is infinite; the meeting must be ahead of the ray's start.
    const double ahead = ((dir64[0] * (hitPosition[0] - rayStart[0])) + (dir64[1] * (hitPosition[1] - rayStart[1]))) +
                         (dir64[2] * (hitPosition[2] - rayStart[2]));
    if (ahead < 0.0) {
        return false;
    }

    hit->position = { static_cast<float>(hitPosition[0]), static_cast<float>(hitPosition[1]),
                      static_cast<float>(hitPosition[2]) };
    hit->normal = { static_cast<float>(normalX), static_cast<float>(normalY), static_cast<float>(normalZ) };
    return true;
}

bool RayIntersectsSphere(const Vec3& origin, const Vec3& dir, const Vec3& centre, float radius) {
    const Vec3 vector = { centre[0] - origin[0], centre[1] - origin[1], centre[2] - origin[2] };
    const float dist2rd = dir[0] * vector[0] + dir[1] * vector[1] + dir[2] * vector[2];
    if (dist2rd > 0) {
        const float a = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
        const float b = vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2];
        if ((b - radius * radius) * a <= dist2rd * dist2rd) {
            return true;
        }
    }
    return false;
}

bool RayIntersectsTransformedBox(const float (&bounds)[6], const Mtxf& mtx, const Vec3& pos, const Vec3& dir) {
    const float xthingx = mtx.m[0][0] * mtx.m[0][0];
    const float xthingy = mtx.m[0][1] * mtx.m[0][1];
    const float xthingz = mtx.m[0][2] * mtx.m[0][2];

    float negL2 = -((xthingx + xthingy) + xthingz);
    const float xpmin = negL2 * bounds[0];
    const float xpmax = negL2 * bounds[1];

    float xsum1 = ((mtx.m[0][0] * dir[0]) + (mtx.m[0][1] * dir[1])) + (mtx.m[0][2] * dir[2]);
    float xsum2 = ((mtx.m[0][0] * (pos[0] - mtx.m[3][0])) + (mtx.m[0][1] * (pos[1] - mtx.m[3][1]))) +
                  (mtx.m[0][2] * (pos[2] - mtx.m[3][2]));

    float xsum3 = -(xsum2 + xpmax);
    xsum2 = -(xsum2 + xpmin);

    if (xsum1 < 0.0f) {
        xsum1 = -xsum1;
        xsum2 = -xsum2;
        xsum3 = -xsum3;
    }
    if ((xsum2 < 0.0f) && (xsum3 < 0.0f)) {
        return false;
    }
    if (xsum3 < xsum2) {
        std::swap(xsum2, xsum3);
    }

    const float ythingx = mtx.m[1][0] * mtx.m[1][0];
    const float ythingy = mtx.m[1][1] * mtx.m[1][1];
    const float ythingz = mtx.m[1][2] * mtx.m[1][2];

    negL2 = -((ythingx + ythingy) + ythingz);
    const float ypmin = negL2 * bounds[2];
    const float ypmax = negL2 * bounds[3];

    float ysum1 = ((mtx.m[1][0] * dir[0]) + (mtx.m[1][1] * dir[1])) + (mtx.m[1][2] * dir[2]);
    float ysum2 = ((mtx.m[1][0] * (pos[0] - mtx.m[3][0])) + (mtx.m[1][1] * (pos[1] - mtx.m[3][1]))) +
                  (mtx.m[1][2] * (pos[2] - mtx.m[3][2]));

    float ysum3 = -(ysum2 + ypmax);
    ysum2 = -(ysum2 + ypmin);

    if (ysum1 < 0.0f) {
        ysum1 = -ysum1;
        ysum2 = -ysum2;
        ysum3 = -ysum3;
    }
    if ((ysum2 < 0.0f) && (ysum3 < 0.0f)) {
        return false;
    }
    if (ysum3 < ysum2) {
        std::swap(ysum2, ysum3);
    }

    const float mult1 = ysum2 * xsum1;
    const float mult2 = xsum2 * ysum1;
    const float mult3 = xsum3 * ysum1;
    const float mult4 = ysum3 * xsum1;

    float bestsum2;
    float bestsum1;
    if (mult1 < mult2) {
        if (mult4 < mult2) {
            return false;
        }
        bestsum2 = xsum2;
        bestsum1 = xsum1;
    } else {
        if (mult3 < mult1) {
            return false;
        }
        bestsum2 = ysum2;
        bestsum1 = ysum1;
    }

    float anotherbestsum3;
    float anotherbestsum1;
    if (mult3 < mult4) {
        anotherbestsum3 = xsum3;
        anotherbestsum1 = xsum1;
    } else {
        anotherbestsum3 = ysum3;
        anotherbestsum1 = ysum1;
    }

    const float zthingx = mtx.m[2][0] * mtx.m[2][0];
    const float zthingy = mtx.m[2][1] * mtx.m[2][1];
    const float zthingz = mtx.m[2][2] * mtx.m[2][2];

    negL2 = -((zthingx + zthingy) + zthingz);
    const float zpmin = negL2 * bounds[4];
    const float zpmax = negL2 * bounds[5];

    float zsum1 = ((mtx.m[2][0] * dir[0]) + (mtx.m[2][1] * dir[1])) + (mtx.m[2][2] * dir[2]);
    float zsum2 = ((mtx.m[2][0] * (pos[0] - mtx.m[3][0])) + (mtx.m[2][1] * (pos[1] - mtx.m[3][1]))) +
                  (mtx.m[2][2] * (pos[2] - mtx.m[3][2]));

    float zsum3 = -(zsum2 + zpmax);
    zsum2 = -(zsum2 + zpmin);

    if (zsum1 < 0.0f) {
        zsum1 = -zsum1;
        zsum2 = -zsum2;
        zsum3 = -zsum3;
    }
    if ((zsum2 < 0.0f) && (zsum3 < 0.0f)) {
        return false;
    }
    if (zsum3 < zsum2) {
        std::swap(zsum2, zsum3);
    }

    if ((bestsum2 * zsum1) < (zsum2 * bestsum1)) {
        if ((anotherbestsum3 * zsum1) < (zsum2 * anotherbestsum1)) {
            return false;
        }
    } else if ((zsum3 * bestsum1) < (bestsum2 * zsum1)) {
        return false;
    }
    return true;
}

bool RayIntersectsHitSphere(const HitSphere& sphere, const Mtxf& nodeMtx, const Mtxf* other, const Vec3& raypos,
                            const Vec3& raydir) {
    Vec3 dir = raydir;
    Vec3 rel = { 1.0f, 0.0f, 0.0f };
    const uint32_t nodeFlags = sphere.flags;

    if (nodeFlags & 0x100) {
        const Vec3& pos = sphere.centre;
        for (size_t i = 0; i < 3; i++) {
            rel[i] = (((pos[0] * nodeMtx.m[0][i]) + (pos[1] * nodeMtx.m[1][i])) + (pos[2] * nodeMtx.m[2][i])) +
                     (nodeMtx.m[3][i] - raypos[i]);
        }
    } else if (nodeFlags & 0x200) {
        if (other != nullptr) {
            for (size_t i = 0; i < 3; i++) {
                rel[i] = ((nodeMtx.m[3][i] + other->m[3][i]) * 0.5f) - raypos[i];
            }
        } else {
            for (size_t i = 0; i < 3; i++) {
                rel[i] = nodeMtx.m[3][i] - raypos[i];
            }
        }
    } else {
        for (size_t i = 0; i < 3; i++) {
            rel[i] = nodeMtx.m[3][i] - raypos[i];
        }
    }

    if (nodeFlags & 0x400) {
        // The ray and the centre are squashed along the matrix's first axis by
        // scale1, then along its second by scale2.
        float projectionScalar = sphere.scale1;
        const float secondAxisScale = sphere.scale2;
        float directionDotProduct =
            (((dir[0] * nodeMtx.m[0][0]) + (dir[1] * nodeMtx.m[0][1])) + (dir[2] * nodeMtx.m[0][2])) * projectionScalar;
        for (size_t i = 0; i < 3; i++) {
            dir[i] = dir[i] + (nodeMtx.m[0][i] * directionDotProduct);
        }
        const float centerProjection =
            (((rel[0] * nodeMtx.m[0][0]) + (rel[1] * nodeMtx.m[0][1])) + (rel[2] * nodeMtx.m[0][2])) * projectionScalar;
        for (size_t i = 0; i < 3; i++) {
            rel[i] = rel[i] + (nodeMtx.m[0][i] * centerProjection);
        }
        directionDotProduct =
            (((dir[0] * nodeMtx.m[1][0]) + (dir[1] * nodeMtx.m[1][1])) + (dir[2] * nodeMtx.m[1][2])) * secondAxisScale;
        for (size_t i = 0; i < 3; i++) {
            dir[i] = dir[i] + (nodeMtx.m[1][i] * directionDotProduct);
        }
        projectionScalar =
            (((rel[0] * nodeMtx.m[1][0]) + (rel[1] * nodeMtx.m[1][1])) + (rel[2] * nodeMtx.m[1][2])) * secondAxisScale;
        for (size_t i = 0; i < 3; i++) {
            rel[i] = rel[i] + (nodeMtx.m[1][i] * projectionScalar);
        }
    } else if ((nodeFlags & 0x800) || (nodeFlags & 0x1000) || (nodeFlags & 0x2000)) {
        const float scale = sphere.scale1;
        const size_t row = (nodeFlags & 0x800) ? 0 : ((nodeFlags & 0x1000) ? 1 : 2);
        Vec3 axis;
        for (size_t i = 0; i < 3; i++) {
            axis[i] = nodeMtx.m[row][i];
        }
        const float directionDotProduct = scale * ((dir[0] * axis[0]) + (dir[1] * axis[1]) + (dir[2] * axis[2]));
        for (size_t i = 0; i < 3; i++) {
            dir[i] = dir[i] + (axis[i] * directionDotProduct);
        }
        const float scaledProjection = scale * ((rel[0] * axis[0]) + (rel[1] * axis[1]) + (rel[2] * axis[2]));
        for (size_t i = 0; i < 3; i++) {
            rel[i] = rel[i] + (axis[i] * scaledProjection);
        }
    }

    const float projectionScalar = (dir[0] * rel[0]) + (dir[1] * rel[1]) + (dir[2] * rel[2]);
    if (0.0f < projectionScalar) {
        const float directionDotProduct = (dir[0] * dir[0]) + (dir[1] * dir[1]) + (dir[2] * dir[2]);
        const float centerDistanceSq = (rel[0] * rel[0]) + (rel[1] * rel[1]) + (rel[2] * rel[2]);
        return ((centerDistanceSq - sphere.radiusSquared) * directionDotProduct) <=
               (projectionScalar * projectionScalar);
    }
    return false;
}

} // namespace Frigate::Level
