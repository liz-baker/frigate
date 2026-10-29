#include "platform/MenuPointer.h"

#include <algorithm>
#include <atomic>

namespace Frigate::Platform::Input {

ScreenPoint MapToScreen(float x, float y, float areaWidth, float areaHeight, float aspect) {
    if (areaWidth <= 0.0f || areaHeight <= 0.0f || aspect <= 0.0f) {
        return {};
    }
    float width = areaWidth;
    float height = areaHeight;
    if (areaWidth > areaHeight * aspect) {
        width = areaHeight * aspect;
    } else {
        height = areaWidth / aspect;
    }
    const float left = (areaWidth - width) * 0.5f;
    const float top = (areaHeight - height) * 0.5f;
    return { std::clamp((x - left) / width, 0.0f, 1.0f), std::clamp((y - top) / height, 0.0f, 1.0f) };
}

PointerSample MenuPointerTracker::Update(const PointerFrame& frame, bool navigating) {
    if (navigating) {
        owned_ = false;
    }
    if (!frame.present) {
        // Gone from the window, or the ImGui menu has it: the next position only
        // seeds the comparison.
        havePrevious_ = false;
        return {};
    }
    const bool moved = havePrevious_ && (frame.x != previousX_ || frame.y != previousY_);
    havePrevious_ = true;
    previousX_ = frame.x;
    previousY_ = frame.y;
    if (moved && !navigating) {
        owned_ = true;
    }
    if (!owned_) {
        return {};
    }
    const ScreenPoint point = MapToScreen(frame.x, frame.y, frame.areaWidth, frame.areaHeight);
    return { true, point.u, point.v };
}

void MenuPointerTracker::Reset() {
    *this = MenuPointerTracker();
}

namespace {
std::atomic<bool> gPointerUsed{ false };
}

void NoteMenuPointerUsed() {
    gPointerUsed.store(true, std::memory_order_relaxed);
}

bool TakeMenuPointerUsed() {
    return gPointerUsed.exchange(false, std::memory_order_relaxed);
}

} // namespace Frigate::Platform::Input
