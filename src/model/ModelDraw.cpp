#include "model/ModelDraw.h"

#include <cstring>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "fast/resource/type/DisplayList.h"
#include "gfx/GbiF3dex.h"
#include "resource/type/ModelTree.h"
#include "ship/resource/ResourceManager.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Model {

namespace {

// libultraship's OTR hash-call opcode; the next command word pair is the hash.
constexpr int8_t kDlOtrHash = 0x31;

// A G_SETTEX the load-time expansion left as stored (see TexExpand); it never
// becomes RDP commands, so the draw has no texture bound.
constexpr uint8_t kSetTex = 0xc0;
constexpr uint8_t kLoadBlock = 0xf3;
constexpr uint8_t kSetCombine = 0xfc;
constexpr uint8_t kTexture = 0xbb;
// F3DEX draw commands (G_TRI4 shares the G_TRI2 opcode).
constexpr uint8_t kTri1 = 0xbf;
constexpr uint8_t kTri2 = 0xb1;
constexpr uint8_t kQuad = 0xb0;

// Model lists address their node matrices in segment 3.
constexpr uint32_t kMatrixSegment = 3;

bool IsExpandedCommand(uint8_t opcode) {
    return opcode == G_SETTIMG_OTR_HASH || opcode == G_DL_OTR_HASH || opcode == G_VTX_OTR_HASH ||
           opcode == G_BRANCH_Z_OTR || opcode == G_MARKER || opcode == G_MTX_OTR || opcode == G_MOVEMEM_OTR;
}

// True if the list loads a texture before its first draw or nested call and
// requests none the expansion left unexpanded. The interpreter imports a
// texture only when a load changes it, so a textured draw with nothing loaded
// leaves the backend indexing an empty texture table and crashing.
bool LoadsTextureBeforeDrawing(const Fast::DisplayList& list) {
    const auto& instructions = list.Instructions;
    bool loaded = false;
    bool drewFirst = false;
    for (size_t i = 0; i < instructions.size(); i += IsExpandedCommand(instructions[i].words.w0 >> 24) ? 2 : 1) {
        auto op = static_cast<uint8_t>(instructions[i].words.w0 >> 24);
        if (op == kSetTex) {
            return false;
        }
        if (op == kLoadBlock) {
            loaded = true;
        } else if (!loaded && (op == kTri1 || op == kTri2 || op == kQuad || op == G_DL_OTR_HASH)) {
            drewFirst = true;
        }
    }
    return loaded && !drewFirst;
}

Gfx CombineShade() {
    Gfx cmd;
    Gfx* pkt = &cmd;
    gDPSetCombineMode(pkt, G_CC_SHADE, G_CC_SHADE);
    return cmd;
}

// Model lists set no combiner of their own; the caller supplies one outside
// the list, and a texture-loading list needs it to sample.
Gfx CombineModulate() {
    Gfx cmd;
    Gfx* pkt = &cmd;
    gDPSetCombineMode(pkt, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
    return cmd;
}

// Lists carry their own combiner and texture-enable commands, so sampling
// can't be turned off from outside; rewrite them to shade-only.
void DisableSampling(Fast::DisplayList& list) {
    auto& instructions = list.Instructions;
    for (size_t i = 0; i < instructions.size(); i += IsExpandedCommand(instructions[i].words.w0 >> 24) ? 2 : 1) {
        auto op = static_cast<uint8_t>(instructions[i].words.w0 >> 24);
        if (op == kSetCombine) {
            instructions[i] = CombineShade();
        } else if (op == kTexture) {
            instructions[i] = Frigate::F3dex::TextureOff();
        }
    }
}

Gfx HashOperand(uint64_t hash) {
    Gfx cmd;
    cmd.words.w0 = static_cast<uint32_t>(hash >> 32);
    cmd.words.w1 = static_cast<uint32_t>(hash);
    return cmd;
}

} // namespace

std::optional<ModelDraw> BuildModelDraw(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                        const std::string& modelPath, const Mat4& base, bool textures,
                                        const std::vector<Gfx>& prefix) {
    auto tree =
        std::dynamic_pointer_cast<ModelTree>(resourceManager->LoadResource(CRC64((modelPath + "/tree").c_str())));
    if (tree == nullptr || tree->Nodes.empty()) {
        spdlog::error("model: no node tree at '{}/tree' in the loaded archives", modelPath);
        return std::nullopt;
    }

    ModelDraw draw;
    ModelWalk walk = WalkModel(tree->Nodes, tree->MatrixCount, base);
    // DIAGNOSTIC (temporary): bisecting an invisible-logo bug -- limit to
    // the first list to test whether chaining many per-node lists together
    // is the issue, independent of texturing (already ruled out) or the
    // per-frame rebuild itself.
    if (walk.lists.size() > 1) {
        walk.lists.resize(1);
    }
    for (const Mat4& mtx : walk.matrices) {
        draw.matrices.push_back(Mtx::ToFixedMtx(mtx.m));
    }

    using namespace Frigate::F3dex;
    draw.commands = prefix;
    auto& out = draw.commands;
    // Every list's own G_MTX replaces the modelview with its node's matrix,
    // which already includes `base`.
    out.push_back(Command(Fast::F3DEX_G_MOVEWORD, ((kMatrixSegment * 4) << 8) | G_MW_SEGMENT,
                          reinterpret_cast<uintptr_t>(draw.matrices.data())));
    out.push_back(TextureOff());
    out.push_back(CombineShade());
    out.push_back(SetGeometryMode(G_ZBUFFER | G_SHADE | Fast::F3DEX_G_SHADING_SMOOTH));
    out.push_back(SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE));
    out.push_back(SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_ZB_OPA_SURF | G_RM_ZB_OPA_SURF2));

    size_t textured = 0;
    for (const ListRef& ref : walk.lists) {
        std::string path = fmt::format("{}/dl/{:06x}/{:06x}", modelPath, ref.node, ref.list);
        // Loaded by hash because that is the key the interpreter resolves the
        // call with, and a by-path load is a separate cache entry. Loading up
        // front also turns a missing factory or hash into a startup error
        // instead of a silently absent draw.
        uint64_t hash = CRC64(path.c_str());
        auto list = std::dynamic_pointer_cast<Fast::DisplayList>(resourceManager->LoadResource(hash));
        if (list == nullptr) {
            spdlog::error("model: failed to load '{}'", path);
            return std::nullopt;
        }
        draw.lists.push_back(list);

        bool loadsTexture = LoadsTextureBeforeDrawing(*list);
        if (loadsTexture) {
            textured++;
        }
        if (!textures || !loadsTexture) {
            DisableSampling(*list);
        }
        out.push_back(textures && loadsTexture ? CombineModulate() : CombineShade());
        out.push_back(Command(kDlOtrHash, 0, 0));
        out.push_back(HashOperand(hash));
    }
    spdlog::info("model: '{}': {} of {} lists load their own texture", modelPath, textured, walk.lists.size());
    return draw;
}

} // namespace Frigate::Model
