#include "menu/ControlsModel.h"

#include <algorithm>
#include <optional>

namespace Frigate::ConfigMenu {

using namespace Platform::Input;

namespace {

ProfileDevice ProfileDeviceFor(const PortDevice& device) {
    return device.kind == DeviceKind::KeyboardMouse ? ProfileDevice::KeyboardMouse : ProfileDevice::Gamepad;
}

std::string SectionTitle(Section section) {
    switch (section) {
        case Section::N64Face:
            return "A, B, Start, L, R, Z";
        case Section::N64Stick:
            return "Control stick";
        default:
            return SectionName(section);
    }
}

} // namespace

const char* ModeName(BindingMode mode) {
    return mode == BindingMode::Modern ? "Modern" : "N64";
}

std::string FaceLegend(FaceLayout layout) {
    const struct {
        PadButton button;
        const char* position;
    } faces[] = { { PadButton::South, "South" },
                  { PadButton::East, "East" },
                  { PadButton::West, "West" },
                  { PadButton::North, "North" } };
    std::string out;
    for (const auto& face : faces) {
        if (!out.empty()) {
            out += ", ";
        }
        out += std::string(face.position) + " " + FaceButtonLabel(face.button, layout);
    }
    return out;
}

std::vector<PhysicalInput> HeldFrom(const PortDevice& device, const AttachedDevices& attached,
                                    const std::vector<RawInput>& held) {
    std::vector<PhysicalInput> out;
    std::optional<size_t> pad;
    if (device.kind == DeviceKind::Gamepad) {
        pad = FindGamepad(device, attached);
    }
    for (const RawInput& raw : held) {
        const bool fromGamepad = raw.source.kind == InputSource::Kind::Gamepad;
        const bool fromDevice = device.kind == DeviceKind::KeyboardMouse
                                    ? !fromGamepad
                                    : fromGamepad && pad && attached.gamepads[*pad].id == raw.source.gamepadId;
        if (fromDevice) {
            out.push_back(raw.input);
        }
    }
    return out;
}

std::string EditorDisabledReason(int port, const PortDevice& device, const AttachedDevices& attached) {
    switch (device.kind) {
        case DeviceKind::Bindings:
            return "Port " + std::to_string(port + 1) +
                   " uses the default bindings. Choose a device for it to edit its bindings.";
        case DeviceKind::None:
            return "Port " + std::to_string(port + 1) + " reads no device, so it has no bindings to edit.";
        case DeviceKind::KeyboardMouse:
        case DeviceKind::Gamepad:
            return DisconnectedReason(device, attached);
    }
    return {};
}

std::string BindAllProgress(size_t step, size_t steps, const std::string& actionLabel) {
    return "Bind all, " + std::to_string(step) + " of " + std::to_string(steps) + ": press the input for " +
           actionLabel + ".";
}

std::string CapturePrompt(Action action, size_t slot) {
    return std::string("Press the input for ") + Info(action).label + (slot == kNewSlot ? " (adds a binding)." : ".");
}

int CountdownSeconds(int64_t remainingMs) {
    return remainingMs <= 0 ? 0 : static_cast<int>((remainingMs + 999) / 1000);
}

PortView BuildPortView(const PortInputs& inputs, const BindingStore& store) {
    PortView view;
    const PortDevice& device = inputs.device;
    view.disabledReason = EditorDisabledReason(inputs.port, device, inputs.attached);
    view.editable = view.disabledReason.empty();
    const bool hasProfile = device.kind == DeviceKind::KeyboardMouse || device.kind == DeviceKind::Gamepad;
    const BindingMode mode = store.Mode(inputs.port);
    view.profileDevice = ProfileDeviceFor(device);
    view.target = { inputs.port, mode, view.profileDevice };
    if (!hasProfile) {
        return view;
    }

    view.isController = device.kind == DeviceKind::Gamepad;
    if (view.isController) {
        view.faceLegend = FaceLegend(inputs.layout);
        view.showStick = true;
        view.showRumble = inputs.features.rumble;
        view.showLed = inputs.features.led;
        view.showGyro = inputs.features.gyro;
    }
    if (mode == BindingMode::Modern) {
        // TODO(https://github.com/liz-baker/frigate/issues/500): drop the note once Modern actions drive gameplay.
        view.modeNote = "These actions take effect once Modern mode drives the game (frigate#500).";
    }

    const std::vector<PhysicalInput> held = HeldFrom(device, inputs.attached, inputs.held);
    const Profile& profile = store.Get(view.target);
    for (const Section section : SectionsOf(mode)) {
        SectionView sectionView{ section, SectionTitle(section), {} };
        for (const Action action : ActionsOf(section)) {
            const ActionInfo& info = Info(action);
            RowView row;
            row.action = action;
            row.label = info.label;
            row.required = info.required;
            const std::vector<PhysicalInput>& bound = profile.Of(action);
            for (size_t slot = 0; slot < bound.size(); slot++) {
                SlotView slotView;
                slotView.slot = slot;
                slotView.label = InputLabel(bound[slot], inputs.layout);
                slotView.held = std::find(held.begin(), held.end(), bound[slot]) != held.end();
                slotView.canRemove = !(info.required && bound.size() <= 1);
                row.active = row.active || slotView.held;
                row.slots.push_back(std::move(slotView));
            }
            row.canAdd = bound.size() < kMaxSlots;
            sectionView.rows.push_back(std::move(row));
        }
        view.sections.push_back(std::move(sectionView));
    }
    return view;
}

} // namespace Frigate::ConfigMenu
