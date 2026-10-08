// Where a view's 2D (texture rectangles, fills, text) lands on a display wider
// than 4:3.
//
// Fast3D draws 3D through each view's stretched viewport, but a 2D rectangle
// bypasses the viewport: its x is squeezed by (4/3) / aspect about the middle of
// the whole framebuffer. A view that fills the width (one or two players) has
// its middle there, so its 2D lines up with it. A quadrant's middle is a
// quarter of the way in, so its 2D has to move there: by the distance its
// middle is from the screen's, times how much wider the display is than 4:3.
// A move left of column 0 or right of the screen's width needs the signed
// ("wide") rectangle commands.
// docs/multiplayer-split-screen.md
#pragma once

#include <cmath>
#include <cstdint>

#include "libultraship/libultra/gbi.h"

namespace Frigate::Display {

namespace Detail {
inline int32_t& Shift2DValue() {
    static int32_t shift = 0;
    return shift;
}
} // namespace Detail

// How far the 2D being drawn is moved right, in columns of the stage's screen;
// 0 outside a split screen's pass.
inline int32_t Shift2D() {
    return Detail::Shift2DValue();
}

// The move that puts the 2D of a view at `viewLeft` and `viewWidth` columns in
// the middle of the view the display stretches it to, on a display `widen`
// times wider than 4:3 and a screen `screenWidth` columns across.
inline int32_t ViewShift2D(int32_t viewLeft, int32_t viewWidth, int32_t screenWidth, float widen) {
    if (widen <= 1.0f) {
        return 0;
    }
    const float middle = static_cast<float>(viewLeft) + (static_cast<float>(viewWidth) * 0.5f);
    return static_cast<int32_t>(std::lround((middle - (static_cast<float>(screenWidth) * 0.5f)) * (widen - 1.0f)));
}

// Sets Shift2D for the scope of a view's pass.
class Shift2DScope {
  public:
    explicit Shift2DScope(int32_t shift) : previous_(Detail::Shift2DValue()) {
        Detail::Shift2DValue() = shift;
    }
    ~Shift2DScope() {
        Detail::Shift2DValue() = previous_;
    }
    Shift2DScope(const Shift2DScope&) = delete;
    Shift2DScope& operator=(const Shift2DScope&) = delete;

  private:
    int32_t previous_;
};

// gDPFillRectangle of a rectangle in whole columns and rows, moved by Shift2D.
// The plain command holds only positive coordinates, so a rectangle with a
// negative one is drawn with the signed command too.
inline Gfx* FillRectangle(Gfx* dl, int32_t left, int32_t top, int32_t right, int32_t bottom) {
    const int32_t shift = Shift2D();
    if (shift == 0 && left >= 0 && top >= 0) {
        gDPFillRectangle(dl++, left, top, right, bottom);
    } else {
        gDPFillWideRectangle(dl++, left + shift, top, right + shift, bottom);
    }
    return dl;
}

// gSPTextureRectangle of a rectangle in quarter columns and rows, moved by
// Shift2D.
inline Gfx* TextureRectangle(Gfx* dl, int32_t left, int32_t top, int32_t right, int32_t bottom, int32_t s, int32_t t,
                             int32_t dsdx, int32_t dtdy) {
    const int32_t shift = Shift2D() * 4;
    if (shift == 0 && left >= 0 && top >= 0) {
        gSPTextureRectangle(dl++, left, top, right, bottom, G_TX_RENDERTILE, s, t, dsdx, dtdy);
    } else {
        gSPWideTextureRectangle(dl++, left + shift, top, right + shift, bottom, G_TX_RENDERTILE, s, t, dsdx, dtdy);
    }
    return dl;
}

} // namespace Frigate::Display
