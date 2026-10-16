#include "resource/factory/DisplayListFactory.h"

#include <optional>

#include "fast/resource/factory/DisplayListFactory.h"
#include "fast/resource/type/DisplayList.h"
#include "fast/resource/type/Texture.h"
#include "menu/CVars.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/resource/ResourceIdentifier.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/archive/ArchiveManager.h"
#include "ship/utils/StrHash64.h"
#include "texture/TexExpand.h"

namespace Frigate {

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

std::optional<TexExpand::ImageInfo> LoadImageInfo(Ship::ResourceManager& resourceManager, uint64_t hash,
                                                  bool mipChains) {
    auto texture = std::dynamic_pointer_cast<Fast::Texture>(resourceManager.LoadResource(hash));
    if (!texture) {
        return std::nullopt;
    }
    auto image = ImageInfoFor(*texture);
    if (!image) {
        return image;
    }

    const char* name = resourceManager.GetArchiveManager()->HashToCString(hash);
    if (name == nullptr) {
        return image->format == 2 ? std::nullopt : image;
    }

    // A mipmapped image's whole chain: docs/formats/goldeneye-texture-mip-chain.md.
    uint64_t mipHash = mipChains ? CRC64((std::string(name) + ".mip").c_str()) : 0;
    if (mipChains && resourceManager.GetArchiveManager()->HashToCString(mipHash) != nullptr) {
        auto chain = std::dynamic_pointer_cast<Fast::Texture>(resourceManager.LoadResource(mipHash));
        uint8_t levels = chain ? TexExpand::MipLevelsForChainSize(*image, chain->ImageDataSize) : 0;
        if (levels >= 2) {
            image->mipHash = mipHash;
            image->mipLevels = levels;
        }
    }

    if (image->format != 2) {
        return image;
    }
    uint64_t paletteHash = CRC64((std::string(name) + ".pal").c_str());
    auto palette = std::dynamic_pointer_cast<Fast::Texture>(resourceManager.LoadResource(paletteHash));
    if (!palette || palette->Width == 0) {
        return std::nullopt;
    }
    image->paletteHash = paletteHash;
    image->paletteEntries = palette->Width;
    return image;
}

void UnloadExpandedDisplayLists(Ship::ResourceManager& resourceManager) {
    for (const char* mask : { "models/*/dl/*", "bg/*/dl/*" }) {
        const auto paths = resourceManager.GetArchiveManager()->ListFiles({ mask }, {});
        for (const std::string& path : *paths) {
            resourceManager.UnloadResource(Ship::ResourceIdentifier(CRC64(path.c_str())));
        }
    }
}

ResourceFactoryBinaryDisplayListV0::ResourceFactoryBinaryDisplayListV0(
    std::weak_ptr<Ship::ResourceManager> resourceManager, std::weak_ptr<Ship::ConsoleVariable> consoleVariables)
    : mResourceManager(std::move(resourceManager)), mConsoleVariables(std::move(consoleVariables)) {
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

    auto consoleVariables = mConsoleVariables.lock();
    const bool mipChains =
        consoleVariables == nullptr || consoleVariables->GetInteger(ConfigMenu::kMipChainsCVar, 1) != 0;
    auto lookup = [&resourceManager, mipChains](uint64_t hash) -> std::optional<TexExpand::ImageInfo> {
        return LoadImageInfo(*resourceManager, hash, mipChains);
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
