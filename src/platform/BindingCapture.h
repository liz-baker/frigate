#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/Bindings.h"
#include "platform/PortAssignment.h"

// "Press the input to bind": a state machine the editor drives once per frame.
// It takes the clock and the inputs as arguments so tests run it without a
// window; BindingsApply.h supplies the real inputs.
namespace Frigate::Platform::Input {

// Where an input came from.
struct InputSource {
    enum class Kind : uint8_t { Keyboard, Mouse, Gamepad };
    Kind kind = Kind::Keyboard;
    // The gamepad's Gamepad::id; unused otherwise.
    int32_t gamepadId = 0;

    bool operator==(const InputSource& other) const {
        return kind == other.kind && gamepadId == other.gamepadId;
    }
};

struct RawInput {
    InputSource source;
    PhysicalInput input;

    bool operator==(const RawInput& other) const {
        return source == other.source && input == other.input;
    }
};

inline constexpr int64_t kCaptureTimeoutMs = 8000;

class Capture {
  public:
    enum class State : uint8_t { Idle, Waiting, Captured, Cancelled, TimedOut };

    // Starts capturing a binding for `action` (in `slot`, kNewSlot to add one)
    // from the port's `selected` device. False, with Hint() saying why, when
    // the port has no device of its own to read, or it isn't attached.
    bool Begin(const BindTarget& target, Action action, size_t slot, const PortDevice& selected,
               const AttachedDevices& attached, int64_t nowMs, int64_t timeoutMs = kCaptureTimeoutMs);

    // The Cancel button.
    void Cancel();

    // Back to Idle once the editor has used the result.
    void Reset();

    // `active` is every input held right now (a wheel step counts for the frame
    // it happens). Inputs held when Begin was called don't count until released,
    // so the click that started the capture can't bind itself. Anything not from
    // the selected device is ignored and explained by Hint(); Esc cancels from
    // any device, and the reserved inputs are refused.
    State Update(int64_t nowMs, const std::vector<RawInput>& active);

    State state() const {
        return mState;
    }
    bool Waiting() const {
        return mState == State::Waiting;
    }
    const PhysicalInput& Result() const {
        return mResult;
    }
    const BindTarget& Target() const {
        return mTarget;
    }
    Action TargetAction() const {
        return mAction;
    }
    size_t Slot() const {
        return mSlot;
    }
    int64_t RemainingMs(int64_t nowMs) const;

    // What to tell the player: why Begin refused, what was ignored, or that
    // the capture ended. Empty when there is nothing to say.
    const std::string& Hint() const {
        return mHint;
    }

  private:
    bool FromSelectedDevice(const InputSource& source) const;
    State mState = State::Idle;
    bool mBaselineTaken = false;
    BindTarget mTarget;
    Action mAction = Action::N64A;
    size_t mSlot = kNewSlot;
    PortDevice mSelected;
    AttachedDevices mAttached;
    int64_t mDeadlineMs = 0;
    std::vector<RawInput> mHeldAtStart;
    PhysicalInput mResult;
    std::string mHint;
};

} // namespace Frigate::Platform::Input
