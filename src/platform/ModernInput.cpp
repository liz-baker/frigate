#include "platform/ModernInput.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace Frigate::Platform::Input {

namespace {

// libultra's CONT_* masks.
constexpr uint16_t kButtonR = 0x0010;
constexpr uint16_t kButtonL = 0x0020;
constexpr uint16_t kButtonDRight = 0x0100;
constexpr uint16_t kButtonDLeft = 0x0200;
constexpr uint16_t kButtonDDown = 0x0400;
constexpr uint16_t kButtonDUp = 0x0800;
constexpr uint16_t kButtonStart = 0x1000;
constexpr uint16_t kButtonZ = 0x2000;
constexpr uint16_t kButtonB = 0x4000;
constexpr uint16_t kButtonA = 0x8000;

// An axis counts as a button past this.
constexpr float kDigitalThreshold = 0.5f;
// Queued wheel notches beyond this are dropped.
constexpr int kMaxQueuedNotches = 8;

struct Reader {
    const Profile& profile;
    const DeviceFrame& frame;
    // Inputs a higher-priority live action already took this frame.
    std::vector<PhysicalInput> consumed;

    bool Consumed(const PhysicalInput& input) const {
        return std::find(consumed.begin(), consumed.end(), input) != consumed.end();
    }

    // 0 to 1: how far the input is pressed. A wheel notch is a one-frame press.
    float ValueOf(const PhysicalInput& input) const {
        switch (input.kind) {
            case InputKind::Key:
            case InputKind::MouseButton:
            case InputKind::PadButton:
                return std::find(frame.held.begin(), frame.held.end(), input) != frame.held.end() ? 1.0f : 0.0f;
            case InputKind::PadAxis: {
                if (input.code < 0 || input.code >= static_cast<int>(frame.axes.size())) {
                    return 0.0f;
                }
                return std::clamp(frame.axes[static_cast<size_t>(input.code)] * static_cast<float>(input.sign), 0.0f,
                                  1.0f);
            }
            case InputKind::MouseWheel:
                return frame.wheel[static_cast<size_t>(input.code) & 3] > 0 ? 1.0f : 0.0f;
            case InputKind::MouseMotion:
                return 0.0f;
        }
        return 0.0f;
    }

    // The strongest binding of the action, wheel included only when asked.
    float Analog(Action action, bool withWheel = true) const {
        float best = 0.0f;
        for (const PhysicalInput& input : profile.Of(action)) {
            if (input.kind == InputKind::MouseMotion || (!withWheel && input.kind == InputKind::MouseWheel) ||
                Consumed(input)) {
                continue;
            }
            best = std::max(best, ValueOf(input));
        }
        return best;
    }

    bool Digital(Action action, bool withWheel = true) const {
        return Analog(action, withWheel) >= kDigitalThreshold;
    }

    // Wheel notches the action's wheel bindings caught this frame.
    int Notches(Action action) const {
        int total = 0;
        for (const PhysicalInput& input : profile.Of(action)) {
            if (input.kind == InputKind::MouseWheel && !Consumed(input)) {
                total += frame.wheel[static_cast<size_t>(input.code) & 3];
            }
        }
        return total;
    }

    // Mouse pixels in the direction the action's motion bindings point.
    float Motion(Action action) const {
        float total = 0.0f;
        for (const PhysicalInput& input : profile.Of(action)) {
            if (input.kind != InputKind::MouseMotion) {
                continue;
            }
            const float delta = input.code == 0 ? frame.mouseDx : frame.mouseDy;
            total += std::max(0.0f, delta * static_cast<float>(input.sign));
        }
        return total;
    }

    void Consume(Action action) {
        for (const PhysicalInput& input : profile.Of(action)) {
            consumed.push_back(input);
        }
    }
};

int8_t ToStick(float value, int range) {
    return static_cast<int8_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * static_cast<float>(range)));
}

// The decomp's turn and pitch response: quadratic in the deflection.
float Response(float value) {
    value = std::clamp(value, -1.0f, 1.0f);
    return value >= 0.0f ? value * value : -(value * value);
}

} // namespace

void ModernSynth::Reset() {
    *this = ModernSynth();
}

ModernOutput ModernSynth::Step(const Profile& profile, ProfileDevice device, const DeviceFrame& input,
                               const ModernContext& context, const ModernSettings& settings) {
    (void)device;
    Reader read{ profile, input, {} };
    ModernOutput out;
    out.frame.active = true;
    uint16_t buttons = 0;

    if (read.Digital(Action::Pause)) {
        buttons |= kButtonStart;
    }

    if (context.menu) {
        const float x = read.Analog(Action::MenuRight) - read.Analog(Action::MenuLeft);
        const float y = read.Analog(Action::MenuUp) - read.Analog(Action::MenuDown);
        out.pad.stickX = ToStick(x, kStickMax);
        out.pad.stickY = ToStick(y, kStickMax);
        if (read.Digital(Action::MenuSelect)) {
            buttons |= kButtonA;
        }
        if (read.Digital(Action::MenuBack)) {
            buttons |= kButtonB;
        }
        out.pad.buttons = buttons;
        // Carried so a key held while the menu closes isn't an edge.
        interactHeld_ = read.Digital(Action::Interact);
        reloadHeld_ = read.Digital(Action::Reload);
        aimX_ = aimY_ = 0.0f;
        nextQueued_ = prevQueued_ = 0;
        weaponPulsed_ = false;
        zoomInFrames_ = zoomOutFrames_ = 0;
        return out;
    }

    const bool aimHeld = read.Digital(Action::Aim);
    // The aim key goes down on the tick the game starts aiming, and the game's
    // own state only follows a tick later.
    const bool aiming = aimHeld || context.aiming;
    const bool zoomLive = aiming && context.zoomWeapon;
    const bool leanLive = aiming && !context.inTank;
    if (aimHeld) {
        buttons |= kButtonL | kButtonR;
    }

    // Zoom and lean take their inputs before the weapon change does, so the
    // wheel zooms while aiming with a scope and changes weapon otherwise.
    const bool zoomIn = zoomLive && read.Digital(Action::ZoomIn, false);
    const bool zoomOut = zoomLive && read.Digital(Action::ZoomOut, false);
    const int zoomInNotches = zoomLive ? read.Notches(Action::ZoomIn) : 0;
    const int zoomOutNotches = zoomLive ? read.Notches(Action::ZoomOut) : 0;
    const bool leanLeft = leanLive && read.Digital(Action::LeanLeft);
    const bool leanRight = leanLive && read.Digital(Action::LeanRight);
    if (zoomLive) {
        read.Consume(Action::ZoomIn);
        read.Consume(Action::ZoomOut);
    }
    if (leanLive) {
        read.Consume(Action::LeanLeft);
        read.Consume(Action::LeanRight);
    }

    // Move and strafe: no walking while aiming, as in the decomp.
    if (!aiming) {
        float x = read.Analog(Action::StrafeRight) - read.Analog(Action::StrafeLeft);
        float y = read.Analog(Action::MoveForward) - read.Analog(Action::MoveBack);
        const float length = std::sqrt((x * x) + (y * y));
        if (length > 1.0f) {
            x /= length;
            y /= length;
        }
        out.frame.moveX = ToStick(x, kStickFull);
        out.frame.moveY = ToStick(y, kStickFull);
    }

    // Look. The mouse is a delta in pixels and the sticks a rate.
    const float yawPixels = read.Motion(Action::TurnRight) - read.Motion(Action::TurnLeft);
    const float pitchPixels = read.Motion(Action::LookUp) - read.Motion(Action::LookDown);
    const float yawRate = read.Analog(Action::TurnRight) - read.Analog(Action::TurnLeft);
    const float pitchRate = read.Analog(Action::LookUp) - read.Analog(Action::LookDown);
    if (aiming) {
        // The crosshair is where the stick would hold it: the mouse moves it
        // there and the right stick is it.
        aimX_ = std::clamp(aimX_ + (yawPixels * kAimStickPerPixel * settings.mouseSensitivity),
                           -static_cast<float>(kStickMax), static_cast<float>(kStickMax));
        aimY_ = std::clamp(aimY_ + (pitchPixels * kAimStickPerPixel * settings.mouseSensitivity),
                           -static_cast<float>(kStickMax), static_cast<float>(kStickMax));
        const float stickX = aimX_ + (yawRate * settings.lookSensitivity * static_cast<float>(kStickMax));
        const float stickY = aimY_ + (pitchRate * settings.lookSensitivity * static_cast<float>(kStickMax));
        out.pad.stickX = static_cast<int8_t>(
            std::lround(std::clamp(stickX, -static_cast<float>(kStickMax), static_cast<float>(kStickMax))));
        out.pad.stickY = static_cast<int8_t>(
            std::lround(std::clamp(stickY, -static_cast<float>(kStickMax), static_cast<float>(kStickMax))));
    } else {
        aimX_ = aimY_ = 0.0f;
        out.frame.lookYaw = yawPixels * kMouseDegreesPerPixel * settings.mouseSensitivity;
        out.frame.lookPitch = pitchPixels * kMouseDegreesPerPixel * settings.mouseSensitivity;
        out.frame.lookYawRate = Response(yawRate) * settings.lookSensitivity;
        out.frame.lookPitchRate = Response(pitchRate) * settings.lookSensitivity;
    }

    if (read.Digital(Action::Fire)) {
        buttons |= kButtonZ;
    }

    out.frame.crouchHeld = read.Digital(Action::Crouch);
    out.frame.crouchToggle = settings.crouchToggle;
    const bool interact = read.Digital(Action::Interact);
    const bool reload = read.Digital(Action::Reload);
    out.frame.interactPressed = interact && !interactHeld_;
    out.frame.reloadPressed = reload && !reloadHeld_;
    interactHeld_ = interact;
    reloadHeld_ = reload;

    // Weapon change: A, and A with Z for backwards, which the decomp reads as
    // A held then fire pressed. A wheel notch is a one-frame press with a frame
    // between, so a fast spin queues.
    nextQueued_ = std::min(nextQueued_ + read.Notches(Action::WeaponNext), kMaxQueuedNotches);
    prevQueued_ = std::min(prevQueued_ + read.Notches(Action::WeaponPrev), kMaxQueuedNotches);
    bool pulseNext = false;
    bool pulsePrev = false;
    if (!weaponPulsed_) {
        if (nextQueued_ > 0) {
            pulseNext = true;
            nextQueued_--;
        } else if (prevQueued_ > 0) {
            pulsePrev = true;
            prevQueued_--;
        }
    }
    weaponPulsed_ = pulseNext || pulsePrev;
    const bool nextHeld = read.Digital(Action::WeaponNext, false);
    const bool prevHeld = read.Digital(Action::WeaponPrev, false);
    if (nextHeld || pulseNext || prevHeld || pulsePrev) {
        buttons |= kButtonA;
    }
    if (prevHeld || pulsePrev) {
        buttons |= kButtonZ;
    }

    // Detonate is the A+B chord.
    out.frame.detonate = read.Digital(Action::Detonate);
    if (out.frame.detonate) {
        buttons |= kButtonA | kButtonB;
    }

    // Zoom: the D-pad, which control style 1.1 reads while aiming with a scope.
    if (zoomLive) {
        zoomInFrames_ += zoomInNotches * kZoomFramesPerNotch;
        zoomOutFrames_ += zoomOutNotches * kZoomFramesPerNotch;
        if (zoomIn || zoomInFrames_ > 0) {
            buttons |= kButtonDUp;
        }
        if (zoomOut || zoomOutFrames_ > 0) {
            buttons |= kButtonDDown;
        }
        zoomInFrames_ = std::max(0, zoomInFrames_ - 1);
        zoomOutFrames_ = std::max(0, zoomOutFrames_ - 1);
    } else {
        zoomInFrames_ = zoomOutFrames_ = 0;
    }

    // Lean (aiming only): the D-pad sides, which the decomp leans on.
    if (leanLeft) {
        buttons |= kButtonDLeft;
    }
    if (leanRight) {
        buttons |= kButtonDRight;
    }

    out.pad.buttons = buttons;
    return out;
}

namespace {

std::mutex gProviderMutex;
ModernContextProvider gProvider;

} // namespace

ModernState& ModernFrames() {
    static ModernState frames;
    return frames;
}

void SetModernContextProvider(ModernContextProvider provider) {
    const std::lock_guard<std::mutex> lock(gProviderMutex);
    gProvider = std::move(provider);
}

ModernContext ModernContextOf(int port) {
    ModernContextProvider provider;
    {
        const std::lock_guard<std::mutex> lock(gProviderMutex);
        provider = gProvider;
    }
    return provider ? provider(port) : ModernContext{};
}

} // namespace Frigate::Platform::Input
