// The shape of the framebuffer the game is presented on, for game code that
// must know how much wider than 4:3 the 3D view is.
//
// Fast3D already keeps 2D (texture rectangles, fills) at its authored 4:3 and
// widens 3D by scaling vertex x by (4/3) / aspect, so a 4:3 projection matrix
// shows the correct wider world. Game code that culls against the view
// (portals, objects, frustum) has to extend its bounds by the same amount.
#pragma once

#include <atomic>
#include <cstdint>

namespace Frigate::Display {

namespace Detail {
inline std::atomic<float>& WidenFactor() {
    static std::atomic<float> factor{ 1.0f };
    return factor;
}
} // namespace Detail

// Records a framebuffer of `width` by `height` pixels. Anything not wider than
// 4:3 leaves the view at exactly its authored extent.
inline void SetDisplaySize(uint32_t width, uint32_t height) {
    float factor = 1.0f;
    if (height != 0 && static_cast<uint64_t>(width) * 3 > static_cast<uint64_t>(height) * 4) {
        factor = static_cast<float>((static_cast<double>(width) * 3.0) / (static_cast<double>(height) * 4.0));
    }
    Detail::WidenFactor().store(factor, std::memory_order_relaxed);
}

// Reads the size the interpreter is drawing at, which follows the window or the
// aspect ratio setting. Call once per displayed frame, before the game steps.
template <class Window> void SyncDisplaySize(const Window& window) {
    if (auto interpreter = window.GetInterpreterWeak().lock()) {
        uint32_t width = 0;
        uint32_t height = 0;
        interpreter->GetCurDimensions(&width, &height);
        SetDisplaySize(width, height);
    }
}

// How many times wider than the authored 4:3 view the displayed 3D view is;
// 1 for 4:3 and narrower.
inline float DisplayWiden() {
    return Detail::WidenFactor().load(std::memory_order_relaxed);
}

} // namespace Frigate::Display
