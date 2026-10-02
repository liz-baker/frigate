#include "level/EffectsDraw.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "gfx/GbiF3dex.h"
#include "level/Frustum.h"
#include "level/Matrix.h"
#include "level/WeaponTable.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Level {

namespace {

// ZBUF_SURFACE.
constexpr int kZbufSurface = 1;

int16_t Short(float value) {
    return static_cast<int16_t>(static_cast<int32_t>(value));
}

Vtx_t ToVtx(const EffectVertex& vertex) {
    Vtx_t out{};
    for (size_t axis = 0; axis < 3; axis++) {
        out.ob[axis] = vertex.position[axis];
    }
    out.flag = 0;
    out.tc[0] = vertex.texture[0];
    out.tc[1] = vertex.texture[1];
    for (size_t i = 0; i < 4; i++) {
        out.cn[i] = vertex.colour[i];
    }
    return out;
}

Mtxf FromGu(const Gu::MatrixF& matrix) {
    Mtxf out;
    std::memcpy(out.m, matrix, sizeof(out.m));
    return out;
}

// The 2D triangles of a quad: gSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0).
Gfx* QuadTriangles(Gfx* dl) {
    *dl++ = F3dex::Triangle(0, 1, 2);
    *dl++ = F3dex::Triangle(0, 2, 3);
    return dl;
}

} // namespace

void EffectsDraw::BeginFrame(const EffectsFrame& frame) {
    frame_ = frame;
    generation_ ^= 1;
    stores_[generation_] = Store{};
    roomMatrices_.clear();
    smokeScale_ = nullptr;
}

Vtx_t* EffectsDraw::Allocate(size_t count) {
    Store& store = stores_[generation_];
    store.vertices.emplace_back(count);
    return store.vertices.back().data();
}

const Mtx::FixedMtx* EffectsDraw::Keep(const float (&matrix)[4][4]) {
    Store& store = stores_[generation_];
    store.matrices.push_back(Mtx::ToFixedMtx(matrix));
    return &store.matrices.back();
}

Gfx* EffectsDraw::ApplyRoomMatrix(Gfx* dl, int room) {
    auto found = roomMatrices_.find(room);
    if (found == roomMatrices_.end()) {
        float matrix[4][4];
        RoomTransform(level_.RoomEntry(room).position, 1.0f / level_.Level().scale, frame_.camera->ModelPosition(),
                      matrix);
        found = roomMatrices_.emplace(room, Keep(matrix)).first;
    }
    *dl++ = F3dex::Matrix(found->second, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    return dl;
}

Gfx* EffectsDraw::SelectImage(Gfx* dl, int32_t offset, int mode, int zbufferMode) const {
    using namespace Frigate::F3dex;
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    // texSetRenderMode, for one cycle.
    uint32_t renderMode;
    switch (mode) {
        case 2:
            if (zbufferMode >= 2) {
                renderMode = G_RM_AA_ZB_XLU_DECAL | G_RM_AA_ZB_XLU_DECAL2;
            } else if (zbufferMode != 0) {
                renderMode = G_RM_AA_ZB_XLU_SURF | G_RM_AA_ZB_XLU_SURF2;
            } else {
                renderMode = G_RM_AA_XLU_SURF | G_RM_AA_XLU_SURF2;
            }
            break;
        case 3:
            renderMode = zbufferMode != 0 ? (G_RM_AA_ZB_TEX_EDGE | G_RM_AA_ZB_TEX_EDGE2)
                                          : (G_RM_AA_TEX_EDGE | G_RM_AA_TEX_EDGE2);
            break;
        case 4:
            renderMode = zbufferMode != 0 ? (G_RM_ZB_CLD_SURF | G_RM_ZB_CLD_SURF2) : (G_RM_CLD_SURF | G_RM_CLD_SURF2);
            break;
        default:
            if (zbufferMode >= 2) {
                renderMode = G_RM_AA_ZB_OPA_DECAL | G_RM_AA_ZB_OPA_DECAL2;
            } else if (zbufferMode != 0) {
                renderMode = G_RM_AA_ZB_OPA_SURF | G_RM_AA_ZB_OPA_SURF2;
            } else {
                renderMode = G_RM_AA_OPA_SURF | G_RM_AA_OPA_SURF2;
            }
            break;
    }
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, renderMode);
    *dl++ = Texture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, 1);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    gDPSetCombineMode(dl++, G_CC_MODULATEIA, G_CC_MODULATEIA);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    char path[32];
    std::snprintf(path, sizeof(path), "hud/fx/%04x", static_cast<unsigned>(offset));
    return DisplayListByHash(dl, CRC64(path));
}

Gfx* EffectsDraw::Quad(Gfx* dl, const std::array<EffectVertex, 4>& vertices) {
    Vtx_t* out = Allocate(4);
    for (size_t i = 0; i < 4; i++) {
        out[i] = ToVtx(vertices[i]);
    }
    *dl++ = F3dex::Vertices(out, 4, 0);
    return QuadTriangles(dl);
}

Gfx* EffectsDraw::Sparks(Gfx* dl) {
    const Mtxf& viewToWorld = frame_.camera->ViewToWorld();
    const float scale = level_.Level().scale;

    for (const BulletSpark& spark : sparks_.Slots()) {
        if (spark.lifetime <= 0 || spark.age < 0) {
            continue;
        }
        if (!PositionOnScreen(frame_.camera->Frustum(), spark.position, spark.radius)) {
            continue;
        }

        int base = EffectImage::kSmoke;
        switch (spark.images) {
            case SparkImages::Flare:
                base = EffectImage::kFlare2;
                break;
            case SparkImages::Smoke:
                base = EffectImage::kSmoke;
                break;
            case SparkImages::Scattered:
                base = EffectImage::kScattered;
                break;
        }
        const int frame = static_cast<int32_t>(static_cast<float>(spark.age) * spark.frameRate);
        const int offset = base + (frame * EffectImage::kEntrySize);
        const EffectImageSize image = images_.Size(offset);

        const int room = spark.room;
        const Vec3& roomPosition = level_.RoomEntry(room).position;
        const float x = spark.position[0];
        const float y = spark.position[1];
        const float z = spark.position[2];

        const float s0[3] = { viewToWorld.m[0][0] * spark.right, viewToWorld.m[0][1] * spark.right,
                              viewToWorld.m[0][2] * spark.right };
        const float s1[3] = { viewToWorld.m[0][0] * spark.up, viewToWorld.m[0][1] * spark.up,
                              viewToWorld.m[0][2] * spark.up };
        const float s2[3] = { viewToWorld.m[1][0] * spark.right, viewToWorld.m[1][1] * spark.right,
                              viewToWorld.m[1][2] * spark.right };
        const float s3[3] = { viewToWorld.m[1][0] * spark.up, viewToWorld.m[1][1] * spark.up,
                              viewToWorld.m[1][2] * spark.up };

        Vtx_t* vertices = Allocate(4);
        const float position[4][3] = {
            { (((x - s0[0]) - s3[0]) * scale) - roomPosition[0], (((y - s0[1]) - s3[1]) * scale) - roomPosition[1],
              (((z - s0[2]) - s3[2]) * scale) - roomPosition[2] },
            { (((x + s1[0]) - s2[0]) * scale) - roomPosition[0], (((y + s1[1]) - s2[1]) * scale) - roomPosition[1],
              (((z + s1[2]) - s2[2]) * scale) - roomPosition[2] },
            { (((x + s0[0]) + s3[0]) * scale) - roomPosition[0], (((y + s0[1]) + s3[1]) * scale) - roomPosition[1],
              (((z + s0[2]) + s3[2]) * scale) - roomPosition[2] },
            { (((x - s1[0]) + s2[0]) * scale) - roomPosition[0], (((y - s1[1]) + s2[1]) * scale) - roomPosition[1],
              (((z - s1[2]) + s2[2]) * scale) - roomPosition[2] },
        };
        const int16_t s[4] = { static_cast<int16_t>(image.width << 5), 0, 0, static_cast<int16_t>(image.width << 5) };
        const int16_t t[4] = { 0, 0, static_cast<int16_t>(image.height << 5), static_cast<int16_t>(image.height << 5) };
        for (size_t i = 0; i < 4; i++) {
            vertices[i] = Vtx_t{};
            for (size_t axis = 0; axis < 3; axis++) {
                vertices[i].ob[axis] = Short(position[i][axis]);
            }
            vertices[i].tc[0] = s[i];
            vertices[i].tc[1] = t[i];
            for (size_t k = 0; k < 4; k++) {
                vertices[i].cn[k] = spark.colour[k];
            }
        }

        *dl++ = F3dex::SetGeometryMode(Fast::F3DEX_G_CULL_BACK);
        *dl++ = F3dex::Matrix(frame_.view->worldProjection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
        dl = ApplyRoomMatrix(dl, room);
        dl = SelectImage(dl, offset, 4, kZbufSurface);
        *dl++ = F3dex::Vertices(vertices, 4, 0);
        dl = QuadTriangles(dl);
        *dl++ = F3dex::Matrix(frame_.view->propProjection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    }
    return dl;
}

Gfx* EffectsDraw::Beam(Gfx* dl, const BeamRecord& beam, int zbufferMode, const BeamRandomFn& random) {
    if (beam.state < 0) {
        return dl;
    }
    using namespace Frigate::F3dex;

    const Vec3 playerPosition = frame_.camera->WorldPosition();
    const Mtxf worldToScreen = FromGu(frame_.camera->WorldToScreen());

    uint8_t colour[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    float startOffset = beam.tail;
    float dist = beam.drawLength;
    Vec3 flareOffset = { 0.0f, 0.0f, 0.0f };
    Vec3 extraOrigin = { 0.0f, 0.0f, 0.0f };
    float extraScale = 1.4142f;
    int32_t imageOffset = EffectImage::kFlare3;
    float radius;

    if (beam.item == kItemLaser) {
        radius = 50.0f;
        imageOffset = EffectImage::kFlare4;
    } else if (beam.item == kItemWatchLaser) {
        radius = 10.0f;
        imageOffset = EffectImage::kFlare4;
        colour[3] = static_cast<uint8_t>((random() % 50U) + 150U);
        if ((random() % 5U) == 0) {
            colour[0] = colour[1] = static_cast<uint8_t>(255U - (random() % 100U));
        }
    } else {
        radius = 30.0f;
    }

    Vec3 pos = beam.position;
    if (startOffset > 0.0f) {
        pos[0] += startOffset * beam.delta[0];
        pos[1] += startOffset * beam.delta[1];
        pos[2] += startOffset * beam.delta[2];
    } else {
        dist += startOffset;
        startOffset = 0.0f;
    }

    if (beam.length < (startOffset + dist)) {
        dist = beam.length - startOffset;
    }

    Vec3 right;
    right[0] = (beam.delta[1] * (playerPosition[2] - (pos[2] + (dist * beam.delta[2])))) -
               (beam.delta[2] * (playerPosition[1] - (pos[1] + (dist * beam.delta[1]))));
    right[1] = (beam.delta[2] * (playerPosition[0] - (pos[0] + (dist * beam.delta[0])))) -
               (beam.delta[0] * (playerPosition[2] - (pos[2] + (dist * beam.delta[2]))));
    right[2] = (beam.delta[0] * (playerPosition[1] - (pos[1] + (dist * beam.delta[1])))) -
               (beam.delta[1] * (playerPosition[0] - (pos[0] + (dist * beam.delta[0]))));

    // guNormalize.
    const auto normalize = [](Vec3* v) {
        const float length = std::sqrt(((*v)[0] * (*v)[0]) + ((*v)[1] * (*v)[1]) + ((*v)[2] * (*v)[2]));
        if (length != 0.0f) {
            (*v)[0] /= length;
            (*v)[1] /= length;
            (*v)[2] /= length;
        }
    };

    if (right[0] != 0.0f || right[1] != 0.0f || right[2] != 0.0f) {
        normalize(&right);
        right[0] *= radius;
        right[1] *= radius;
        right[2] *= radius;
    } else {
        right = { 0.0f, radius, 0.0f };
    }

    Vec3 up = { (beam.delta[1] * right[2]) - (beam.delta[2] * right[1]),
                (beam.delta[2] * right[0]) - (beam.delta[0] * right[2]),
                (beam.delta[0] * right[1]) - (beam.delta[1] * right[0]) };
    normalize(&up);
    up[0] *= radius;
    up[1] *= radius;
    up[2] *= radius;

    const size_t count = beam.item == kItemLaser ? 8 : 4;
    Vtx_t* vertices = Allocate(count);
    for (size_t i = 0; i < count; i++) {
        vertices[i] = Vtx_t{};
        for (size_t k = 0; k < 4; k++) {
            vertices[i].cn[k] = colour[k];
        }
    }

    Mtxf placement = TranslationMtxf(pos);
    ScaleRows(&placement, 0.1f);
    placement = MultiplyHomogeneous(worldToScreen, placement);
    const Mtx::FixedMtx* matrix = Keep(placement.m);

    if (beam.item == kItemWatchLaser) {
        Vec3 tmp = { pos[0] + beam.delta[0] * dist, pos[1] + beam.delta[1] * dist, pos[2] + beam.delta[2] * dist };
        tmp = TransformVector(worldToScreen, tmp);

        // divide3DCoordinates.
        const float divisor = -tmp[2];
        const float divisorRecip = 1.0f / divisor;
        const float input = radius / 10.0f;
        const float outY = input * divisorRecip * frame_.camera->RecipScaleY();
        const float outX = input * divisorRecip * frame_.camera->RecipScaleX();
        (void)outY;
        if (outX < 2.0f) {
            tmp[0] *= outX * 0.5f;
            tmp[1] *= outX * 0.5f;
            tmp[2] *= outX * 0.5f;
        }

        tmp = TransformVector(frame_.camera->ViewToWorld(), tmp);
        tmp[0] -= pos[0];
        tmp[1] -= pos[1];
        tmp[2] -= pos[2];
        flareOffset = { tmp[0] * 10.0f, tmp[1] * 10.0f, tmp[2] * 10.0f };
    } else {
        flareOffset = { beam.delta[0] * (dist * 10.0f), beam.delta[1] * (dist * 10.0f),
                        beam.delta[2] * (dist * 10.0f) };
    }

    const EffectImageSize image = images_.Size(imageOffset);
    const auto place = [&](size_t index, const Vec3& at, int16_t s, int16_t t) {
        for (size_t axis = 0; axis < 3; axis++) {
            vertices[index].ob[axis] = Short(at[axis]);
        }
        vertices[index].tc[0] = s;
        vertices[index].tc[1] = t;
    };
    const int16_t width = static_cast<int16_t>(image.width << 5);
    const int16_t height = static_cast<int16_t>(image.height << 5);
    place(0, right, width, 0);
    place(1, { -right[0], -right[1], -right[2] }, 0, 0);
    place(
        2,
        { flareOffset[0] + (right[0] * 0.9f), flareOffset[1] + (right[1] * 0.9f), flareOffset[2] + (right[2] * 0.9f) },
        width, height);
    place(
        3,
        { flareOffset[0] - (right[0] * 0.9f), flareOffset[1] - (right[1] * 0.9f), flareOffset[2] - (right[2] * 0.9f) },
        0, height);

    const EffectImageSize extra = images_.Size(EffectImage::kFlare5);
    if (beam.item == kItemLaser) {
        float dx = playerPosition[0] - pos[0];
        float dy = playerPosition[1] - pos[1];
        float dz = playerPosition[2] - pos[2];
        const float toStart = (dx * dx) + (dy * dy) + (dz * dz);

        dx = playerPosition[0] - (pos[0] + (beam.delta[0] * dist));
        dy = playerPosition[1] - (pos[1] + (beam.delta[1] * dist));
        dz = playerPosition[2] - (pos[2] + (beam.delta[2] * dist));
        const float toEnd = (dx * dx) + (dy * dy) + (dz * dz);

        if (toEnd < toStart) {
            extraOrigin = flareOffset;
            extraScale *= 0.9f;
        }

        const int16_t extraWidth = static_cast<int16_t>(extra.width << 5);
        const int16_t extraHeight = static_cast<int16_t>(extra.height << 5);
        place(4,
              { extraOrigin[0] + (up[0] * extraScale), extraOrigin[1] + (up[1] * extraScale),
                extraOrigin[2] + (up[2] * extraScale) },
              extraWidth, extraHeight);
        place(5,
              { extraOrigin[0] - (up[0] * extraScale), extraOrigin[1] - (up[1] * extraScale),
                extraOrigin[2] - (up[2] * extraScale) },
              0, 0);
        place(6,
              { extraOrigin[0] + (right[0] * extraScale), extraOrigin[1] + (right[1] * extraScale),
                extraOrigin[2] + (right[2] * extraScale) },
              0, extraHeight);
        place(7,
              { extraOrigin[0] - (right[0] * extraScale), extraOrigin[1] - (right[1] * extraScale),
                extraOrigin[2] - (right[2] * extraScale) },
              extraWidth, 0);
    }

    *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_BACK);
    *dl++ = Matrix(matrix, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);

    if (beam.item == kItemLaser) {
        dl = SelectImage(dl, EffectImage::kFlare5, 4, zbufferMode);
        *dl++ = Vertices(vertices, 8, 0);
        *dl++ = Triangle(4, 5, 6);
        *dl++ = Triangle(4, 5, 7);
        dl = SelectImage(dl, imageOffset, 4, zbufferMode);
        *dl++ = Triangle(0, 2, 3);
        *dl++ = Triangle(0, 3, 1);
    } else {
        dl = SelectImage(dl, imageOffset, 4, zbufferMode);
        *dl++ = Vertices(vertices, 4, 0);
        *dl++ = Triangle(0, 2, 3);
        *dl++ = Triangle(0, 3, 1);
    }
    return dl;
}

Gfx* EffectsDraw::Shards(Gfx* dl) {
    using namespace Frigate::F3dex;
    dl = SelectImage(dl, EffectImage::kGlassOverlay + EffectImage::kEntrySize, 2, kZbufSurface);

    *dl++ = Texture(0x0D80, 0x0D80, 2, G_TX_RENDERTILE, 1);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_2CYCLE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_LOD);
    *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    *dl++ = SetGeometryMode(G_LIGHTING | G_TEXTURE_GEN);
    *dl++ = Matrix(frame_.view->worldProjection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);

    const Vec3& modelPosition = frame_.camera->ModelPosition();
    for (const GlassShard& shard : shards_.Shards()) {
        if (shard.active <= 0) {
            continue;
        }
        Mtxf placement = RotationXYZ(shard.rotation);
        placement.m[3][0] = shard.position[0];
        placement.m[3][1] = shard.position[1];
        placement.m[3][2] = shard.position[2];
        placement.m[3][0] -= modelPosition[0];
        placement.m[3][1] -= modelPosition[1];
        placement.m[3][2] -= modelPosition[2];

        Vtx_t* vertices = Allocate(3);
        for (size_t i = 0; i < 3; i++) {
            vertices[i] = Vtx_t{};
            for (size_t axis = 0; axis < 3; axis++) {
                vertices[i].ob[axis] = shard.corners[i].position[axis];
            }
            vertices[i].tc[0] = shard.corners[i].texture[0];
            vertices[i].tc[1] = shard.corners[i].texture[1];
            for (size_t k = 0; k < 4; k++) {
                vertices[i].cn[k] = shard.corners[i].colour[k];
            }
        }
        *dl++ = Matrix(Keep(placement.m), Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
        *dl++ = Vertices(vertices, 3, 0);
        *dl++ = Triangle(0, 1, 2);
    }

    *dl++ = ClearGeometryMode(G_LIGHTING | G_TEXTURE_GEN);
    *dl++ = Matrix(frame_.view->propProjection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    *dl++ = Matrix(frame_.view->modelview, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    return dl;
}

Gfx* EffectsDraw::Particles(Gfx* dl) {
    using namespace Frigate::F3dex;
    const Mtxf worldToScreen = FromGu(frame_.camera->WorldToScreen());

    *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);
    dl = DisplayListByHash(dl, CRC64("hud/gdl/0a50"));
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_ZB_XLU_SURF | G_RM_AA_ZB_XLU_SURF2);

    for (const Explosions::Particle& particle : explosions_.Particles()) {
        if (particle.active <= 0) {
            continue;
        }
        Mtxf placement = RotationXYZ(particle.rotation);
        placement.m[3][0] = particle.position[0];
        placement.m[3][1] = particle.position[1];
        placement.m[3][2] = particle.position[2];
        placement = MultiplyHomogeneous(worldToScreen, placement);

        if (placement.m[3][0] < 20000.0f && placement.m[3][0] > -20000.0f && placement.m[3][1] < 20000.0f &&
            placement.m[3][1] > -20000.0f && placement.m[3][2] < 20000.0f && placement.m[3][2] > -20000.0f) {
            *dl++ = Matrix(Keep(placement.m), Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
            dl = Quad(dl, particle.vertices);
        }
    }
    return dl;
}

Gfx* EffectsDraw::RoomMarks(Gfx* dl) {
    using namespace Frigate::F3dex;

    // explosionRenderScorchBuffer.
    *dl++ = SetGeometryMode(Fast::F3DEX_G_CULL_BACK);
    *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_FRONT | G_FOG);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_NOISE);
    dl = SelectImage(dl, EffectImage::kGeneric, 4, kZbufSurface);
    int room = -1;
    for (const BulletImpacts::Scorch& scorch : impacts_.Scorches()) {
        if (scorch.room >= 0 && visibility_.RoomRendered(scorch.room)) {
            if (room != scorch.room) {
                room = scorch.room;
                dl = ApplyRoomMatrix(dl, room);
            }
            dl = Quad(dl, scorch.vertices);
        }
    }
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_BAYER);

    // explosionRenderBulletImpactOnProp with no prop, the opaque marks.
    *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_NOISE);
    room = -1;
    int shownType = -1;
    for (const BulletImpact& impact : impacts_.Impacts()) {
        if (impact.prop >= 0 || impact.room < 0 || !visibility_.RoomRendered(impact.room)) {
            continue;
        }
        const ImpactType& type = ImpactTypeOf(impact.type);
        if (!(type.unk2 >= 2 || type.unk1 != 2)) {
            continue;
        }
        if (room != impact.room) {
            room = impact.room;
            dl = ApplyRoomMatrix(dl, room);
        }
        if (shownType != impact.type) {
            dl = SelectImage(dl, EffectImage::kImpacts + (impact.type * EffectImage::kEntrySize), type.unk1, type.unk2);
            shownType = impact.type;
        }
        dl = Quad(dl, impact.vertices);
    }
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_BAYER);
    return dl;
}

Gfx* EffectsDraw::PropMarks(Gfx* dl, int prop, const Mtx::FixedMtx* matrices, size_t matrixCount, bool withAlpha,
                            bool* drawn) {
    using namespace Frigate::F3dex;
    *drawn = false;

    *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_NOISE);

    int matrixIndex = -1;
    int shownType = -1;
    for (const BulletImpact& impact : impacts_.Impacts()) {
        if (impact.prop != prop || impact.room < 0) {
            continue;
        }
        const ImpactType& type = ImpactTypeOf(impact.type);
        const bool blended = type.unk2 < 2 && type.unk1 == 2;
        if (withAlpha ? !blended : blended) {
            continue;
        }
        *drawn = true;

        if (matrixIndex != impact.matrixIndex && impact.matrixIndex >= 0 &&
            static_cast<size_t>(impact.matrixIndex) < matrixCount) {
            matrixIndex = impact.matrixIndex;
            *dl++ = Matrix(&matrices[matrixIndex], Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
        }
        if (shownType != impact.type) {
            dl = SelectImage(dl, EffectImage::kImpacts + (impact.type * EffectImage::kEntrySize), type.unk1, type.unk2);
            shownType = impact.type;
        }
        dl = Quad(dl, impact.vertices);
    }

    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_BAYER);
    return dl;
}

Gfx* EffectsDraw::ExplosionProp(Gfx* dl, int prop, const Prop& record, bool withAlpha) {
    using namespace Frigate::F3dex;
    // g_ExplosionDisplayLists: the flare's image for each of the 15 frames, in the
    // global table at 0x78 and each 0xA8 after.
    constexpr int kFlareFrames = 15;
    constexpr int kFirstFlareList = 0x78;
    constexpr int kFlareListSize = 0xA8;

    const Explosions::Explosion* explosion = explosions_.ExplosionOf(prop);
    if (explosion == nullptr || !withAlpha) {
        return dl;
    }

    const int room = record.rooms[0];
    const Vec3& roomPosition = level_.RoomEntry(room).position;
    const float flareSpeed = ExplosionTypeOf(explosion->type).flareSpeed;
    const Mtxf& viewToWorld = frame_.camera->ViewToWorld();
    const Vec3 player = frame_.camera->WorldPosition();
    const float scale = level_.Level().scale;

    *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH | G_FOG);
    *dl++ = Matrix(frame_.view->worldProjection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    dl = ApplyRoomMatrix(dl, room);

    for (int image = kFlareFrames - 1; image >= 0; image--) {
        char path[24];
        std::snprintf(path, sizeof(path), "hud/gdl/%04x", kFirstFlareList + (image * kFlareListSize));
        dl = DisplayListByHash(dl, CRC64(path));

        for (const Explosions::Part& part : explosion->parts) {
            if (!(part.frame > 0 && image == static_cast<int32_t>(static_cast<float>(part.frame - 1) / flareSpeed))) {
                continue;
            }

            // explosionRenderPart: the quad is drawn nearer the camera than the part
            // is, so a flare close up does not poke through the walls.
            const float dx = part.position[0] - player[0];
            const float dy = part.position[1] - player[1];
            const float dz = part.position[2] - player[2];
            const float distance = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
            float range = distance * 0.5f;
            if (range > 100.0f) {
                range = 100.0f;
            }
            const float mult = distance == 0.0f ? 0.0f : (distance - range) / distance;

            const float size = part.size * mult;
            const float rotation = part.rotation * mult;
            const float centre[3] = { player[0] + (dx * mult), player[1] + (dy * mult), player[2] + (dz * mult) };

            const float right[3] = { viewToWorld.m[0][0] * size, viewToWorld.m[0][1] * size,
                                     viewToWorld.m[0][2] * size };
            const float rightTurn[3] = { viewToWorld.m[0][0] * rotation, viewToWorld.m[0][1] * rotation,
                                         viewToWorld.m[0][2] * rotation };
            const float up[3] = { viewToWorld.m[1][0] * size, viewToWorld.m[1][1] * size, viewToWorld.m[1][2] * size };
            const float upTurn[3] = { viewToWorld.m[1][0] * rotation, viewToWorld.m[1][1] * rotation,
                                      viewToWorld.m[1][2] * rotation };

            Vtx_t* vertices = Allocate(4);
            const int16_t s[4] = { 1760, 0, 0, 1760 };
            const int16_t t[4] = { 0, 0, 1760, 1760 };
            for (size_t i = 0; i < 4; i++) {
                vertices[i] = Vtx_t{};
                for (size_t axis = 0; axis < 3; axis++) {
                    float offset = 0.0f;
                    switch (i) {
                        case 0:
                            offset = -right[axis] - upTurn[axis];
                            break;
                        case 1:
                            offset = rightTurn[axis] - up[axis];
                            break;
                        case 2:
                            offset = right[axis] + upTurn[axis];
                            break;
                        default:
                            offset = -rightTurn[axis] + up[axis];
                            break;
                    }
                    vertices[i].ob[axis] = Short(((centre[axis] + offset) * scale) - roomPosition[axis]);
                }
                vertices[i].tc[0] = s[i];
                vertices[i].tc[1] = t[i];
                vertices[i].cn[0] = 0xFF;
                vertices[i].cn[1] = 0xFF;
                vertices[i].cn[2] = 0xFF;
                vertices[i].cn[3] = 0xFF;
            }
            *dl++ = Vertices(vertices, 4, 0);
            dl = QuadTriangles(dl);
        }
    }

    *dl++ = Matrix(frame_.view->propProjection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    explosions_.EndFlares(prop);
    return dl;
}

Gfx* EffectsDraw::SmokeProp(Gfx* dl, int prop, const Prop& record, bool withAlpha) {
    using namespace Frigate::F3dex;
    if (record.type != PropType::Smoke) {
        return dl;
    }
    const Explosions::Smoke* smoke = explosions_.SmokeOf(prop);
    if (smoke == nullptr || !withAlpha) {
        return dl;
    }

    const int room = record.rooms[0];
    const Vec3& roomPosition = level_.RoomEntry(room).position;
    const SmokeType& type = SmokeTypeOf(smoke->type);
    const Mtxf& viewToWorld = frame_.camera->ViewToWorld();
    const Vec3 player = frame_.camera->WorldPosition();
    const float scale = level_.Level().scale;

    *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH | G_FOG);
    *dl++ = Matrix(frame_.view->worldProjection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    dl = ApplyRoomMatrix(dl, room);
    if (smokeScale_ == nullptr) {
        // guScale(0.1, 0.1, 0.1).
        const float scaleMatrix[4][4] = { { 0.1f, 0, 0, 0 }, { 0, 0.1f, 0, 0 }, { 0, 0, 0.1f, 0 }, { 0, 0, 0, 1.0f } };
        smokeScale_ = Keep(scaleMatrix);
    }
    *dl++ = Matrix(smokeScale_, Fast::F3DEX_G_MTX_MODELVIEW);
    dl = DisplayListByHash(dl, CRC64("hud/gdl/0000"));
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_NOISE);

    for (const Explosions::SmokePart& part : smoke->parts) {
        if (!(part.size > 0.0f)) {
            continue;
        }

        uint8_t alpha;
        if (type.rateAppear >= part.count) {
            alpha = static_cast<uint8_t>(static_cast<int32_t>((part.alpha / static_cast<float>(type.rateAppear)) *
                                                              static_cast<float>(part.count)));
        } else {
            alpha = static_cast<uint8_t>(static_cast<int32_t>(part.alpha));
        }

        float sp88 = std::cos(part.rotation) * part.size;
        float sp84 = std::sin(part.rotation) * part.size;

        float sp80 = part.position[0] + (std::sin(part.offset1) * 7.0f);
        float sp7C = part.position[1];
        float sp78 = part.position[2] + (std::sin(part.offset2) * 7.0f);

        const float sp6C = sp80 - player[0];
        const float sp68 = sp7C - player[1];
        const float sp64 = sp78 - player[2];

        const float distance = std::sqrt((sp6C * sp6C) + (sp68 * sp68) + (sp64 * sp64));
        if (distance > 30000.0f) {
            continue;
        }

        float range = distance * 0.5f;
        if (range > 100.0f) {
            range = 100.0f;
        }
        const float mult = distance == 0.0f ? 0.0f : (distance - range) / distance;

        sp88 = sp88 * mult;
        sp84 = sp84 * mult;

        sp80 = player[0] + sp6C * mult;
        sp7C = player[1] + sp68 * mult;
        sp78 = player[2] + sp64 * mult;

        const float spB0[3] = { viewToWorld.m[0][0] * sp88, viewToWorld.m[0][1] * sp88, viewToWorld.m[0][2] * sp88 };
        const float spA4[3] = { viewToWorld.m[0][0] * sp84, viewToWorld.m[0][1] * sp84, viewToWorld.m[0][2] * sp84 };
        const float sp98[3] = { viewToWorld.m[1][0] * sp88, viewToWorld.m[1][1] * sp88, viewToWorld.m[1][2] * sp88 };
        const float sp8C[3] = { viewToWorld.m[1][0] * sp84, viewToWorld.m[1][1] * sp84, viewToWorld.m[1][2] * sp84 };

        const float corner[4][3] = {
            { ((sp80 - spB0[0] - sp8C[0]) * scale - roomPosition[0]) * 10.0f,
              ((sp7C - spB0[1] - sp8C[1]) * scale - roomPosition[1]) * 10.0f,
              ((sp78 - spB0[2] - sp8C[2]) * scale - roomPosition[2]) * 10.0f },
            { ((((sp80 + spA4[0]) - sp98[0]) * scale) - roomPosition[0]) * 10.0f,
              ((((sp7C + spA4[1]) - sp98[1]) * scale) - roomPosition[1]) * 10.0f,
              ((((sp78 + spA4[2]) - sp98[2]) * scale) - roomPosition[2]) * 10.0f },
            { (((sp80 + spB0[0] + sp8C[0]) * scale) - roomPosition[0]) * 10.0f,
              (((sp7C + spB0[1] + sp8C[1]) * scale) - roomPosition[1]) * 10.0f,
              (((sp78 + spB0[2] + sp8C[2]) * scale) - roomPosition[2]) * 10.0f },
            { ((((sp80 - spA4[0]) + sp98[0]) * scale) - roomPosition[0]) * 10.0f,
              ((((sp7C - spA4[1]) + sp98[1]) * scale) - roomPosition[1]) * 10.0f,
              ((((sp78 - spA4[2]) + sp98[2]) * scale) - roomPosition[2]) * 10.0f },
        };
        bool outside = false;
        for (size_t axis = 0; axis < 3; axis++) {
            if (corner[0][axis] > 30000.0f || corner[0][axis] < -30000.0f) {
                outside = true;
            }
        }
        if (outside) {
            continue;
        }

        Vtx_t* vertices = Allocate(4);
        const int16_t s[4] = { 1760, 0, 0, 1760 };
        const int16_t t[4] = { 0, 0, 1760, 1760 };
        for (size_t i = 0; i < 4; i++) {
            vertices[i] = Vtx_t{};
            for (size_t axis = 0; axis < 3; axis++) {
                vertices[i].ob[axis] = Short(corner[i][axis]);
            }
            vertices[i].tc[0] = s[i];
            vertices[i].tc[1] = t[i];
            vertices[i].cn[0] = type.r;
            vertices[i].cn[1] = type.g;
            vertices[i].cn[2] = type.b;
            vertices[i].cn[3] = alpha;
        }
        *dl++ = Vertices(vertices, 4, 0);
        dl = QuadTriangles(dl);
    }

    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_BAYER);
    *dl++ = Matrix(frame_.view->propProjection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    return dl;
}

} // namespace Frigate::Level
