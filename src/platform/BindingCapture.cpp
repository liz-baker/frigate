#include "platform/BindingCapture.h"

#include <algorithm>

namespace Frigate::Platform::Input {

namespace {

ProfileDevice ProfileDeviceFor(const PortDevice& device) {
    return device.kind == DeviceKind::KeyboardMouse ? ProfileDevice::KeyboardMouse : ProfileDevice::Gamepad;
}

std::string SourceName(const InputSource& source, const AttachedDevices& attached) {
    if (source.kind != InputSource::Kind::Gamepad) {
        return "the keyboard and mouse";
    }
    for (const Gamepad& pad : attached.gamepads) {
        if (pad.id == source.gamepadId) {
            return pad.name.empty() ? "a gamepad" : pad.name;
        }
    }
    return "a gamepad";
}

} // namespace

bool Capture::Begin(const BindTarget& target, Action action, size_t slot, const PortDevice& selected,
                    const AttachedDevices& attached, int64_t nowMs, int64_t timeoutMs) {
    mState = State::Idle;
    mHint.clear();
    if (selected.kind != DeviceKind::KeyboardMouse && selected.kind != DeviceKind::Gamepad) {
        mHint = "Choose a device for this port to edit its bindings.";
        return false;
    }
    if (const std::string reason = DisconnectedReason(selected, attached); !reason.empty()) {
        mHint = reason;
        return false;
    }
    if (target.device != ProfileDeviceFor(selected) || Info(action).mode != target.mode) {
        mHint = "These bindings are for a different device than the one this port reads.";
        return false;
    }
    mTarget = target;
    mAction = action;
    mSlot = slot;
    mSelected = selected;
    mAttached = attached;
    mDeadlineMs = nowMs + timeoutMs;
    mState = State::Waiting;
    mBaselineTaken = false;
    mHeldAtStart.clear();
    mHint = "Press the input to bind, or Esc to cancel.";
    return true;
}

void Capture::Cancel() {
    if (mState == State::Waiting) {
        mState = State::Cancelled;
        mHint = "Cancelled.";
    }
}

void Capture::Reset() {
    mState = State::Idle;
    mHint.clear();
    mHeldAtStart.clear();
}

int64_t Capture::RemainingMs(int64_t nowMs) const {
    return mState == State::Waiting ? std::max<int64_t>(0, mDeadlineMs - nowMs) : 0;
}

bool Capture::FromSelectedDevice(const InputSource& source) const {
    if (mSelected.kind == DeviceKind::KeyboardMouse) {
        return source.kind != InputSource::Kind::Gamepad;
    }
    const auto index = FindGamepad(mSelected, mAttached);
    return source.kind == InputSource::Kind::Gamepad && index && mAttached.gamepads[*index].id == source.gamepadId;
}

Capture::State Capture::Update(int64_t nowMs, const std::vector<RawInput>& active) {
    if (mState != State::Waiting) {
        return mState;
    }
    // The first frame records what is already down; an input held then only
    // counts again after it has been released.
    if (!mBaselineTaken) {
        mHeldAtStart = active;
        mBaselineTaken = true;
    } else {
        mHeldAtStart.erase(std::remove_if(mHeldAtStart.begin(), mHeldAtStart.end(),
                                          [&active](const RawInput& raw) {
                                              return std::find(active.begin(), active.end(), raw) == active.end();
                                          }),
                           mHeldAtStart.end());
    }

    for (const RawInput& raw : active) {
        if (std::find(mHeldAtStart.begin(), mHeldAtStart.end(), raw) != mHeldAtStart.end()) {
            continue;
        }
        if (raw.input.kind == InputKind::Key && raw.input.code == kScancodeEscape) {
            mState = State::Cancelled;
            mHint = "Cancelled.";
            return mState;
        }
        if (!FromSelectedDevice(raw.source)) {
            if (raw.input.kind != InputKind::MouseMotion) {
                mHint = "Ignored input from " + SourceName(raw.source, mAttached) + "; this port reads " +
                        DeviceLabel(mSelected, mAttached) + ".";
            }
            continue;
        }
        if (IsReserved(raw.input)) {
            mHint = Describe(BindCheck::Status::Reserved);
            continue;
        }
        if (!Accepts(mAction, raw.input, mTarget.device)) {
            continue;
        }
        mResult = raw.input;
        mState = State::Captured;
        mHint.clear();
        return mState;
    }

    if (nowMs >= mDeadlineMs) {
        mState = State::TimedOut;
        mHint = "Timed out.";
    }
    return mState;
}

} // namespace Frigate::Platform::Input
