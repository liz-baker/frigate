#pragma once

#include <chrono>
#include <optional>

namespace Frigate::Game {

// The VI rate: 60 ticks a second on NTSC.
constexpr int kTicksPerSecond = 60;

// A stretch of the front-end that draws at its own frame length. Screens only
// say which phase they are in; how long each phase's frames last is the one
// table in FrameTiming.cpp.
enum class FramePhase {
    Default,
    // The gun barrel screen: the spotlight's sweep, the sight scrolling in
    // before Bond enters it, and every frame with Bond drawn.
    EyeIntroSweep,
    EyeIntroSight,
    EyeIntroBond,
};

// g_ClockTimer: the VI ticks a frame of `phase` lasts.
//
// The decomp has no fixed frame rate. waitForNextFrame() (game/frametiming.c)
// starts a frame once the previous one's graphics are done and counts the
// ticks that elapsed, so a frame that is cheap to draw takes a tick and a
// heavy one two or more. A PC can't reproduce what the RDP took, so the
// table holds the ticks seen on hardware.
int TicksPerFrame(FramePhase phase);

// g_ClockTimer for a stage played live: the VI ticks that passed since the previous frame was
// stepped. A machine that draws at 60 frames a second gets 1 every frame, as it always has; one
// that takes two refreshes a frame steps the stage two ticks, so the game keeps its speed and
// drops frames instead of slowing down. A stall is capped, so the game runs slowly through it
// rather than jumping.
class TickMeter {
  public:
    using Clock = std::chrono::steady_clock;

    // The heaviest frames of the original ran at 3 or 4 ticks.
    static constexpr int kMaxTicks = 4;

    // 1 for the first frame and the first after Reset(); then the whole ticks since the last call,
    // from 1 to kMaxTicks.
    int Ticks(Clock::time_point now);

    // Forget the last frame: a stage that has just started, or came back from a pause.
    void Reset() {
        last_.reset();
    }

  private:
    std::optional<Clock::time_point> last_;
};

} // namespace Frigate::Game
