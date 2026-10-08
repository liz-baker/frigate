#pragma once

#include <cstdint>

namespace Frigate::Game::Menu::MpStageSelectLayout {

// constructor_menu12_mpstage's geometry, in the front-end's 440 by 330 screen
// space. Each of the three rows of previews sits in a film strip: a black rule
// with a pattern of dots (IMG_DOT) along its top and bottom edge.
constexpr int32_t kRuleLeft = 0x25;
constexpr int32_t kRuleRight = 0x185;
constexpr int32_t kRuleTop = 0x6C;
constexpr int32_t kRuleBottom = 0xA0;
constexpr int32_t kRuleSpacing = 0x46;

// The first dot pattern's centre and half height, how far apart a strip's two
// are and how far apart the strips' are.
constexpr float kDotFirstY = 4.0f + 100.0f;
constexpr float kDotHalfHeight = 4.0f;
constexpr float kDotPairSpacing = 60.0f;
constexpr float kDotRowSpacing = 70.0f;

// A preview's centre and half extents.
constexpr float kPreviewHalfWidth = 34.0f;
constexpr float kPreviewHalfHeight = 22.0f;
constexpr float kPreviewFirstX = 86.0f;
constexpr float kPreviewFirstY = kPreviewHalfHeight + 108.0f + 4.0f;
constexpr float kPreviewColumnSpacing = 85.0f;
constexpr float kPreviewRowSpacing = 70.0f;

// A preview's tint: the highlighted one is white (and brightened by the fog it is
// drawn with), an open one dimmed and a locked one nearly black.
constexpr uint8_t kHighlightTint = 0xFF;
constexpr uint8_t kOpenTint = 0x6E;
constexpr uint8_t kLockedTint = 0xF;
constexpr uint8_t PreviewTint(bool highlighted, bool unlocked) {
    return highlighted ? kHighlightTint : (unlocked ? kOpenTint : kLockedTint);
}

// A caption is drawn down from the bottom of its row, over the foot of the
// preview (a single line measures no height), at its column's centre less the
// inset.
constexpr int32_t kCaptionFirstX = 0x56;
constexpr int32_t kCaptionColumnSpacing = 0x55;
constexpr int32_t kCaptionFirstBottom = 0x97;
constexpr int32_t kCaptionRowSpacing = 0x46;
constexpr int32_t kCaptionLeftInset = 0x1F;

} // namespace Frigate::Game::Menu::MpStageSelectLayout
