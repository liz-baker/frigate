#pragma once

#include <cstdint>

// Fourcc-style resource type identifiers for frigate's own GoldenEye-specific
// resource types (frigate issue #11). libultraship's ResourceType.h only
// defines Blob/Json/Shader, and its ResourceLoader only auto-registers
// Json/Shader (see ResourceLoader::RegisterGlobalResourceFactories()) -- any
// other type, including these, must be registered by the consumer via
// Ship::ResourceLoader::RegisterResourceFactory().
//
// Chosen to avoid colliding with Ship::ResourceType's own values
// (Blob/Json/Shader) and with RESOURCE_TYPE_FONT
// (ship/window/gui/resource/Font.h) -- that's libultraship's unrelated
// ImGui/TTF font loader for UI text, not GoldenEye's own bitmap font-bank
// format (see Frigate::Font).
namespace Frigate {

constexpr uint32_t kTextResourceType = 0x47455458;           // "GETX" -- GoldenEye text bank
constexpr uint32_t kFontResourceType = 0x4745464E;           // "GEFN" -- GoldenEye bitmap font bank
constexpr uint32_t kDisplayListResourceType = 0x4745444C;    // "GEDL" -- display list with G_SETTEX requests
constexpr uint32_t kModelTreeResourceType = 0x47454D44;      // "GEMD" -- model node tree
constexpr uint32_t kModelSkeletonResourceType = 0x4745534B;  // "GESK" -- per-model joint table
constexpr uint32_t kAnimationClipResourceType = 0x4745414E;  // "GEAN" -- packed animation clip
constexpr uint32_t kStanResourceType = 0x47455354;           // "GEST" -- a stage's collision tiles
constexpr uint32_t kAudioBankResourceType = 0x47454142;      // "GEAB" -- an audio bank and its samples
constexpr uint32_t kMusicSequencesResourceType = 0x47454D53; // "GEMS" -- the music sequence table and sequences

} // namespace Frigate
