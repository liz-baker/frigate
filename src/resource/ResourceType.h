#pragma once

#include <cstdint>

// Fourcc-style resource type identifiers for frigate's own GoldenEye-specific
// resource types (frigate issue #11). libultraship-wiiu's ResourceType.h only
// defines Blob/Json/Shader, and its ResourceLoader only auto-registers
// Json/Shader (see ResourceLoader::RegisterGlobalResourceFactories()) -- any
// other type, including these, must be registered by the consumer via
// Ship::ResourceLoader::RegisterResourceFactory().
//
// Chosen to avoid colliding with Ship::ResourceType's own values
// (Blob/Json/Shader) and with RESOURCE_TYPE_FONT
// (ship/window/gui/resource/Font.h) -- that's libultraship-wiiu's unrelated
// ImGui/TTF font loader for UI text, not GoldenEye's own bitmap font-bank
// format (see Frigate::Font).
namespace Frigate {

constexpr uint32_t kTextResourceType = 0x47455458; // "GETX" -- GoldenEye text bank
constexpr uint32_t kFontResourceType = 0x4745464E; // "GEFN" -- GoldenEye bitmap font bank

} // namespace Frigate
