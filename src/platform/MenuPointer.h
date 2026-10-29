#pragma once

#include "platform/InputState.h"

// The mouse as an absolute pointer in the front-end menus (frigate#501), a
// Modern-only departure: the decomp's menus move their cursor with the stick.
// Pure logic, unit-tested without a window; Poll (platform/Input.h) feeds it.
// Design: docs/modern-controls.md.
namespace Frigate::Platform::Input {

// The shape the front end is presented in: libultraship pillar or letterboxes
// the menus to 4:3 whatever the window is.
inline constexpr float kMenuAspect = 4.0f / 3.0f;

// The mouse in window coordinates, and the area the game is drawn in.
struct PointerFrame {
    bool present = false;
    float x = 0.0f;
    float y = 0.0f;
    float areaWidth = 0.0f;
    float areaHeight = 0.0f;
};

// `x` and `y` across the largest `aspect` rectangle centred in the area, as
// [0, 1] along each side; a point in the bars clamps to the nearest edge.
struct ScreenPoint {
    float u = 0.0f;
    float v = 0.0f;
};
ScreenPoint MapToScreen(float x, float y, float areaWidth, float areaHeight, float aspect = kMenuAspect);

// Decides whether the pointer or the keyboard and stick hold the cursor. The
// pointer takes over when the mouse moves and gives the cursor back when a
// direction is pressed, so the two don't fight; a click without a move doesn't
// take over.
class MenuPointerTracker {
  public:
    // `navigating`: a direction key or stick is deflected this frame.
    PointerSample Update(const PointerFrame& frame, bool navigating);
    void Reset();

  private:
    bool owned_ = false;
    bool havePrevious_ = false;
    float previousX_ = 0.0f;
    float previousY_ = 0.0f;
};

// A front-end screen read the pointer this frame (the game calls it); the next
// Poll hides the operating system's pointer while it did, since the menu draws
// its own cursor.
void NoteMenuPointerUsed();
bool TakeMenuPointerUsed();

} // namespace Frigate::Platform::Input
