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

// True if a G_SETCOMBINE's RGB or alpha inputs read either texel.
bool CombinerSamplesTexture(const Gfx& cmd) {
    uint32_t w0 = cmd.words.w0;
    uint32_t w1 = cmd.words.w1;
    auto isTexel = [](uint32_t input) { return input == 1 || input == 2; };
    // RGB c also has TEXEL0/1_ALPHA at 8 and 9.
    auto isTexelC = [&](uint32_t input) { return isTexel(input) || input == 8 || input == 9; };
    for (int cycle = 0; cycle < 2; cycle++) {
        uint32_t a = cycle == 0 ? (w0 >> 20) & 0xf : (w0 >> 5) & 0xf;
        uint32_t c = cycle == 0 ? (w0 >> 15) & 0x1f : w0 & 0x1f;
        uint32_t aa = cycle == 0 ? (w0 >> 12) & 7 : (w1 >> 21) & 7;
        uint32_t ac = cycle == 0 ? (w0 >> 9) & 7 : (w1 >> 18) & 7;
        uint32_t b = cycle == 0 ? (w1 >> 28) & 0xf : (w1 >> 24) & 0xf;
        uint32_t d = cycle == 0 ? (w1 >> 15) & 7 : (w1 >> 6) & 7;
        uint32_t ab = cycle == 0 ? (w1 >> 12) & 7 : (w1 >> 3) & 7;
        uint32_t ad = cycle == 0 ? (w1 >> 9) & 7 : w1 & 7;
        if (isTexel(a) || isTexelC(c) || isTexel(b) || isTexel(d) || isTexel(aa) || isTexel(ac) || isTexel(ab) ||
            isTexel(ad)) {
            return true;
        }
    }
    return false;
}

// True if the list loads a texture and never draws or calls a nested list
// with its combiner sampling a texture before that first load. The
// interpreter imports a texture only when a load changes it, so a sampling
// draw with nothing loaded leaves the backend indexing an empty texture
// table and crashing. A list may draw untextured pieces before its first
// load (the legal page's does), and a list that sets no combiner is assumed
// to sample.
bool LoadsTextureBeforeDrawing(const Fast::DisplayList& list) {
    const auto& instructions = list.Instructions;
    bool loaded = false;
    bool sampling = true;
    bool unsafe = false;
    for (size_t i = 0; i < instructions.size();
         i += Frigate::F3dex::IsExpandedCommand(instructions[i].words.w0 >> 24) ? 2 : 1) {
        auto op = static_cast<uint8_t>(instructions[i].words.w0 >> 24);
        if (op == kSetTex) {
            return false;
        }
        if (op == kLoadBlock) {
            loaded = true;
        } else if (op == kSetCombine) {
            sampling = CombinerSamplesTexture(instructions[i]);
        } else if (!loaded && sampling && (op == kTri1 || op == kTri2 || op == kQuad || op == G_DL_OTR_HASH)) {
            unsafe = true;
        }
    }
    return loaded && !unsafe;
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

bool ListLoadsTextureBeforeDrawing(const Fast::DisplayList& list) {
    return LoadsTextureBeforeDrawing(list);
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
                if ((render->passes & pass) != 0 &&
                    !DrawDisplayListNode(resourceManager, *render, pass, modelPath, node, out, draw.lists,
                                         &draw.vertices)) {
                    return std::nullopt;
                }
                // dogfnegx: a muzzle flash that shows draws over the model.
                if (pass == kSecondaryPass && (render->passes & pass) != 0 && node.type == kGunfireNodeType &&
                    std::find(flashes.begin(), flashes.end(), node.offset) != flashes.end()) {
                    const Mat4* mtx = FindNodeMatrix(nodes, walk, static_cast<int32_t>(visited.node));
                    if (mtx != nullptr) {
                        const EffectImage image{ node.effectWidth, node.effectHeight };
                        const EffectQuad quad = BuildGunfireQuad(*mtx, node.sortPoint, node.effectSize,
                                                                 overrides->modelScale,
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
            if (screen.slot >= tree->Switches.size() || tree->Switches[screen.slot] < 0) {
                continue;
            }
            const int32_t index = tree->Switches[screen.slot];
            const ModelNode& node = nodes[static_cast<size_t>(index)];
            if (node.type != kDisplayListCollisionNodeType) {
                continue;
            }
            const uint64_t vertexHash = CRC64(fmt::format("{}/vtx/{:06x}", modelPath, node.offset).c_str());
            auto original = std::dynamic_pointer_cast<Fast::Vertex>(resourceManager->LoadResource(vertexHash));
            if (original == nullptr || original->VertexList.size() < 4) {
                spdlog::error("model: '{}/vtx/{:06x}' does not load", modelPath, node.offset);
                return std::nullopt;
            }
            draw.lists.push_back(original);
            const Mat4* mtx = FindNodeMatrix(nodes, walk, index);
            if (mtx == nullptr) {
                continue;
            }
            auto& quad = draw.flashVertices.emplace_back();
            for (size_t i = 0; i < 4; i++) {
                quad[i] = original->VertexList[i].v;
                quad[i].tc[0] = screen.s[i];
                quad[i].tc[1] = screen.t[i];
                for (size_t channel = 0; channel < 4; channel++) {
                    quad[i].cn[channel] = screen.rgba[channel];
                }
            }
            out.push_back(SetGeometryMode(Fast::F3DEX_G_CULL_BACK));
            if (screen.select) {
                screen.select(out);
            }
            const auto matrix = static_cast<size_t>(mtx - walk.matrices.data());
            out.push_back(Matrix(&draw.matrices[matrix], Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD));
            out.push_back(Vertices(quad.data(), 4));
            out.push_back(Triangle(0, 1, 2));
            out.push_back(Triangle(0, 2, 3));
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

        bool loadsTexture = LoadsTextureBeforeDrawing(*list);
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
    spdlog::info("model: '{}': {} of {} lists load their own texture", modelPath, textured, walk.lists.size());
    return draw;
}

} // namespace Frigate::Model
