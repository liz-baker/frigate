#include "viewer/ModelStub.h"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "fast/resource/type/DisplayList.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/archive/ArchiveManager.h"
#include "ship/utils/StrHash64.h"
#include "viewer/GbiF3dex.h"

namespace Frigate::Viewer {

namespace {

// libultraship's OTR hash-call opcode; the next command word pair is the hash.
constexpr int8_t kDlOtrHash = 0x31;

// The GoldenEye texture-request command, rewritten to RDP commands at load.
constexpr uint8_t kSetTex = 0xc0;

// Models address node matrices in segment 3, which the game fills at run time.
// GoldenEye's segment addresses lack the low "segmented" bit the interpreter
// tests for, so it would dereference 0x03xxxxxx as a raw pointer and crash.
// Turn those commands into no-ops so the node draws untransformed.
// TODO(https://github.com/liz-baker/frigate/issues/12): replaced by the
// scene-graph walk supplying the matrices.
constexpr uint32_t kMtxSegment = 3;

bool IsExpandedCommand(uint8_t opcode) {
    return opcode == G_SETTIMG_OTR_HASH || opcode == G_DL_OTR_HASH || opcode == G_VTX_OTR_HASH ||
           opcode == G_BRANCH_Z_OTR || opcode == G_MARKER || opcode == G_MTX_OTR || opcode == G_MOVEMEM_OTR;
}

size_t NeutralizeSegmentMatrices(Fast::DisplayList& list) {
    size_t count = 0;
    auto& instructions = list.Instructions;
    for (size_t i = 0; i < instructions.size(); i += IsExpandedCommand(instructions[i].words.w0 >> 24) ? 2 : 1) {
        auto& cmd = instructions[i];
        if (static_cast<uint8_t>(cmd.words.w0 >> 24) == static_cast<uint8_t>(Fast::F3DEX_G_MTX) &&
            (cmd.words.w1 >> 24) == kMtxSegment) {
            cmd.words.w0 = static_cast<uint32_t>(static_cast<uint8_t>(Fast::F3DEX_G_SPNOOP)) << 24;
            cmd.words.w1 = 0;
            count++;
        }
    }
    return count;
}

bool HasTextureRequest(const Fast::DisplayList& list) {
    const auto& instructions = list.Instructions;
    for (size_t i = 0; i < instructions.size(); i += IsExpandedCommand(instructions[i].words.w0 >> 24) ? 2 : 1) {
        if (static_cast<uint8_t>(instructions[i].words.w0 >> 24) == kSetTex) {
            return true;
        }
    }
    return false;
}

Gfx CombineModulate() {
    Gfx cmd;
    Gfx* pkt = &cmd;
    gDPSetCombineMode(pkt, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
    return cmd;
}

Gfx CombineShade() {
    Gfx cmd;
    Gfx* pkt = &cmd;
    gDPSetCombineMode(pkt, G_CC_SHADE, G_CC_SHADE);
    return cmd;
}

Gfx HashOperand(uint64_t hash) {
    Gfx cmd;
    cmd.words.w0 = static_cast<uint32_t>(hash >> 32);
    cmd.words.w1 = static_cast<uint32_t>(hash);
    return cmd;
}

} // namespace

std::unique_ptr<ModelStub> ModelStub::Create(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                             const std::string& modelPath, float distance, bool textures,
                                             const std::vector<Gfx>& prefix) {
    auto lists = resourceManager->GetArchiveManager()->ListFiles(modelPath + "/dl/*");
    if (lists == nullptr || lists->empty()) {
        spdlog::error("viewer: no display lists under '{}/dl/' in the loaded archives", modelPath);
        return nullptr;
    }
    std::sort(lists->begin(), lists->end());

    auto stub = std::unique_ptr<ModelStub>(new ModelStub());
    stub->mCamera = MakeCamera(distance, 320.0f / 240.0f);
    stub->mListCount = lists->size();

    using namespace F3dex;
    auto& out = stub->mCommands;
    out = prefix;
    out.push_back(Viewport(&stub->mCamera.viewport));
    // Modelview first: the projection load multiplies by the top of the
    // modelview stack, which is empty until the first modelview load.
    out.push_back(Matrix(&stub->mCamera.modelview, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD));
    out.push_back(Matrix(&stub->mCamera.projection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD));
    out.push_back(SetGeometryMode(G_ZBUFFER | G_SHADE | Fast::F3DEX_G_SHADING_SMOOTH));
    out.push_back(SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE));
    out.push_back(SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_ZB_OPA_SURF | G_RM_ZB_OPA_SURF2));

    for (const auto& path : *lists) {
        // Loaded by hash because that is the key the interpreter resolves the
        // call with, and a by-path load is a separate cache entry. Loading up
        // front also turns a missing factory or hash into a startup error
        // instead of a silently absent draw.
        uint64_t hash = CRC64(path.c_str());
        auto list = std::dynamic_pointer_cast<Fast::DisplayList>(resourceManager->LoadResource(hash));
        if (list == nullptr) {
            spdlog::error("viewer: failed to load '{}'", path);
            return nullptr;
        }
        NeutralizeSegmentMatrices(*list);
        stub->mLists.push_back(list);

        // The game sets texturing and the combiner per material before it
        // calls a model's lists. Sampling with no texture bound crashes the
        // renderer, so only lists that request one get texturing on.
        if (textures && HasTextureRequest(*list)) {
            out.push_back(TextureOn());
            out.push_back(CombineModulate());
        } else {
            out.push_back(TextureOff());
            out.push_back(CombineShade());
        }
        out.push_back(Command(kDlOtrHash, 0, 0));
        out.push_back(HashOperand(hash));
    }
    out.push_back(EndDisplayList());
    return stub;
}

} // namespace Frigate::Viewer
