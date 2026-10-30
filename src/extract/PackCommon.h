#pragma once

#include "extract/DisplayListEmit.h"
#include "extract/Error.h"
#include "extract/Extractor.h"
#include "extract/O2rWriter.h"
#include "extract/RomLayout.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace Frigate::Extract {

// Helpers the model and level packers share.

inline std::string Hex6(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%06x", value);
    return text;
}

inline bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

// The part of `path` after its last '/'.
inline std::string_view BaseName(std::string_view path) {
    return path.substr(path.rfind('/') == std::string_view::npos ? 0 : path.rfind('/') + 1);
}

inline std::string_view WithoutExtension(std::string_view name) {
    size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

constexpr uint32_t kEmbeddedImageResourceVersion = 1;

// Writes the lists of `lists` and clears them; the embedded images stay until WriteImages().
inline Error WriteLists(O2rWriter& writer, ReemittedLists& lists) {
    for (const EmittedList& list : lists.lists) {
        if (Error err =
                writer.AddResource(list.path, kDisplayListResourceType, kDisplayListResourceVersion, 0, list.payload);
            !err.ok()) {
            return err;
        }
    }
    lists.lists.clear();
    return {};
}

inline Error WriteImages(O2rWriter& writer, ReemittedLists& lists) {
    std::vector<uint8_t> payload;
    for (const EmbeddedImage& image : lists.images) {
        if (Error err = EncodeEmbeddedImagePayload(image, payload); !err.ok()) {
            return err;
        }
        if (Error err = writer.AddResource(image.path, "OTEX", kEmbeddedImageResourceVersion, 0, payload); !err.ok()) {
            return err;
        }
    }
    lists.images.clear();
    return {};
}

inline Error WriteVertexBlock(O2rWriter& writer, const std::string& path, std::span<const uint8_t> vertices,
                              std::vector<uint8_t>& scratch) {
    if (Error err = EncodeVertexPayload(vertices, scratch); !err.ok()) {
        return err;
    }
    return writer.AddResource(path, kVertexResourceType, kVertexResourceVersion, 0, scratch);
}

inline Error RequireKnownRegion(const StageContext& context, const char* what) {
    if (!context.layout().constants.known) {
        return Error::Make(ErrorCode::RegionUnsupported, std::string("The ") + GetRegionInfo(context.region()).name +
                                                             " ROM's " + what + " are not mapped yet.");
    }
    return {};
}

} // namespace Frigate::Extract
