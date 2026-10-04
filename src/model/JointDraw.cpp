#include "model/JointDraw.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "fast/resource/type/DisplayList.h"
#include "fast/resource/type/Vertex.h"
#include "gfx/GbiF3dex.h"
#include "model/EffectQuads.h"
#include "model/JointDrawList.h"
#include "model/ModelDraw.h"
#include "model/Random.h"
#include "model/Trig.h"
#include "ship/resource/Resource.h"
#include "ship/resource/ResourceManager.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Model {

namespace {

// Model lists address their node matrices in segment 3.
constexpr uint32_t kMatrixSegment = 3;

constexpr uint8_t kDisplayListNodeType = 0x04;
constexpr uint8_t kDisplayListCollisionNodeType = 0x18;
constexpr uint8_t kDisplayListPrimaryNodeType = 0x16;

// N64 image format codes.
constexpr uint8_t kFormatCi = 2;
constexpr uint8_t kFormatI = 4;

Gfx FogColorBlack() {
    Gfx cmd;
    Gfx* pkt = &cmd;
    gDPSetFogColor(pkt, 0x00, 0x00, 0x00, 0x00);
    return cmd;
}

Gfx PipeSync() {
    Gfx cmd;
    Gfx* pkt = &cmd;
    gDPPipeSync(pkt);
    return cmd;
}

// The combiner an effect's texture is drawn with: intensity images modulate
// the vertex colour, the rest its alpha too.
Gfx ModulateCombiner(bool intensity) {
    Gfx cmd;
    Gfx* pkt = &cmd;
    if (intensity) {
        gDPSetCombineMode(pkt, G_CC_MODULATEI, G_CC_MODULATEI);
    } else {
        gDPSetCombineMode(pkt, G_CC_MODULATEIA, G_CC_MODULATEIA);
    }
    return cmd;
}

// Appends the commands of the list `list` in place of a call to it, with every
// vertex load from the model's own array for the node (`vertexHash`) loading
// from `replaced` instead. The array's commands name it by offset into the
// original, which the interpreter may already have resolved to a pointer in the
// shared list.
bool AppendListWithVertices(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                            const Fast::DisplayList& list, uint64_t vertexHash, const Vtx_t* original,
                            const NodeVertices& replaced, std::vector<Gfx>& commands,
                            std::vector<std::shared_ptr<Ship::IResource>>& lists) {
    const auto& instructions = list.Instructions;
    for (size_t i = 0; i < instructions.size();) {
        const auto op = static_cast<uint8_t>(instructions[i].words.w0 >> 24);
        if (op == static_cast<uint8_t>(Fast::F3DEX_G_ENDDL)) {
            return true;
        }
        if (!F3dex::IsExpandedCommand(op)) {
            commands.push_back(instructions[i]);
            i++;
            continue;
        }
        if (i + 1 >= instructions.size()) {
            return false;
        }
        const uint64_t hash =
            (static_cast<uint64_t>(instructions[i + 1].words.w0) << 32) | instructions[i + 1].words.w1;
        if (op == G_VTX_OTR_HASH && hash == vertexHash) {
            // The offset is a byte offset, or, once resolved, the address.
            const uintptr_t word = instructions[i].words.w1;
            const uintptr_t offset = word > 0xFFFFF ? word - reinterpret_cast<uintptr_t>(original) : word;
            const uint32_t w0 = instructions[i].words.w0;
            const uint32_t count = (w0 >> 12) & 0xFF;
            const uint32_t first = ((w0 >> 1) & 0x7F) - count;
            const size_t index = offset / sizeof(Vtx_t);
            if (index + count > replaced.vertices.size()) {
                spdlog::error("model: a vertex load runs past the node's {} vertices", replaced.vertices.size());
                return false;
            }
            commands.push_back(F3dex::Vertices(replaced.vertices.data() + index, count, first));
        } else if (op == G_DL_OTR_HASH) {
            auto called = std::dynamic_pointer_cast<Fast::DisplayList>(resourceManager->LoadResource(hash));
            if (called == nullptr) {
                spdlog::error("model: a list a clipped node calls does not load");
                return false;
            }
            lists.push_back(called);
            if (!AppendListWithVertices(resourceManager, *called, vertexHash, original, replaced, commands, lists)) {
                return false;
            }
            // A jump, rather than a call, does not come back.
            if ((instructions[i].words.w0 & 0x10000) != 0) {
                return true;
            }
        } else {
            commands.push_back(instructions[i]);
            commands.push_back(instructions[i + 1]);
        }
        i += 2;
    }
    return true;
}

// The backend crashes on a draw that samples a texture nothing loaded, so a
// list that never loads one is drawn shade-only instead. A list that draws
// nothing is left alone: the combiner it would set outlives it, and a node's
// secondary list (which sets none of its own) would draw with it.
bool CallNodeList(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const std::string& modelPath,
                  const ModelNode& node, uint32_t list, std::vector<Gfx>& commands,
                  std::vector<std::shared_ptr<Ship::IResource>>& lists,
                  const std::vector<NodeVertices>* replaced = nullptr) {
    const std::string listPath = fmt::format("{}/dl/{:06x}/{:06x}", modelPath, node.offset, list);
    auto call = CallList(resourceManager, listPath);
    if (!call) {
        return false;
    }
    lists.push_back(call->list);

    auto displayList = std::dynamic_pointer_cast<Fast::DisplayList>(call->list);
    if (displayList != nullptr && ListDraws(*displayList) && !ListLoadsTextureBeforeDrawing(*displayList)) {
        DisableListSampling(*displayList);
        commands.push_back(ShadeOnlyCombiner());
    }

    const NodeVertices* nodeVertices = nullptr;
    if (replaced != nullptr) {
        for (const NodeVertices& entry : *replaced) {
            if (entry.node == node.offset) {
                nodeVertices = &entry;
            }
        }
    }
    if (nodeVertices != nullptr && displayList != nullptr) {
        const uint64_t vertexHash = CRC64(fmt::format("{}/vtx/{:06x}", modelPath, node.offset).c_str());
        auto original = std::dynamic_pointer_cast<Fast::Vertex>(resourceManager->LoadResource(vertexHash));
        if (original == nullptr || original->VertexList.empty()) {
            spdlog::error("model: '{}/vtx/{:06x}' does not load", modelPath, node.offset);
            return false;
        }
        lists.push_back(original);
        return AppendListWithVertices(resourceManager, *displayList, vertexHash, &original->VertexList[0].v,
                                      *nodeVertices, commands, lists);
    }
    commands.insert(commands.end(), call->commands.begin(), call->commands.end());
    return true;
}

class Emitter {
  public:
    Emitter(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const ModelRenderParams& render,
            JointDraw& draw)
        : resourceManager_(resourceManager), render_(render), draw_(draw) {
    }

    // Draws one node of `model` (the `modelIndex`th of the draw) for `pass`.
    // Returns false if a list fails to load.
    bool DrawNode(uint32_t pass, size_t modelIndex, const ModelInstance& model, const JointModel& jointModel,
                  const VisitedNode& visited) {
        const ModelNode& node = VisitedModelNode(jointModel, visited);
        const std::string& path = visited.attachment < 0 ? model.Path() : model.HeadPath();

        switch (node.type) {
            case kDisplayListNodeType:
            case kDisplayListCollisionNodeType: {
                const std::vector<NodeVertices>* painted = PaintedVertices(model, visited.attachment >= 0, path, node);
                return DrawDisplayListNode(resourceManager_, render_, pass, path, node, draw_.commands, draw_.lists,
                                           painted);
            }
            case kGunfireNodeType:
                return DrawGunfire(pass, modelIndex, model, jointModel, visited, node);
            case kShadowNodeType:
                return DrawShadow(pass, modelIndex, model, jointModel, visited, node);
            case kDisplayListPrimaryNodeType:
                return DrawStarGunfire(pass, path, node);
            default:
                return true;
        }
    }

  private:
    // The node's own vertices with the alpha the model paints on them, or null
    // when it paints none of this node.
    const std::vector<NodeVertices>* PaintedVertices(const ModelInstance& model, bool head, const std::string& path,
                                                     const ModelNode& node) {
        bool painted = false;
        for (const ModelInstance::VertexPaint& paint : model.Paints()) {
            painted = painted || (paint.head == head && paint.node == node.offset);
        }
        if (!painted) {
            return nullptr;
        }
        const uint64_t vertexHash = CRC64(fmt::format("{}/vtx/{:06x}", path, node.offset).c_str());
        auto original = std::dynamic_pointer_cast<Fast::Vertex>(resourceManager_->LoadResource(vertexHash));
        if (original == nullptr || original->VertexList.empty()) {
            return nullptr;
        }
        draw_.lists.push_back(original);

        std::vector<NodeVertices>& replaced = draw_.paintedVertices.emplace_back(1);
        replaced[0].node = node.offset;
        for (const auto& vertex : original->VertexList) {
            replaced[0].vertices.push_back(vertex.v);
        }
        for (const ModelInstance::VertexPaint& paint : model.Paints()) {
            if (paint.head == head && paint.node == node.offset && paint.vertex >= 0 &&
                static_cast<size_t>(paint.vertex) < replaced[0].vertices.size()) {
                Vtx_t& vertex = replaced[0].vertices[static_cast<size_t>(paint.vertex)];
                if (paint.colour) {
                    for (size_t channel = 0; channel < 3; channel++) {
                        vertex.cn[channel] = (*paint.colour)[channel];
                    }
                }
                vertex.cn[3] = paint.alpha;
            }
        }
        return &replaced;
    }

    // dogfnegx(): a muzzle flash, while its node is switched on. Drawn in the
    // secondary pass, over the model.
    bool DrawGunfire(uint32_t pass, size_t modelIndex, const ModelInstance& model, const JointModel& jointModel,
                     const VisitedNode& visited, const ModelNode& node) {
        if (pass != kSecondaryPass || !model.GunfireVisible(node.offset)) {
            return true;
        }
        const Mat4* mtx = FindNodeMatrix(*jointModel.nodes, *jointModel.walk, static_cast<int32_t>(visited.node));
        if (mtx == nullptr) {
            return true;
        }
        const EffectImage image{ node.effectWidth, node.effectHeight };
        const EffectQuad quad = BuildGunfireQuad(*mtx, node.sortPoint, node.effectSize, model.playback.scale,
                                                 node.effectTextured ? &image : nullptr);
        return DrawEffectQuad(modelIndex, model, jointModel, *mtx, node, quad);
    }

    // dorottex(): the star muzzle flash, in the secondary pass. Its list is drawn
    // over a copy of the node's vertices in which each quad of four has its
    // corners spun by a random angle, shrunk by a random amount and given
    // texture coordinates to match.
    bool DrawStarGunfire(uint32_t pass, const std::string& modelPath, const ModelNode& node) {
        if (pass != kSecondaryPass || node.primaryList == kNoList) {
            return true;
        }
        const uint64_t vertexHash = CRC64(fmt::format("{}/vtx/{:06x}", modelPath, node.offset).c_str());
        auto original = std::dynamic_pointer_cast<Fast::Vertex>(resourceManager_->LoadResource(vertexHash));
        if (original == nullptr || original->VertexList.empty()) {
            spdlog::error("model: '{}/vtx/{:06x}' does not load", modelPath, node.offset);
            return false;
        }
        draw_.lists.push_back(original);

        std::vector<Vtx_t> source;
        source.reserve(original->VertexList.size());
        for (const auto& vertex : original->VertexList) {
            source.push_back(vertex.v);
        }
        std::vector<NodeVertices>& replaced = draw_.starVertices.emplace_back(1);
        replaced[0].node = node.offset;
        std::vector<Vtx_t>& vertices = replaced[0].vertices = source;
        for (size_t base = 0; base + 4 <= source.size(); base += 4) {
            const auto angle = static_cast<uint16_t>((RandomGetNext() << 10) & 0xffff);
            const int32_t sine = ((Sins(angle) << 5) * 181) >> 18;
            const int32_t cosine = ((Coss(angle) << 5) * 181) >> 18;
            const auto offset = static_cast<size_t>(RandomGetNext() >> 31);
            const int32_t scale = 0x10000 - static_cast<int32_t>(RandomGetNext() & 0x3fff);
            const auto corner1 = static_cast<int16_t>(0x200 + sine);
            const auto corner2 = static_cast<int16_t>(0x200 - sine);
            const auto corner3 = static_cast<int16_t>(0x200 - cosine);
            const auto corner4 = static_cast<int16_t>(0x200 + cosine);

            const std::array<std::array<int16_t, 2>, 4> texture = { {
                { corner3, corner2 },
                { corner1, corner3 },
                { corner4, corner1 },
                { corner2, corner4 },
            } };
            for (size_t i = 0; i < 4; i++) {
                Vtx_t& vertex = vertices[base + i];
                const Vtx_t& from = source[base + ((offset + i) % 4)];
                for (size_t axis = 0; axis < 3; axis++) {
                    vertex.ob[axis] = static_cast<int16_t>((static_cast<int64_t>(from.ob[axis]) * scale) >> 16);
                }
                vertex.tc[0] = texture[i][0];
                vertex.tc[1] = texture[i][1];
            }
        }

        draw_.commands.push_back(FogColorBlack());
        return CallNodeList(resourceManager_, modelPath, node, node.primaryList, draw_.commands, draw_.lists,
                            &replaced);
    }

    // doshadow(): the ground shadow, at the model's shadow level, in the
    // secondary pass.
    bool DrawShadow(uint32_t pass, size_t modelIndex, const ModelInstance& model, const JointModel& jointModel,
                    const VisitedNode& visited, const ModelNode& node) {
        if (render_.shadowLevel <= 0 || pass != kSecondaryPass) {
            return true;
        }
        const Mat4* mtx = FindNodeMatrix(*jointModel.nodes, *jointModel.walk, static_cast<int32_t>(visited.node));
        if (mtx == nullptr) {
            return true;
        }
        const bool scaledByEnvironment = render_.propType == kRenderPropChr || render_.propType == kRenderPropSmoke;
        const uint8_t alpha = ShadowAlpha(scaledByEnvironment, render_.envColour, render_.shadowLevel);
        const float position[2] = { node.sortPoint[0], node.sortPoint[2] };
        const float size[2] = { node.effectSize[0], node.effectSize[1] };
        const EffectImage image{ node.effectWidth, node.effectHeight };
        // How high the root is above its ground: the sort's shadowHeight is the
        // other way about.
        const EffectQuad quad = BuildShadowQuad(position, size, -jointModel.shadowHeight, model.playback.scale,
                                                render_.zbufferEnabled, alpha, node.effectTextured ? &image : nullptr);
        return DrawEffectQuad(modelIndex, model, jointModel, *mtx, node, quad);
    }

    // The tail of dogfnegx() and doshadow(): select the image, then draw the
    // quad as two triangles under the node's matrix.
    bool DrawEffectQuad(size_t modelIndex, const ModelInstance& model, const JointModel& jointModel, const Mat4& mtx,
                        const ModelNode& node, const EffectQuad& quad) {
        const auto index = static_cast<size_t>(&mtx - jointModel.walk->matrices.data());
        return AppendEffectQuad(resourceManager_, render_, model.Path(), node, quad, &draw_.matrices[modelIndex][index],
                                draw_.commands, draw_.lists, draw_.vertices);
    }

    const std::shared_ptr<Ship::ResourceManager>& resourceManager_;
    const ModelRenderParams& render_;
    JointDraw& draw_;
};

} // namespace

namespace {

// texSelect() for an effect's image, or for none: the render mode, texture
// and combiner an effect quad draws with, and the image's own load. Only
// the base level of a mipmapped image exists (docs/formats/
// goldeneye-display-list-resource.md), so it draws as a single tile
// whatever the entry's level count. Returns false if the image's list
// fails to load.
bool SelectEffectTexture(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const ModelRenderParams& render,
                         const std::string& modelPath, const ModelNode& node, std::vector<Gfx>& out,
                         std::vector<std::shared_ptr<Ship::IResource>>& lists) {
    bool textured = node.effectTextured;
    std::optional<ListCall> image;
    if (textured) {
        image = CallList(resourceManager, fmt::format("{}/fx/{:06x}", modelPath, node.offset));
        if (!image) {
            return false;
        }
        lists.push_back(image->list);
        // An image the load-time expansion couldn't set up would leave the
        // draw sampling nothing.
        auto list = std::dynamic_pointer_cast<Fast::DisplayList>(image->list);
        textured = list != nullptr && ListLoadsTextureBeforeDrawing(*list);
    }

    const bool z = render.zbufferEnabled;
    out.push_back(PipeSync());
    out.push_back(F3dex::SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE));
    if (textured) {
        out.push_back(F3dex::SetOtherModeL(
            G_MDSFT_RENDERMODE, 29, z ? (G_RM_ZB_CLD_SURF | G_RM_ZB_CLD_SURF2) : (G_RM_CLD_SURF | G_RM_CLD_SURF2)));
    } else {
        out.push_back(F3dex::SetOtherModeL(G_MDSFT_RENDERMODE, 29,
                                           z ? (G_RM_AA_ZB_OPA_SURF | G_RM_AA_ZB_OPA_SURF2)
                                             : (G_RM_AA_OPA_SURF | G_RM_AA_OPA_SURF2)));
    }
    out.push_back(F3dex::Texture(0xffff, 0xffff, 0, G_TX_RENDERTILE, 1));

    if (!textured) {
        out.push_back(ShadeOnlyCombiner());
        return true;
    }

    out.push_back(F3dex::SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE));
    out.push_back(ModulateCombiner(node.effectFormat == kFormatI));
    out.insert(out.end(), image->commands.begin(), image->commands.end());
    if (node.effectFormat != kFormatCi) {
        out.push_back(F3dex::SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE));
    }
    return true;
}

} // namespace

bool AppendEffectQuad(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const ModelRenderParams& render,
                      const std::string& modelPath, const ModelNode& node, const EffectQuad& quad,
                      const Mtx::FixedMtx* matrix, std::vector<Gfx>& commands,
                      std::vector<std::shared_ptr<Ship::IResource>>& lists,
                      std::deque<std::array<Vtx_t, 4>>& vertexStore) {
    if (!SelectEffectTexture(resourceManager, render, modelPath, node, commands, lists)) {
        return false;
    }

    auto& vertices = vertexStore.emplace_back();
    for (size_t i = 0; i < quad.size(); i++) {
        Vtx_t& vertex = vertices[i];
        for (int axis = 0; axis < 3; axis++) {
            vertex.ob[axis] = quad[i].position[axis];
        }
        vertex.flag = 0;
        vertex.tc[0] = quad[i].s;
        vertex.tc[1] = quad[i].t;
        for (int channel = 0; channel < 4; channel++) {
            vertex.cn[channel] = quad[i].colour[channel];
        }
    }

    commands.push_back(F3dex::SetGeometryMode(Fast::F3DEX_G_CULL_BACK));
    commands.push_back(F3dex::Matrix(matrix, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD));
    commands.push_back(F3dex::Vertices(vertices.data(), 4));
    commands.push_back(F3dex::Triangle(0, 1, 2));
    commands.push_back(F3dex::Triangle(2, 3, 0));
    return true;
}

bool DrawDisplayListNode(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const ModelRenderParams& render,
                         uint32_t pass, const std::string& modelPath, const ModelNode& node, std::vector<Gfx>& commands,
                         std::vector<std::shared_ptr<Ship::IResource>>& lists,
                         const std::vector<NodeVertices>* replaced) {
    if (node.type != kDisplayListNodeType && node.type != kDisplayListCollisionNodeType) {
        return true;
    }
    if (node.primaryList == kNoList) {
        return true;
    }
    const bool hasSecondary = node.secondaryList != kNoList;

    // modelRenderNodeGundl / modelRenderNodeDl: a node's primary list is drawn
    // in the first pass with its setup; in the second, a type-4 node's
    // secondary list is drawn with its own.
    if (pass == kPrimaryPass) {
        ApplyCullMode(commands, render);
        ApplyRenderModeType(commands, node.renderType, render, true);
        if (!CallNodeList(resourceManager, modelPath, node, node.primaryList, commands, lists, replaced)) {
            return false;
        }
        if (node.renderType == 3 && hasSecondary) {
            ApplyRenderModeType(commands, node.renderType, render, false);
            return CallNodeList(resourceManager, modelPath, node, node.secondaryList, commands, lists, replaced);
        }
    } else if (pass == kSecondaryPass && node.renderType == 4 && hasSecondary) {
        ApplyCullMode(commands, render);
        ApplyRenderModeType(commands, node.renderType, render, false);
        return CallNodeList(resourceManager, modelPath, node, node.secondaryList, commands, lists, replaced);
    }
    return true;
}

Frigate::JointModel MakeJointModel(const ModelInstance& model) {
    Frigate::JointModel out;
    out.nodes = &model.Nodes();
    out.walk = &model.Walk();
    // The walk records an attachment each time it reaches the placeholder,
    // which a body with several level-of-detail variants does more than once.
    if (model.HeadNodes() != nullptr) {
        out.attachmentNodes.assign(model.Walk().attachments.size(), model.HeadNodes());
    }
    const RootState& root = model.playback.root;
    out.shadowHeight = root.ground - root.pos.y;
    return out;
}

std::optional<JointDraw> BuildJointDraw(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                        const std::vector<std::shared_ptr<ModelInstance>>& models,
                                        const ModelRenderParams& render) {
    JointDraw draw;

    std::vector<JointModel> jointModels;
    jointModels.reserve(models.size());
    draw.matrices.reserve(models.size());
    for (size_t m = 0; m < models.size(); m++) {
        const ModelInstance& model = *models[m];
        jointModels.push_back(MakeJointModel(model));

        std::vector<Mtx::FixedMtx> fixed;
        for (const Mat4& mtx : model.Walk().matrices) {
            fixed.push_back(Mtx::ToFixedMtx(mtx.m));
        }
        draw.matrices.push_back(std::move(fixed));
    }

    Emitter emitter(resourceManager, render, draw);

    if (!render.sortJoints) {
        // subdraw: each model's nodes in the order the walk visited them, both
        // passes of a node before the next.
        for (size_t m = 0; m < models.size(); m++) {
            draw.commands.push_back(F3dex::Command(Fast::F3DEX_G_MOVEWORD, ((kMatrixSegment * 4) << 8) | G_MW_SEGMENT,
                                                   reinterpret_cast<uintptr_t>(draw.matrices[m].data())));
            for (const VisitedNode& visited : models[m]->Walk().visited) {
                for (uint32_t pass : { kPrimaryPass, kSecondaryPass }) {
                    if ((render.passes & pass) != 0 &&
                        !emitter.DrawNode(pass, m, *models[m], jointModels[m], visited)) {
                        return std::nullopt;
                    }
                }
            }
        }
        return draw;
    }

    const std::vector<JointEntry> entries = BuildSortedJointEntries(jointModels);
    // Draws the node `v` of an entry for the passes `mask` selects.
    const auto drawNode = [&](uint32_t mask, const JointEntry& entry, size_t v) {
        const ModelInstance& model = *models[entry.model];
        const VisitedNode& visited = model.Walk().visited[v];
        for (uint32_t pass : { kPrimaryPass, kSecondaryPass }) {
            if ((mask & pass) != 0 && !emitter.DrawNode(pass, entry.model, model, jointModels[entry.model], visited)) {
                return false;
            }
        }
        return true;
    };
    const auto drawEntry = [&](uint32_t mask, const JointEntry& entry) {
        draw.commands.push_back(F3dex::Command(Fast::F3DEX_G_MOVEWORD, ((kMatrixSegment * 4) << 8) | G_MW_SEGMENT,
                                               reinterpret_cast<uintptr_t>(draw.matrices[entry.model].data())));
        if (!drawNode(mask, entry, entry.visit)) {
            return false;
        }
        for (size_t v : JointEntryNodes(jointModels[entry.model], entry.visit)) {
            if (!drawNode(mask, entry, v)) {
                return false;
            }
        }
        return true;
    };
    const std::vector<uint32_t> calls = render.passes == 0 ? std::vector<uint32_t>{ kPrimaryPass, kSecondaryPass }
                                                           : std::vector<uint32_t>{ render.passes };
    for (uint32_t mask : calls) {
        for (const JointEntry& entry : entries) {
            if (!drawEntry(mask, entry)) {
                return std::nullopt;
            }
        }
    }
    return draw;
}

} // namespace Frigate::Model
