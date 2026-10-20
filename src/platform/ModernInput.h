#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "platform/Bindings.h"
#include "platform/InputState.h"
#include "platform/MenuPointer.h"

// Modern controls: the port's Modern bindings turned into the game's input.
// ModernSynth::Step is a pure function of the bindings, one frame of the
// selected device and what the game is doing, so it is unit-tested without a
// window; Poll (platform/Input.h) feeds it and the stage reads the result.
// Design and the departures from the decomp: docs/modern-controls.md.
namespace Frigate::Platform::Input {

// The N64 stick's travel in the game's tuning, and what a key deflects it by
// (full speed once the game's dead zone is taken off).
inline constexpr int kStickMax = 80;
inline constexpr int kStickFull = 75;

// Degrees the view turns per mouse pixel at sensitivity 1, and stick units the
// sight-aim crosshair moves per pixel.
inline constexpr float kMouseDegreesPerPixel = 0.105f;
inline constexpr float kAimStickPerPixel = 0.3f;

// What the game is doing, which decides what a binding means. Provided per port
// by the stage (SetModernContextProvider).
struct ModernContext {
    // A front-end screen or the watch is up: the menu bindings drive the
    // cursor and Pause is the only gameplay action.
    bool menu = true;
    bool aiming = false;
    // The right-hand weapon zooms (the decomp's DisableCrouch gate on zoom).
    bool zoomWeapon = false;
    bool inTank = false;
};

// One frame of the port's selected device.
struct DeviceFrame {
    // Keys, mouse buttons and gamepad buttons held.
    std::vector<PhysicalInput> held;
    // By PadAxis; sticks positive right and up and already through the port's
    // dead zone and sensitivity settings, triggers 0 to 1.
    std::array<float, static_cast<size_t>(PadAxis::Count)> axes{};
    // Notches this frame, by WheelDir.
    std::array<int, 4> wheel{};
    // Mouse motion in pixels this frame, positive right and down; zero unless
    // the mouse is captured.
    float mouseDx = 0.0f;
    float mouseDy = 0.0f;
    // The pointer's position for the front-end menus; not present while the ImGui
    // menu has the mouse or the pointer is outside the window.
    PointerFrame pointer;
};

struct ModernSettings {
    // Multipliers: 1 is the default feel.
    float mouseSensitivity = 1.0f;
    float lookSensitivity = 1.0f;
    bool crouchToggle = false;
};

// What the game reads besides the pad. Departs from the decomp, which has no
// free look and one B tap for interact and reload: only Modern input sets it.
struct ModernFrame {
    bool active = false;
    // The left stick or WASD as stick units (kStickFull at full): walk and
    // strafe on foot, walk and hull turn in a tank. Zero while aiming.
    int8_t moveX = 0;
    int8_t moveY = 0;
    // The mouse, as degrees to turn and pitch this frame, positive right and
    // up. The right stick, as -1 to 1 at the decomp's response curve and the
    // look sensitivity, for the game to turn into degrees with the frame time.
    float lookYaw = 0.0f;
    float lookPitch = 0.0f;
    float lookYawRate = 0.0f;
    float lookPitchRate = 0.0f;
    bool crouchHeld = false;
    bool crouchToggle = false;
    // Edges: true for the one frame the input goes down.
    bool interactPressed = false;
    bool reloadPressed = false;
    // Detonate is held; the pad also carries the A+B chord it stands for.
    bool detonate = false;
};

struct ModernOutput {
    // The stick and buttons as the game's control style 1.1 reads them: the
    // sight-aim crosshair stick (or the menu stick), L and R for aim, Z for fire,
    // A for the weapon change, the D-pad for lean and zoom, A+B for detonate and
    // Start for pause.
    PadSample pad;
    ModernFrame frame;
};

// One port's synthesis; keeps what carries over frames (edges, the crosshair
// position, queued wheel notches).
class ModernSynth {
  public:
    ModernOutput Step(const Profile& profile, ProfileDevice device, const DeviceFrame& input,
                      const ModernContext& context, const ModernSettings& settings);
    void Reset();

  private:
    float aimX_ = 0.0f;
    float aimY_ = 0.0f;
    bool interactHeld_ = false;
    bool reloadHeld_ = false;
    int nextQueued_ = 0;
    int prevQueued_ = 0;
    bool weaponPulsed_ = false;
    int zoomInFrames_ = 0;
    int zoomOutFrames_ = 0;
};

// Frames of zoom one wheel notch asks for, and the most that can be waiting: a
// spin of the wheel past a few notches would otherwise keep zooming long after
// it stopped, and take as long to undo.
inline constexpr int kZoomFramesPerNotch = 6;
inline constexpr int kMaxQueuedZoomFrames = 3 * kZoomFramesPerNotch;

// Every port's result of the latest Poll; a port that is not in Modern mode
// has active = false.
using ModernState = std::array<ModernFrame, kPortCount>;
ModernState& ModernFrames();

// A state with every port off, for callers with no Modern input.
inline const ModernState kNoModernInput{};

// What the stage tells the synthesis about a port's player; without one every
// port is in the menu context.
using ModernContextProvider = std::function<ModernContext(int port)>;
void SetModernContextProvider(ModernContextProvider provider);
ModernContext ModernContextOf(int port);

} // namespace Frigate::Platform::Input
