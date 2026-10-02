#include "level/Sky.h"

#include <algorithm>
#include <cmath>

#include <spdlog/fmt/fmt.h>

#include "fast/resource/type/DisplayList.h"
#include "gfx/Gu.h"
#include "gfx/GbiF3dex.h"
#include "model/ModelDraw.h"
#include "model/GameMath.h"
#include "ship/resource/ResourceManager.h"
#include "ship/utils/StrHash64.h"

// Each RDP command is built into a temporary Gfx by the libultra macro, which
// takes the command pointer as its first argument. The macros that expand to
// the RSP's set-other-mode command aren't used: they number it as F3DEX2 does,
// and the indy ucode numbers it as F3DEX (gfx/GbiF3dex.h).
#define EMIT(dl, command)   \
    do {                    \
        Gfx* gfx_ = (dl)++; \
        command;            \
    } while (0)

namespace Frigate::Level {

namespace {

// The polygon corners skyRender names: the view's four corners and the points
// where the horizon crosses its four edges.
enum Point : uint8_t { C0, C1, C2, C3, L, R, T, B };

struct Polygon {
    int count;
    // sp430: the second triangle of the water quad is drawn a line lower.
    bool shiftSecondTriangle;
    std::array<Point, 5> points;
};

// The polygon below the horizon for each combination of corners in the sky
// (bit 3 the top left corner, then the top right, bottom left and bottom
// right), and the polygon above it. A count of 0 draws nothing: all corners in
// the sky leaves no water, and none in the sky no sky. The combinations a
// horizontal horizon cannot make are not listed (skyRender returns for them).
constexpr std::array<Polygon, 16> kWaterPolygons = { {
    { 4, false, { C0, C1, C2, C3 } },   // 0
    { 5, false, { C2, C0, C1, R, B } }, // 1
    { 5, false, { C0, C1, C3, B, L } }, // 2
    { 4, false, { C0, C1, L, R } },     // 3
    { 5, false, { C3, C2, C0, T, R } }, // 4
    { 4, false, { C2, C0, B, T } },     // 5
    { 0, false, {} },                   // 6
    { 3, false, { C0, T, L } },         // 7
    { 5, false, { C1, C3, C2, L, T } }, // 8
    { 0, false, {} },                   // 9
    { 4, false, { C1, C3, T, B } },     // 10
    { 3, false, { C1, R, T } },         // 11
    { 4, true, { C3, C2, R, L } },      // 12
    { 3, false, { C2, L, B } },         // 13
    { 3, false, { C3, B, R } },         // 14
    { 0, false, {} },                   // 15
} };

constexpr std::array<Polygon, 16> kSkyPolygons = { {
    { 0, false, {} },                   // 0
    { 3, false, { C3, B, R } },         // 1
    { 3, false, { C2, L, B } },         // 2
    { 4, false, { C3, C2, R, L } },     // 3
    { 3, false, { C1, R, T } },         // 4
    { 4, false, { C1, C3, T, B } },     // 5
    { 0, false, {} },                   // 6
    { 5, false, { C1, C3, C2, L, T } }, // 7
    { 3, false, { C0, T, L } },         // 8
    { 0, false, {} },                   // 9
    { 4, false, { C2, C0, B, T } },     // 10
    { 5, false, { C3, C2, C0, T, R } }, // 11
    { 4, false, { C0, C1, L, R } },     // 12
    { 5, false, { C0, C1, C3, B, L } }, // 13
    { 5, false, { C2, C0, C1, R, B } }, // 14
    { 4, false, { C0, C1, C2, C3 } },   // 15
} };

// SkyRelated18: a polygon corner before it is projected.
struct SkyCorner {
    float position[3];
    // The texture coordinates, in the RDP's 1/32 texel units.
    float s;
    float t;
    uint8_t r, g, b, a;
};

// SkyRelated38: a projected corner. `x` and `y` are in quarter pixels.
struct SkyVertex {
    float w = 0.0f;
    float s = 0.0f;
    float t = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

// skyClamp.
float SkyClamp(float a, float b, float c) {
    if (a < b) {
        return b;
    }
    if (c < a) {
        return c;
    }
    return a;
}

// skyChooseCloudVtxColour and skyChooseWaterVtxColour: the environment's
// colour, lightened by the plane's tint (`cloudR`, `cloudG`, `cloudB`) the
// further the corner is from the horizon.
void ChooseColour(SkyCorner& corner, const SkyEnvironment& env, float cloudR, float cloudG, float cloudB,
                  float fraction) {
    const auto r = static_cast<float>(env.red);
    const auto g = static_cast<float>(env.green);
    const auto b = static_cast<float>(env.blue);
    corner.r = static_cast<uint8_t>(r + cloudR * (1.0f - r / 255.0f) * (1.0f - fraction));
    corner.g = static_cast<uint8_t>(g + cloudG * (1.0f - g / 255.0f) * (1.0f - fraction));
    corner.b = static_cast<uint8_t>(b + cloudB * (1.0f - b / 255.0f) * (1.0f - fraction));
    corner.a = 0xff;
}

// sub_GAME_7F097388: `corner` through `matrix`, to screen position (quarter
// pixels), clip depth and texture coordinates. `concavity` is the environment's
// WaterConcavity.
SkyVertex Project(const SkyCorner& corner, const Mtxf& matrix, uint16_t scaleFactor, float sScale, float tScale,
                  const SkyView& view, float concavity) {
    const float mult = static_cast<float>(scaleFactor) / 65536.0f;

    float clip[4];
    for (int k = 0; k < 4; k++) {
        clip[k] = (corner.position[0] * matrix.m[0][k] + corner.position[1] * matrix.m[1][k] +
                   corner.position[2] * matrix.m[2][k]) +
                  matrix.m[3][k];
    }

    const float s = corner.s * (sScale / 65536.0f);
    const float t = corner.t * (tScale / 65536.0f);

    float reciprocal;
    if (clip[3] == 0.0f) {
        reciprocal = 32767.0f;
    } else {
        reciprocal = 1.0f / (clip[3] * mult);
    }

    float f0 = reciprocal;
    if (f0 < 0.0f) {
        f0 = 32767.0f;
    }

    float scaled[4];
    for (int k = 0; k < 4; k++) {
        scaled[k] = clip[k] * f0 * mult;
    }

    const auto width = static_cast<float>(view.view.viewWidth);
    const auto height = static_cast<float>(view.view.viewHeight);
    const auto left = static_cast<float>(view.view.viewLeft);
    const auto top = static_cast<float>(view.view.viewTop);

    float x = scaled[0] * (width * 2) + (width * 2 + left * 4);
    float y = -scaled[1] * (height * 2) + (height * 2 + top * 4);

    x = SkyClamp(x, -4090.0f, 4090.0f);
    y = SkyClamp(y, -4090.0f, 4090.0f);

    SkyVertex out;
    out.w = clip[3];
    out.s = s;
    out.t = t;
    out.x = x;
    out.y = y - concavity * 4.0f;
    out.r = corner.r;
    out.g = corner.g;
    out.b = corner.b;
    out.a = corner.a;
    return out;
}

// skyVerticesAreTheSame.
bool SameVertex(const SkyVertex& a, const SkyVertex& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt((dx * dx) + (dy * dy)) < 1.0f;
}

// Draws sky polygons as vertex triangles. Carrying each corner's clip depth in
// its vertex's w follows goldeneye-pc-port's approach (THIRD_PARTY_LICENSES.md).
struct Painter {
    Gfx*& dl;
    const SkyView& view;
    std::deque<std::array<Vtx_t, 3>>& vertices;
    std::deque<Mtx::FixedMtx>& matrices;
    // The scale that keeps clip-space depth in a vertex's 16 bits, shared by
    // every triangle of one polygon so its shared corners agree.
    float depthScale = 1.0f;

    // The texture coordinates of one triangle are kept within a signed 16 bit
    // range by folding them by a whole number of texture periods (64 texels
    // of 32 units each); a triangle spanning more than that is halved.
    static constexpr float kPeriod = 2048.0f;
    static constexpr float kMaxSpan = 30000.0f;

    // Carries the projected `corner` onto the screen as a clip-space vertex.
    Vtx_t MakeVertex(const SkyVertex& v, float foldS, float foldT) const {
        const auto left = static_cast<float>(view.view.viewLeft);
        const auto top = static_cast<float>(view.view.viewTop);
        const float right = left + static_cast<float>(view.view.viewWidth);
        const float bottom = top + static_cast<float>(view.view.viewHeight);
        const float screenX = v.x * 0.25f;
        const float screenY = v.y * 0.25f;
        const float ndcX = 2.0f * ((screenX - left) / (right - left)) - 1.0f;
        const float ndcY = 1.0f - 2.0f * ((screenY - top) / (bottom - top));
        const float depth = v.w / depthScale;

        const auto colour = [](float channel) { return static_cast<uint8_t>(std::clamp(channel, 0.0f, 255.0f)); };
        Vtx_t out{};
        out.ob[0] = static_cast<int16_t>(std::lround(ndcX * depth));
        out.ob[1] = static_cast<int16_t>(std::lround(ndcY * depth));
        out.ob[2] = static_cast<int16_t>(std::lround(depth));
        out.flag = 0;
        out.tc[0] = static_cast<int16_t>(std::lround(v.s - foldS));
        out.tc[1] = static_cast<int16_t>(std::lround(v.t - foldT));
        out.cn[0] = colour(v.r);
        out.cn[1] = colour(v.g);
        out.cn[2] = colour(v.b);
        out.cn[3] = colour(v.a);
        return out;
    }

    static SkyVertex Midpoint(const SkyVertex& a, const SkyVertex& b) {
        // Everything but the screen position is linear in the world, so it
        // halves as is; the screen position is linear in clip space.
        SkyVertex out;
        out.w = (a.w + b.w) * 0.5f;
        out.s = (a.s + b.s) * 0.5f;
        out.t = (a.t + b.t) * 0.5f;
        out.r = (a.r + b.r) * 0.5f;
        out.g = (a.g + b.g) * 0.5f;
        out.b = (a.b + b.b) * 0.5f;
        out.a = (a.a + b.a) * 0.5f;
        out.x = ((a.x * a.w) + (b.x * b.w)) * 0.5f / out.w;
        out.y = ((a.y * a.w) + (b.y * b.w)) * 0.5f / out.w;
        return out;
    }

    // skyRenderTri: a triangle, unless two of its corners are the same pixel
    // or it has no area.
    void Tri(const SkyVertex& a, const SkyVertex& b, const SkyVertex& c) {
        if (SameVertex(a, b) || SameVertex(b, c) || SameVertex(c, a)) {
            return;
        }
        const float dx2 = b.x - a.x;
        const float dy2 = b.y - a.y;
        const float dx3 = c.x - a.x;
        const float dy3 = c.y - a.y;
        if (((dx3 * dy2) - (dx2 * dy3)) / 65536.0f == 0.0f) {
            return;
        }
        Triangle(a, b, c);
    }

    // skyRenderFull: the quad `a`, `b`, `c`, `d` (the view's top edge, then the
    // bottom edge), unless two of its corners are the same pixel.
    void Quad(const SkyVertex& a, const SkyVertex& b, const SkyVertex& c, const SkyVertex& d) {
        if (SameVertex(a, b) || SameVertex(b, c) || SameVertex(c, a) || SameVertex(d, a) || SameVertex(d, b) ||
            SameVertex(d, c)) {
            return;
        }
        Triangle(a, b, d);
        Triangle(a, d, c);
    }

    void Triangle(const SkyVertex& a, const SkyVertex& b, const SkyVertex& c, int depth = 0) {
        const float minS = std::min({ a.s, b.s, c.s });
        const float maxS = std::max({ a.s, b.s, c.s });
        const float minT = std::min({ a.t, b.t, c.t });
        const float maxT = std::max({ a.t, b.t, c.t });
        if (depth < 8 && ((maxS - minS) > kMaxSpan || (maxT - minT) > kMaxSpan)) {
            // Split the longest side.
            const SkyVertex* corner[3] = { &a, &b, &c };
            int split = 0;
            float longest = -1.0f;
            for (int i = 0; i < 3; i++) {
                const SkyVertex& p = *corner[i];
                const SkyVertex& q = *corner[(i + 1) % 3];
                const float length = std::max(std::fabs(p.s - q.s), std::fabs(p.t - q.t));
                if (length > longest) {
                    longest = length;
                    split = i;
                }
            }
            const SkyVertex& p = *corner[split];
            const SkyVertex& q = *corner[(split + 1) % 3];
            const SkyVertex& other = *corner[(split + 2) % 3];
            const SkyVertex middle = Midpoint(p, q);
            Triangle(p, middle, other, depth + 1);
            Triangle(middle, q, other, depth + 1);
            return;
        }

        const float foldS = std::floor(minS / kPeriod) * kPeriod;
        const float foldT = std::floor(minT / kPeriod) * kPeriod;
        auto& block = vertices.emplace_back();
        block[0] = MakeVertex(a, foldS, foldT);
        block[1] = MakeVertex(b, foldS, foldT);
        block[2] = MakeVertex(c, foldS, foldT);

        // clip.x = ob.x * scale, clip.y = ob.y * scale, clip.z = 0 and
        // clip.w = ob.z * scale, which returns each vertex to the screen
        // position it was given while the rasteriser interpolates its texture
        // coordinates with the depth it carries.
        Gu::MatrixF projection = {};
        projection[0][0] = depthScale;
        projection[1][1] = depthScale;
        projection[2][3] = depthScale;
        matrices.push_back(Mtx::ToFixedMtx(projection));

        *dl++ = F3dex::Matrix(&matrices.back(), Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
        *dl++ = F3dex::Vertices(block.data(), 3);
        *dl++ = F3dex::Triangle(0, 1, 2);
    }
};

} // namespace

namespace SkyMath {

Vec3 WorldPosFromScreenPos(const SkyView& view, float offsetX, float offsetY, float waterConcavity) {
    const float inX = static_cast<float>(view.view.viewLeft) + offsetX;
    const float inY = waterConcavity + (offsetY + static_cast<float>(view.view.viewTop));

    // transformAndNormalizeByLength2Dto3D, a hundred units long.
    const float y = (view.halfHeight - (inY - static_cast<float>(view.view.viewTop))) * view.scaleY;
    const float x = ((inX - static_cast<float>(view.view.viewLeft)) - view.halfWidth) * view.scaleX;
    const float z = -1.0f;
    const float norm = 100.0f / std::sqrt((x * x) + (y * y) + (z * z));
    const float ray[3] = { x * norm, y * norm, -1.0f * norm };

    // mtx4RotateVecInPlace.
    Vec3 out{};
    for (int i = 0; i < 3; i++) {
        out[i] =
            view.viewToWorld.m[0][i] * ray[0] + view.viewToWorld.m[1][i] * ray[1] + view.viewToWorld.m[2][i] * ray[2];
    }
    return out;
}

Corner CornerInSky(const Vec3& eye, const Vec3& corner, float cloudHeight) {
    Corner out;
    float f12 = 2.0f * corner[1] / std::sqrt(corner[0] * corner[0] + corner[2] * corner[2] + 0.0001f);
    if (f12 > 1.0f) {
        f12 = 1.0f;
    }
    out.fraction = 1.0f - f12;

    const float rise = corner[1] == 0.0f ? 0.01f : corner[1];
    if (rise > 0.0f) {
        float toPlane = (cloudHeight - eye[1]) / rise;
        const float distance = std::sqrt(corner[0] * corner[0] + corner[2] * corner[2]) * toPlane;
        if (distance > 300000) {
            toPlane *= 300000 / distance;
        }
        out.position = { eye[0] + toPlane * corner[0], eye[1] + toPlane * rise, eye[2] + toPlane * corner[2] };
        out.valid = true;
    }
    return out;
}

Corner CornerInWater(const Vec3& eye, const Vec3& corner, float waterHeight) {
    Corner out;
    float f12 = -2.0f * corner[1] / std::sqrt(corner[0] * corner[0] + corner[2] * corner[2] + 0.0001f);
    if (f12 > 1.0f) {
        f12 = 1.0f;
    }
    out.fraction = 1.0f - f12;

    const float rise = corner[1] == 0.0f ? -0.01f : corner[1];
    if (rise < 0.0f) {
        float toPlane = (waterHeight - eye[1]) / rise;
        const float distance = std::sqrt(corner[0] * corner[0] + corner[2] * corner[2]) * toPlane;
        if (distance > 300000) {
            toPlane *= 300000 / distance;
        }
        out.position = { eye[0] + toPlane * corner[0], eye[1] + toPlane * rise, eye[2] + toPlane * corner[2] };
        out.valid = true;
    }
    return out;
}

Vec3 EdgeVertex(const Vec3& base, const Vec3& ref) {
    const float mult = base[1] / (base[1] - ref[1]);
    return { ((ref[0] - base[0]) * mult) + base[0], 0.0f, ((ref[2] - base[2]) * mult) + base[2] };
}

} // namespace SkyMath

void Sky::Tick(int clockTimer) {
    const auto delta = static_cast<float>(clockTimer);

    // sub_GAME_7F092E50: the water's scroll and the cross-fade phase.
    waterScrollS_ += delta * 0.25f;
    if (waterScrollS_ >= 256.0f) {
        waterScrollS_ -= 256.0f;
    }
    if (waterScrollS_ < 0.0f) {
        waterScrollS_ += 256.0f;
    }

    waterScrollT_ += delta * 0.1f;
    if (waterScrollT_ >= 256.0f) {
        waterScrollT_ -= 256.0f;
    }
    if (waterScrollT_ < 0.0f) {
        waterScrollT_ += 256.0f;
    }

    waterPhase_ += delta * 0.04f;
    // 6.2831802f is not quite equal to a full turn.
    if (waterPhase_ >= 6.2831802f) {
        waterPhase_ -= 6.2831802f;
    }
    if (waterPhase_ < 0.0f) {
        waterPhase_ += 6.2831802f;
    }

    // skyTick.
    cloudOffset_ += delta;
    if (cloudOffset_ > 4096.0f) {
        cloudOffset_ -= 4096.0f;
    }
}

namespace {

// viSetFillColor, in the 16 bit colour mode.
Gfx* SetFillColour(Gfx* dl, int r, int g, int b) {
    EMIT(dl, gDPSetFillColor(gfx_, (GPACK_RGBA5551(r, g, b, 1) << 16) | GPACK_RGBA5551(r, g, b, 1)));
    return dl;
}

// The other-mode words the sky sets, in the indy ucode's numbering.
Gfx CycleType(uint32_t cycle) {
    return F3dex::SetOtherModeH(G_MDSFT_CYCLETYPE, 2, cycle);
}
Gfx RenderMode(uint32_t cycle1, uint32_t cycle2) {
    return F3dex::SetOtherModeL(G_MDSFT_RENDERMODE, 29, cycle1 | cycle2);
}

// The scale that keeps a polygon's clip-space depths in a vertex's 16 bits.
float DepthScale(const SkyVertex* vertices, int count) {
    float maxDepth = 0.0f;
    for (int i = 0; i < count; i++) {
        maxDepth = std::max(maxDepth, std::fabs(vertices[i].w));
    }
    return maxDepth > 30000.0f ? maxDepth / 30000.0f : 1.0f;
}

// What the polygon draws need ahead of their vertices: no lighting, culling or
// fog (the rasteriser the game draws with has none), the smooth-shaded vertex
// colour, no depth test (the sky is drawn first), and an identity modelview.
Gfx* PaintSetup(Gfx* dl, std::deque<Mtx::FixedMtx>& matrices) {
    Gu::MatrixF identity = {};
    for (int i = 0; i < 4; i++) {
        identity[i][i] = 1.0f;
    }
    matrices.push_back(Mtx::ToFixedMtx(identity));
    *dl++ = F3dex::ClearGeometryMode(G_LIGHTING | Fast::F3DEX_G_CULL_BOTH | G_FOG | G_ZBUFFER);
    *dl++ = F3dex::SetGeometryMode(G_SHADE | Fast::F3DEX_G_SHADING_SMOOTH);
    *dl++ = F3dex::Matrix(&matrices.back(), Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    return dl;
}

// The clip-space product every sky polygon is projected with: the projection,
// then the world to screen transform, then the scale that undoes the one the
// polygon's corners were multiplied by.
Mtxf ProjectionFor(const SkyView& view, float scale) {
    const Mtxf projected = Multiply(view.projection, view.worldToScreen);
    Gu::MatrixF scaleMatrix;
    Gu::ScaleF(scaleMatrix, 1.0f / scale, 1.0f / scale, 1.0f / scale);
    Mtxf scaled{};
    std::copy(&scaleMatrix[0][0], &scaleMatrix[0][0] + 16, &scaled.m[0][0]);
    return Multiply(projected, scaled);
}

// The screen area a polygon's corners are clamped to.
void ClampToView(SkyVertex& v, const SkyView& view) {
    const auto left = static_cast<float>(view.view.viewLeft);
    const auto top = static_cast<float>(view.view.viewTop);
    const auto width = static_cast<float>(view.view.viewWidth);
    const auto height = static_cast<float>(view.view.viewHeight);
    v.x = SkyClamp(v.x, left * 4.0f, (left + width) * 4.0f - 1.0f);
    v.y = SkyClamp(v.y, top * 4.0f, (top + height) * 4.0f - 1.0f);
}

} // namespace

bool Sky::SelectImage(Gfx*& dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, FrameData& frame,
                      int index) const {
    // texSetRenderMode for a single cycle without the z-buffer, the texture on,
    // and level of detail off; the combiner texSelect picks by format is
    // replaced by the caller's before anything draws.
    EMIT(dl, gDPPipeSync(gfx_));
    *dl++ = CycleType(G_CYC_1CYCLE);
    *dl++ = RenderMode(G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    *dl++ = F3dex::TextureOn();
    *dl++ = F3dex::SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);

    auto image = Model::CallList(resourceManager, fmt::format("sky/img/{}", index));
    if (!image) {
        return false;
    }
    frame.lists.push_back(image->list);
    for (const Gfx& command : image->commands) {
        *dl++ = command;
    }
    // None of the sky and water images is paletted.
    *dl++ = F3dex::SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    return true;
}

void Sky::UpdateRoomWater(const std::shared_ptr<Ship::ResourceManager>& resourceManager) const {
    // The images whose rooms call a list (the Caverns water and the water in Dam
    // and Complex), and the commands in it that move: the two tile sizes and
    // the primitive colour.
    constexpr std::array<int, 2> kWaterImages = { 1508, 1511 };
    constexpr size_t kFirstTileSize = 2;
    constexpr size_t kSecondTileSize = 3;
    constexpr size_t kPrimColor = 4;
    constexpr uint32_t kTileSizeCommand = 0xF2u << 24;
    constexpr uint32_t kScrollMask = 0xFF;

    const auto scrollS = static_cast<uint32_t>(waterScrollS_);
    const auto scrollT = static_cast<uint32_t>(waterScrollT_);
    const auto fade = static_cast<uint32_t>(GameSinf(waterPhase_) * 127.0f + 128.0f);
    for (const int image : kWaterImages) {
        const auto list = std::dynamic_pointer_cast<Fast::DisplayList>(
            resourceManager->LoadResource(CRC64(fmt::format("bg/water/{}", image).c_str())));
        if (list == nullptr || list->Instructions.size() <= kPrimColor) {
            continue;
        }
        // gsDPSetTileSize's upper left corner is the scroll; the second layer
        // sits at a fixed offset from the first.
        list->Instructions[kFirstTileSize].words.w0 = kTileSizeCommand | ((scrollS & 0xFFF) << 12) | (scrollT & 0xFFF);
        list->Instructions[kSecondTileSize].words.w0 =
            kTileSizeCommand | (((scrollS + 90) & kScrollMask) << 12) | ((scrollT + 150) & kScrollMask);
        list->Instructions[kPrimColor].words.w0 = (list->Instructions[kPrimColor].words.w0 & ~0xFFu) | fade;
    }
}

Gfx* Sky::SetupWater(Gfx* dl) const {
    // The game declares these tiles as 16 bit colour over a 4 word line, which
    // reads the water image as its 8 bit palette indices that its microcode
    // expands to colours as it loads the image; the image is kept as those
    // indices here, so the tiles are declared as what they are.
    EMIT(dl, gDPSetTile(gfx_, G_IM_FMT_CI, G_IM_SIZ_8b, 4, 0, 0, 0, 0, 5, 0, 0, 5, 0));
    EMIT(dl, gDPSetTile(gfx_, G_IM_FMT_CI, G_IM_SIZ_8b, 4, 0, 1, 0, 0, 5, 0, 0, 5, 0));
    EMIT(dl, gDPSetTileSize(gfx_, 0, 0, 0, 0, 0));
    EMIT(dl, gDPSetTileSize(gfx_, 1, 90, 150, 0, 0));
    EMIT(dl,
         gDPSetPrimColor(gfx_, 0, static_cast<int>(GameSinf(waterPhase_) * 127.0f + 128.0f), 0xFF, 0xFF, 0xFF, 0xFF));
    *dl++ = F3dex::SetOtherModeH(G_MDSFT_TEXTDETAIL, 2, G_TD_CLAMP);
    *dl++ = F3dex::SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    EMIT(dl, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,
                               COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0));
    *dl++ = RenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
    *dl++ = F3dex::SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    *dl++ = CycleType(G_CYC_2CYCLE);
    *dl++ = F3dex::SetGeometryMode(Fast::F3DEX_G_CULL_BACK);
    return dl;
}

Gfx* Sky::Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const SkyView& view,
                 const Fog& fog) {
    generation_ ^= 1;
    FrameData& frame = frames_[generation_];
    frame = FrameData{};

    const SkyEnvironment& env = fog.Environment().sky;
    const float scale = view.levelScale / 30.0f;
    const auto left = static_cast<float>(view.view.viewLeft);
    const auto top = static_cast<float>(view.view.viewTop);
    const auto width = static_cast<float>(view.view.viewWidth);
    const auto height = static_cast<float>(view.view.viewHeight);

    if (!env.clouds) {
        if (view.playerCount == 1) {
            *dl++ = CycleType(G_CYC_FILL);
            dl = SetFillColour(dl, env.red, env.green, env.blue);
            EMIT(dl, gDPFillRectangle(gfx_, view.view.viewLeft, view.view.viewTop,
                                      view.view.viewLeft + view.view.viewWidth - 1,
                                      view.view.viewTop + view.view.viewHeight - 1));
            EMIT(dl, gDPPipeSync(gfx_));
            return dl;
        }

        EMIT(dl, gDPPipeSync(gfx_));
        *dl++ = CycleType(G_CYC_FILL);
        *dl++ = RenderMode(G_RM_NOOP, G_RM_NOOP2);
        EMIT(dl,
             gDPFillRectangle(gfx_, view.view.viewLeft, view.view.viewTop, view.view.viewLeft + view.view.viewWidth - 1,
                              view.view.viewTop + view.view.viewHeight - 1));
        EMIT(dl, gDPPipeSync(gfx_));
        return dl;
    }

    dl = SetFillColour(dl, env.red, env.green, env.blue);

    // The rays through the view's four corners.
    const std::array<Vec3, 4> ray = {
        SkyMath::WorldPosFromScreenPos(view, 0.0f, 0.0f, env.waterConcavity),
        SkyMath::WorldPosFromScreenPos(view, width - 0.1f, 0.0f, env.waterConcavity),
        SkyMath::WorldPosFromScreenPos(view, 0.0f, height - 0.1f, env.waterConcavity),
        SkyMath::WorldPosFromScreenPos(view, width - 0.1f, height - 0.1f, env.waterConcavity),
    };

    // Where each ray meets the cloud plane and the sea plane, indexed by Point
    // (the last four being the horizon's crossings of the view's sides).
    std::array<SkyMath::Corner, 8> sky;
    std::array<SkyMath::Corner, 8> water;
    for (int i = 0; i < 4; i++) {
        sky[static_cast<size_t>(i)] = SkyMath::CornerInSky(view.eye, ray[static_cast<size_t>(i)], env.cloudRepeat);
        water[static_cast<size_t>(i)] = SkyMath::CornerInWater(view.eye, ray[static_cast<size_t>(i)], env.waterRepeat);
    }
    const bool inSky0 = sky[C0].valid;
    const bool inSky1 = sky[C1].valid;
    const bool inSky2 = sky[C2].valid;
    const bool inSky3 = sky[C3].valid;

    // A crossing of the horizon between two corners' rays: the ray to where the
    // segment meets y = 0, and the planes found along it.
    const auto crossing = [&](Point point, const Vec3& from, const Vec3& to) {
        const Vec3 edge = SkyMath::EdgeVertex(from, to);
        sky[point] = SkyMath::CornerInSky(view.eye, edge, env.cloudRepeat);
        water[point] = SkyMath::CornerInWater(view.eye, edge, env.waterRepeat);
    };

    // sp54c and sp548: the rows the horizon crosses the left and right sides at.
    float leftRow = 0.0f;
    float rightRow = 0.0f;
    if (inSky0 != inSky2) {
        leftRow = top + height * (ray[C0][1] / (ray[C0][1] - ray[C2][1]));
        crossing(L, ray[C0], ray[C2]);
    }
    if (inSky1 != inSky3) {
        rightRow = top + height * (ray[C1][1] / (ray[C1][1] - ray[C3][1]));
        crossing(R, ray[C1], ray[C3]);
    }
    if (inSky0 != inSky1) {
        crossing(T, ray[C0], ray[C1]);
    }
    if (inSky2 != inSky3) {
        crossing(B, ray[C2], ray[C3]);
    }

    const int mask = (inSky0 << 3) | (inSky1 << 2) | (inSky2 << 1) | static_cast<int>(inSky3);
    if (mask == 6 || mask == 9) {
        return dl;
    }

    const Mtxf combined = ProjectionFor(view, scale);
    Painter painter{ dl, view, frame.vertices, frame.matrices };

    // A polygon's corners, from the plane they lie on.
    const auto corners = [&](const Polygon& polygon, const std::array<SkyMath::Corner, 8>& plane, bool cloud) {
        std::array<SkyCorner, 5> out{};
        for (int i = 0; i < polygon.count; i++) {
            const SkyMath::Corner& corner = plane[polygon.points[static_cast<size_t>(i)]];
            SkyCorner& vertex = out[static_cast<size_t>(i)];
            for (int axis = 0; axis < 3; axis++) {
                vertex.position[axis] = corner.position[axis] * scale;
            }
            if (cloud) {
                vertex.s = corner.position[0] * 0.1f;
                vertex.t = corner.position[2] * 0.1f + cloudOffset_;
                ChooseColour(vertex, env, env.cloudRed, env.cloudGreen, env.cloudBlue, corner.fraction);
            } else {
                vertex.s = corner.position[0];
                vertex.t = corner.position[2] + cloudOffset_;
                ChooseColour(vertex, env, env.waterRed, env.waterGreen, env.waterBlue, corner.fraction);
            }
        }
        return out;
    };

    // The polygon below the horizon.
    const Polygon& waterPolygon = kWaterPolygons[static_cast<size_t>(mask)];
    if (waterPolygon.count > 0) {
        const std::array<SkyCorner, 5> waterCorners = corners(waterPolygon, water, /*cloud=*/false);
        std::array<SkyVertex, 5> vertex{};
        for (int i = 0; i < waterPolygon.count; i++) {
            SkyVertex& v = vertex[static_cast<size_t>(i)];
            v = Project(waterCorners[static_cast<size_t>(i)], combined, 130, 65535.0f, 65535.0f, view,
                        env.waterConcavity);
            ClampToView(v, view);
            if (v.y > top * 4.0f + 4.0f && v.y < (top + height) * 4.0f - 4.0f) {
                v.y -= 4.0f;
            }
        }

        if (!env.isWater) {
            float minX = 1279.0f;
            float maxX = 0.0f;
            float minY = 959.0f;
            float maxY = 0.0f;
            for (int j = 0; j < waterPolygon.count; j++) {
                const SkyVertex& v = vertex[static_cast<size_t>(j)];
                minX = std::min(minX, v.x);
                maxX = std::max(maxX, v.x);
                minY = std::min(minY, v.y);
                maxY = std::max(maxY, v.y);
            }
            EMIT(dl, gDPPipeSync(gfx_));
            *dl++ = CycleType(G_CYC_FILL);
            *dl++ = RenderMode(G_RM_NOOP, G_RM_NOOP2);
            *dl++ = F3dex::SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);
            EMIT(dl, gDPFillRectangle(gfx_, static_cast<int>(minX * 0.25f), static_cast<int>(minY * 0.25f),
                                      static_cast<int>(maxX * 0.25f), static_cast<int>(maxY * 0.25f)));
            EMIT(dl, gDPPipeSync(gfx_));
            *dl++ = F3dex::SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
        } else {
            EMIT(dl, gDPPipeSync(gfx_));
            if (!SelectImage(dl, resourceManager, frame, env.waterImageId)) {
                return dl;
            }
            dl = SetupWater(dl);
            *dl++ = RenderMode(G_RM_OPA_SURF, G_RM_OPA_SURF2);
            dl = PaintSetup(dl, frame.matrices);
            painter.depthScale = DepthScale(vertex.data(), waterPolygon.count);
            if (waterPolygon.count == 4) {
                painter.Tri(vertex[0], vertex[1], vertex[3]);
                if (waterPolygon.shiftSecondTriangle) {
                    for (int i = 0; i < 4; i++) {
                        vertex[static_cast<size_t>(i)].y++;
                    }
                }
                painter.Tri(vertex[3], vertex[2], vertex[0]);
            } else if (waterPolygon.count == 5) {
                painter.Tri(vertex[0], vertex[1], vertex[2]);
                painter.Tri(vertex[0], vertex[2], vertex[3]);
                painter.Tri(vertex[0], vertex[3], vertex[4]);
            } else if (waterPolygon.count == 3) {
                painter.Tri(vertex[0], vertex[1], vertex[2]);
            }
        }
    }

    // The polygon above it.
    const Polygon& skyPolygon = kSkyPolygons[static_cast<size_t>(mask)];
    if (skyPolygon.count == 0) {
        return dl;
    }

    EMIT(dl, gDPPipeSync(gfx_));
    if (!SelectImage(dl, resourceManager, frame, env.skyImageId)) {
        return dl;
    }
    EMIT(dl, gDPSetEnvColor(gfx_, env.red, env.green, env.blue, 0xff));
    EMIT(dl, gDPSetCombineLERP(gfx_, SHADE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, SHADE, SHADE, ENVIRONMENT,
                               TEXEL0, ENVIRONMENT, 0, 0, 0, SHADE));

    const std::array<SkyCorner, 5> skyCorners = corners(skyPolygon, sky, /*cloud=*/true);
    std::array<SkyVertex, 5> vertex{};
    for (int i = 0; i < skyPolygon.count; i++) {
        SkyVertex& v = vertex[static_cast<size_t>(i)];
        v = Project(skyCorners[static_cast<size_t>(i)], combined, 130, 65535.0f, 65535.0f, view, env.waterConcavity);
        ClampToView(v, view);
    }

    dl = PaintSetup(dl, frame.matrices);
    painter.depthScale = DepthScale(vertex.data(), skyPolygon.count);
    const float right = (left + width) * 4.0f - 1.0f;
    if (skyPolygon.count == 4) {
        if (mask == 12) {
            // The horizon crosses both sides: the sky is a quad that spans the
            // view's width.
            const auto spanView = [&]() {
                vertex[0].x = left * 4.0f;
                vertex[0].y = top * 4.0f;
                vertex[1].x = right;
                vertex[1].y = top * 4.0f;
                vertex[2].x = left * 4.0f;
                vertex[3].x = right;
            };
            if (rightRow < leftRow) {
                if (vertex[3].y >= vertex[1].y + 4.0f) {
                    spanView();
                    painter.Quad(vertex[0], vertex[1], vertex[2], vertex[3]);
                } else {
                    painter.Tri(vertex[0], vertex[1], vertex[2]);
                }
            } else if (vertex[2].y >= vertex[0].y + 4.0f) {
                spanView();
                painter.Quad(vertex[1], vertex[0], vertex[3], vertex[2]);
            } else {
                painter.Tri(vertex[1], vertex[0], vertex[3]);
            }
        } else {
            painter.Tri(vertex[0], vertex[1], vertex[3]);
            painter.Tri(vertex[3], vertex[2], vertex[0]);
        }
    } else if (skyPolygon.count == 5) {
        painter.Tri(vertex[0], vertex[1], vertex[2]);
        painter.Tri(vertex[0], vertex[2], vertex[3]);
        painter.Tri(vertex[0], vertex[3], vertex[4]);
    } else if (skyPolygon.count == 3) {
        painter.Tri(vertex[0], vertex[1], vertex[2]);
    }

    return dl;
}

} // namespace Frigate::Level
