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
#include <functional>
#include <utility>

namespace Frigate::Display {

namespace Detail {
inline std::atomic<float>& WidenFactor() {
    static std::atomic<float> factor{ 1.0f };
    return factor;
}
inline std::atomic<bool>& LivePlayFlag() {
    // Defaults on so code that draws a stage outside the game loop (the level
    // viewer) is not pillarboxed.
    static std::atomic<bool> live{ true };
    return live;
}
inline std::function<void()>& LivePlayHandler() {
    static std::function<void()> handler;
    return handler;
}
} // namespace Detail

// Only live gameplay follows the Aspect Ratio setting and draws wider than
// 4:3; menus, cutscenes and attract demos are always 4:3. The game loop sets
// this between frames, and the handler re-applies the render settings so the
// framebuffer is reshaped before the next frame is drawn.
inline bool LivePlay() {
    return Detail::LivePlayFlag().load(std::memory_order_relaxed);
}
inline void SetLivePlayChangedHandler(std::function<void()> handler) {
    Detail::LivePlayHandler() = std::move(handler);
}
inline void SetLivePlay(bool live, bool force = false) {
    if (Detail::LivePlayFlag().exchange(live, std::memory_order_relaxed) != live || force) {
        if (Detail::LivePlayHandler()) {
            Detail::LivePlayHandler()();
        }
    }
}

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
// 1 for 4:3 and narrower, and whenever the scene is not live play.
inline float DisplayWiden() {
    return LivePlay() ? Detail::WidenFactor().load(std::memory_order_relaxed) : 1.0f;
}

} // namespace Frigate::Display
