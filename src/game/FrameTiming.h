#pragma once

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

} // namespace Frigate::Game
