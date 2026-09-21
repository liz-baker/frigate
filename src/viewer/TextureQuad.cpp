#include "viewer/TextureQuad.h"

#include <spdlog/spdlog.h>

#include "fast/resource/type/Texture.h"
#include "resource/factory/DisplayListFactory.h"
#include "ship/resource/ResourceManager.h"
#include "ship/utils/StrHash64.h"
#include "texture/TexExpand.h"

namespace Frigate::Viewer {

namespace {

constexpr uint32_t kSetTex = 0xc0;
constexpr uint32_t kTypeTile = 3;
constexpr uint32_t kTexRect = 0xe4;
constexpr uint32_t kRdpHalf1 = 0xe1;
constexpr uint32_t kRdpHalf2 = 0xf1;
constexpr int32_t kOriginX = 16;
constexpr int32_t kOriginY = 16;

} // namespace

std::vector<Gfx> BuildTextureQuad(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                  const std::string& texturePath, const std::vector<Gfx>& prefix) {
    uint64_t hash = CRC64(texturePath.c_str());
    auto texture = std::dynamic_pointer_cast<Fast::Texture>(resourceManager->LoadResource(hash));
    auto image = texture ? ImageInfoFor(*texture) : std::nullopt;
    if (!image) {
        spdlog::error("viewer: cannot load texture '{}'", texturePath);
        return {};
    }

    // A request with clamped s and t and no sample offset (the interpreter
    // crops a row to the tile's width only when the tile clamps, which a
    // width that is not a power of two needs), then the reference the model
    // lists emit after it.
    std::vector<TexExpand::Word> request = {
        { (kSetTex << 24) | (1u << 22) | (1u << 20) | kTypeTile, 0 },
        { (uint32_t{ 0x20 } << 24) | (uint32_t{ image->format } << 21) | (uint32_t{ image->size } << 19) |
              (image->width - 1u),
          0 },
        { static_cast<uint32_t>(hash >> 32), static_cast<uint32_t>(hash) },
    };
    TexExpand::Stats stats;
    auto lookup = [&image](uint64_t) { return image; };
    std::vector<TexExpand::Word> words = TexExpand::ExpandSetTex(request, lookup, &stats);
    if (stats.expanded != 1) {
        spdlog::error("viewer: '{}' is not a texture the expansion handles", texturePath);
        return {};
    }

    using namespace F3dex;
    std::vector<Gfx> out = prefix;
    out.push_back(SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE));
    out.push_back(SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_POINT));
    out.push_back(SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_OPA_SURF | G_RM_OPA_SURF2));
    Gfx combine;
    Gfx* pkt = &combine;
    gDPSetCombineMode(pkt, G_CC_DECALRGBA, G_CC_DECALRGBA);
    out.push_back(combine);
    for (const auto& word : words) {
        Gfx cmd;
        cmd.words.w0 = word.w0;
        cmd.words.w1 = word.w1;
        out.push_back(cmd);
    }

    // Coordinates are U10.2 screen pixels; s/t start at 0 and step one texel
    // per pixel (S5.10).
    uint32_t xh = (kOriginX + image->width) << 2;
    uint32_t yh = (kOriginY + image->height) << 2;
    out.push_back(Command(kTexRect, (xh << 12) | yh, (uint32_t{ kOriginX << 2 } << 12) | (kOriginY << 2)));
    out.push_back(Command(kRdpHalf1, 0, 0));
    out.push_back(Command(kRdpHalf2, 0, (1u << 10) << 16 | (1u << 10)));
    out.push_back(EndDisplayList());
    return out;
}

} // namespace Frigate::Viewer
