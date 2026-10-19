#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/BindingCapture.h"
#include "platform/Bindings.h"
#include "platform/PortAssignment.h"

// What the binding editor does between "bind this" and the binding being
// stored: one capture, or Bind all walking a list of actions, with the conflict
// question in between. Pure logic driven once per frame with the clock and the
// inputs held, so it is unit-tested; the ImGui side only draws its state.
namespace Frigate::ConfigMenu {

// Longer than this between updates means the menu was closed meanwhile, so the
// flow is dropped instead of binding whatever is pressed on return.
inline constexpr int64_t kFlowStaleMs = 1000;

// The question asked when the captured input is already used.
struct ConflictPrompt {
    // "Key W is already used by Move Forward."
    std::string text;
    bool canSwap = false;
    bool canReplace = false;
    // Why Replace is unavailable; empty when it is available.
    std::string replaceBlocked;
};

class BindingFlow {
  public:
    enum class Phase : uint8_t { Idle, Capturing, Deciding };

    // Captures one binding for `action` in `slot` (kNewSlot to add one). False,
    // with Message() saying why, when the port can't read a device to capture from.
    bool BeginSingle(const Platform::Input::BindTarget& target, Platform::Input::Action action, size_t slot,
                     const Platform::Input::PortDevice& selected, const Platform::Input::AttachedDevices& attached,
                     Platform::Input::FaceLayout layout, int64_t nowMs, const Platform::Input::BindingStore& store);

    // Prompts for each of `actions` in turn. The first binding of an action is
    // replaced (an action with none gets one); further bindings are kept.
    bool BeginBindAll(const Platform::Input::BindTarget& target, std::vector<Platform::Input::Action> actions,
                      const Platform::Input::PortDevice& selected, const Platform::Input::AttachedDevices& attached,
                      Platform::Input::FaceLayout layout, int64_t nowMs, const Platform::Input::BindingStore& store);

    // Once per frame while the editor is drawn. Stores a captured binding in
    // `store`; Consume that with TakeChanged() to save it.
    void Update(Platform::Input::BindingStore& store, int64_t nowMs,
                const std::vector<Platform::Input::RawInput>& held);

    // The answer to the conflict prompt.
    void Decide(Platform::Input::BindingStore& store, Platform::Input::ConflictChoice choice);

    // Bind all only: leave this action as it is and go on to the next.
    void Skip(const Platform::Input::BindingStore& store);

    // The Cancel button, and Cancel all in Bind all. Ends the flow.
    void Cancel();

    // True once after the store changed.
    bool TakeChanged();

    Phase phase() const {
        return mPhase;
    }
    bool Active() const {
        return mPhase != Phase::Idle;
    }
    bool BindAll() const {
        return mBindAll;
    }
    int Port() const {
        return mTarget.port;
    }
    const Platform::Input::BindTarget& Target() const {
        return mTarget;
    }
    Platform::Input::Action CurrentAction() const {
        return mActions.empty() ? Platform::Input::Action::N64A : mActions[mStep];
    }
    size_t CurrentSlot() const {
        return mSlot;
    }
    // 1-based step and total of Bind all.
    size_t Step() const {
        return mStep + 1;
    }
    size_t Steps() const {
        return mActions.size();
    }
    int64_t RemainingMs(int64_t nowMs) const {
        return mCapture.RemainingMs(nowMs);
    }
    // The capture's live hint ("Press the input to bind", what was ignored).
    const std::string& Hint() const {
        return mCapture.Hint();
    }
    // What happened last: a refused binding, a timeout, a cancel; empty when nothing to say.
    const std::string& Message() const {
        return mMessage;
    }
    const ConflictPrompt& Prompt() const {
        return mPrompt;
    }

  private:
    bool BeginStep(const Platform::Input::BindingStore& store, int64_t nowMs);
    void Advance(const Platform::Input::BindingStore& store, int64_t nowMs);
    void Finish(const std::string& message);
    void Retry(const Platform::Input::BindingStore& store, int64_t nowMs, const std::string& message);
    size_t SlotFor(const Platform::Input::BindingStore& store, Platform::Input::Action action) const;

    Phase mPhase = Phase::Idle;
    bool mBindAll = false;
    bool mChanged = false;
    Platform::Input::BindTarget mTarget;
    std::vector<Platform::Input::Action> mActions;
    size_t mStep = 0;
    size_t mSlot = Platform::Input::kNewSlot;
    Platform::Input::PortDevice mSelected;
    Platform::Input::AttachedDevices mAttached;
    Platform::Input::FaceLayout mLayout = Platform::Input::FaceLayout::Xbox;
    Platform::Input::Capture mCapture;
    Platform::Input::PhysicalInput mPending;
    ConflictPrompt mPrompt;
    std::string mMessage;
    int64_t mLastUpdateMs = 0;
};

// The prompt for binding `input` when `check` found conflicts.
ConflictPrompt BuildConflictPrompt(const Platform::Input::BindCheck& check, const Platform::Input::PhysicalInput& input,
                                   Platform::Input::FaceLayout layout);

} // namespace Frigate::ConfigMenu
