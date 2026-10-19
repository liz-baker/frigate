#pragma once

#include <string>
#include <vector>

#include "menu/System.h"
#include "platform/BindingCapture.h"
#include "platform/Bindings.h"
#include "platform/PortAssignment.h"

// What a port's tab in the Controls editor shows and what is enabled, worked
// out from the port's device, mode and the binding store. Pure logic with no
// ImGui, so it is unit-tested (tests/menu/ControlsModelTest.cpp); the ImGui
// side (ControlsEditor.cpp) only draws it.
namespace Frigate::ConfigMenu {

// How many bindings one action can hold in the editor.
inline constexpr size_t kMaxSlots = 4;

// Everything the view depends on besides the store.
struct PortInputs {
    int port = 0;
    Platform::Input::PortDevice device;
    Platform::Input::AttachedDevices attached;
    Platform::Input::FaceLayout layout = Platform::Input::FaceLayout::Xbox;
    ControllerFeatures features;
    // Inputs held now on every attached device (System::PollRawInputs).
    std::vector<Platform::Input::RawInput> held;
};

struct SlotView {
    size_t slot = 0;
    std::string label;
    // The slot's input is held now.
    bool held = false;
    // False for the last binding of a required action.
    bool canRemove = true;
};

struct RowView {
    Platform::Input::Action action;
    std::string label;
    std::vector<SlotView> slots;
    bool canAdd = true;
    // Some binding of the action is held now.
    bool active = false;
    bool required = false;
};

struct SectionView {
    Platform::Input::Section section;
    std::string title;
    std::vector<RowView> rows;
};

struct PortView {
    // Why the binding editor and the device settings are unavailable; empty when
    // they can be edited.
    std::string disabledReason;
    bool editable = false;
    Platform::Input::BindTarget target;
    Platform::Input::ProfileDevice profileDevice = Platform::Input::ProfileDevice::KeyboardMouse;
    std::vector<SectionView> sections;
    // Modern mode: its actions don't drive the game yet.
    std::string modeNote;
    // A gamepad's face buttons as printed on it; empty for the keyboard.
    std::string faceLegend;

    // Device settings: only a controller has them, and only where the platform
    // supports them.
    bool isController = false;
    bool showStick = false;
    bool showRumble = false;
    bool showLed = false;
    bool showGyro = false;

    bool ShowDeviceSettings() const {
        return showStick || showRumble || showLed || showGyro;
    }
};

// The gamepad or keyboard inputs from `device`'s source in `held`.
std::vector<Platform::Input::PhysicalInput> HeldFrom(const Platform::Input::PortDevice& device,
                                                     const Platform::Input::AttachedDevices& attached,
                                                     const std::vector<Platform::Input::RawInput>& held);

// Why the port's bindings can't be edited: no device chosen, none, or not attached.
std::string EditorDisabledReason(int port, const Platform::Input::PortDevice& device,
                                 const Platform::Input::AttachedDevices& attached);

PortView BuildPortView(const PortInputs& inputs, const Platform::Input::BindingStore& store);

// "South A, East B, West X, North Y" for the layout.
std::string FaceLegend(Platform::Input::FaceLayout layout);

const char* ModeName(Platform::Input::BindingMode mode);

// Bind all's prompt: "Bind all 3 of 18: press the input for Start."
std::string BindAllProgress(size_t step, size_t steps, const std::string& actionLabel);

// "Press the input for A (adds a binding)" for one capture.
std::string CapturePrompt(Platform::Input::Action action, size_t slot);

// The seconds left in a countdown, rounded up.
int CountdownSeconds(int64_t remainingMs);

} // namespace Frigate::ConfigMenu
