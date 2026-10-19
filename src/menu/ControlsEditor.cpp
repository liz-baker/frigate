#include "menu/ControlsEditor.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <functional>
#include <string>

#include <imgui.h>

#include "menu/BindingFlow.h"
#include "menu/ControlsModel.h"
#include "menu/MenuDraw.h"
#include "menu/MenuModel.h"
#include "menu/tabs/ControlsTab.h"
#include "platform/Bindings.h"
#include "platform/PortAssignment.h"
#include "ship/window/gui/IconsFontAwesome4.h"

namespace Frigate::ConfigMenu {

using namespace Platform::Input;

namespace {

constexpr const char* kFlowPopup = "Bind input###bindFlow";
constexpr const char* kAddPopup = "Add device###addDevice";
// How long the Test button runs a rumble motor.
constexpr int64_t kRumbleTestMs = 1000;

struct RumbleTest {
    bool running = false;
    int port = 0;
    std::string id;
    int64_t untilMs = 0;
};

struct EditorState {
    BindingFlow flow;
    RumbleTest rumble;
    // Set by the frame's drawing, to drop what belongs to a tab that wasn't drawn.
    bool flowDrawn = false;
    bool rumbleDrawn = false;
    std::array<Widget, kPortCount> reset;
    bool resetReady = false;
};

EditorState& State() {
    static EditorState state;
    return state;
}

int64_t NowMs() {
    return static_cast<int64_t>(ImGui::GetTime() * 1000.0);
}

ImVec4 WarningColor() {
    return ImVec4(1.0f, 0.75f, 0.3f, 1.0f);
}

void WrappedText(const ImVec4& color, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

ImVec4 ActiveColor() {
    return ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
}

// Label, then [-] slider [+] and a reset button once the value differs from the default.
bool DrawIntSetting(const char* label, const IntSetting& setting, const char* format, int& out) {
    ImGui::TextUnformatted(label);
    ImGui::PushID(label);
    int value = setting.value;
    bool changed = false;
    const float button = ImGui::GetFrameHeight();
    if (ImGui::Button(ICON_FA_MINUS "##minus", ImVec2(button, button))) {
        value = std::max(setting.min, value - 1);
        changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.5f));
    if (ImGui::SliderInt("##value", &value, setting.min, setting.max, format, ImGuiSliderFlags_AlwaysClamp)) {
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS "##plus", ImVec2(button, button))) {
        value = std::min(setting.max, value + 1);
        changed = true;
    }
    ImGui::PopID();
    out = value;
    return changed;
}

// A "Reset to default" button beside a setting; true when pressed.
bool DrawResetButton(const IntSetting& setting, const char* id) {
    if (setting.IsDefault()) {
        return false;
    }
    ImGui::SameLine();
    return ImGui::Button((std::string("Reset##") + id).c_str());
}

// "Press a button on the device" popup; `tryAdd` is polled every frame it is open.
void DrawAddFromPress(const char* buttonLabel, const char* what, const std::function<bool()>& tryAdd) {
    ImGui::PushID(buttonLabel);
    if (ImGui::Button(buttonLabel)) {
        ImGui::OpenPopup(kAddPopup);
    }
    if (ImGui::BeginPopupModal(kAddPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Press any button or move any axis on the controller\nto add its %s.", what);
        const bool cancel = ImGui::Button("Cancel");
        if (cancel || tryAdd()) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void DrawStickSettings(System& system, int port) {
    ImGui::SeparatorText("Stick options");
    const struct {
        Stick stick;
        const char* name;
        const char* tooltip;
    } sticks[] = {
        { Stick::Left, "Left stick", "The N64 stick reads this one." },
        { Stick::Right, "Right stick", "Turns and pitches the view in Modern mode; N64 mode doesn't read it." },
    };
    for (const auto& entry : sticks) {
        if (!ImGui::TreeNodeEx(entry.name, entry.stick == Stick::Left ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            continue;
        }
        ImGui::TextDisabled("%s", entry.tooltip);
        const struct {
            StickSetting setting;
            const char* label;
            const char* format;
        } settings[] = {
            { StickSetting::Sensitivity, "Sensitivity", "%d%%" },
            { StickSetting::Deadzone, "Deadzone", "%d%%" },
            { StickSetting::NotchSnap, "Notch snap angle", "%d\xc2\xb0" },
        };
        for (const auto& item : settings) {
            const IntSetting current = system.StickValue(port, entry.stick, item.setting);
            ImGui::PushID(static_cast<int>(item.setting));
            int value = current.value;
            if (DrawIntSetting(item.label, current, item.format, value)) {
                system.SetStickValue(port, entry.stick, item.setting, value);
            }
            if (DrawResetButton(current, "stick")) {
                system.ResetStickValue(port, entry.stick, item.setting);
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

void DrawRumbleSettings(System& system, int port, const ControllerFeatures& features, int64_t nowMs) {
    ImGui::SeparatorText("Rumble");
    EditorState& state = State();
    for (const RumbleDevice& device : system.RumbleDevices(port)) {
        ImGui::PushID(device.id.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(device.name.c_str());
        ImGui::SameLine();
        const bool testing = state.rumble.running && state.rumble.port == port && state.rumble.id == device.id;
        if (ImGui::Button(testing ? "Stop" : "Test")) {
            if (testing) {
                system.TestRumble(port, device.id, false);
                state.rumble.running = false;
            } else {
                if (state.rumble.running) {
                    system.TestRumble(state.rumble.port, state.rumble.id, false);
                }
                state.rumble = { true, port, device.id, nowMs + kRumbleTestMs };
            }
        }
        if (testing) {
            state.rumbleDrawn = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TIMES "##remove")) {
            system.RemoveRumbleDevice(port, device.id);
        }
        int value = 0;
        if (DrawIntSetting("Small motor", device.small, "%d%%", value)) {
            system.SetRumbleIntensity(port, device.id, false, value);
        }
        if (DrawResetButton(device.small, "small")) {
            system.ResetRumbleIntensity(port, device.id, false);
        }
        if (DrawIntSetting("Large motor", device.large, "%d%%", value)) {
            system.SetRumbleIntensity(port, device.id, true, value);
        }
        if (DrawResetButton(device.large, "large")) {
            system.ResetRumbleIntensity(port, device.id, true);
        }
        ImGui::PopID();
    }
    if (features.rumbleAdd) {
        DrawAddFromPress("Add rumble device", "rumble motors",
                         [&system, port] { return system.AddRumbleDeviceFromPress(port); });
    } else if (system.RumbleDevices(port).empty()) {
        ImGui::TextDisabled("No rumble device.");
    }
}

void DrawLedSettings(System& system, int port) {
    ImGui::SeparatorText("LEDs");
    for (const LedDevice& device : system.LedDevices(port)) {
        ImGui::PushID(device.id.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(device.name.c_str());
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TIMES "##remove")) {
            system.RemoveLedDevice(port, device.id);
        }
        int source = static_cast<int>(device.source);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        bool changed = ImGui::Combo("LED color", &source, "Off\0Set\0Game\0\0");
        float color[3] = { device.r / 255.0f, device.g / 255.0f, device.b / 255.0f };
        if (source == static_cast<int>(LedSource::Set)) {
            ImGui::SameLine();
            changed = ImGui::ColorEdit3("##color", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel) ||
                      changed;
        }
        if (changed) {
            system.SetLed(port, device.id, static_cast<LedSource>(source), static_cast<uint8_t>(color[0] * 255.0f),
                          static_cast<uint8_t>(color[1] * 255.0f), static_cast<uint8_t>(color[2] * 255.0f));
        }
        ImGui::PopID();
    }
    DrawAddFromPress("Add LED device", "LED", [&system, port] { return system.AddLedDeviceFromPress(port); });
}

void DrawGyroSettings(System& system, int port) {
    ImGui::SeparatorText("Gyro");
    const GyroDevice gyro = system.Gyro(port);
    if (!gyro.present) {
        DrawAddFromPress("Add gyro device", "gyro", [&system, port] { return system.AddGyroFromPress(port); });
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(gyro.name.c_str());
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TIMES "##removeGyro")) {
        system.RemoveGyro(port);
        return;
    }
    int value = 0;
    if (DrawIntSetting("Sensitivity", gyro.sensitivity, "%d%%", value)) {
        system.SetGyroSensitivity(port, value);
    }
    if (DrawResetButton(gyro.sensitivity, "gyro")) {
        system.ResetGyroSensitivity(port);
    }
    if (ImGui::Button("Recalibrate")) {
        system.RecalibrateGyro(port);
    }
}

void DrawDeviceSettings(System& system, int port, const PortView& view, const ControllerFeatures& features,
                        int64_t nowMs) {
    if (view.showStick) {
        DrawStickSettings(system, port);
    }
    if (view.showRumble) {
        DrawRumbleSettings(system, port, features, nowMs);
    }
    if (view.showLed) {
        DrawLedSettings(system, port);
    }
    if (view.showGyro) {
        DrawGyroSettings(system, port);
    }
}

// Starts a capture for one slot, or a Bind all run.
struct FlowRequest {
    bool single = false;
    Action action = Action::N64A;
    size_t slot = kNewSlot;
    bool bindAll = false;
};

void DrawRow(System& system, const PortView& view, const RowView& row, FlowRequest& request) {
    ImGui::PushID(static_cast<int>(row.action));
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    if (row.active) {
        ImGui::TextColored(ActiveColor(), "%s", row.label.c_str());
    } else {
        ImGui::TextUnformatted(row.label.c_str());
    }
    ImGui::TableSetColumnIndex(1);
    for (const SlotView& slot : row.slots) {
        ImGui::PushID(static_cast<int>(slot.slot));
        if (slot.held) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(slot.label.c_str())) {
            request = { true, row.action, slot.slot, false };
        }
        if (slot.held) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            ShowTooltip("Click to rebind.");
        }
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginDisabled(!slot.canRemove);
        if (ImGui::SmallButton(ICON_FA_TIMES)) {
            Bindings().Unbind(view.target, row.action, slot.slot);
            system.BindingsChanged();
        }
        ImGui::EndDisabled();
        if (!slot.canRemove &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) {
            ShowTooltip("This action needs at least one binding.");
        }
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!row.canAdd);
    if (ImGui::Button(ICON_FA_PLUS "##add")) {
        request = { true, row.action, kNewSlot, false };
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) {
        ShowTooltip(row.canAdd ? "Add another binding." : "The most bindings an action can have.");
    }
    ImGui::PopID();
}

void DrawBindings(System& system, int port, const PortView& view, FlowRequest& request) {
    if (!view.faceLegend.empty()) {
        ImGui::TextDisabled("Face buttons: %s", view.faceLegend.c_str());
    }
    if (view.showCrouchToggle) {
        bool toggle = view.crouchToggle;
        if (ImGui::Checkbox("Toggle crouch", &toggle)) {
            system.SetCrouchToggle(port, toggle);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            ShowTooltip("Crouch stays on until pressed again instead of only while held. It also ends when you die, "
                        "start a stage, enter a tank or switch to a scoped gun.");
        }
    }
    if (view.showMouseSensitivity || view.showLookSensitivity) {
        const bool mouse = view.showMouseSensitivity;
        const IntSetting& current = mouse ? view.mouseSensitivity : view.lookSensitivity;
        ImGui::PushID("lookSensitivity");
        int value = current.value;
        if (DrawIntSetting(mouse ? "Mouse sensitivity" : "Look sensitivity", current, "%d%%", value)) {
            if (mouse) {
                system.SetMouseSensitivity(port, value);
            } else {
                system.SetLookSensitivity(port, value);
            }
        }
        if (DrawResetButton(current, "look")) {
            if (mouse) {
                system.SetMouseSensitivity(port, current.defaultValue);
            } else {
                system.SetLookSensitivity(port, current.defaultValue);
            }
        }
        ImGui::PopID();
    }
    for (const SectionView& section : view.sections) {
        ImGui::PushID(static_cast<int>(section.section));
        ImGui::SeparatorText(section.title.c_str());
        if (ImGui::BeginTable("##rows", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("bindings", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            for (const RowView& row : section.rows) {
                DrawRow(system, view, row, request);
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
}

void DrawFlowPopup(System& system, BindingFlow& flow, int64_t nowMs) {
    if (flow.Active() && !ImGui::IsPopupOpen(kFlowPopup)) {
        ImGui::OpenPopup(kFlowPopup);
    }
    if (!ImGui::IsPopupOpen(kFlowPopup)) {
        return;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kFlowPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (!flow.Active()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const Action action = flow.CurrentAction();
    if (flow.BindAll()) {
        ImGui::TextUnformatted(BindAllProgress(flow.Step(), flow.Steps(), Info(action).label).c_str());
    } else {
        ImGui::TextUnformatted(CapturePrompt(action, flow.CurrentSlot()).c_str());
    }
    if (flow.phase() == BindingFlow::Phase::Capturing) {
        if (!flow.Hint().empty()) {
            ImGui::TextWrapped("%s", flow.Hint().c_str());
        }
        if (!flow.Message().empty()) {
            WrappedText(WarningColor(), flow.Message());
        }
        ImGui::Text("Cancels in %d s", CountdownSeconds(flow.RemainingMs(nowMs)));
        ImGui::Spacing();
        if (flow.BindAll()) {
            if (ImGui::Button("Skip")) {
                flow.Skip(Bindings());
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel all")) {
                flow.Cancel();
            }
        } else if (ImGui::Button("Cancel")) {
            flow.Cancel();
        }
    } else {
        const ConflictPrompt& prompt = flow.Prompt();
        ImGui::TextWrapped("%s", prompt.text.c_str());
        ImGui::Spacing();
        ImGui::BeginDisabled(!prompt.canSwap);
        if (ImGui::Button("Swap")) {
            flow.Decide(Bindings(), ConflictChoice::Swap);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) {
            ShowTooltip("The other action takes the input this binding had.");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!prompt.canReplace);
        if (ImGui::Button("Replace")) {
            flow.Decide(Bindings(), ConflictChoice::Replace);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) {
            ShowTooltip(prompt.canReplace ? "Takes the input from the other action." : prompt.replaceBlocked.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            flow.Decide(Bindings(), ConflictChoice::Cancel);
        }
    }
    if (flow.TakeChanged()) {
        system.BindingsChanged();
    }
    ImGui::EndPopup();
}

void DrawDevice(System& system, int port, const AttachedDevices& attached) {
    const PortDevice selected = system.SelectedDevice(port);
    const DeviceChoices choices = BuildDeviceChoices(selected, attached);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
    if (ImGui::BeginCombo("Device", choices.choices[choices.selected].label.c_str())) {
        for (size_t i = 0; i < choices.choices.size(); i++) {
            if (ImGui::Selectable(choices.choices[i].label.c_str(), i == choices.selected)) {
                system.SelectDevice(port, choices.choices[i].device);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ShowTooltip("What feeds this port. Default bindings leaves the bindings as they are: port 1 reads the "
                    "keyboard, mouse and every gamepad, the other ports nothing. Choosing a device makes it the only "
                    "one this port reads. A device that is unplugged stays chosen, listed as disconnected, and the "
                    "port reads no input until it is back.");
    }
    PortDevices devices{};
    for (int other = 0; other < kPortCount; other++) {
        devices[static_cast<size_t>(other)] = system.SelectedDevice(other);
    }
    const std::string warning = DuplicateDeviceWarning(devices, attached, port);
    if (!warning.empty()) {
        WrappedText(WarningColor(), warning);
    }
}

void DrawMode(System& system, int port) {
    BindingStore& store = Bindings();
    const BindingMode mode = store.Mode(port);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Mode");
    const struct {
        BindingMode mode;
        const char* tooltip;
    } options[] = {
        { BindingMode::Modern, "Bindings are actions, and the in-game control settings are ignored." },
        { BindingMode::N64, "Bindings are N64 buttons, and the in-game control style applies." },
    };
    for (const auto& option : options) {
        ImGui::SameLine();
        if (ImGui::RadioButton(ModeName(option.mode), mode == option.mode) && mode != option.mode) {
            store.SetMode(port, option.mode);
            system.BindingsChanged();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            ShowTooltip(option.tooltip);
        }
    }
}

void DrawPortTab(System& system, int port, const AttachedDevices& attached, const std::vector<RawInput>& held,
                 int64_t nowMs) {
    EditorState& state = State();
    const size_t index = static_cast<size_t>(port);

    bool connected = system.PortConnected(port);
    if (ImGui::Checkbox("Connected", &connected)) {
        system.SetPortConnected(port, connected);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ShowTooltip("Counts this port as connected even with no device attached. A connected port with no device "
                    "reads no input.");
    }
    ImGui::TextWrapped("%s", PlayerCountText(ControllerCount()).c_str());

    DrawDevice(system, port, attached);

    PortInputs inputs;
    inputs.port = port;
    inputs.device = system.SelectedDevice(port);
    inputs.attached = attached;
    inputs.layout = system.PortFaceLayout(port);
    inputs.features = system.Features(port);
    inputs.crouchToggle = system.CrouchToggle(port);
    inputs.mouseSensitivity = system.MouseSensitivity(port);
    inputs.lookSensitivity = system.LookSensitivity(port);
    inputs.held = held;
    PortView view = BuildPortView(inputs, Bindings());
    if (!view.disabledReason.empty()) {
        WrappedText(WarningColor(), view.disabledReason);
    }

    DrawMode(system, port);

    BindingFlow& flow = state.flow;
    if (flow.Active() && flow.Port() == port) {
        if (!view.editable || flow.Target() != view.target) {
            flow.Cancel();
        } else {
            flow.Update(Bindings(), nowMs, held);
            state.flowDrawn = true;
            if (flow.TakeChanged()) {
                system.BindingsChanged();
                view = BuildPortView(inputs, Bindings());
            }
        }
    }

    ImGui::BeginDisabled(!view.editable);
    FlowRequest request;
    DrawBindings(system, port, view, request);
    if (view.ShowDeviceSettings()) {
        DrawDeviceSettings(system, port, view, inputs.features, nowMs);
    }
    ImGui::Separator();

    Widget& reset = state.reset[index];
    reset.type = WidgetType::Button;
    reset.name = "Reset to default";
    reset.confirm = std::string("This replaces the ") + ModeName(view.target.mode) + " bindings of " +
                    (view.profileDevice == ProfileDevice::Gamepad ? "this port's gamepad" : "the keyboard and mouse") +
                    " with the defaults.\n\nContinue?";
    const BindTarget target = view.target;
    reset.callback = [sys = &system, target] {
        Bindings().ResetProfile(target);
        sys->BindingsChanged();
    };
    if (ImGui::BeginTable("##bottom", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawButton(reset);
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("Bind all", ImVec2(-FLT_MIN, 0.0f))) {
            request.bindAll = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            ShowTooltip("Asks for an input for every action of this mode in turn. Skip leaves an action as it is.");
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();

    if (view.editable && (request.single || request.bindAll)) {
        if (request.bindAll) {
            flow.BeginBindAll(view.target, ActionsOf(view.target.mode), inputs.device, attached, inputs.layout, nowMs,
                              Bindings());
        } else {
            flow.BeginSingle(view.target, request.action, request.slot, inputs.device, attached, inputs.layout, nowMs,
                             Bindings());
        }
    }
    if (flow.Active() && flow.Port() == port) {
        state.flowDrawn = true;
    }
    if (flow.Port() == port) {
        DrawFlowPopup(system, flow, nowMs);
    }
    if (!flow.Active() && !flow.Message().empty() && flow.Port() == port) {
        WrappedText(WarningColor(), flow.Message());
    }
}

} // namespace

void DrawControlsEditor(System& system) {
    EditorState& state = State();
    const int64_t nowMs = NowMs();
    const AttachedDevices attached = system.Attached();
    // The pointer over any item, last frame's Cancel button included, must not bind a click.
    const std::vector<RawInput> held = system.PollRawInputs(ImGui::IsAnyItemHovered());

    state.flowDrawn = false;
    state.rumbleDrawn = false;
    if (ImGui::BeginTabBar("##controllerPorts")) {
        for (int port = 0; port < kPortCount; port++) {
            const std::string label = "Port " + std::to_string(port + 1) + "###port" + std::to_string(port);
            if (ImGui::BeginTabItem(label.c_str())) {
                ImGui::PushID(port);
                DrawPortTab(system, port, attached, held, nowMs);
                ImGui::PopID();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    if (state.flow.Active() && !state.flowDrawn) {
        state.flow.Cancel();
    }
    if (state.rumble.running) {
        if (!state.rumbleDrawn || nowMs >= state.rumble.untilMs) {
            system.TestRumble(state.rumble.port, state.rumble.id, false);
            state.rumble.running = false;
        } else {
            system.TestRumble(state.rumble.port, state.rumble.id, true);
        }
    }
}

} // namespace Frigate::ConfigMenu
