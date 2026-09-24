#include "model/JointDraw.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "fast/resource/type/DisplayList.h"
#include "gfx/GbiF3dex.h"
#include "model/JointDrawList.h"
#include "model/ModelDraw.h"
#include "ship/resource/Resource.h"

namespace Frigate::Model {

namespace {

// Model lists address their node matrices in segment 3.
constexpr uint32_t kMatrixSegment = 3;

// The passes' flags in the decomp's ModelRenderData: primary lists, then
// secondary lists.
constexpr uint32_t kPrimaryPass = 1;
constexpr uint32_t kSecondaryPass = 2;

constexpr uint8_t kDisplayListNodeType = 0x04;
constexpr uint8_t kDisplayListCollisionNodeType = 0x18;
constexpr uint8_t kGunfireNodeType = 0x0c;
constexpr uint8_t kShadowNodeType = 0x0d;
constexpr uint8_t kDisplayListPrimaryNodeType = 0x16;

class Emitter {
  public:
    Emitter(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const ModelRenderParams& render,
            JointDraw& draw)
        : resourceManager_(resourceManager), render_(render), draw_(draw) {
    }

    // Draws one node of `model` for `pass`. Returns false if a list fails to load.
    bool DrawNode(uint32_t pass, const ModelInstance& model, const JointModel& jointModel, const VisitedNode& visited) {
        const ModelNode& node = VisitedModelNode(jointModel, visited);
        const std::string& path = visited.attachment < 0 ? model.Path() : model.HeadPath();

        switch (node.type) {
            case kDisplayListNodeType:
            case kDisplayListCollisionNodeType:
                return DrawDisplayListNode(pass, path, node);
            case kGunfireNodeType:
            case kShadowNodeType:
            case kDisplayListPrimaryNodeType:
                // TODO(https://github.com/liz-baker/frigate/issues/99): draw the muzzle flash, shadow and star
                // gunfire nodes.
                return true;
            default:
                return true;
        }
    }

  private:
    // modelRenderNodeGundl / modelRenderNodeDl: a node's primary list is drawn
    // in the first pass with its setup; in the second, a type-4 node's
    // secondary list is drawn with its own.
    bool DrawDisplayListNode(uint32_t pass, const std::string& path, const ModelNode& node) {
        if (node.primaryList == kNoList) {
            return true;
        }
        const bool hasSecondary = node.secondaryList != kNoList;

        if (pass == kPrimaryPass) {
            ApplyCullMode(draw_.commands, render_);
            ApplyRenderModeType(draw_.commands, node.renderType, render_, true);
            if (!CallNodeList(path, node, node.primaryList)) {
                return false;
            }
            if (node.renderType == 3 && hasSecondary) {
                ApplyRenderModeType(draw_.commands, node.renderType, render_, false);
                return CallNodeList(path, node, node.secondaryList);
            }
        } else if (pass == kSecondaryPass && node.renderType == 4 && hasSecondary) {
            ApplyCullMode(draw_.commands, render_);
            ApplyRenderModeType(draw_.commands, node.renderType, render_, false);
            return CallNodeList(path, node, node.secondaryList);
        }
        return true;
    }

    bool CallNodeList(const std::string& modelPath, const ModelNode& node, uint32_t list) {
        const std::string listPath = fmt::format("{}/dl/{:06x}/{:06x}", modelPath, node.offset, list);
        auto call = CallList(resourceManager_, listPath);
        if (!call) {
            return false;
        }
        draw_.lists.push_back(call->list);

        // The backend crashes on a draw that samples a texture nothing loaded,
        // so a list that never loads one is drawn shade-only instead.
        if (auto list = std::dynamic_pointer_cast<Fast::DisplayList>(call->list);
            list != nullptr && !ListLoadsTextureBeforeDrawing(*list)) {
            DisableListSampling(*list);
            draw_.commands.push_back(ShadeOnlyCombiner());
        }
        draw_.commands.insert(draw_.commands.end(), call->commands.begin(), call->commands.end());
        return true;
    }

    const std::shared_ptr<Ship::ResourceManager>& resourceManager_;
    const ModelRenderParams& render_;
    JointDraw& draw_;
};

} // namespace

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
        if (model.HeadNodes() != nullptr) {
            jointModels[m].attachmentNodes.push_back(model.HeadNodes());
        }
        const RootState& root = model.playback.root;
        jointModels[m].shadowHeight = root.ground - root.pos.y;

        std::vector<Mtx::FixedMtx> fixed;
        for (const Mat4& mtx : model.Walk().matrices) {
            fixed.push_back(Mtx::ToFixedMtx(mtx.m));
        }
        draw.matrices.push_back(std::move(fixed));
    }

    const std::vector<JointEntry> entries = BuildSortedJointEntries(jointModels);

    Emitter emitter(resourceManager, render, draw);
    for (uint32_t pass : { kPrimaryPass, kSecondaryPass }) {
        for (const JointEntry& entry : entries) {
            const ModelInstance& model = *models[entry.model];
            const JointModel& jointModel = jointModels[entry.model];
            const auto& visited = model.Walk().visited;

            draw.commands.push_back(F3dex::Command(Fast::F3DEX_G_MOVEWORD, ((kMatrixSegment * 4) << 8) | G_MW_SEGMENT,
                                                   reinterpret_cast<uintptr_t>(draw.matrices[entry.model].data())));

            if (!emitter.DrawNode(pass, model, jointModel, visited[entry.visit])) {
                return std::nullopt;
            }
            for (size_t v : JointEntryNodes(jointModel, entry.visit)) {
                if (!emitter.DrawNode(pass, model, jointModel, visited[v])) {
                    return std::nullopt;
                }
            }
        }
    }
    return draw;
}

} // namespace Frigate::Model
