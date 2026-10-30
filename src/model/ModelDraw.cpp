#include "model/ModelDraw.h"

#include <algorithm>
#include <cstring>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "fast/resource/type/DisplayList.h"
#include "fast/resource/type/Vertex.h"
#include "gfx/GbiF3dex.h"
#include "model/JointDraw.h"
#include "resource/type/ModelTree.h"
#include "ship/resource/ResourceManager.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Model {

namespace {

// A G_SETTEX the load-time expansion left as stored (see TexExpand); it never
// becomes RDP commands, so the draw has no texture bound.
constexpr uint8_t kDisplayListCollisionNodeType = 0x18;
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

// Group (0x02, 0x03) and simple group (0x15) nodes carry an origin.
bool IsGroupNodeType(uint8_t type) {
    return type == 0x02 || type == 0x03 || type == 0x15;
}

// True if the list sets up a texture to sample. A request the load-time
// expansion couldn't resolve is still a G_SETTEX, which leaves nothing loaded.
// A list may draw before its first load (a far-LOD leg does), with whatever
// texture the previous list left loaded, as the original does.
bool LoadsTexture(const Fast::DisplayList& list) {
    const auto& instructions = list.Instructions;
    bool loaded = false;
    for (size_t i = 0; i < instructions.size();
         i += Frigate::F3dex::IsExpandedCommand(instructions[i].words.w0 >> 24) ? 2 : 1) {
        auto op = static_cast<uint8_t>(instructions[i].words.w0 >> 24);
        if (op == kSetTex) {
            return false;
        }
        if (op == kLoadBlock) {
            loaded = true;
        }
    }
    return loaded;
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
    for (size_t i = 0; i < instructions.size();
         i += Frigate::F3dex::IsExpandedCommand(instructions[i].words.w0 >> 24) ? 2 : 1) {
        auto op = static_cast<uint8_t>(instructions[i].words.w0 >> 24);
        if (op == kSetCombine) {
            instructions[i] = CombineShade();
        } else if (op == kTexture) {
            instructions[i] = Frigate::F3dex::TextureOff();
        }
    }
}

} // namespace

bool ListLoadsTexture(const Fast::DisplayList& list) {
    return LoadsTexture(list);
}

bool ListDraws(const Fast::DisplayList& list) {
    const auto& instructions = list.Instructions;
    for (size_t i = 0; i < instructions.size();
         i += Frigate::F3dex::IsExpandedCommand(instructions[i].words.w0 >> 24) ? 2 : 1) {
        const auto op = static_cast<uint8_t>(instructions[i].words.w0 >> 24);
        if (op == kTri1 || op == kTri2 || op == kQuad || op == G_DL_OTR_HASH) {
            return true;
        }
    }
    return false;
}

void DisableListSampling(Fast::DisplayList& list) {
    DisableSampling(list);
}

Gfx ShadeOnlyCombiner() {
    return CombineShade();
}

std::optional<ListCall> CallList(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                 const std::string& path) {
    uint64_t hash = CRC64(path.c_str());
    auto list = resourceManager->LoadResource(hash);
    if (list == nullptr) {
        spdlog::error("model: failed to load '{}'", path);
        return std::nullopt;
    }
    ListCall call{ {}, list };
    Frigate::F3dex::DisplayListByHash(call.commands.data(), hash);
    return call;
}

const Mat4* ModelDraw::SwitchMatrix(size_t slot) const {
    if (tree == nullptr || slot >= tree->Switches.size() || tree->Switches[slot] == kNoNode) {
        return nullptr;
    }
    return FindNodeMatrix(tree->Nodes, walk, tree->Switches[slot]);
}

const Mat4* ModelDraw::NodeMatrix(int32_t node) const {
    if (tree == nullptr || node == kNoNode) {
        return nullptr;
    }
    return FindNodeMatrix(tree->Nodes, walk, node);
}

std::optional<ModelDraw> BuildModelDraw(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                        const std::string& modelPath, const Mat4& base, bool textures,
                                        const std::vector<Gfx>& prefix,
                                        const std::optional<std::array<float, 3>>& instanceOffset, bool hideSwitches,
                                        const ModelRenderParams* render, ModelDrawOverrides* overrides) {
    auto tree =
        std::dynamic_pointer_cast<ModelTree>(resourceManager->LoadResource(CRC64((modelPath + "/tree").c_str())));
    if (tree == nullptr || tree->Nodes.empty()) {
        spdlog::error("model: no node tree at '{}/tree' in the loaded archives", modelPath);
        return std::nullopt;
    }

    std::vector<ModelNode> nodes = tree->Nodes;
    if (instanceOffset && IsGroupNodeType(nodes[0].type)) {
        std::copy(instanceOffset->begin(), instanceOffset->end(), nodes[0].origin);
    }

    ModelDraw draw;
    std::vector<uint32_t> hidden;
    if (hideSwitches) {
        for (const ModelNode& node : nodes) {
            if (node.type == kSwitchNodeType) {
                hidden.push_back(node.offset);
            }
        }
    }
    if (overrides != nullptr) {
        for (const uint8_t slot : overrides->hiddenSwitchSlots) {
            if (slot < tree->Switches.size() && tree->Switches[slot] >= 0) {
                hidden.push_back(nodes[static_cast<size_t>(tree->Switches[slot])].offset);
            }
        }
    }
    ModelWalk walk = overrides != nullptr && overrides->matrices
                         ? WalkModelWithMatrices(nodes, overrides->matrices(*tree), hidden)
                         : WalkModel(nodes, tree->MatrixCount, base, {}, hidden);
    if (overrides != nullptr) {
        draw.vertices = std::move(overrides->vertices);
    }
    draw.tree = tree;
    draw.walk = walk;
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

    if (render != nullptr) {
        std::vector<uint32_t> flashes;
        std::vector<ModelDrawOverrides::Screen> screens;
        if (overrides != nullptr && overrides->screens && (render->passes & kPrimaryPass) != 0) {
            screens = overrides->screens(*tree);
        }
        if (overrides != nullptr) {
            for (const uint8_t slot : overrides->visibleGunfireSlots) {
                if (slot < tree->Switches.size() && tree->Switches[slot] >= 0) {
                    flashes.push_back(nodes[static_cast<size_t>(tree->Switches[slot])].offset);
                }
            }
        }
        // subdraw: each node, both passes of it before the next.
        for (const VisitedNode& visited : walk.visited) {
            const ModelNode& node = nodes[visited.node];
            for (const uint32_t pass : { kPrimaryPass, kSecondaryPass }) {
                if ((render->passes & pass) != 0 && !DrawDisplayListNode(resourceManager, *render, pass, modelPath,
                                                                         node, out, draw.lists, &draw.vertices)) {
                    return std::nullopt;
                }
                // dogfnegx: a muzzle flash that shows draws over the model.
                if (pass == kSecondaryPass && (render->passes & pass) != 0 && node.type == kGunfireNodeType &&
                    std::find(flashes.begin(), flashes.end(), node.offset) != flashes.end()) {
                    const Mat4* mtx = FindNodeMatrix(nodes, walk, static_cast<int32_t>(visited.node));
                    if (mtx != nullptr) {
                        const EffectImage image{ node.effectWidth, node.effectHeight };
                        const EffectQuad quad =
                            BuildGunfireQuad(*mtx, node.sortPoint, node.effectSize, overrides->modelScale,
                                             node.effectTextured ? &image : nullptr);
                        const auto index = static_cast<size_t>(mtx - walk.matrices.data());
                        if (!AppendEffectQuad(resourceManager, *render, modelPath, node, quad, &draw.matrices[index],
                                              out, draw.lists, draw.flashVertices)) {
                            return std::nullopt;
                        }
                    }
                }
            }
        }
        // process_monitor_animation_microcode: each screen's quad, under the
        // model's own matrix.
        for (const ModelDrawOverrides::Screen& screen : screens) {
            if (!AppendScreen(resourceManager, modelPath, nodes, *tree, walk, draw.matrices, screen, out, draw.lists,
                              draw.flashVertices)) {
                return std::nullopt;
            }
        }
        return draw;
    }

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

        bool loadsTexture = LoadsTexture(*list);
        if (loadsTexture) {
            textured++;
        }
        if (!textures || !loadsTexture) {
            DisableSampling(*list);
        }
        out.push_back(textures && loadsTexture ? CombineModulate() : CombineShade());
        Gfx call[2];
        Frigate::F3dex::DisplayListByHash(call, hash);
        out.push_back(call[0]);
        out.push_back(call[1]);
    }
    spdlog::debug("model: '{}': {} of {} lists load their own texture", modelPath, textured, walk.lists.size());
    return draw;
}

} // namespace Frigate::Model
