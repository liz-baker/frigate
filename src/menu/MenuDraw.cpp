#include "menu/MenuDraw.h"

#include <algorithm>
#include <cfloat>

#include <imgui.h>

#include "ship/window/gui/IconsFontAwesome4.h"

namespace Frigate::ConfigMenu {

namespace {

void DrawCheckbox(Widget& widget, ValueStore& store) {
    bool value = widget.GetInt(store) != 0;
    if (ImGui::Checkbox(widget.name.c_str(), &value)) {
        widget.SetInt(store, value ? 1 : 0);
    }
}

// Label, then [-] slider [+]; the slider shows the value.
void DrawSlider(Widget& widget, ValueStore& store) {
    const bool isFloat = widget.type == WidgetType::SliderFloat;
    ImGui::TextUnformatted(widget.name.c_str());

    const float button = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    if (ImGui::Button(ICON_FA_MINUS "##minus", ImVec2(button, button))) {
        isFloat ? widget.StepFloat(store, -1) : widget.StepInt(store, -1);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - button - spacing));
    if (isFloat) {
        float value = widget.GetFloat(store);
        const char* format = widget.format.empty() ? "%.2f" : widget.format.c_str();
        if (ImGui::SliderFloat("##value", &value, widget.floatMin, widget.floatMax, format)) {
            widget.SetFloat(store, value);
        }
    } else {
        int value = widget.GetInt(store);
        const char* format = widget.format.empty() ? "%d" : widget.format.c_str();
        if (ImGui::SliderInt("##value", &value, widget.intMin, widget.intMax, format)) {
            widget.SetInt(store, value);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS "##plus", ImVec2(button, button))) {
        isFloat ? widget.StepFloat(store, 1) : widget.StepInt(store, 1);
    }
}

void DrawCombo(Widget& widget, ValueStore& store) {
    ImGui::TextUnformatted(widget.name.c_str());
    const int current = widget.GetInt(store);
    const bool inRange = current >= 0 && current < static_cast<int>(widget.options.size());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##combo", inRange ? widget.options[static_cast<size_t>(current)].c_str() : "")) {
        for (int i = 0; i < static_cast<int>(widget.options.size()); i++) {
            if (ImGui::Selectable(widget.options[static_cast<size_t>(i)].c_str(), i == current)) {
                widget.SetInt(store, i);
            }
            if (i == current) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void DrawChildren(Widget& widget, ValueStore& store) {
    for (auto& child : widget.children) {
        DrawWidget(*child, store);
    }
}

void DrawHeader(Widget& widget, ValueStore& store) {
    ImGui::SetNextItemOpen(widget.defaultOpen, ImGuiCond_Once);
    if (ImGui::CollapsingHeader(widget.name.c_str())) {
        DrawChildren(widget, store);
    }
}

void DrawGroup(Widget& widget, ValueStore& store) {
    if (ImGui::BeginChild("##group", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", widget.name.c_str());
        DrawChildren(widget, store);
    }
    ImGui::EndChild();
}

} // namespace

void ShowTooltip(const char* text) {
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void DrawWidget(Widget& widget, ValueStore& store) {
    if (widget.hidden) {
        return;
    }
    ImGui::PushID(&widget);
    if (widget.disabled) {
        ImGui::BeginDisabled();
    }
    // The group is what the tooltip attaches to, so multi-part controls
    // (a slider and its buttons) share one.
    ImGui::BeginGroup();
    switch (widget.type) {
        case WidgetType::Checkbox:
            DrawCheckbox(widget, store);
            break;
        case WidgetType::SliderInt:
        case WidgetType::SliderFloat:
            DrawSlider(widget, store);
            break;
        case WidgetType::Combo:
            DrawCombo(widget, store);
            break;
        case WidgetType::Button:
            if (ImGui::Button(widget.name.c_str(), ImVec2(-FLT_MIN, 0.0f)) && widget.callback) {
                widget.callback();
            }
            break;
        case WidgetType::Text:
            ImGui::TextWrapped("%s", widget.name.c_str());
            break;
        case WidgetType::Section:
            ImGui::SeparatorText(widget.name.c_str());
            break;
        case WidgetType::Header:
            DrawHeader(widget, store);
            break;
        case WidgetType::Group:
            DrawGroup(widget, store);
            break;
        case WidgetType::Custom:
            if (widget.draw) {
                widget.draw();
            }
            break;
    }
    ImGui::EndGroup();
    if (widget.disabled) {
        ImGui::EndDisabled();
    }
    // Containers hand their tooltip to the children inside them.
    if (!widget.IsContainer() &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) {
        if (widget.disabled && !widget.disabledReason.empty()) {
            ShowTooltip(widget.disabledReason.c_str());
        } else if (!widget.tooltip.empty()) {
            ShowTooltip(widget.tooltip.c_str());
        }
    }
    ImGui::PopID();
}

void DrawSidebar(Sidebar& sidebar, ValueStore& store) {
    const int columns = std::max(1, sidebar.columns);
    if (columns == 1) {
        for (auto& widget : sidebar.widgets) {
            DrawWidget(*widget, store);
        }
        return;
    }
    if (!ImGui::BeginTable("##columns", columns, ImGuiTableFlags_SizingStretchSame)) {
        return;
    }
    ImGui::TableNextRow();
    for (int column = 0; column < columns; column++) {
        ImGui::TableSetColumnIndex(column);
        for (auto& widget : sidebar.widgets) {
            if (std::clamp(widget->column, 0, columns - 1) == column) {
                DrawWidget(*widget, store);
            }
        }
    }
    ImGui::EndTable();
}

} // namespace Frigate::ConfigMenu
