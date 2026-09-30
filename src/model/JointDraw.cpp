#include "model/JointDraw.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "fast/resource/type/DisplayList.h"
#include "gfx/GbiF3dex.h"
#include "model/EffectQuads.h"
#include "model/JointDrawList.h"
#include "model/ModelDraw.h"
#include "ship/resource/Resource.h"

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

// The backend crashes on a draw that samples a texture nothing loaded, so a
// list that never loads one is drawn shade-only instead.
bool CallNodeList(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const std::string& modelPath,
                  const ModelNode& node, uint32_t list, std::vector<Gfx>& commands,
                  std::vector<std::shared_ptr<Ship::IResource>>& lists) {
    const std::string listPath = fmt::format("{}/dl/{:06x}/{:06x}", modelPath, node.offset, list);
    auto call = CallList(resourceManager, listPath);
    if (!call) {
        return false;
    }
    lists.push_back(call->list);

    if (auto list = std::dynamic_pointer_cast<Fast::DisplayList>(call->list);
        list != nullptr && !ListLoadsTextureBeforeDrawing(*list)) {
        DisableListSampling(*list);
        commands.push_back(ShadeOnlyCombiner());
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
            case kDisplayListCollisionNodeType:
                return DrawDisplayListNode(resourceManager_, render_, pass, path, node, draw_.commands, draw_.lists);
            case kGunfireNodeType:
                return DrawGunfire(pass, modelIndex, model, jointModel, visited, node);
            case kShadowNodeType:
                return DrawShadow(pass, modelIndex, model, jointModel, visited, node);
            case kDisplayListPrimaryNodeType:
                // TODO(https://github.com/liz-baker/frigate/issues/110): draw the star gunfire nodes.
                return true;
            default:
                return true;
        }
    }

  private:
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
        if (!SelectEffectTexture(model.Path(), node)) {
            return false;
        }

        auto& vertices = draw_.vertices.emplace_back();
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

        const auto index = static_cast<size_t>(&mtx - jointModel.walk->matrices.data());
        auto& out = draw_.commands;
        out.push_back(F3dex::SetGeometryMode(Fast::F3DEX_G_CULL_BACK));
        out.push_back(
            F3dex::Matrix(&draw_.matrices[modelIndex][index], Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD));
        out.push_back(F3dex::Vertices(vertices.data(), 4));
        out.push_back(F3dex::Triangle(0, 1, 2));
        out.push_back(F3dex::Triangle(2, 3, 0));
        return true;
    }

    // texSelect() for an effect's image, or for none: the render mode, texture
    // and combiner an effect quad draws with, and the image's own load. Only
    // the base level of a mipmapped image exists (docs/formats/
    // goldeneye-display-list-resource.md), so it draws as a single tile
    // whatever the entry's level count. Returns false if the image's list
    // fails to load.
    bool SelectEffectTexture(const std::string& modelPath, const ModelNode& node) {
        bool textured = node.effectTextured;
        std::optional<ListCall> image;
        if (textured) {
            image = CallList(resourceManager_, fmt::format("{}/fx/{:06x}", modelPath, node.offset));
            if (!image) {
                return false;
            }
            draw_.lists.push_back(image->list);
            // An image the load-time expansion couldn't set up would leave the
            // draw sampling nothing.
            auto list = std::dynamic_pointer_cast<Fast::DisplayList>(image->list);
            textured = list != nullptr && ListLoadsTextureBeforeDrawing(*list);
        }

        const bool z = render_.zbufferEnabled;
        auto& out = draw_.commands;
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

    const std::shared_ptr<Ship::ResourceManager>& resourceManager_;
    const ModelRenderParams& render_;
    JointDraw& draw_;
};

} // namespace

bool DrawDisplayListNode(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const ModelRenderParams& render,
                         uint32_t pass, const std::string& modelPath, const ModelNode& node, std::vector<Gfx>& commands,
                         std::vector<std::shared_ptr<Ship::IResource>>& lists) {
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
        if (!CallNodeList(resourceManager, modelPath, node, node.primaryList, commands, lists)) {
            return false;
        }
        if (node.renderType == 3 && hasSecondary) {
            ApplyRenderModeType(commands, node.renderType, render, false);
            return CallNodeList(resourceManager, modelPath, node, node.secondaryList, commands, lists);
        }
    } else if (pass == kSecondaryPass && node.renderType == 4 && hasSecondary) {
        ApplyCullMode(commands, render);
        ApplyRenderModeType(commands, node.renderType, render, false);
        return CallNodeList(resourceManager, modelPath, node, node.secondaryList, commands, lists);
    }
    return true;
}

std::optional<JointDraw> BuildJointDraw(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                        const std::vector<std::shared_ptr<ModelInstance>>& models,
                                        const ModelRenderParams& render) {
    JointDraw draw;

    std::vector<JointModel> jointModels(models.size());
    draw.matrices.reserve(models.size());
    for (size_t m = 0; m < models.size(); m++) {
        const ModelInstance& model = *models[m];
        jointModels[m].nodes = &model.Nodes();
        jointModels[m].walk = &model.Walk();
        // The walk records an attachment each time it reaches the placeholder,
        // which a body with several level-of-detail variants does more than once.
        if (model.HeadNodes() != nullptr) {
            jointModels[m].attachmentNodes.assign(model.Walk().attachments.size(), model.HeadNodes());
        }
        const RootState& root = model.playback.root;
        jointModels[m].shadowHeight = root.ground - root.pos.y;

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
