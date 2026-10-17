#include "level/ScreenLayout.h"

namespace Frigate::Level {

namespace {

// fr.h's SCREEN_WIDTH_440, SCREEN_HEIGHT_330 and the z-buffer's 4:3 sizes.
constexpr int32_t kHiResWidth = 440;
constexpr int32_t kHiResHeight = 330;

// VIEWPORT_WIDTH_4P (159 = (640 / 4) - 1), VIEWPORT_HEIGHT_4P_109 and the
// columns and rows the players' views start at.
constexpr int32_t kQuadrantWidth = 159;
constexpr int32_t kSplitHeight = 109;
constexpr int32_t kRightColumn = 0xA1;
constexpr int32_t kUpperTop = 10;
constexpr int32_t kLowerTop = 121;

// Z_BUFFER_HEIGHT_NTSC: a game of two or more players shares one z-buffer
// half the screen's height.
constexpr int32_t kSplitZBufferHeight = 120;

// The one-player screen settings: VIEWPORT_HEIGHT_DEFAULT_NTSC and the heights
// and tops the watch's arm opens the letterboxed settings up from.
constexpr int32_t kFullHeight = 220;
constexpr int32_t kFullTop = 10;
constexpr float kWideGrow = 40.0f;
constexpr int32_t kWideHeight = 180;
constexpr float kWideShift = -20.0f;
constexpr int32_t kWideTop = 30;
constexpr float kCinemaGrow = 84.0f;
constexpr int32_t kCinemaHeight = 136;
constexpr float kCinemaShift = -42.0f;
constexpr int32_t kCinemaTop = 52;

// VIEWPORT_HEIGHT_FULLSCREEN, _WIDESCREEN and _CINEMA, and the
// VIEWPORT_ULY_CAM_* tops, of the camera feed's one-player views.
constexpr int32_t kCameraFullHeight = 304;
constexpr int32_t kCameraFullTop = 13;
constexpr int32_t kCameraWideHeight = 248;
constexpr int32_t kCameraWideTop = 41;
constexpr int32_t kCameraCinemaHeight = 190;
constexpr int32_t kCameraCinemaTop = 70;

// Whether `player`'s view is in the lower half of the screen.
bool InLowerHalf(int playerCount, int player) {
    return playerCount == 2 ? player == 1 : (playerCount >= 3 && player >= 2);
}

} // namespace

ViewMetrics PlayerViewMetrics(int playerCount, int player, ScreenSize screen, float pauseAnimationPercent,
                              bool cameraBuffer) {
    ViewMetrics vi;
    // getWidth320or440 and getHeight330or240.
    vi.x = cameraBuffer ? kHiResWidth : kScreenWidth;
    vi.y = cameraBuffer ? kHiResHeight : kScreenHeight;

    // bondviewGetCurrentPlayerViewportWidth: VIEWPORT_WIDTH_FULLSCREEN,
    // _WIDESCREEN and _CINEMA are all the screen's width.
    vi.viewWidth = playerCount >= 3 ? kQuadrantWidth : (cameraBuffer ? kHiResWidth : kScreenWidth);
    // get_curplayer_viewport_ulx.
    vi.viewLeft = (playerCount > 2 && (player == 1 || player == 3)) ? kRightColumn : 0;

    if (playerCount >= 2) {
        vi.viewHeight = kSplitHeight;
        // bondviewGetCurrentPlayerViewportUly.
        vi.viewTop = InLowerHalf(playerCount, player) ? kLowerTop : kUpperTop;
        return vi;
    }

    if (cameraBuffer) {
        switch (screen) {
            case ScreenSize::Wide:
                vi.viewHeight = kCameraWideHeight;
                vi.viewTop = kCameraWideTop;
                break;
            case ScreenSize::Cinema:
                vi.viewHeight = kCameraCinemaHeight;
                vi.viewTop = kCameraCinemaTop;
                break;
            default:
                vi.viewHeight = kCameraFullHeight;
                vi.viewTop = kCameraFullTop;
                break;
        }
        return vi;
    }

    switch (screen) {
        case ScreenSize::Wide:
            vi.viewHeight = static_cast<int32_t>(kWideGrow * pauseAnimationPercent) + kWideHeight;
            vi.viewTop = static_cast<int32_t>(kWideShift * pauseAnimationPercent) + kWideTop;
            break;
        case ScreenSize::Cinema:
            vi.viewHeight = static_cast<int32_t>(kCinemaGrow * pauseAnimationPercent) + kCinemaHeight;
            vi.viewTop = static_cast<int32_t>(kCinemaShift * pauseAnimationPercent) + kCinemaTop;
            break;
        default:
            vi.viewHeight = kFullHeight;
            vi.viewTop = kFullTop;
            break;
    }
    return vi;
}

Extent ScreenExtent(bool hiRes) {
    return hiRes ? Extent{ kHiResWidth, kHiResHeight } : Extent{ kScreenWidth, kScreenHeight };
}

Extent ZBufferAllocation(int playerCount, bool hiRes) {
    if (hiRes) {
        return { kHiResWidth, kHiResHeight };
    }
    return { kScreenWidth, playerCount == 1 ? kScreenHeight : kSplitZBufferHeight };
}

int32_t ZBufferRowOffset(int playerCount, int player) {
    // zbufInit moves the depth image back by SCREEN_WIDTH * SCREEN_HEIGHT
    // bytes: half the screen's rows of 16 bit depths.
    constexpr int32_t kRows = (kScreenWidth * kScreenHeight) / (2 * kScreenWidth);
    const bool shifted = player >= 2 || (playerCount == 2 && player == 1);
    return shifted ? kRows : 0;
}

ZBufferRect ZBufferClearRect(int playerCount, int player, bool hiRes, bool cameraBuffer) {
    const Extent screen = ScreenExtent(cameraBuffer);
    const Extent zbuffer = ZBufferAllocation(playerCount, hiRes);
    ZBufferRect rect;
    if (playerCount < 3) {
        rect.left = 0;
        rect.right = screen.width - 1;
    } else if (player == 0 || player == 2) {
        rect.left = 0;
        rect.right = (screen.width / 2) - 1;
    } else {
        rect.left = screen.width / 2;
        rect.right = screen.width - 1;
    }
    rect.top = ZBufferRowOffset(playerCount, player);
    rect.bottom = rect.top + zbuffer.height - 1;
    return rect;
}

} // namespace Frigate::Level
