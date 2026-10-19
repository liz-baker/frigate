#include "menu/BindingFlow.h"

#include <utility>

namespace Frigate::ConfigMenu {

using namespace Platform::Input;

namespace {

std::string ActionList(const std::vector<Conflict>& conflicts) {
    std::string out;
    for (size_t i = 0; i < conflicts.size(); i++) {
        if (i > 0) {
            out += i + 1 == conflicts.size() ? " and " : ", ";
        }
        out += Info(conflicts[i].action).label;
    }
    return out;
}

} // namespace

ConflictPrompt BuildConflictPrompt(const BindCheck& check, const PhysicalInput& input, FaceLayout layout) {
    ConflictPrompt prompt;
    prompt.text = InputLabel(input, layout) + " is already used by " + ActionList(check.conflicts) + ".";
    prompt.canSwap = check.canSwap;
    prompt.canReplace = check.canReplace;
    if (!check.canReplace) {
        prompt.replaceBlocked = "Replace would leave a required action without a binding.";
    }
    return prompt;
}

size_t BindingFlow::SlotFor(const BindingStore& store, Action action) const {
    return store.Get(mTarget).Of(action).empty() ? kNewSlot : 0;
}

bool BindingFlow::BeginSingle(const BindTarget& target, Action action, size_t slot, const PortDevice& selected,
                              const AttachedDevices& attached, FaceLayout layout, int64_t nowMs,
                              const BindingStore&) {
    mPhase = Phase::Idle;
    mBindAll = false;
    mTarget = target;
    mActions = { action };
    mStep = 0;
    mSlot = slot;
    mSelected = selected;
    mAttached = attached;
    mLayout = layout;
    mMessage.clear();
    mLastUpdateMs = nowMs;
    if (!mCapture.Begin(target, action, slot, selected, attached, nowMs)) {
        mMessage = mCapture.Hint();
        return false;
    }
    mPhase = Phase::Capturing;
    return true;
}

bool BindingFlow::BeginBindAll(const BindTarget& target, std::vector<Action> actions, const PortDevice& selected,
                               const AttachedDevices& attached, FaceLayout layout, int64_t nowMs,
                               const BindingStore& store) {
    mPhase = Phase::Idle;
    mBindAll = true;
    mTarget = target;
    mActions = std::move(actions);
    mStep = 0;
    mSelected = selected;
    mAttached = attached;
    mLayout = layout;
    mMessage.clear();
    mLastUpdateMs = nowMs;
    if (mActions.empty()) {
        return false;
    }
    return BeginStep(store, nowMs);
}

bool BindingFlow::BeginStep(const BindingStore& store, int64_t nowMs) {
    mSlot = SlotFor(store, mActions[mStep]);
    if (!mCapture.Begin(mTarget, mActions[mStep], mSlot, mSelected, mAttached, nowMs)) {
        Finish(mCapture.Hint());
        return false;
    }
    mPhase = Phase::Capturing;
    return true;
}

void BindingFlow::Advance(const BindingStore& store, int64_t nowMs) {
    mMessage.clear();
    if (!mBindAll) {
        Finish({});
        return;
    }
    mStep++;
    if (mStep >= mActions.size()) {
        mStep = mActions.size() - 1;
        Finish("Bind all finished.");
        return;
    }
    BeginStep(store, nowMs);
}

void BindingFlow::Finish(const std::string& message) {
    mPhase = Phase::Idle;
    mMessage = message;
    mCapture.Reset();
}

void BindingFlow::Retry(const BindingStore& store, int64_t nowMs, const std::string& message) {
    if (!mBindAll) {
        Finish(message);
        return;
    }
    if (BeginStep(store, nowMs)) {
        mMessage = message;
    }
}

void BindingFlow::Update(BindingStore& store, int64_t nowMs, const std::vector<RawInput>& held) {
    if (mPhase == Phase::Idle) {
        return;
    }
    const bool stale = nowMs - mLastUpdateMs > kFlowStaleMs;
    mLastUpdateMs = nowMs;
    if (stale) {
        Cancel();
        return;
    }
    if (mPhase != Phase::Capturing) {
        return;
    }
    const Action action = mActions[mStep];
    switch (mCapture.Update(nowMs, held)) {
        case Capture::State::Waiting:
        case Capture::State::Idle:
            return;
        case Capture::State::Cancelled:
            Finish("Cancelled.");
            return;
        case Capture::State::TimedOut:
            Finish(mBindAll ? "Timed out; Bind all stopped." : "Timed out.");
            return;
        case Capture::State::Captured:
            break;
    }
    const PhysicalInput input = mCapture.Result();
    switch (store.Bind(mTarget, action, input, mSlot)) {
        case BindStatus::Bound:
            mChanged = true;
            Advance(store, nowMs);
            return;
        case BindStatus::NeedsDecision:
            mPending = input;
            mPrompt = BuildConflictPrompt(store.Check(mTarget, action, input, mSlot), input, mLayout);
            mPhase = Phase::Deciding;
            mCapture.Reset();
            return;
        case BindStatus::Reserved:
            Retry(store, nowMs, Describe(BindCheck::Status::Reserved));
            return;
        case BindStatus::Invalid:
            Retry(store, nowMs, Describe(BindCheck::Status::Invalid));
            return;
        case BindStatus::AlreadyBound:
            Retry(store, nowMs, Describe(BindCheck::Status::AlreadyBound));
            return;
        case BindStatus::Refused:
        case BindStatus::Cancelled:
            Retry(store, nowMs, "That binding was not applied.");
            return;
    }
}

void BindingFlow::Decide(BindingStore& store, ConflictChoice choice) {
    if (mPhase != Phase::Deciding) {
        return;
    }
    const Action action = mActions[mStep];
    const int64_t nowMs = mLastUpdateMs;
    const BindStatus status = store.Bind(mTarget, action, mPending, mSlot, &choice);
    if (status == BindStatus::Bound) {
        mChanged = true;
        Advance(store, nowMs);
    } else if (status == BindStatus::Refused) {
        Retry(store, nowMs, "That choice isn't possible for this binding.");
    } else {
        Retry(store, nowMs, "Cancelled; nothing changed.");
    }
}

void BindingFlow::Skip(const BindingStore& store) {
    if (!mBindAll || mPhase == Phase::Idle) {
        return;
    }
    Advance(store, mLastUpdateMs);
}

void BindingFlow::Cancel() {
    if (mPhase == Phase::Idle) {
        return;
    }
    mCapture.Cancel();
    Finish("Cancelled.");
}

bool BindingFlow::TakeChanged() {
    return std::exchange(mChanged, false);
}

} // namespace Frigate::ConfigMenu
