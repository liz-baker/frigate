#include "resource/factory/DisplayListFactory.h"

#include <optional>

#include "fast/resource/factory/DisplayListFactory.h"
#include "fast/resource/type/DisplayList.h"
#include "fast/resource/type/Texture.h"
#include "ship/resource/ResourceManager.h"
#include "texture/TexExpand.h"

namespace Frigate {

namespace {

// N64 format/size for a Fast::Texture's type, matching how the interpreter
// decodes it.
std::optional<TexExpand::ImageInfo> ImageInfoFor(const Fast::Texture& texture) {
    enum { kRgba = 0, kCi = 2, kIa = 3, kI = 4 };
    enum { k4 = 0, k8 = 1, k16 = 2, k32 = 3 };

    uint8_t format;
    uint8_t size;
    switch (texture.Type) {
        case Fast::TextureType::RGBA32bpp:
            format = kRgba, size = k32;
            break;
        case Fast::TextureType::RGBA16bpp:
            format = kRgba, size = k16;
            break;
        case Fast::TextureType::Palette4bpp:
            format = kCi, size = k4;
            break;
        case Fast::TextureType::Palette8bpp:
            format = kCi, size = k8;
            break;
        case Fast::TextureType::Grayscale4bpp:
            format = kI, size = k4;
            break;
        case Fast::TextureType::Grayscale8bpp:
            format = kI, size = k8;
            break;
        case Fast::TextureType::GrayscaleAlpha4bpp:
            format = kIa, size = k4;
            break;
        case Fast::TextureType::GrayscaleAlpha8bpp:
            format = kIa, size = k8;
            break;
        case Fast::TextureType::GrayscaleAlpha16bpp:
            format = kIa, size = k16;
            break;
        default:
            return std::nullopt;
    }
    return TexExpand::ImageInfo{ format, size, texture.Width, texture.Height };
}

} // namespace

ResourceFactoryBinaryDisplayListV0::ResourceFactoryBinaryDisplayListV0(
    std::weak_ptr<Ship::ResourceManager> resourceManager)
    : mResourceManager(std::move(resourceManager)) {
}

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryDisplayListV0::ReadResource(std::shared_ptr<Ship::File> file,
                                                 std::shared_ptr<Ship::ResourceInitData> initData) {
    auto resource = Fast::ResourceFactoryBinaryDisplayListV0().ReadResource(std::move(file), std::move(initData));
    auto displayList = std::static_pointer_cast<Fast::DisplayList>(resource);
    auto resourceManager = mResourceManager.lock();
    if (!displayList || !resourceManager) {
        return resource;
    }

    std::vector<TexExpand::Word> words;
    words.reserve(displayList->Instructions.size());
    for (const Gfx& command : displayList->Instructions) {
        words.push_back({ static_cast<uint32_t>(command.words.w0), static_cast<uint32_t>(command.words.w1) });
    }

    auto lookup = [&resourceManager](uint64_t hash) -> std::optional<TexExpand::ImageInfo> {
        auto texture = std::static_pointer_cast<Fast::Texture>(resourceManager->LoadResource(hash));
        if (!texture) {
            return std::nullopt;
        }
        return ImageInfoFor(*texture);
    };

    words = TexExpand::ExpandSetTex(words, lookup);

    displayList->Instructions.clear();
    for (const TexExpand::Word& word : words) {
        Gfx command = {};
        command.words.w0 = word.w0;
        command.words.w1 = word.w1;
        displayList->Instructions.push_back(command);
    }
    return displayList;
}

} // namespace Frigate
